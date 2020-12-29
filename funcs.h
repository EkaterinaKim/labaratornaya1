#ifndef FUNCS_H
#define FUNCS_H

#include <stdbool.h>
#include <ctype.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

bool folderCheck(char *path)
{
    // проверяем папка или нет по указанному пути

    struct stat s;

    int err = stat(path, &s);
    if(-1 != err)
    {
//        if(S_ISDIR(s.st_mode))
        if(S_IFDIR & s.st_mode)
            return true;
    }

    return false;
}

#endif // FUNCS_H
