/* Minimal POSIX dirent.h stub for Cmpl self-hosting.
 *
 * Only the subset used by pp_include.c (directory iteration for the
 * include-path subdirectory search) is declared.  The layout mirrors
 * the glibc/x86_64 struct dirent so that d_name is at the same offset
 * as the real system header when the self-hosted binary links libc.
 */

#ifndef _DIRENT_H
#define _DIRENT_H

typedef struct _DIR DIR;

struct dirent {
    unsigned long d_ino;       /* inode number */
    long          d_off;       /* offset to next dirent */
    unsigned short d_reclen;   /* length of this record */
    unsigned char d_type;      /* file type */
    char          d_name[256]; /* filename (null-terminated) */
};

DIR*           opendir(const char* name);
struct dirent* readdir(DIR* dirp);
int            closedir(DIR* dirp);

#endif
