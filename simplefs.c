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

typedef struct write_params_t {
    int fsfd;               // deskryptor systemu plików
    int fd;                 // deskryptor pliku
    char* data;             // dane do zapisu
    unsigned int data_length;        // długość danych
    int lock_blocks;        // czy mają być zablokowane bloki (czyli co właściwie?)
    long file_offset;       // offset w pisanym pliku (-1 = append)
    int (*for_each_record)(void*, int, void*);
    void* additional_param;
} write_params;

/**
 * Funkcja opakowująca dla mmap, która radzi sobie z alignem stron i zwraca właściwy wskaźnik
 */
void *mmap_enhanced(void *addr, size_t length, int prot, int flags, int fd, off_t offset, unsigned* delta) {
    off_t legal_offset = offset & ~(sysconf(_SC_PAGE_SIZE) - 1);
    *delta = offset - legal_offset;
    return mmap(addr, length + *delta, prot, flags, fd, legal_offset) + *delta;
}

int munmap_enhanced(void *addr, size_t length, unsigned delta) {
    return munmap(addr - delta, length);
}

/**
 * Funkcja zwracająca docelowy rozmiar systemu plików na podstawie rozmiaru bloku i pożądanej liczby bloków danych
 * @return przygotowany master_block, gotowy do umieszczenia go na dysku
 */
