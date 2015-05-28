#include "simplefs.h"
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <math.h>
#include <stdio.h>
#include <sys/mman.h>
#include <string.h>

/**
 * Funkcja zwracająca docelowy rozmiar systemu plików na podstawie rozmiaru bloku i pożądanej liczby bloków danych
 * @return iilość bajtów, które będzie zajmował system plików
 */
master_block _get_masterblock(unsigned block_size, unsigned number_of_blocks) {
    master_block masterblock;
    masterblock.block_size = block_size;
    masterblock.number_of_blocks = number_of_blocks;
    masterblock.number_of_free_blocks = number_of_blocks;
    masterblock.first_free_block_number = 0;
    masterblock.number_of_bitmap_blocks = ceil((double) number_of_blocks / (block_size * 8));
    masterblock.number_of_inode_table_blocks = ceil((double) number_of_blocks / floor((double) block_size / sizeof(inode)));
    masterblock.data_start_block = 1 + masterblock.number_of_bitmap_blocks + masterblock.number_of_inode_table_blocks;
    masterblock.magic_number = SIMPLEFS_MAGIC_NUMBER;
    return masterblock;
}

int simplefs_init(char * path, unsigned block_size, unsigned number_of_blocks) { //Michał
    
    if(block_size < 1024) {
        return BLOCK_SIZE_TOO_SMALL;
    }
    
    if(number_of_blocks == 0) {
        return NUMBER_OF_BLOCKS_ZERO;
    }
    
    int fd = open(path, O_WRONLY | O_CREAT, 0644);
    if(fd == -1) {
        return HOST_FILE_ACCESS_ERROR;
    }
    
    //get master block
    master_block masterblock = _get_masterblock(block_size, number_of_blocks);
    unsigned fs_size = (1 + masterblock.number_of_bitmap_blocks + masterblock.number_of_inode_table_blocks + 
            masterblock.number_of_blocks) * masterblock.block_size;
    
    //insert master block
    write(fd, &masterblock, sizeof(master_block));
    printf("Masterblock written\n");
    
    //insert root inode
    inode root_inode;
    memset(&root_inode, 0, sizeof(inode));
    strcpy(root_inode.filename, "/");
    root_inode.type = INODE_DIR;
    root_inode.is_open = FALSE;
    write(fd, &root_inode, sizeof(inode));
    
    //allocate space
    lseek(fd, fs_size - 1, SEEK_SET);
    write(fd, "\0", 1);
    printf("Allocated %d bytes\n", fs_size);
    
    close(fd);
}

int simplefs_openfs(char *path) { //Adam
    return -1;
}

int simplefs_closefs(int fsfd) { //Adam
    return -1;
}

int simplefs_open(char *name, int mode, int fsfd) { //Michal
    //need to find the right inode. name is a path separated by 
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
