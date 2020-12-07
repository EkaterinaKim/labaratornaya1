#include <stdio.h>
#include <getopt.h>

#include <dlfcn.h>
#include <fcntl.h>
#include <stdlib.h>

#include "funcs.h"
#include "plugin_api.h"

// Команда запуска [[опции] каталог]
// задание --mac-addr <набор значений>

char *plugins;

char **files;
int filesCount = 0, filesDone = 0;

char currentWorkDir[PATH_MAX];
int pluginsLength = 0;

struct option *in_opts;
size_t in_opts_len = 0;

int logger = 0;

void filesRecursively(char *path, int mode)
{
    fprintf(stdout, "Каталог: %s\n", path);

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
        p = strncat(p, "/", 1);
        p = strncat(p, dp->d_name, strlen(dp->d_name));

        if (strcmp(dp->d_name, ".") != 0  && strcmp(dp->d_name, "..") != 0)
        {
            // дальше по рекурсии идут только папки
            if (folderCheck(p) == true)
                filesRecursively(p, mode);
            else
            {
                if (mode == 0)
                    filesCount++;
                else
                {
                    files[filesDone] = malloc(sizeof(char) * strlen(p) + 1);
                    files[filesDone] = strcpy(files[filesDone], p);
                    filesDone++;
                }
            }
        }

        free(p);

    }

    closedir(dir);

}

