#include <stdio.h>
#include <getopt.h>

#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>

#include "funcs.h"
#include "plugin_api.h"

// Команда запуска [[опции] каталог]
// задание --mac-addr <набор значений> каталог

int debugOutputEnabled = 0;
int optionsUnion = 1; //  условие объединения опций - по умолчанию AND = true, OR = false
int invert = 0;

int any_result = 0;

//-------------------------------------
struct pluginData
{
    char *fullPath;
    void *handle;
    struct option opt;
    int optionToUse;
    int (*plGetInfo)(struct plugin_info *);
    int (*plProcFile)(const char *,
                      struct option *[],
                      size_t,
                      char *,
                      size_t);
};
struct pluginData **loadedPlugins;
int loadedPluginsCount = 0;
//-------------------------------------

int logger = -1;

void closeLog()
{
    if (logger != -1)
        close(logger);
}

void writeLog(char *message)
{
    if (logger == -1)
        return;

    write(logger, message, sizeof(char) * strlen(message));
}

void filesRecursively(char *path)
{
    if (path == NULL)
        return;

    struct dirent *dp = NULL;
    DIR *dir = opendir(path);

    if (!dir)
    {
        fprintf(stderr, "Ошибка открытия каталога %s\n", path);
        return;
    }

    while ((dp = readdir(dir)) != NULL)
    {
        char *p = malloc(sizeof(char) * (strlen(path) + strlen(dp->d_name) + 2));
        p = strcpy(p, path);
        p = strcat(p, "/");
        p = strcat(p, dp->d_name); // файл или папка

        if (strcmp(dp->d_name, ".") != 0  && strcmp(dp->d_name, "..") != 0)
        {
            // дальше по рекурсии идут только папки
            if (folderCheck(p) == true)
                filesRecursively(p);
            else
            {
                // work
                int result = -1; // переменная для итогового результата

                writeLog("Файл ");
                writeLog(p);
                writeLog(". Обработка\n");

                // перебираем все опции из плагинов
                for (int i = 0; i < loadedPluginsCount; i++)
                {
                    // если опция не помечена как используемая - пропускаем
                    if (loadedPlugins[i]->optionToUse == 0)
                        continue;

                    size_t out_buff_len = 255;
                    char *out_buff = malloc(sizeof (char) * out_buff_len);

                    struct option *opt_tmp[] = { &loadedPlugins[i]->opt };

                    // по каждой опции запускаем чек файла
                    int ret = loadedPlugins[i]->plProcFile(p, opt_tmp, 1, out_buff, out_buff_len);

                    if (ret == -1) // плагин вернул ошибку
                    {
                        writeLog("Плагин ");
                        writeLog((char *)loadedPlugins[i]->opt.name);
                        writeLog(". Возникла ошибка! ");
                        writeLog(out_buff);
                        writeLog(". Передаваемое значение ");
                        writeLog((char *)loadedPlugins[i]->opt.flag);
                        writeLog("\n");

                        if (debugOutputEnabled == 1)
                            fprintf(stderr, "Плагин %s. Возникла ошибка! %s . Передаваемое значение %s.\n",
                                        (char *)loadedPlugins[i]->opt.name,
                                        out_buff,
                                        (char *)loadedPlugins[i]->opt.flag);
                        continue;
                    }

                    free(out_buff);

                    // при инверсии мы ищем файлы, которые не подходят
                    // файл с содержимым нужно отсечь, без содержимого пометить как корректный
                    if (invert == 1 && ret == 1)
                        ret = 0;
                    else
                    {
                        if (invert == 1 && ret == 0)
                            ret = 1;
                    }

                    // если условие объединения опций AND
                    // для всех файлов result должен быть 0
                    if (optionsUnion == 1)
                    {
                        if (ret == 0 && result != 1)
                        {
                            result = 0;
                        }
                        else
                            result = 1;
                    }
                    else
                    {
                        // если условие объединения опций OR
                        // для хотя бы одного файла result должен быть 0

                        if (ret == 0)
                            result = 0;
                        else
                            result = 1;
                    }

                }

                if (result == 0)
                {
                    writeLog("Файл ");
                    writeLog(p);
                    writeLog(" удвлетворяет условиям");
                    writeLog("\n");

                    fprintf(stdout, "Файл %s удвлетворяет условиям\n", p);

                    any_result = 1;
                }
                else
                    if (result == 1)
                    {
                        writeLog("Файл ");
                        writeLog(p);
                        writeLog(" не удвлетворяет условиям");
                        writeLog("\n");

                        if (debugOutputEnabled == 1)
                            fprintf(stderr, "Файл %s не удвлетворяет условиям\n", p);
                    }
            }
        }

        free(p);

    }

    closedir(dir);

}

