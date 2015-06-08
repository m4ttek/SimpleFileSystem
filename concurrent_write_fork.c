#include <stdio.h>
#include "simplefs.h"
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#define LEN 500000

char msg[LEN];

int main() {
    if(fork() == 0) {
        int fsfd = simplefs_openfs("concurrent_write_fs");
        simplefs_creat("/a.txt", fsfd);
        int fd;
        fd = simplefs_open("/a.txt", WRITE_MODE ,fsfd);
        int i;
        for(i = 0; i < LEN; ++i) {
            msg[i] = 'a';
        }
        simplefs_write(fd, msg, LEN, fsfd);
        simplefs_close(fd);
        sleep(2); //niech oba zdaza zapisac
        fd = simplefs_open("/b.txt", READ_MODE, fsfd);
        simplefs_read(fd, msg, LEN, fsfd);
        for(i = 0; i < LEN; ++i) {
            if(msg[i] != 'b') {
                printf("Blad odczytu");
            }
        }
        simplefs_close(fd);
        printf("dziecko koniec");
    } else {
        int fsfd = simplefs_openfs("concurrent_write_fs");
        simplefs_creat("/b.txt", fsfd);
        int fd;
        fd = simplefs_open("/b.txt", WRITE_MODE ,fsfd);
        int i;
        for(i = 0; i < LEN; ++i) {
            msg[i] = 'b';
        }
        simplefs_write(fd, msg, LEN, fsfd);
        simplefs_close(fd);
        sleep(2); //niech oba zdaza zapisac
        fd = simplefs_open("/a.txt", READ_MODE, fsfd);
        simplefs_read(fd, msg, LEN, fsfd);
        for(i = 0; i < LEN; ++i) {
            if(msg[i] != 'a') {
                printf("Blad odczytu");
            }
        }
        simplefs_lseek(fd, SEEK_SET, 0, fsfd);
        printf("ojciec koniec");
    }
}
