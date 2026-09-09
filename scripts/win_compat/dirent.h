/* scripts/win_compat/dirent.h -- minimal <dirent.h> for the MSVC-target
 * cross build (scripts/build_win_cross.sh).
 *
 * The MSVC CRT has no <dirent.h>, and pp/inc/pp_include_paths.c uses
 * opendir/readdir/closedir to probe subdirectories of an include path.
 * MinGW ships dirent.h, so only the MSVC-ABI build needs this: a thin
 * FindFirstFileA/FindNextFileA wrapper providing just the fields that file
 * reads (d_name).
 *
 * This directory is NOT part of cmpl's include/ set: it is build support
 * for the host compiler, added with -I in the cross build only.
 */

#ifndef WIN_COMPAT_DIRENT_H
#define WIN_COMPAT_DIRENT_H

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

typedef struct dirent {
    char d_name[MAX_PATH];
} dirent;

typedef struct DIR {
    HANDLE          h;
    WIN32_FIND_DATAA fd;
    struct dirent   ent;
    int             first;
} DIR;

static DIR*
opendir(const char* path)
{
    char pat[MAX_PATH * 2];
    DIR* d = (DIR*)calloc(1, sizeof(DIR));

    if (!d) return NULL;

    _snprintf(pat, sizeof(pat), "%s\\*", path);
    d->h = FindFirstFileA(pat, &d->fd);
    if (d->h == INVALID_HANDLE_VALUE) {
        free(d);
        return NULL;
    }
    d->first = 1;
    return d;
}

static struct dirent*
readdir(DIR* d)
{
    if (!d) return NULL;

    if (!d->first && !FindNextFileA(d->h, &d->fd)) return NULL;
    d->first = 0;

    _snprintf(d->ent.d_name, sizeof(d->ent.d_name), "%s", d->fd.cFileName);
    return &d->ent;
}

static int
closedir(DIR* d)
{
    if (!d) return -1;
    FindClose(d->h);
    free(d);
    return 0;
}

#endif /* WIN_COMPAT_DIRENT_H */