master_block get_initial_master_block(unsigned block_size, unsigned number_of_blocks) {
    master_block masterblock;
    printf("Setting block size to %d\n", block_size);
    masterblock.block_size = block_size;
    masterblock.number_of_blocks = number_of_blocks;
    masterblock.number_of_free_blocks = number_of_blocks;
    masterblock.first_free_block_number = 0;
    masterblock.number_of_bitmap_blocks = ceil((double) number_of_blocks / (block_size * 8));
    masterblock.number_of_inode_table_blocks = ceil((double) number_of_blocks / floor((double) block_size / sizeof(inode)));
    masterblock.data_start_block = 1 + masterblock.number_of_bitmap_blocks + masterblock.number_of_inode_table_blocks;
    masterblock.first_inode_table_block = 1 + masterblock.number_of_bitmap_blocks;
    masterblock.first_free_inode = 2; // 0 - root inode, 1 - .lock
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

master_block* _get_master_block(int fd);

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
    master_block* mblock = _get_master_block(fd);
    printf("Master block read without mmap. Block size is %d\n", mblock->block_size);
    printf("mmaped master block pointer. Block size is %d\n", master_block_pointer->block_size);
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
    inode * inodes_table = (inode *) mmap( /*(void *) sizeof(master_block) + bitmaps_size*/NULL, inodes_size,
                       PROT_READ | PROT_WRITE, MAP_SHARED, fd,
                       (1 * master_block_pointer->block_size + bitmaps_size) & ~(sysconf(_SC_PAGE_SIZE) - 1));
    if (inodes_table == MAP_FAILED) {
        perror("mmap");
        printf("Failed params: size = %d, fd = %d, offset = %d\n", inodes_size, fd, 1*master_block_pointer->block_size + bitmaps_size);
        munmap(0, sizeof(master_block) + bitmaps_size);
        close(fd);
        return NULL;
    } else {
        printf("Mmapped! size = %d, offset = %d at 0x%X\n", inodes_size,
                (1 * master_block_pointer->block_size + bitmaps_size) & ~(sysconf(_SC_PAGE_SIZE) - 1));
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
    int result = munmap(initialized_structures_pointer, initialized_structures_pointer->master_block_pointer->block_size *
                   (1 + initialized_structures_pointer->master_block_pointer->number_of_bitmap_blocks
                    + initialized_structures_pointer->master_block_pointer->number_of_inode_table_blocks));
    printf("Munmap result: %d", result);
}

/**
 * Funkcja czytająca z dysku blok określony numerem bloku z offsetem
 * @return odczytany blok
 */
void* _read_block(int fd, long block_no, long block_offset, long block_size) {
    void* block_read = malloc(sizeof(block));
    lseek(fd, (block_no + block_offset) * block_size, SEEK_SET);
    read(fd, block_read, sizeof(block));
    return block_read;
}

/**
 * Funkcja czytająca inode katalogu głównego /
 * @return odczytany inode
 */
inode* _get_root_inode(int fd, master_block* masterblock) {
    printf("Seeking to %lu\n", masterblock->first_inode_table_block * masterblock->block_size);
    printf("first inode table block is %d and block size is %d\n", masterblock->first_inode_table_block, masterblock->block_size);
    lseek(fd, masterblock->first_inode_table_block * masterblock->block_size, SEEK_SET);
    inode* root_inode = malloc(sizeof(inode));
    read(fd, root_inode, sizeof(inode));
    printf("Read root inode. Name is %s. Type is %c and first data block is %lu\n", root_inode->filename, root_inode->type, root_inode->first_data_block);
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
                printf("%lu", signature->inode_no);
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
        printf("In _get_inode_by_path - get root inode. Block size is %d\n", masterblock->block_size);
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
 * Blokuje first free block (ekskluzywnie).
 *
 * @param fd deksryptor systemu plików
 * @param fl niewypełniona struktura flock
 */
int _block_first_free_block(int fd, struct flock * fl) {
    /* F_RDLCK, F_WRLCK, F_UNLCK    */
    fl->l_type = F_WRLCK;
    /* SEEK_SET, SEEK_CUR, SEEK_END */
    fl->l_whence = SEEK_SET;
    fl->l_start = 32;
    fl->l_len = 8;
    fl->l_pid = getpid();
    return fcntl(fd, F_SETLKW, fl);
}

/**
 * Odblokowuje first free block (ekskluzywnie).
 *
 * @param fd deksryptor systemu plików
 * @param fl niewypełniona struktura flock
 */
int _unblock_first_free_block(int fd, struct flock * fl) {
    fl->l_type = F_UNLCK;
    return fcntl(fd, F_SETLK, fl);
}

/**
 * Wyszukuje dostępne wolne bloki, nie jest cross-process-safe.
 *
 * @param initialized_structures_pointer wskaźnik na zainicjalizowane struktury systemu plików
 * @param number_of_free_blocks liczba wolnych bloków do wyszukania
 * @param free_blocks tablica, gdzie zostaną zapisane identyfikatory znalezionych bloków
 */
int _find_free_blocks(initialized_structures * initialized_structures_pointer, unsigned long number_of_free_blocks,
                      unsigned long * free_blocks) {
    master_block * master_block = initialized_structures_pointer->master_block_pointer;
    unsigned long first_free_block_number = master_block->first_free_block_number;
    block_bitmap * block_bitmap_pointer = initialized_structures_pointer->block_bitmap_pointer;

    return 0;
}

/**
 * Pobiera strukturę pliku na podstawie podanego identyfikatora pliku.
 */
file * _get_file_by_fd(int fd) {
    file* file_found;
    HASH_FIND_INT( open_files, &fd, file_found);
    return file_found;
}

/**
 * Ładuje i-node dla podanej struktury pliku.
 */
inode * _load_inode_from_file_structure(initialized_structures * initialized_structures_pointer, file * file_pointer) {
    if (file_pointer == NULL) {
        return NULL;
    }
    return initialized_structures_pointer->inode_table + (sizeof(inode) * file_pointer->inode_no);
}

void _get_blocks_numbers_taken_by_file(int fsfd, unsigned int first_block_no, master_block * master_block_pointer, unsigned int * blocks_table) {
    int i = 0;
    blocks_table[i++] = first_block_no;
    block * block_pointer = _read_block(fsfd, first_block_no, master_block_pointer->data_start_block, master_block_pointer->block_size);
    while (block_pointer->next_data_block != 0) {
        block_pointer = _read_block(fsfd, block_pointer->next_data_block, master_block_pointer->data_start_block, master_block_pointer->block_size);
        blocks_table[i++] = block_pointer->next_data_block;
    }
}

void _lock_file_blocks(int fsfd, unsigned int * blocks_table) {

}

void _unlock_file_blocks(int fsfd, unsigned int * blocks_table) {

}

/**
 * Podstawowa funkcja zapisująca podany blok danych do wskazanego pliku.
 * W normalnym przypadku pozwala na równoległy zapis wielu procesów, podanie file_offset < 0 powoduje dodatkowe
 * zablokowanie bloków danych, do których ta funkcja pisze (czyli robi append, przydatne w przypadku katalogów).
 * Na początku działania funkcji blokowany jest first free node w strukturze master block, tak aby możliwe było
 * poprawne przydzielenie wymaganych bloków dla funkcji. Po znalezieniu takich bloków i zmianie w strukturze bitmapy
 * następuje odblokowanie first free node.
 */
int _write_unsafe(initialized_structures * initialized_structures_pointer, write_params params) {
    struct flock flock_structure;
    // blokada first free block
    _block_first_free_block(params.fsfd, &flock_structure);

    master_block * master_block_pointer = initialized_structures_pointer->master_block_pointer;
    unsigned int real_block_size = master_block_pointer->block_size - 8;

    // załadowanie odpowiedniej struktury inode
    file * file_structure = _get_file_by_fd(params.fd);
    inode * file_inode = _load_inode_from_file_structure(initialized_structures_pointer, file_structure);
    unsigned long file_size = file_inode->size;

    // sprawdzenie poprawności dostępu do pliku
    if (file_structure->mode == READ_MODE) {
        _unblock_first_free_block(params.fsfd, &flock_structure);
        return WRONG_MODE;
    }

    // wyznaczenie prawdziwego offsetu dla pliku (< 0 => append)
    unsigned int real_file_offset = 0;
    if (params.file_offset < 0) {
        real_file_offset = file_size;
    } else {
        real_file_offset = (unsigned int) params.file_offset;
    }

    // wyznaczenie ile aktualnie zajmuje plik, a ile może zajmować po operacji zapisu
    unsigned int number_of_all_taken_blocks_by_file = ceil((double) file_size / real_block_size);
    unsigned int number_of_blocks_to_be_taken_by_file =
            ceil(((double)real_file_offset + (double) params.data_length) / real_block_size);
    // przypadek specjalny - pierwszy zapis do nowo utworzonego pliku
    if (number_of_blocks_to_be_taken_by_file == 0) {
        number_of_blocks_to_be_taken_by_file = 1;
    }
    unsigned int * blocks_table = NULL;
    if (number_of_all_taken_blocks_by_file >= number_of_blocks_to_be_taken_by_file) {
        blocks_table = (unsigned int *) malloc(sizeof(unsigned int) * number_of_all_taken_blocks_by_file);
    } else {
        blocks_table = (unsigned int *) malloc(sizeof(unsigned int) * number_of_blocks_to_be_taken_by_file);
    }
    // czy trzeba wyszukać nowe bloki danych dla pliku
    if ((number_of_blocks_to_be_taken_by_file > number_of_all_taken_blocks_by_file)
        || file_inode->first_data_block == 0) {
        // wyszukanie nowych bloków danych
        unsigned int number_of_free_blocks = number_of_blocks_to_be_taken_by_file - number_of_all_taken_blocks_by_file;
        _find_free_blocks(initialized_structures_pointer, number_of_free_blocks, blocks_table + number_of_all_taken_blocks_by_file);
    }

    // czy zablokować dodatkowo wszystkie bloki danych, do których funkcja będzie zapisywać dane
    if (params.lock_blocks) {
        _get_blocks_numbers_taken_by_file(params.fsfd, file_inode->first_data_block, master_block_pointer, blocks_table);
        _lock_file_blocks(params.fsfd, blocks_table);
    }

    // zapis nowej długości pliku
    if (file_size >= real_file_offset + (unsigned long)params.data_length) {
        file_inode->size = file_size;
    } else {
        file_inode->size = real_file_offset + (unsigned long)params.data_length;
    }

    // odblokowanie first free node
    _unblock_first_free_block(params.fsfd, &flock_structure);

    // operacja zapisu do pliku

    // odblokowanie zablokowanych bloków danych (jeśli było to żądane)
    if (params.lock_blocks) {
        _unlock_file_blocks(params.fsfd, blocks_table);
    }
    free(blocks_table);
    return 0;
}

/**
 * Funkcja odczytująca master block z dysku
 * @return odczytany master block
 */
master_block* _get_master_block(int fd) {
    printf("get master block. fd = %d\n", fd);
    lseek(fd, 0, SEEK_SET);
    master_block* masterblock = malloc(sizeof(master_block));
    read(fd, masterblock, sizeof(master_block));
    printf("Read first inode table block: %d\n", masterblock->first_inode_table_block);
    printf("Sizeof block_size is %d\n", sizeof(masterblock->block_size));
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

/**
 * Funkcja umieszczająca bezpiecznie inode w pierwszym wolnym miejscu
 * @return numer umieszczonego inode'u
 */
unsigned long _insert_new_inode(inode* new_inode, initialized_structures* structures, int fd) {
    struct flock lock;
    lock.l_type = F_RDLCK + F_WRLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = FIRST_FREE_INODE_OFFSET;
    lock.l_len = sizeof(structures->master_block_pointer->first_free_inode);
    lock.l_pid = getpid();
printf("Writing new inode: masterblock pointer: %d\n", structures->master_block_pointer);
    //lock first free inode in master block
    fcntl(fd, F_SETLKW, &lock);
    printf("Writing new inode: masterblock pointer: %d\n", structures->master_block_pointer);
    unsigned long inode_no = structures->master_block_pointer->first_free_inode;
    structures->inode_table[inode_no] = *new_inode;
    //now need to find new next free inode
    unsigned long i;
    for(i = inode_no + 1; i < (structures->master_block_pointer->number_of_inode_table_blocks * structures->master_block_pointer->block_size)
            / sizeof(inode); i++) {
        if(structures->inode_table[i].type == 'E') {
            structures->master_block_pointer->first_free_inode = i;
            lock.l_type = F_UNLCK;
            fcntl(fd, F_SETLK, &lock);
            return inode_no;
        }
    }
    //no free inodes!
    //unlock
    lock.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &lock);
    return 0;
    /*int block_no = inode_no/INODES_IN_BLOCK;
    while(block_no < masterblock->data_start_block) {
        inode* inodes_in_block = (inode*) _read_block(fd, block_no, current_mb->first_inode_table_block, current_mb->block_size);
        int i;
        for(i = 0; i < masterblock->block_size / sizeof(inode); i++) {
            if(inodes_in_block[i].type == 'E') {
                lseek(fd, FIRST_FREE_INODE_OFFSET, SEEK_SET);
                unsigned long new_first_inode_no = block_no * INODES_IN_BLOCK;
                write(fd, &new_first_inode_no, sizeof(unsigned long));
                //unlock
                lock.l_type = F_UNLCK;
                fcntl(fd, F_SETLK, &lock);
                return inode_no;
            }
        }
    }
    //no free inodes!
    //unlock
    lock.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &lock);
    return 0;*/
}

/**
 * Funkcja sprawdzająca, czy nie istnieje plik o zadanej nazwie w danym bloku
 * @param data_block blok danych
 * @param block_size rozmiar bloku
 * @param name - nazwa pliku, który chcemy utworzyć
 * @return 0, jeśli plik istnieje, 1, jeśli nie
 */
int _check_duplicate_file_names_in_block(block* data_block, int block_size, void* name) {
    file_signature** signatures = (file_signature**) data_block->data;
    int i;
    for(i = 0; i < block_size / sizeof(file_signature); i++) {
        if(signatures[i]->inode_no != 0 && strcmp(signatures[i]->name, (char*) name)) {
            //exists!
            return FALSE;
        }
    }
    return TRUE;
}

int simplefs_init(char * path, unsigned block_size, unsigned number_of_blocks) { //Michał

    if(block_size < 1024) {
        return BLOCK_SIZE_TOO_SMALL;
    }
    if(block_size % sizeof(inode) != 0) {
        return WRONG_BLOCK_SIZE;
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

    //insert space for bitmap
    lseek(fd, (1 + masterblock.number_of_bitmap_blocks) * masterblock.block_size, SEEK_SET);

    //insert root inode
    inode root_inode;
    memset(&root_inode, 0, sizeof(inode));
    strcpy(root_inode.filename, "/");
    root_inode.type = INODE_DIR;
    write(fd, &root_inode, sizeof(inode));

    //allocate space for data
    lseek(fd, fs_size - 1, SEEK_SET);
    write(fd, "\0", 1);
    printf("Allocated %d bytes\n", fs_size);

    close(fd);
    return 0;
}

int simplefs_openfs(char *path) { //Adam
    int fd = open(path, O_RDWR, 0644);
    printf("OPEN FS. fd = %d\n", fd);
    if(fd == -1) {
        return -1;
    }
    master_block * mb = _get_master_block(fd);
    int magic_check_result = check_magic_number(mb);
    if(check_magic_number(mb) == -1) {
        free(mb);
        close(fd);
        return -1;
    }
    free(mb);
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
    } /*else if(file_inode->type != INODE_FILE) {
        free(masterblock);
        free(file_inode);
        return FILE_DOESNT_EXIST;
    }*/
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
    new_file->inode_no = tmp;
    HASH_ADD_INT(open_files, fd, new_file);
    pthread_mutex_unlock(&open_files_write_mutex);
    free(masterblock);
    return i;
}

int simplefs_unlink(char *name, int fsfd) { //Michal
    return -1;
}

int simplefs_mkdir(char *name, int fsfd) { //Michal
    return _create_file_or_dir(name, READ_AND_WRITE, fsfd, TRUE);
    //separate new dir name from the path
    /*char* path = _get_path_for_new_file(name);
    initialized_structures* structures = _initialize_structures(fsfd, 1);
    int fd = simplefs_open(path, READ_AND_WRITE, fsfd);
    if(fd < 0) {
        return PARENT_DIR_DOESNT_EXIST;
    }
    inode new_dir_inode;
    strcpy(new_dir_inode.filename, name);
    new_dir_inode.size = 0;
    new_dir_inode.type = 'D';
    unsigned long inode_no = _insert_new_inode(&new_dir_inode, structures->master_block_pointer, fsfd);

    file_signature new_file_sig;
    new_file_sig.inode_no = inode_no;
    strcpy(new_file_sig.name, name);

    write_params params;
    params.fsfd = fsfd;
    params.fd = fd;
    params.file_offset = -1;
    params.lock_blocks = 1;
    params.data = (char*) (&new_file_sig);
    params.data_length = sizeof(file_signature);
    params.for_each_record = _check_duplicate_file_names_in_block;
    params.record_length = sizeof(file_signature);
    params.additional_param = name;

    _write_unsafe(structures, params);

    free(structures);
    simplefs_close(fd);
    return 0;*/
}

void _try_lock_lock_inode(master_block * mb, int fd) {
    //lock inode
    struct flock lock_inode;
    lock_inode.l_type = F_WRLCK;
    lock_inode.l_whence = SEEK_SET;
    lock_inode.l_start = (mb->first_inode_table_block * mb->block_size + sizeof(inode) * 1);
    lock_inode.l_len = sizeof(inode);
    lock_inode.l_pid = getpid();
    fcntl(fd, F_SETLK, &lock_inode);
}

void _unlock_lock_inode(master_block * mb, int fd) {
    //lock inode
    struct flock lock_inode;
    lock_inode.l_type = F_UNLCK;
    lock_inode.l_whence = SEEK_SET;
    lock_inode.l_start = (mb->first_inode_table_block * mb->block_size + sizeof(inode) * 1);
    lock_inode.l_len = sizeof(inode);
    lock_inode.l_pid = getpid();
    fcntl(fd, F_SETLK, &lock_inode);
}

void _lock_lock_file(master_block * mb, int fd) {
    _try_lock_lock_inode(mb, fd);
    //lock lock block
    struct flock lock_block;
    lock_block.l_type = F_WRLCK;
    lock_block.l_whence = SEEK_SET;
    lock_block.l_start = (mb->data_start_block);
    lock_block.l_len = mb->block_size;
    lock_block.l_pid = getpid();
    fcntl(fd, F_SETLKW, &lock_block);
    unsigned delta;
    int * counter = (int *) mmap_enhanced(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED,
                    fd, mb->data_start_block * mb->block_size, &delta);
    printf("Printing master block data. data start block = %d, block size = %d\n", mb->data_start_block, mb->block_size);
    printf("mmapped counter, address = %d\n", counter);
    printf("Co wychodzi? %X\n", (mb->data_start_block * mb->block_size) & ~(sysconf(_SC_PAGE_SIZE) - 1));
    printf("Counter = %d", *counter);
    (*counter)++;
    printf("Counter = %d", *counter);
    printf("Munmap result = %d\n", munmap_enhanced(counter, sizeof(int), delta));
    //unlock lock block
    lock_block.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &lock_block);
}

void _unlock_lock_file(master_block * mb, int fd) {
    //lock lock block
    struct flock lock_block;
    lock_block.l_type = F_WRLCK;
    lock_block.l_whence = SEEK_SET;
    lock_block.l_start = (mb->data_start_block);
    lock_block.l_len = mb->block_size;
    lock_block.l_pid = getpid();
    fcntl(fd, F_SETLKW, &lock_block);
    unsigned delta;
    int * counter = (int *) mmap_enhanced(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED,
                    fd, mb->data_start_block * mb->block_size, &delta);
    (*counter)--;
    int value = *counter;
    munmap_enhanced(counter, sizeof(int), delta);
    if(value != 0) {
        _try_lock_lock_inode(mb, fd);
    } else {
        _unlock_lock_inode(mb, fd);
    }
    //unlock lock block
    lock_block.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &lock_block);
}

