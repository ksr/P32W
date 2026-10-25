/* dirent.h -- P32W Unix (D22): directories are read as files of 32-byte entries. */
#ifndef _DIRENT_H
#define _DIRENT_H
struct dirent {
    unsigned d_ino;
    char d_name[29];
};
typedef struct _DIR { int fd; struct dirent ent; } DIR;
DIR *opendir(const char *path);
struct dirent *readdir(DIR *d);
int closedir(DIR *d);
#endif
