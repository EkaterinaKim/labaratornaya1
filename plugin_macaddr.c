#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <ctype.h>
/*
    Структура, описывающая опцию, поддерживаемую плагином.
*/
struct plugin_option {
    /* Опция в формате, поддерживаемом getopt_long (man 3 getopt_long). */
    struct option opt;

    /* Описание опции, которое предоставляет плагин. */
    const char *opt_descr;
};

/*
    Структура, содержащая информацию о плагине.
*/
struct plugin_info {
    /* Название плагина */
    const char *plugin_name;

    /* Длина списка опций */
    size_t sup_opts_len;

    /* Список опций, поддерживаемых плагином */
    struct plugin_option *sup_opts;
};

/*
    Функция, позволяющая получить информацию о плагине.

    Аргументы:
        ppi - адрес структуры, которую заполняет информацией плагин.

    Возвращаемое значение:
        0 - в случае успеха,
        1 - в случае неудачи (в этом случае продолжать работу с этим плагином нельзя).
*/
int plugin_get_info(struct plugin_info* ppi)
{
    if (ppi == NULL)
        return 1;

    ppi->plugin_name = "mac";
    ppi->sup_opts_len = 1;

    struct plugin_option *po = (struct plugin_option *) calloc(1, sizeof(struct plugin_option));

    po[0].opt.name = "mac-addr";
    po[0].opt.has_arg = 1;
    po[0].opt.flag = 0;
    po[0].opt.val = 'm';

    po[0].opt_descr = "Поиск mac-адреса в файлe";

    ppi->sup_opts = po;

    return 0;
}

/*
    Фунция, позволяющая выяснить, отвечает ли файл заданным критериям.

    Аргументы:
        in_opts - список опций (критериев поиска), которые передаются плагину.
           struct option {
               const char *name;
               int         has_arg;
               int        *flag;
               int         val;
           };
           Поле name используется для передачи имени опции, поле flag - для передачи
           значения опции (в виде строки). Если у опции есть аргумент, поле has_arg
           устанавливается в ненулевое значение.

        in_opts_len - длина списка опций.

        out_buff - буфер, предназначенный для возврата данных от плагина. В случае ошибки
            в этот буфер, если в данном параметре передано ненулевое значение,
            копируется сообщение об ошибке.

        out_buff_len - размер буфера. Если размера буфера оказалось недостаточно, функция
            завершается с ошибкой.

    Возвращаемое значение:
          0 - файл отвечает заданным критериям,
        > 0 - файл НЕ отвечает заданным критериям,
        < 0 - в процессе работы возникла ошибка (код ошибки).
*/
int plugin_process_file(const char *fname,
        struct option *in_opts[],
        size_t in_opts_len,
        char *out_buff,
        size_t out_buff_len)
{
    // открываем файл и ищем в нем адреса (или ищем файлы без адресов)

    FILE *f = fopen(fname, "r");

    if (f)
    {
        int cnt = 0;
        for (char c = getc(f); c != EOF; c = getc(f))
            cnt++;

        fseek(f, 0, SEEK_SET);

        char *file = (char *)malloc((cnt + 1) * sizeof (char));
        int i = 0;

        for (char c = getc(f); c != EOF; c = getc(f), i++)
        {
            *(file + i) = c;
        }
        fclose(f);

        char *macAddr = strdup((char *)in_opts[0]->flag);

        if (macAddr == NULL)
        {
            sprintf(out_buff, "Мac-адрес не заполнен");
            return -1;
        }

        char *macAddr_forTest = strdup((char *)in_opts[0]->flag);
        int j = 0, s = 0;

        // символы должны принадлежать к шестнадцатеричному разряду, т.е. являться одним из: 0 1 2 3 4 5 6 7 8 9 a b c d e f A B C D E F, либо : -
        while (*macAddr_forTest)
        {
            if (isxdigit(*macAddr_forTest))
                j++;
            else
                if (*macAddr_forTest == ':' || *macAddr_forTest == '-')
                {
                    if (j == 0 || j / 2 - 1 != s)
                        break;

                    ++s;
                }
                else
                    s = -1;

            ++macAddr_forTest;
        }

        if ((j == 12 && (s == 5 || s == 0)) == 0)
        {
            sprintf(out_buff, "Не является mac-адресом");
            return -1;
        }

        // мак должен быть в файле
        if (strstr(file, macAddr) != NULL)
            return 0;

    }
    else
    {
        sprintf(out_buff, "Ошибка открытия файла");
        return -1;
    }


    return 1;
}
