#include "simplefs.h"
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <math.h>
#include <stdio.h>

int _file_system_size(int block_size, int number_of_blocks) {
    return 1 +
        ceil((double) number_of_blocks / (block_size * 8)) +
        ceil((double) number_of_blocks / floor((double) block_size / sizeof(inode))) +
        number_of_blocks;
}

int simplefs_init(char * path, int block_size, int number_of_blocks) { //Michał
    
    int fd = open(path, O_WRONLY | O_CREAT, 0644);
    if(fd == -1) {
        return -1;
    }
    lseek(fd, _file_system_size(block_size, number_of_blocks));
    printf("dupa: %d ", _file_system_size(block_size, number_of_blocks));
    close(fd);
   // if(fd == )
}

int simplefs_openfs(char *path) { //Adam
    return -1;
}

int simplefs_closefs(int fsfd) { //Adam
    return -1;
}

int simplefs_open(char *name, int mode, int fsfd) { //Michal
    return -1;
}

int simplefs_unlink(char *name, int fsfd) { //Michal
    return -1;
}

int simplefs_mkdir(char *name, int fsfd) { //Michal
    return -1;
}

int simplefs_creat(char *name, int mode, int fsfd) { //Adam
    return -1;
}

int simplefs_read(int fd, char *buf, int len, int fsfd) { //Adam
    return -1;
}

int simplefs_write(int fd, char *buf, int len, int fsfd) { //Mateusz
    return -1;
}

int simplefs_lseek(int fd, int whence, int offser, int fsfd) { //Mateusz
    return -1;
}
