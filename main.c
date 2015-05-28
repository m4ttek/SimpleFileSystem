#include <stdio.h>
#include "simplefs.h"


int main(void) {
    simplefs_init(NULL, 0, 0);
    simplefs_openfs(NULL);
    simplefs_closefs(0);
    //simplefs_open(NULL, READ_MODE, 0);
    simplefs_init("filesystem", 4096, 10);
    printf("%lu", sizeof(inode));
    return 0;
}
