#include <stdio.h>
#include "simplefs.h"
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>


int main(void) {
    //simplefs_init(NULL, 0, 0);
    //simplefs_openfs(NULL);
    //simplefs_closefs(0);

    //int fdfs = open("filesystem", O_RDWR, 0644);
    //simplefs_init("filesystem", 4096, 1024);
    int fdfs = simplefs_openfs("filesystem");
    if(fdfs == -1) {
        printf("Error opening filesystem\n");
        return 1;
    }
    printf("Opened filesystem. FD = %d\n", fdfs);
    //int fd = simplefs_open(NULL, READ_MODE, fdfs);
    //simplefs_lseek(fd, SEEK_CUR, 0, fdfs);
    printf("Doing!\n");

    simplefs_creat("/ada", 0, fdfs);
    //simplefs_init("filesystem", 4096, 10);
    //printf("%lu", sizeof(inode));
    printf("Done!\n");
    return 0;
}
