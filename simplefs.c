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
#include <pthread.h>

/*
 * ---------------------------------------------------------------------------------------------------------------------
 * Sekcja danych globalnych do wykorzystania we wszystkich funkcjach.
 */
file * open_files = NULL;
pthread_mutex_t open_files_write_mutex = PTHREAD_MUTEX_INITIALIZER;
/*
 * ---------------------------------------------------------------------------------------------------------------------
 * ---------------------------------------------------------------------------------------------------------------------
 */

/**
 * Funkcja zwracająca docelowy rozmiar systemu plików na podstawie rozmiaru bloku i pożądanej liczby bloków danych
 * @return przygotowany master_block, gotowy do umieszczenia go na dysku
 */
master_block get_initial_master_block(unsigned block_size, unsigned number_of_blocks) {
    master_block masterblock;
    masterblock.block_size = block_size;
    masterblock.number_of_blocks = number_of_blocks;
    masterblock.number_of_free_blocks = number_of_blocks;
    masterblock.first_free_block_number = 0;
    masterblock.number_of_bitmap_blocks = ceil((double) number_of_blocks / (block_size * 8));
    masterblock.number_of_inode_table_blocks = ceil((double) number_of_blocks / floor((double) block_size / sizeof(inode)));
    masterblock.data_start_block = 1 + masterblock.number_of_bitmap_blocks + masterblock.number_of_inode_table_blocks;
    masterblock.first_inode_table_block = 1 + masterblock.number_of_bitmap_blocks;
    masterblock.magic_number = SIMPLEFS_MAGIC_NUMBER;
    return masterblock;
}

void _print_masterblock_info(master_block * system_master_block) {
    if (system_master_block == NULL) {
        printf("System master block was not initialized!");
    }
    printf("Master block:\nBlock size: %ud\nNumber of blocks: %ld\nNumber of free blocks: %ld\nFirst free block number: %ld\n"
         "Number of bitmap blocks: %ld", system_master_block->block_size, system_master_block->number_of_blocks,
           system_master_block->number_of_free_blocks, system_master_block->first_free_block_number, system_master_block->number_of_bitmap_blocks);
}

/**
 * Sprawdza czy podany master block jest rzeczywistym master blockiem, poprzez sprawdzenie magic_number
 */

int check_magic_number(master_block * mb) {
    // sprawdzenie magic number
    if (mb->magic_number != SIMPLEFS_MAGIC_NUMBER) {
        return -1;
    }
    return 0;
}

/**
 * Funkcja inicjalizująca główne struktury systemu plików - master block, i-nodes, oraz jeśli wymagane - bitmap.
 * Sprawdza czy wczytany plik jest rzeczywiście systemem plików (przy użyciu magic number), w p.p. zwraca NULL.
 * W przypadku jakiegokolwiek błedu zamyka deskryptor pliku i zwraca NULL.
 *
 * @param fd deskryptor pliku
 * @param init_bitmaps jeśli != 0 inicjalizuje bitmapę
 * @return initialized_structures* wskaźnik na zainicjalizowane strutkury systemu pliku, w przypadku błędu - NULL
 */
