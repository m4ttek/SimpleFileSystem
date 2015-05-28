#include "simplefs.h"
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>


int simplefs_init(char * path, int block_size, int number_of_blocks) {
    
    int fd = open(path, O_WRONLY | O_CREAT);
    if(fd == )
}

int simplefs_openfs(char *path) {
    return -1;
}

int simplefs_closefs(int fsfd) {
    return -1;
}

int simplefs_open(char *name, int mode, int fsfd) {
    return -1;
}

int simplefs_unlink(char *name, int fsfd) {
    return -1;
}

int simplefs_mkidr(char *name, int fsfd) {
    return -1;
}

int simplefs_creat(char *name, int mode, int fsfd) {
    return -1;
}

int simplefs_read(int fd, char *buf, int len, int fsfd) {
    return -1;
}

int simplefs_write(int fd, char *buf, int len, int fsfd) {
    return -1;
}

int simplefs_lseek(int fd, int whence, int offser, int fsfd) {
    return -1;
}