int simplefs_close(int fd) {
    file* file_found;
    HASH_FIND_INT( open_files, &fd, file_found);
    if(file_found == NULL) {
        return UNKNOWN_DESCRIPTOR;
    }
    HASH_DEL(open_files, file_found);
    free(file_found);
}

int _create_file_or_dir(char *name, int mode, int fsfd, int is_dir) {

    int result = OK;
    int full_path_length = strlen(name);
    int path_length;
    for(path_length = full_path_length; path_length > 0 && name[path_length - 1] != '/'; path_length--) {
    }
    //ścieżka
    char *path = (char*)malloc(path_length + 1);
    //nazwa
    char *file_name = (char*)malloc(full_path_length - path_length + 1);
    strncpy(path, name, path_length);
    int file_name_length;
    if(full_path_length - path_length > FILE_NAME_LENGTH) {
        return NAME_TOO_LONG;
    } else {
        file_name_length = full_path_length - path_length;
    }
    strncpy(file_name, name + path_length, full_path_length - path_length);
    path[path_length] = '\0';
    file_name[file_name_length] = '\0';
    printf("%d", path_length);
    printf("%s\n", path);
    printf("%s\n", file_name);
    //blokujemy plik .lock
    initialized_structures * is = _initialize_structures(fsfd, 0);
    printf("masterblock pointer: %d\n", is->master_block_pointer);
    inode * parent_node;
    printf("Przed seg fault: is = %d\n", is);
    _lock_lock_file(is->master_block_pointer, fsfd);
    do {
        unsigned long tmp;
        parent_node = _get_inode_by_path(path, is->master_block_pointer, fsfd, &tmp);
        if(parent_node == NULL) {
            result = DIR_DOESNT_EXIST;
            break;
        }
        printf("%d", tmp);
        printf("\n%s\n", parent_node->filename);
        inode new_file;
        strcpy(new_file.filename, file_name);
        new_file.type = (is_dir ? 'D' : 'F');
        new_file.size = 0;
        new_file.first_data_block = 0;
        if(mode & READ_AND_WRITE != 0) {
            result = WRONG_MODE;
            break;
        }
        unsigned long inode_no = _insert_new_inode(&new_file, is, fsfd);
        path[path_length] = '\0'; //przerobic katalog na plik
        write_params write_params_value;
        write_params_value.fsfd = fsfd;
        int fd = simplefs_open(path, READ_AND_WRITE, fsfd);
        write_params_value.fd = fd;
        file_signature fs;
        strcpy(fs.name, file_name);
        fs.inode_no = inode_no;
        write_params_value.data = &fs;
        write_params_value.data_length = sizeof(file_signature);
        write_params_value.lock_blocks = TRUE;
        write_params_value.file_offset = -1; //append
        write_params_value.for_each_record = _check_duplicate_file_names_in_block;
        write_params_value.additional_param = file_name;
        _write_unsafe(is, write_params_value);
    } while( 0 );
    _unlock_lock_file(is->master_block_pointer, fsfd);
    _uninitilize_structures(is);
    free(parent_node);
    free(path);
    free(file_name);
    return result;
}

