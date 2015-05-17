#include <stdio.h>
#include "simplefs.h"

int main(void) {
    simplefs_init(NULL, 0, 0);
    simplefs_openfs(NULL);
    simplefs_closefs(0);
    simplefs_open(NULL, READ_MODE, 0);
}
