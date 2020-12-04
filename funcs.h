#ifndef FUNCS_H
#define FUNCS_H

#include <stdbool.h>
#include <ctype.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

int validateMacAddress(const char* mac)
{
    // символы должны принадлежать к шестнадцатеричному разряду, т.е. являться одним из: 0 1 2 3 4 5 6 7 8 9 a b c d e f A B C D E F, либо : -

    if (mac == NULL)
        return 0;

    int i = 0;
    int s = 0;

    while (*mac)
    {
        if (isxdigit(*mac))
            i++;
        else
            if (*mac == ':' || *mac == '-')
            {
                if (i == 0 || i / 2 - 1 != s)
                    break;

                ++s;
            }
            else
                s = -1;

        ++mac;
    }

    return (i == 12 && (s == 5 || s == 0));
}

bool folderCheck(char *path)
{
    // проверяем папка или нет по указанному пути

    struct stat s;

    int err = stat(path, &s);
    if(-1 != err)
    {
        if(S_ISDIR(s.st_mode))
            return true;
    }

    return false;
}

#endif // FUNCS_H
