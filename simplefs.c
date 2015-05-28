#include "simplefs.h"
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <math.h>
#include <stdio.h>
#include <sys/mman.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

/*
 * ---------------------------------------------------------------------------------------------------------------------
 * Sekcja danych globalnych do wykorzystania we wszystkich funkcjach.
 */
master_block * system_master_block = NULL;

inode * inodes_table = NULL;

block_bitmap * bitmaps = NULL;
/*
 * ---------------------------------------------------------------------------------------------------------------------
 * ---------------------------------------------------------------------------------------------------------------------
 */

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

void _print_masterblock_info() {
    if (system_master_block == NULL) {
        printf("System master block was not initialized!");
    }
    printf("Master block:\nBlock size: %d\nNumber of blocks: %d\nNumber of free blocks: %d\nFirst free block number: %d\n"
         "Number of bitmap blocks: %d", system_master_block->block_size, system_master_block->number_of_blocks,
           system_master_block->number_of_free_blocks, system_master_block->first_free_block_number, system_master_block->number_of_bitmap_blocks);
}

/**
 * Funkcja inicjalizująca główne struktury systemu plików - master block, i-nodes, oraz jeśli wymagane - bitmap.
 * Sprawdza czy wczytany plik jest rzeczywiście systemem plików (przy użyciu magic number), w p.p. zwraca kod błedu.
 * W przypadku jakiegokolwiek błedu zamyka deskryptor pliku i zwraca kod błędu.
 *
 * @param fd deskryptor pliku
 * @return 0 - wszystko ok,
 */
int _initialize_structures(int fd, int init_bitmaps) {
    // zamapowanie master blocka
    system_master_block = (master_block *) mmap(NULL, sizeof(master_block), PROT_READ, MAP_SHARED, fd, 0);
    if (system_master_block == (master_block *) MAP_FAILED) {
        close(fd);
        return -1;
    }
    // sprawdzenie magic number
    if (system_master_block->magic_number != SIMPLEFS_MAGIC_NUMBER) {
        close(fd);
        return -1;
    }
    // inicjalizacja tablicy inodów
    unsigned int inodes_size = system_master_block->number_of_inode_table_blocks * system_master_block->block_size;
    inodes_table = (inode *) mmap(sizeof(master_block), inodes_size,
                       PROT_READ | PROT_WRITE, MAP_SHARED, fd, 1 * system_master_block->block_size);
    if (inodes_table == MAP_FAILED) {
        close(fd);
        return -1;
    }

    // pobranie bitmapy
    if (init_bitmaps != 0) {
        unsigned int bitmaps_size = system_master_block->number_of_bitmap_blocks * system_master_block->block_size;
        bitmaps = (block_bitmap *) mmap(sizeof(master_block) + inodes_size, bitmaps_size,
                       PROT_READ | PROT_WRITE, MAP_SHARED, fd, inodes_size + 1 * system_master_block->block_size);
        if (bitmaps == MAP_FAILED) {
            close(fd);
            return -1;
        }
    }
    return 0;
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

int simplefs_lseek(int fd, int whence, int offset, int fsfd) { //Mateusz
    if (_initialize_structures(fsfd, 1) < 0) {
        printf("Blad");
        return -1;
    }
    _print_masterblock_info();
    int effective_offset = 0;
    switch(whence) {
        case SEEK_SET:
            effective_offset = offset;
            break;
        case SEEK_CUR:
            effective_offset = 1;
            break;
        case SEEK_END:
            break;
        default:
            return - 1;
    }

    return 0;
}