initialized_structures * _initialize_structures(int fd, int init_bitmaps) {
    initialized_structures * initialized_structures_pointer = malloc(sizeof(initialized_structures));
    printf("wchodze");
    // zamapowanie master blocka
    master_block * master_block_pointer
            = (master_block *) mmap(NULL, sizeof(master_block), PROT_READ, MAP_SHARED, fd, 0);
    if (master_block_pointer == (master_block *) MAP_FAILED) {
        close(fd);
        return NULL;
    }
    if(check_magic_number(master_block_pointer) == -1) {
        munmap(0, sizeof(master_block));
        close(fd);
        return NULL;
    }

    // pobranie bitmapy
    block_bitmap * block_bitmap_pointer = NULL;
    unsigned int bitmaps_size = 0;
    if (init_bitmaps != 0) {
        bitmaps_size = master_block_pointer->number_of_bitmap_blocks * master_block_pointer->block_size;
        block_bitmap_pointer = (block_bitmap *) mmap( (void *) sizeof(master_block), bitmaps_size,
                                                     PROT_READ | PROT_WRITE, MAP_SHARED, fd, master_block_pointer->block_size);
        if (block_bitmap_pointer == MAP_FAILED) {
            munmap(0, sizeof(master_block));
            close(fd);
            return NULL;
        }
    }

    // inicjalizacja tablicy inodów
    unsigned int inodes_size = master_block_pointer->number_of_inode_table_blocks * master_block_pointer->block_size;
    inode * inodes_table = (inode *) mmap( (void *) sizeof(master_block) + bitmaps_size, inodes_size,
                       PROT_READ | PROT_WRITE, MAP_SHARED, fd, 1 * master_block_pointer->block_size + bitmaps_size);
    if (inodes_table == MAP_FAILED) {
        munmap(0, sizeof(master_block) + bitmaps_size);
        close(fd);
        return NULL;
    }

    initialized_structures_pointer->master_block_pointer = master_block_pointer;
    initialized_structures_pointer->block_bitmap_pointer = block_bitmap_pointer;
    initialized_structures_pointer->inode_table = inodes_table;
    return initialized_structures_pointer;
}

/**
 * Funkcja bezpiecznie usuwa struktury związane z systemem plików.
 * Powinna być wywowałana zawsze po zakończeniu pracy nad strukturami zwróconymi przez funkcję {_initialize_structures}
 */
void _uninitilize_structures(initialized_structures * initialized_structures_pointer) {
    int result = munmap(NULL, initialized_structures_pointer->master_block_pointer->block_size *
                   (1 + initialized_structures_pointer->master_block_pointer->number_of_bitmap_blocks
                    + initialized_structures_pointer->master_block_pointer->number_of_inode_table_blocks));
    printf("Munmap result: %d", result);
}

/**
 * Funkcja czytająca z dysku blok określony numerem bloku z offsetem
 * @return odczytany blok
 */
block* _read_block(int fd, long block_no, long block_offset, long block_size) {
    block* blockRead = malloc(sizeof(block));
    lseek(fd, block_offset * block_size, SEEK_SET);
    read(fd, blockRead, sizeof(block));
    return blockRead;
}

/**
 * Funkcja czytająca inode katalogu głównego /
 * @return odczytany inode
 */
inode* _get_root_inode(int fd, master_block* masterblock) {
    printf("Seeking to %d\n", masterblock->first_inode_table_block * masterblock->block_size);
    lseek(fd, masterblock->first_inode_table_block * masterblock->block_size, SEEK_SET);
    inode* root_inode = malloc(sizeof(inode));
    read(fd, root_inode, sizeof(inode));
    return root_inode;
}

/**
 * Funkcja znajdująca inode pliku znajdującego się w katalogu reprezentowanym przez dany inode
 * @return znaleziony inode
 */
inode* _get_inode_in_dir(int fd, inode* parent_inode, char* name, master_block* masterblock, unsigned long* inode_no) {
    printf("_get_inode_in_dir %c", parent_inode->type);
    if(parent_inode->type != INODE_DIR) {
        return NULL;
    }
    block* dir_block = _read_block(fd, parent_inode->first_data_block, masterblock->data_start_block, masterblock->block_size);
    long i;
    while(1) {
        printf("finding dir");
        for(i = 0; i <= masterblock->block_size - sizeof(file_signature); i += sizeof(file_signature)) {
            file_signature* signature = (file_signature*) (dir_block->data + i * sizeof(char));
            if(strcmp(name, signature->name) == 0) {
                //calculate number of inode table block, where we'll find the right node
                long block_to_read = signature->inode_no / masterblock->block_size;
                //zapameitujemy numer inode w tablicy inodow
                *inode_no = signature->inode_no;
                printf("%d", signature->inode_no);
                block* inode_block = _read_block(fd, block_to_read, masterblock->first_inode_table_block, masterblock->block_size);
                inode* result_inode = malloc(sizeof(inode));
                memcpy(result_inode, inode_block, sizeof(inode) * (signature->inode_no % masterblock->block_size));
                free(dir_block);
                free(inode_block);
                return result_inode;
            }
        }
        if(dir_block->next_data_block == NULL) {
            free(dir_block);
            return NULL;
        }
        dir_block = _read_block(fd, dir_block->next_data_block, masterblock->data_start_block, masterblock->block_size);
    }
}