int checkPluginPath(char *path)
{
    if (path == NULL)
        return -2;

    struct dirent *dir;
    DIR *d = opendir(path);
    if (d)
    {
        while ((dir = readdir(d)) != NULL)
        {
            char *p1 = strtok(dir->d_name, ".");
            char *p2 = strtok(NULL, ".");

            if(p2 != NULL)
            {
                if (strcmp(p2, "so") == 0)
                {
                    writeLog("Найден плагин: ");
                    writeLog(p1);
                    writeLog("\n");

                    if (debugOutputEnabled == 1)
                        fprintf(stdout, "Найден плагин: %s\n", p1);

                    // пробуем прогрузить и опросить
                    int (*plGetInfo)(struct plugin_info *);

                    int (*plProcFile)(const char *,
                                      struct option *[],
                                      size_t,
                                      char *,
                                      size_t);

                    struct plugin_info ppi = { 0 };

                    char *error = NULL;

                    char *soPath = malloc(sizeof(char) * (strlen(path) + strlen(dir->d_name) + 5));
                    soPath = strcpy(soPath, path);
                    soPath = strcat(soPath, "/");
                    soPath = strcat(soPath, dir->d_name);
                    soPath = strcat(soPath, ".so");

                    void *handle = dlopen(soPath, RTLD_LAZY | RTLD_GLOBAL);
                    if (!handle)
                    {
                        writeLog("   Ошибка открытия файла");
                        writeLog(dir->d_name);
                        writeLog(dlerror());
                        writeLog("\n");

                        if (debugOutputEnabled == 1)
                            fprintf(stderr, "   Ошибка открытия файла: %s %s\n", dir->d_name, dlerror());

                        continue;
                    }
                    else
                    {
                        writeLog("   Плагин открыт");
                        writeLog("\n");

                        if (debugOutputEnabled == 1)
                            fprintf(stdout, "   Плагин открыт\n");
                    }

                    (void) dlerror();

                    plGetInfo = (int (*)(struct plugin_info *))dlsym(handle, "plugin_get_info");
                    if ((error = dlerror()) != NULL)
                    {
                        writeLog("   Ошибка ");
                        writeLog(error);
                        writeLog("\n");

                        if (debugOutputEnabled == 1)
                            fprintf(stderr, "   Ошибка: %s\n", error);

                        continue;
                    }
                    else
                    {
                        writeLog("   Плагин: адрес функции plugin_get_info найден");
                        writeLog("\n");

                        if (debugOutputEnabled == 1)
                            fprintf(stdout, "   Плагин: адрес функции plugin_get_info найден\n");
                    }

                    (void) dlerror();

                    plProcFile = (int (*)(const char *, struct option *[], size_t, char *, size_t))dlsym(handle, "plugin_process_file");
                    if ((error = dlerror()) != NULL)
                    {
                        writeLog("   Ошибка ");
                        writeLog(error);
                        writeLog("\n");

                        if (debugOutputEnabled == 1)
                            fprintf(stderr, "   Ошибка: %s\n", error);

                        continue;
                    }
                    else
                    {
                        writeLog("   Плагин: адрес функции plugin_process_file найден");
                        writeLog("\n");

                        if (debugOutputEnabled == 1)
                            fprintf(stdout, "   Плагин: адрес функции plugin_process_file найден\n");
                    }

                    int v = (*plGetInfo)(&ppi);

                    if (v == 0)
                    {
                        writeLog("   Плагин ");
                        writeLog((char *)ppi.plugin_name);
                        writeLog(" можно использовать\n");
                        writeLog("       Доступные опции: \n");

                        if (debugOutputEnabled == 1)
                        {
                            fprintf(stdout, "   Плагин %s можно использовать\n", ppi.plugin_name);
                            fprintf(stdout, "       Доступные опции: \n");
                        }

                        // записываем все опции поддерживаемые плагином
                        for (int i = 0; i < (int)ppi.sup_opts_len; i++)
                        {
                            if (loadedPluginsCount == 0)
                                loadedPlugins = calloc(1, sizeof(struct pluginData));
                            else
                                loadedPlugins = realloc(loadedPlugins, sizeof(struct pluginData) * (loadedPluginsCount + 1));

                            writeLog("          ");
                            writeLog((char *)ppi.sup_opts[i].opt.name);
                            writeLog(" требует аргумент: ");
                            writeLog(ppi.sup_opts[i].opt.has_arg == 0 ? "нет" : "да");
                            writeLog("\n");

                            if (debugOutputEnabled == 1)
                            {
                                fprintf(stdout, "          %s требует аргумент: %s\n", (char *)ppi.sup_opts[i].opt.name, ppi.sup_opts[i].opt.has_arg == 0 ? "нет" : "да");
                            }

                            loadedPlugins[loadedPluginsCount] = malloc(sizeof(struct pluginData));
                            loadedPlugins[loadedPluginsCount]->fullPath = strdup(soPath);
                            loadedPlugins[loadedPluginsCount]->handle = handle;
                            loadedPlugins[loadedPluginsCount]->plGetInfo = plGetInfo;
                            loadedPlugins[loadedPluginsCount]->plProcFile = plProcFile;
                            loadedPlugins[loadedPluginsCount]->opt = ppi.sup_opts[i].opt;
                            loadedPlugins[loadedPluginsCount]->optionToUse = 0;

                            loadedPluginsCount++;
                        }

                    }
                    else
                    {
                        writeLog("   Ошибка! Использование невозможно. Плагин вернул ошибки ");
                        writeLog("\n");

                        if (debugOutputEnabled == 1)
                            fprintf(stderr, "Ошибка! Плагин вернул код ошибки %d. Использование невозможно.\n", v);
                    }

                    free(soPath);
                }

            }

        }
        closedir(d);
    }
    else
        return -1;

    return 0;
}

