#include <stdio.h>
#include "simplefs.h"
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

int main() {
    simplefs_init("concurrent_write_fs", 4096, 1024);
}