/**
 * Funkcja znajdująca inode pliku reprezentowanego przez pełną ścieżkę (np. /dir/file)
 * @param inode_no, parametr wyjsciowy, do zwrocenie polozenia inoda w tablicy inodow
 * @return znelziony inode
 */
inode* _get_inode_by_path(char* path, master_block* masterblock, int fd, unsigned long* inode_no) {
    if(path[0] != '/') {
        return NULL;
    }
    if(strlen(path) == 1) {
        //root inode
        *inode_no = 0;
        return _get_root_inode(fd, masterblock);
    }
    unsigned path_index = 1;
    unsigned i = 0;
    char* path_part = malloc(strlen(path) * sizeof(char));
    inode* current_inode = _get_root_inode(fd, masterblock);
    while(1) {
        while(path[path_index] != '/' && path[path_index] != '\0') {
            path_part[i++] = path[path_index++];
        }
        path_part[i] = '\0';
        inode* new_inode = _get_inode_in_dir(fd, current_inode, path_part, masterblock, inode_no); //inode_no sie nie zmieni jak sie okaze ze path_part nie jest juz katalogiem
        free(current_inode);
        if(new_inode == NULL) {
            free(path_part);
            return NULL;
        }
        current_inode = new_inode;
        if(path[path_index] == '\0') {
            break;
        }
    }
    free(path_part);
    return current_inode;
}

/**
 * Funkcja odczytująca master block z dysku
 * @return odczytany master block
 */
master_block* _get_master_block(int fd) {
    printf("get master block. fd = %X\n" + fd);
    lseek(fd, 0, SEEK_SET);
    master_block* masterblock = malloc(sizeof(master_block));
    read(fd, masterblock, sizeof(master_block));
    printf("Read first inode table block: %d\n");
    return masterblock;
}

/**
 * Funkcja zwracająca podaną ścieżkę skróconą o ostatnią część - sluży więc do oddzielenia istniejącej ścieżki
 * od nazwy pliku (folderu), który ma być utworzony
 */
char* _get_path_for_new_file(char* full_path) {
    char* path = malloc(strlen(full_path) * sizeof(char));
    strcpy(path, full_path);
    int i;
    for(i = strlen(path) - 1; i > 0; i--) {
        if(path[i] == '/') {
            path[i] = '\0';
            return path;
        }
    }
    return NULL;
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
    master_block masterblock = get_initial_master_block(block_size, number_of_blocks);
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
    write(fd, &root_inode, sizeof(inode));

    //allocate space
    lseek(fd, fs_size - 1, SEEK_SET);
    write(fd, "\0", 1);
    printf("Allocated %d bytes\n", fs_size);

    close(fd);
    return 0;
}

int simplefs_openfs(char *path) { //Adam
    int fd = open(path, O_RDWR, 0644);
    printf("OPEN FS. fd = %d\n" + fd);
    if(fd == -1) {
        return -1;
    }
    master_block * mb = _get_master_block(fd);
    int magic_check_result = check_magic_number(mb);
    free(mb);
    if(check_magic_number(mb) == -1) {
        close(fd);
        return -1;
    }
    return fd;
}

int simplefs_closefs(int fsfd) { //Adam
    close(fsfd);
    return 0;
}