int checkPluginPath(char *path, int mode)
{
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
                    fprintf(stdout, "Найден плагин: %s\n", p1);

                    // пробуем прогрузить и опросить
                    int (*plGetInfo)(struct plugin_info *);

                    int (*plProcFile)(const char *,
                                      struct option *[],
                                      size_t,
                                      char *,
                                      size_t);

                    struct plugin_info ppi;

                    char *error;
                    char *soPath = strcat(strcat(strcat(currentWorkDir, "/"), dir->d_name), ".so");

                    void *handle = dlopen(soPath, RTLD_LAZY);
                    if (!handle)
                    {
                        fprintf(stderr, "Ошибка открытия файла %s: %s\n", dir->d_name, dlerror());
                        continue;
                    }
                    else
                        fprintf(stdout, "Плагин открыт\n");

                    (void) dlerror();

                    plGetInfo = (int (*)(struct plugin_info *))dlsym(handle, "plugin_get_info");
                    if ((error = dlerror()) != NULL)
                    {
                        fprintf(stderr, "Ошибка: %s\n", error);
                        continue;
                    }
                    else
                        fprintf(stdout, "Плагин: адрес функции plugin_get_info найден\n");

                    (void) dlerror();

                    plProcFile = (int (*)(const char *, struct option *[], size_t, char *, size_t))dlsym(handle, "plugin_process_file");
                    if ((error = dlerror()) != NULL)
                    {
                        fprintf(stderr, "Ошибка: %s\n", error);
                        continue;
                    }
                    else
                        fprintf(stdout, "Плагин: адрес функции plugin_process_file найден\n");

                    int v = (*plGetInfo)(&ppi);

                    if (v == 0)
                    {
                        fprintf(stdout, "Плагин %s можно использовать\n\n", ppi.plugin_name);

                        plugins = strdup(soPath);
                        pluginsLength++;

                        if (mode == 1)
                        {
                            char *out_buff = 0;
                            size_t out_buff_len = 0;

                            for (int i = 0; i < filesDone; i++)
                            {
                                int ret = (*plProcFile)(files[i], &in_opts, in_opts_len, out_buff, out_buff_len);

                                if (ret == 0)
                                    fprintf(stdout, "Файл %s удвлетворяет условиям\n", files[i]);
                                else
                                    if (ret == 1)
                                        fprintf(stderr, "Файл %s не удвлетворяет условиям\n", files[i]);
                                    else
                                        fprintf(stderr, "Возникла ошибка! Код %d\n", ret);
                            }

                        }
                    }
                    else
                        fprintf(stderr, "Ошибка! Плагин вернул код ошибки %d. Использование невозможно.\n", v);

                    dlclose(handle);
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
    if (getcwd(currentWorkDir, sizeof(currentWorkDir)) != 0)
        fprintf(stdout, "Рабочий каталог: %s\n", currentWorkDir);
    else
    {
        fprintf(stderr, "Ошибка определения рабочего каталога\n");
        return -1;
    }

    int optionsUnion = 1; //  условие объединения опций - по умолчанию AND = true, OR = false
    char *pluginPath = NULL; // доп каталог для плагинов - может быть не заполенено значение
    char *logPath = NULL; // каталог для логов - может быть не заполенено значение
    char *searchPath = NULL;
    char *mac = NULL;

    char *version = "0.0.1";
    char *help = " -P dir            - каталог с плагинами\n"
                 " -l /path/to/log   - путь к лог-файлу\n"
                 " -C cond           - условие объединения опций. возможные значения AND, OR. значение по умолчанию AND\n"
                 " -N                - инвертирование условий поиска\n"
                 " -v                - вывод версии программы\n"
                 " -h                - вывод справки по опциям\n";

    fprintf(stdout, "Параметры, полученные при запуске:\n");

    int opt = 0;
    int option_index = 0;

    logger = open("./logfile", O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (logger == -1)
    {
        fprintf(stderr, "Ошибка открытия или создания лог-файла\n");
    }

    // разбираем параметры
    do
    {
        static struct option long_options[] = {
            {"PluginPath -P", required_argument, NULL, 'P'}, // 0
            {"LogPath -l", required_argument, NULL, 'l'}, // 1
            {"Concat -C", required_argument, NULL, 'C'}, // 2
            {"Invert -N", no_argument, NULL, 'N'}, // 3
            {"Version -v", no_argument, NULL, 'v'}, // 4
            {"Help -h", no_argument, NULL, 'h'}, // 5
            {"mac-addr", required_argument, NULL, 'm'}, // 6
            {0, 0, 0, 0}
        };

        int ok = 0;

        switch (opt = getopt_long(argc, argv, "P:l:C:Nvh", long_options, &option_index))
        {
            case 'P':
                fprintf(stdout, "параметр %s", long_options[0].name);
                if (optarg)
                {
                    fprintf(stdout, " с аргументом %s\n", optarg);

                    if (folderCheck(optarg))
                        pluginPath = optarg;
                    else
                        fprintf(stderr, "Аргумент не является каталогом\n");
                }
                else
                    fprintf(stderr, "После опции -Р не указан каталог\n");
                break;

            case 'l':
                fprintf(stdout, "параметр %s", long_options[1].name);
                if (optarg)
                {
                    fprintf(stdout, " с аргументом %s\n", optarg);
                    if (folderCheck(optarg))
                    {
                        logPath = strdup(optarg);

                        char *fn = malloc(sizeof(char) * (strlen(logPath) + 9));
                        fn = strcpy(fn, logPath);
                        fn = strncat(fn, "/logfile", 8);

                        if (logger != -1)
                        {
                            close(logger);
                        }

                        logger = open(fn, O_WRONLY | O_APPEND | O_CREAT, 0644);
                        if (logger == -1)
                        {
                            fprintf(stderr, "Ошибка открытия или создания лог-файла по указанному пути\n");
                        }

                        free(fn);
                    }
                    else
                        fprintf(stderr, "Аргумент не является каталогом\n");
                }
                else
                    fprintf(stderr, "После опции -l не указан каталог\n");
                break;

            case 'C':
                fprintf(stdout, "параметр %s", long_options[2].name);
                if (optarg)
                    fprintf(stdout, " с аргументом %s\n", optarg);

                if (strcasecmp(optarg, "AND") == 0)
                    optionsUnion = 1;
                else
                    if (strcasecmp(optarg, "OR") == 0)
                        optionsUnion = 0;
                    else
                    {
                        fprintf(stderr, "Указанное после опции -C значение %s не является корректным\n", optarg);
                        break;
                    }

                fprintf(stdout, " \n");

                if (optionsUnion == 0)
                {
                    if (in_opts_len == 0)
                        in_opts = (struct option *) calloc(1, sizeof(struct option));
                    else
                        in_opts = realloc(in_opts, sizeof (in_opts) * (in_opts_len + 1));

                    in_opts[in_opts_len].name = "Concat";
                    in_opts[in_opts_len].has_arg = no_argument;
                    in_opts[in_opts_len].flag = 0;
                    in_opts[in_opts_len].val = 'C';

                    in_opts_len++;
                }

                break;

            case 'N':
                fprintf(stdout, "параметр %s\n", long_options[3].name);

                if (in_opts_len == 0)
                    in_opts = (struct option *) calloc(1, sizeof(struct option));
                else
                    in_opts = realloc(in_opts, sizeof (in_opts) * (in_opts_len + 1));

                in_opts[in_opts_len].name = "Invert";
                in_opts[in_opts_len].has_arg = no_argument;
                in_opts[in_opts_len].flag = 0;
                in_opts[in_opts_len].val = 'N';

                in_opts_len++;

                break;

            case 'v':
                fprintf(stdout, "параметр %s. Текущая версия %s\n", long_options[4].name, version);
                break;

            case 'h':
                fprintf(stdout, "параметр %s\n%s\n", long_options[5].name, help);
                break;

            case 'm':
                // параметр mac-addr
                if (optarg)
                {
                    fprintf(stdout, "параметр %s ", long_options[6].name);

                    if (validateMacAddress(optarg))
                    {
                        mac = strdup(optarg);
                    }
                    else
                    {
                        // пробуем посмотреть что дальше в строке, если мак записан как 6 групп по 2 символа
                        // объединяем их в одну строку и проверяем
                        // иначе кидаем ошибку проверки мака
                        optind--;
                        char *macAddr = 0;
                        for(int i = 0; optind < argc && *argv[optind] != '-' && i < 6; optind++, i++)
                        {
                            if (strlen(argv[optind]) == 2)
                            {
                                fprintf(stdout, "%s", argv[optind]);
                                strncat(macAddr, argv[optind], 2);
                            }
                        }

                        optind--;

                        fprintf(stdout, "\n");

                        if (validateMacAddress(macAddr))
                        {
                            mac = strdup(macAddr);
                        }
                        else
                            fprintf(stderr, "Аргумент не является mac-адресом\n\n");
                    }

                    if (mac != NULL)
                    {
                        fprintf(stdout, " с аргументом %s \n", mac);

                        if (in_opts_len == 0)
                            in_opts = (struct option *) calloc(1, sizeof(struct option));
                        else
                            in_opts = realloc(in_opts, sizeof(struct option) * (in_opts_len + 1));

                        in_opts[in_opts_len].name = "--mac-addr";
                        in_opts[in_opts_len].has_arg = required_argument;
                        in_opts[in_opts_len].flag = (int *)mac;
                        in_opts[in_opts_len].val = 'm';

                        in_opts_len++;
                    }
                }
                else
                    fprintf(stderr, "После опции --mac-addr не указан аргумент\n");

                break;

            case -1:
                // проверяем наличие каталога
                if (argv[optind] != NULL)
                {
                    searchPath = strdup(argv[optind]);

                    if (!folderCheck(searchPath))
                    {
                        fprintf(stderr, "Не является каталогом\n");
                        break;
                    }
                    else
                    {
                        fprintf(stdout, "Каталог для поиска: %s\n", searchPath);

                        // проверяем заполнен ли мак
                        if (mac == NULL)
                        {
                            fprintf(stderr, "МАС-адрес не заполнен или некорректен\n");
                            break;
                        }

                        // ищем мак по файлам

                        char *sp = malloc(strlen(searchPath) + 1);
                        strcpy(sp, searchPath);
                        filesRecursively(searchPath, 0); // считаем сколько файлов

                        fprintf(stdout, "Files count %d\n", filesCount);

                        files = malloc(sizeof(char*) * filesCount + 1); // выделяем память по числу файлов

                        filesRecursively(sp, 1); // заполняем массив путей к файлам

                        free(sp);

                    }

                    ok = 1;
                }
                else
                {
                    // Если каталога нет - выводим ошибку, справку по опциям и доступным плагинам и выходим

                    // ищем и грузим плагины в текущем каталоге
                    if (checkPluginPath(".", 0) != 0)
                        fprintf(stderr, "Ошибка открытия каталога %s\n", ".");

                    // ищем и грузим плагины в заданном опцией каталоге
                    if (pluginPath != NULL)
                        if (checkPluginPath(pluginPath, 0) != 0)
                            fprintf(stderr, "Ошибка открытия каталога %s\n", pluginPath);

                    fprintf(stderr, "\n\nНе указан каталог для поиска\n");
                    fprintf(stdout, "Справка:\n %s \n", help);
                    fprintf(stdout, "Доступные плагины:\n -----------------\n");

                    fprintf(stdout, "%s\n", plugins);

                    fprintf(stdout, "-----------------\n");

                    break;
                }

                if (ok == 1)
                {
                    char *sp = malloc(sizeof(char) * (strlen(searchPath) + 1));
                    sp = strcpy(sp, searchPath);

                    // ищем и грузим плагины в текущем каталоге
                    if (checkPluginPath(".", 1) != 0)
                        fprintf(stderr, "Ошибка открытия каталога %s\n", ".");

                    // ищем и грузим плагины в заданном опцией каталоге
                    if (pluginPath != NULL)
                        if (checkPluginPath(pluginPath, 1) != 0)
                            fprintf(stderr, "Ошибка открытия каталога %s\n", pluginPath);
                }

                break;

            default:
                fprintf(stdout, "?? getopt возвратило код символа %o ??\n", opt);
        }

    } while (opt >= 0);

    fprintf(stdout, "Очистка\n");

    if (logger != -1)
    {
        close(logger);
    }

    free(plugins);
    free(mac);
    free(searchPath);

    return 0;
}
