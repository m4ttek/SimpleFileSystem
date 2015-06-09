#include <stdio.h>
#include "simplefs.h"
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#define LEN 500000
#define NUM_OF_FILES 10
char msg[LEN];

int main() {
    unlink("concurent_creat_fs");
    simplefs_init("concurrent_creat_fs", 4096, 1024);
    if(fork() == 0) {
        int fsfd = simplefs_openfs("concurrent_creat_fs");
        char name[10] = "/a00.txt";
        int i = 0;
        for(i = 0; i < NUM_OF_FILES; ++i) {
            name[2] = '0' + i / 10;
            name[3] = '0' + i % 10;
            simplefs_creat(name, fsfd);
            printf("US DZIEC %d", simplefs_unlink(name, fsfd));
        }
        sleep(1);
        char drop[10] = "/b00.txt";
        for(i = 0; i < NUM_OF_FILES; ++i) {
            drop[2] = '0' + i / 10;
            drop[3] = '0' + i % 10;
            printf("try unlink %s child %d\n", drop, simplefs_unlink(drop, fsfd));
        }
        printf("dziecko koniec\n");
    } else {
        int fsfd = simplefs_openfs("concurrent_creat_fs");
        char name[10] = "/b00.txt";
        int i = 0;
        for(i = 0; i < NUM_OF_FILES; ++i) {
            name[2] = '0' + i / 10;
            name[3] = '0' + i % 10;
            simplefs_creat(name, fsfd);
            printf("US OJC %d", simplefs_unlink(name, fsfd));
        }
        sleep(1);
        char drop[10] = "/a00.txt";
        for(i = 0; i < NUM_OF_FILES; ++i) {
            drop[2] = '0' + i / 10;
            drop[3] = '0' + i % 10;
            printf("try unlink %s father %d\n", drop, simplefs_unlink(drop, fsfd));
        }
        printf("ojciec koniec\n");
    }
    return 0;
}