int simplefs_open(char *name, int mode, int fsfd) { //Michal
    //need to find the right inode. name is a path separated by /
    master_block* masterblock = _get_master_block(fsfd);
    unsigned i = 0;
    unsigned long tmp;
    inode* file_inode = _get_inode_by_path(name, masterblock, fsfd, &tmp);
    if(file_inode == NULL) {
        free(masterblock);
        return FILE_DOESNT_EXIST;
    } else if(file_inode->type != INODE_FILE) {
        free(masterblock);
        free(file_inode);
        return FILE_DOESNT_EXIST;
    }
    //file_inode now points to the real file
    //need to create file struct
    i = 0;
    while(1) {
        file* file_found;
        pthread_mutex_lock(&open_files_write_mutex);
        HASH_FIND_INT( open_files, &i, file_found);
        if(file_found == NULL) {
            break;
        }
        pthread_mutex_unlock(&open_files_write_mutex);
        i++;
    }
    //got the first free descriptor
    file* new_file = malloc(sizeof(file));
    new_file->fd = i;
    new_file->position = 0;
    HASH_ADD_INT(open_files, fd, new_file);
    pthread_mutex_unlock(&open_files_write_mutex);
    free(masterblock);
    return i;
}

int simplefs_unlink(char *name, int fsfd) { //Michal
    return -1;
}

int simplefs_mkdir(char *name, int fsfd) { //Michal
    //separate new dir name from the path
    char* path = _get_path_for_new_file(name);

    /*master_block* masterblock = _get_master_block(fsfd);
    inode* dir_inode = _get_inode_by_path(path, masterblock, fsfd, NULL);

    //now, create a dir under that inode
    block* current_block = _read_block(fsfd, dir_inode->first_data_block, masterblock->data_start_block, masterblock->block_size);
    while(1) {
        int i;
        //lock(current_block)
        for(i = 0; i <= BLOCK_DATA_SIZE - sizeof(file_signature); i += sizeof(file_signature)) {
            file_signature* signature = (file_signature*) (current_block->data + i * sizeof(char));
            if(signature->inode_no == 0) {
                //inode wolny, można zająć!
            }
        }
    }*/
    return -1;
}

int simplefs_creat(char *name, int mode, int fsfd) { //Adam
    int full_path_lenght = strlen(name);
    int i;
    for(i = full_path_lenght; i > 0 && name[i - 1] != '/'; i--) {
    }
    //ścieżka
    char *path = (char*)malloc(i);
    //nazwa
    char *file_name = (char*)malloc(full_path_lenght - i + 1);
    strncpy(path, name, i);
    strncpy(file_name, name + i, full_path_lenght - i);
    path[i] = '\0';
    file_name[full_path_lenght - i] = '\0';
    printf("%d", i);
    printf("%s\n", path);
    printf("%s\n", file_name);

    master_block * masterblock =  _get_master_block(fsfd);
    unsigned long inode_no;
    inode * parent_node = _get_inode_by_path(path, masterblock, fsfd, &inode_no);
    printf("%d", inode_no);
    printf("\n%c\n", parent_node->filename);

    // pobranie bitmapy
    block_bitmap * block_bitmap_pointer = NULL;
    int bitmaps_size = masterblock->number_of_bitmap_blocks * masterblock->block_size;
    //jafcntl(


    free(parent_node);
    free(masterblock);
    free(path);
    free(file_name);
}

int simplefs_read(int fd, char *buf, int len, int fsfd) { //Adam
    return -1;
}

int simplefs_write(int fd, char *buf, int len, int fsfd) { //Mateusz
    return -1;
}

int simplefs_lseek(int fd, int whence, int offset, int fsfd) { //Mateusz
    initialized_structures * initialized_structures_pointer = _initialize_structures(fsfd, 1);
    if (initialized_structures_pointer != NULL) {
        printf("Blad");
        return -1;
    }
    //_print_masterblock_info(initialized_structures_pointer->master_block_pointer);
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

    //_uninitilize_structures(initialized_structures_pointer);
    return 0;
}