int main(int argc, char *argv[])
{
    char *pluginPath = NULL; // доп каталог для плагинов - может быть не заполенено значение
    char *searchPath = strdup(argv[argc - 1]);
    argv[argc - 1] = NULL;
    --argc;

    char *version = "0.0.1";
    char *help = " -P dir            - каталог с плагинами\n"
                 " -l /path/to/log   - путь к лог-файлу\n"
                 " -C cond           - условие объединения опций. возможные значения AND, OR. значение по умолчанию AND\n"
                 " -N                - инвертирование условий поиска\n"
                 " -v                - вывод версии программы\n"
                 " -h                - вывод справки по опциям\n"
                 " -d                - вывод всей информации\n";

    int opt = 0;
    int option_index = 0;

    // копируем аргументы, чтобы второй проход по ним был корректен
    char **arguments = calloc(argc, sizeof(char *));
    for (int i = 0; i < argc; i++)
    {
        arguments[i] = strdup(argv[i]);
    }

    opterr = 0;
    // ищем в опциях все кроме опций, задающих условие поиска и путь для поиска
    do
    {
        static struct option opts[] = {
            {"PluginPath -P", required_argument, NULL, 'P'}, // 0
            {"LogPath -l", required_argument, NULL, 'l'}, // 1
            {"Concat -C", required_argument, NULL, 'C'}, // 2
            {"Invert -N", no_argument, NULL, 'N'}, // 3
            {"Version -v", no_argument, NULL, 'v'}, // 4
            {"Help -h", no_argument, NULL, 'h'}, // 5
            {"Debug -d", no_argument, NULL, 'd'}, // 6
            {0, 0, 0, 0}
        };

        switch (opt = getopt_long(argc, argv, "-:P:l:C:Nvhd", opts, &option_index))
        {
            case 'd':
                debugOutputEnabled = 1;
                break;
            case 'C':
                if (optarg)
                {
                    if (strcasecmp(optarg, "AND") == 0)
                        optionsUnion = 1;
                    else
                        if (strcasecmp(optarg, "OR") == 0)
                            optionsUnion = 0;
                        else
                        {
                            writeLog("Указанное после опции -C значение ");
                            writeLog(optarg);
                            writeLog(" не является корректным. Работа программы будет остановлена.\n");
                            fprintf(stderr, "Указанное после опции -C значение %s не является корректным. Работа программы будет остановлена.\n", optarg);
                            closeLog();
                            exit(671);
                        }
                }
                else
                {
                    writeLog("Опция -С требует аргумент.Работа программы будет остановлена.\n");
                    fprintf(stderr, "Опция -С требует аргумент. Работа программы будет остановлена.\n");

                    closeLog();

                    exit(67);
                }
                break;
            case 'N':
                invert = 1;
                break;
            case 'v':
                fprintf(stdout, "пТекущая версия %s\n", version);
                break;
            case 'h':
                fprintf(stdout, "%s\n", help);
                break;
            case 'P':
//                fprintf(stdout, "параметр %s", opts[0].name);
                if (optarg)
                {
//                    fprintf(stdout, " с аргументом %s\n", optarg);

                    if (folderCheck(optarg))
                        pluginPath = optarg;
                    else
                    {
                        fprintf(stderr, "Аргумент не является каталогом. Работа программы будет остановлена.\n");
                        writeLog("Аргумент не является каталогом. Работа программы будет остановлена.\n");
                        closeLog();
                        exit(801);
                    }
                }
                else
                {
                    writeLog("После опции -Р не указан каталог. Работа программы будет остановлена.\n");
                    fprintf(stderr, "После опции -Р не указан каталог. Работа программы будет остановлена.\n");
                    closeLog();
                    exit(80);
                }
                break;

            case 'l':
                if (optarg)
                {
                    if (folderCheck(optarg))
                    {
                        char *fn = malloc(sizeof(char) * (strlen(optarg) + 9));
                        fn = strcpy(fn, optarg);
                        fn = strcat(fn, "/logfile");

                        if (logger != -1)
                        {
                            close(logger);
                        }

                        logger = open(fn, O_WRONLY | O_TRUNC | O_CREAT, 0644);
                        if (logger == -1)
                        {
                            fprintf(stderr, "Ошибка открытия или создания лог-файла по указанному пути\n");
                        }
                        else
                            writeLog("Log open\n");

                        free(fn);
                    }
                    else
                    {
                        fprintf(stderr, "Аргумент не является каталогом. Требуется ввести путь к папке, в которой будет создан лог-файл.. Работа программы будет остановлена.\n");
                        exit(1081);
                    }
                }
                else
                {
                    fprintf(stderr, "После опции -l не указан каталог. Создание лог-файла невозможно. Работа программы будет остановлена.\n");
                    exit(108);
                }
                break;
            case -1:
                break;
            case 1:
                break;
            case ':':
                fprintf(stderr, "Одна из опций с обязательным аргументом указана без него.\n");
                exit(58);
            case '?':
                break;
        }
    } while (opt >= 0);

    // ищем и грузим плагины в текущем каталоге
    if (checkPluginPath(".") != 0)
        fprintf(stderr, "Ошибка открытия каталога %s\n", ".");

    // ищем и грузим плагины в заданном опцией каталоге
    if (pluginPath != NULL)
        if (checkPluginPath(pluginPath) != 0)
            fprintf(stderr, "Ошибка открытия каталога %s\n", pluginPath);

    // ищем путь для поиска
    if (!folderCheck(searchPath))
    {
        fprintf(stderr, "Путь для поиска не является каталогом или каталог для поиска не указан\n");
        fprintf(stdout, "Справка:\n %s \n", help);
        fprintf(stdout, "Доступные плагины:\n -----------------\n");

        for (int i = 0; i < (int)loadedPluginsCount; i++)
        {
            fprintf(stdout, "%s, опция %s\n", loadedPlugins[i]->fullPath, loadedPlugins[i]->opt.name);
        }

        fprintf(stdout, "-----------------\n");
        return 1;
    }

    opt = 0;
    optind = 0;

    struct option *long_options_only = calloc(loadedPluginsCount + 1, sizeof(struct option));
    for (int i = 0; i < (int)loadedPluginsCount; i++)
    {
        long_options_only[i].name = loadedPlugins[i]->opt.name;
        long_options_only[i].has_arg = loadedPlugins[i]->opt.has_arg;
        long_options_only[i].flag = NULL;
        long_options_only[i].val = 0;
    }
    long_options_only[loadedPluginsCount].name = NULL;
    long_options_only[loadedPluginsCount].has_arg = no_argument;
    long_options_only[loadedPluginsCount].flag = NULL;
    long_options_only[loadedPluginsCount].val = 0;

//    for (int i = 0; i < argc; i++)
//        fprintf(stdout, "%s\n", arguments[i]);

    do
    {
        option_index = -1;

        switch (opt = getopt_long_only(argc, arguments, "", long_options_only, &option_index))
        {
            case 0:
//                fprintf(stdout, "1getopt возвратило %s ??\n", long_options_only[option_index].name);
                for (int i = 0; i < loadedPluginsCount; i++)
                {
                    if (strcmp(loadedPlugins[i]->opt.name, long_options_only[option_index].name) == 0)
                    {
                        loadedPlugins[i]->optionToUse = 1;

                        if (loadedPlugins[i]->opt.has_arg == required_argument && optarg)
                        {
                            if (strlen(optarg) > 2)
                                loadedPlugins[i]->opt.flag = (int *)optarg;
                            else
                            {
                                optind--;
                                char *opt = calloc(12, sizeof(char));
                                for(int i = 0; optind < argc && *arguments[optind] != '-' && i < 6; optind++, i++)
                                {
                                    if (strlen(arguments[optind]) == 2)
                                    {
                                        strcat(opt, arguments[optind]);
                                    }
                                }
                                optind--;

                                loadedPlugins[i]->opt.flag = (int *)opt;
                                free(opt);
                            }
                        }
                        else
                        {
                            if (debugOutputEnabled == 1)
                                fprintf(stderr, "Длинная опция %s требует аргумент. Выход.\n", long_options_only[option_index].name);

                            writeLog("Длинная опция ");
                            writeLog((char *)long_options_only[option_index].name);
                            writeLog(" требует аргумент. Выход.\n");
                            exit(100);
                        }

                        break;
                    }

                }
                break;
            case ':':
                fprintf(stderr, "Одна из опций, поддерживаемых лагином с обязательным аргументом указана без него.\n");
                exit(581);
            default:
                break;
        }

    } while (opt >= 0);

    filesRecursively(searchPath);

    if (any_result == 0)
        fprintf(stdout, "Файлов, удвлетворяющих условиям поиска, не найдено\n");

    writeLog("Очистка\n");
    if (debugOutputEnabled == 1)
        fprintf(stdout, "Очистка\n");

    for (int i = 0;i < (int)loadedPluginsCount; i++)
        dlclose(loadedPlugins[i]->handle);

    free(searchPath);

    for(int i = 0; i < argc; i++)
        free(arguments[i]);

    free(arguments);

    writeLog("Очистка завершена\n");
    if (debugOutputEnabled == 1)
        fprintf(stdout, "Очистка завершена\n");

    closeLog();

    return 0;
}