/**
 * TODO: sprawdzanei istnienia
 */
int simplefs_creat(char *name, int mode, int fsfd) { //Adam
    return _create_file_or_dir(name, mode, fsfd, FALSE);
}

int simplefs_read(int fd, char *buf, int len, int fsfd) { //Adam
    return -1;
}

int simplefs_write(int fd, char *buf, int len, int fsfd) { //Mateusz
    initialized_structures * initialized_structures_pointer = _initialize_structures(fsfd, 1);
    if (initialized_structures_pointer != NULL) {
        printf("Blad");
        return -1;
    }
    file * file_pointer = _get_file_by_fd(fd);
    if (file_pointer == NULL) {
        return FD_NOT_FOUND;
    }
    write_params write_params_structure;
    write_params_structure.data_length = len;
    write_params_structure.data = buf;
    write_params_structure.fsfd = fsfd;
    write_params_structure.fd = fd;
    write_params_structure.lock_blocks = 0;
    write_params_structure.file_offset = file_pointer->position;
    int result = _write_unsafe(initialized_structures_pointer, write_params_structure);
    _uninitilize_structures(initialized_structures_pointer);
    return result;
}

int simplefs_lseek(int fd, int whence, int offset, int fsfd) { //Mateusz
    initialized_structures * initialized_structures_pointer = _initialize_structures(fsfd, 1);
    if (initialized_structures_pointer != NULL) {
        printf("Blad");
        return -1;
    }
    file * file_pointer =_get_file_by_fd(fd);
    if (file_pointer == NULL) {
        return FD_NOT_FOUND;
    }
    int effective_offset = 0;
    switch(whence) {
        case SEEK_SET:
            effective_offset = offset;
            break;
        case SEEK_CUR:
            effective_offset = file_pointer->position + offset;
            break;
        case SEEK_END:
            effective_offset = _load_inode_from_file_structure(initialized_structures_pointer, file_pointer)->size + offset;
            break;
        default:
            return - 1;
    }
    file_pointer->position = effective_offset;
    _uninitilize_structures(initialized_structures_pointer);
    return 0;
}
