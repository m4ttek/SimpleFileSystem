#include <stdio.h>
#include "simplefs.h"
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

void init_file_system(char * name) {
    simplefs_init(name, 4096, 1024);
}

int open_file_system(char * name) {
    int fdfs = simplefs_openfs(name);
    if(fdfs == -1) {
        printf("Error opening filesystem\n");
        return 1;
    }
    printf("Opened filesystem. FD = %d\n", fdfs);
    return fdfs;
}

void creat_file(char *filesystem, char *path) {

    int fdfs = open_file_system(filesystem);
    if(fdfs < -1) {
        return;
    }


    if(simplefs_creat(path, fdfs) == FILE_ALREADY_EXISTS) {
        printf("Plik już istnieje!\n");
    }
}

/**
 * Usage: -f file -i - create a filesystem with name 'file'
 * Usage: -f file -o - open a filesystem 'file'
 * Usage: -f file -c path - create a file with path 'path' in a filesystem 'file'
 */
int main(int argc, char ** argv) {
    char *value = 0;
    int option;
    while ((option = getopt (argc, argv, "f:ioc:")) != -1) {
        switch(option) {
            case 'f':
                value = optarg;
                break;
            case 'i':
                if(value != 0) {
                    init_file_system(value);
                }
                break;
            case 'o':
                open_file_system(value);
                break;
            case 'c':
                creat_file(value, optarg);
                break;
        }
    }
    int fdfs = simplefs_openfs("filesystem");
    simplefs_creat("/a.txt", fdfs);
    int fd = simplefs_open("/a.txt", READ_AND_WRITE, fdfs);
    printf("Result of simplefs_open %d\n", fd);
    printf("Wrint to file result %d", simplefs_write(fd, "adam to glupi programista", 15, fdfs));
    char result[20];
    simplefs_lseek(fd, SEEK_SET,0,fdfs);
    printf("Readig from file %d", simplefs_read(fd, result, 10, fdfs));
    result[10] = '\0';
    printf("================Read================\n %s\n", result);

    return 0;
}
