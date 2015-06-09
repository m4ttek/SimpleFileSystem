#include <stdio.h>
#include "simplefs.h"
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#define LEN 500000

char msg[LEN];

int main() {
    unlink("concurent_write_unlink_fs");
    simplefs_init("concurrent_write_unlink_fs", 4096, 1024);
    if(fork() == 0) {
        int fsfd = simplefs_openfs("concurrent_write_unlink_fs");
        simplefs_creat("/creation", fsfd);
        int fd;
        fd = simplefs_open("/creation", WRITE_MODE ,fsfd);
        int i;
        for(i = 0; i < LEN; ++i) {
            msg[i] = 'a';
            if(i % 1000 == 0) {
                usleep(100);
            }
        }
        simplefs_write(fd, msg, LEN, fsfd);
        simplefs_close(fd);
        sleep(2); //niech oba zdaza zapisac
        printf("result %d\n", simplefs_open("/creation", READ_MODE, fsfd));
        printf("dziecko koniec\n");
    } else {
        int fsfd = simplefs_openfs("concurrent_write_unlink_fs");
        usleep(1000);
        printf("result unlink %d", simplefs_unlink("/creation", fsfd));
        printf("ojciec koniec\n");
    }
    return 0;
}
