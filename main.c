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
    return 0;
}
