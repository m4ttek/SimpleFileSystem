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

#define DEBUG //printf

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
    memset(&masterblock, 0, sizeof(master_block));
    DEBUG("Setting block size to %d\n", block_size);
    masterblock.block_size = block_size;
    masterblock.number_of_blocks = number_of_blocks;
    masterblock.number_of_free_blocks = number_of_blocks - 1;
    masterblock.first_free_block_number = 1;
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
        DEBUG("System master block was not initialized!");
    }
    DEBUG("Master block:\nBlock size: %ud\nNumber of blocks: %ld\nNumber of free blocks: %ld\nFirst free block number: %ld\n"
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

unsigned long _get_block_offset(master_block * master_block_pointer, unsigned long block_number) {
    return master_block_pointer->block_size * (block_number + master_block_pointer->data_start_block);
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
    // zamapowanie master blocka
    master_block * master_block_pointer
            = (master_block *) mmap(NULL, sizeof(master_block), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (master_block_pointer == (master_block *) MAP_FAILED) {
        perror("mmap");
        close(fd);
        return NULL;
    }
    if(check_magic_number(master_block_pointer) == -1) {
        munmap(master_block_pointer, sizeof(master_block));
        close(fd);
        return NULL;
    }
    DEBUG("mmaped master block pointer. Block size is %d\n", master_block_pointer->block_size);
    // pobranie bitmapy
    char * block_bitmap_pointer = NULL;
    unsigned int bitmaps_size = 0;
    unsigned bitmap_delta;
    if (init_bitmaps != 0) {
        bitmaps_size = master_block_pointer->number_of_bitmap_blocks * master_block_pointer->block_size;
        block_bitmap_pointer = (char *) mmap_enhanced(NULL, bitmaps_size,
                                                     PROT_READ | PROT_WRITE, MAP_SHARED, fd, master_block_pointer->block_size, &bitmap_delta);
        if (block_bitmap_pointer == MAP_FAILED) {
            munmap(master_block_pointer, sizeof(master_block));
            close(fd);
            return NULL;
        }
    }

    // inicjalizacja tablicy inodów
    unsigned int inodes_size = master_block_pointer->number_of_inode_table_blocks * master_block_pointer->block_size;
    unsigned inode_delta;
    inode * inodes_table = (inode *) mmap_enhanced( NULL, inodes_size,
                       PROT_READ | PROT_WRITE, MAP_SHARED, fd, 1 * master_block_pointer->block_size + bitmaps_size, &inode_delta);
    if (inodes_table == MAP_FAILED) {
        perror("mmap");
        DEBUG("Failed params: size = %d, fd = %d, offset = %d\n", inodes_size, fd, 1*master_block_pointer->block_size + bitmaps_size);
        munmap(master_block_pointer, sizeof(master_block));
        munmap_enhanced(block_bitmap_pointer, bitmaps_size, bitmap_delta);
        close(fd);
        return NULL;
    }

    initialized_structures_pointer->master_block_pointer = master_block_pointer;
    initialized_structures_pointer->block_bitmap_pointer = block_bitmap_pointer;
    initialized_structures_pointer->inode_table = inodes_table;
    initialized_structures_pointer->bitmap_delta = bitmap_delta;
    initialized_structures_pointer->inode_delta = inode_delta;
    DEBUG("Initalized structures!\n");
    return initialized_structures_pointer;
}

/**
 * Funkcja bezpiecznie usuwa struktury związane z systemem plików.
 * Powinna być wywowałana zawsze po zakończeniu pracy nad strukturami zwróconymi przez funkcję {_initialize_structures}
 */
void _uninitilize_structures(initialized_structures * initialized_structures_pointer) {
    DEBUG("Uninitializing structs");
    master_block* mb = initialized_structures_pointer->master_block_pointer;
    int result = munmap_enhanced(initialized_structures_pointer->block_bitmap_pointer,
            mb->number_of_bitmap_blocks * mb->block_size, initialized_structures_pointer->bitmap_delta);
    DEBUG("Munmap result: %d", result);
    result = munmap_enhanced(initialized_structures_pointer->inode_table,
            mb->number_of_inode_table_blocks * mb->block_size, initialized_structures_pointer->inode_delta);
    DEBUG("Munmap result: %d", result);
    result = munmap(initialized_structures_pointer->master_block_pointer, sizeof(master_block));
    DEBUG("Munmap result: %d", result);
    /*munmap(initialized_structures_pointer->master_block_pointer, sizeof(master_block));
    int result = munmap(initialized_structures_pointer, initialized_structures_pointer->master_block_pointer->block_size *
                   (1 + initialized_structures_pointer->master_block_pointer->number_of_bitmap_blocks
                    + initialized_structures_pointer->master_block_pointer->number_of_inode_table_blocks));*/
    //DEBUG("Munmap result: %d", result);
}

/**
 * Funkcja czytająca z dysku blok określony numerem bloku z offsetem
 * @return odczytany blok
 */
void* _read_block(int fsfd, long block_no, long block_offset, long block_size) {
    char* block_data = malloc(block_size-sizeof(unsigned long));
    block* block_read = malloc(sizeof(block));
    lseek(fsfd, (block_no + block_offset) * block_size, SEEK_SET);
    //read data
    read(fsfd, block_data, block_size - sizeof(unsigned long));
    //read next data block
    read(fsfd, &(block_read->next_data_block), sizeof(unsigned long));
    block_read->data = block_data;
    return block_read;
}

/**
 * Funkcja czytająca z dysku dla danego bloku tylko jego następnik
 * @return numer następnego bloku
 */
unsigned long _find_next_block(int fd, long block_no, long block_offset, long block_size) {
    unsigned long next_block = 0;
    lseek(fd, (block_no + block_offset) * block_size + block_size - sizeof(unsigned long), SEEK_SET);
    //read data
    read(fd, &next_block, sizeof(unsigned long));
    return next_block;
}

void free_block_struct(block* bl) {
    free(bl->data);
    free(bl);
}

/**
 * Funkcja czytająca inode katalogu głównego /
 * @return odczytany inode
 */
inode* _get_root_inode(int fd, master_block* masterblock) {
    DEBUG("Seeking to %lu\n", masterblock->first_inode_table_block * masterblock->block_size);
    DEBUG("first inode table block is %d and block size is %d\n", masterblock->first_inode_table_block, masterblock->block_size);
    lseek(fd, masterblock->first_inode_table_block * masterblock->block_size, SEEK_SET);
    inode* root_inode = malloc(sizeof(inode));
    read(fd, root_inode, sizeof(inode));
    DEBUG("Read root inode. Name is %s. Type is %c and first data block is %lu\n", root_inode->filename, root_inode->type, root_inode->first_data_block);
    return root_inode;
}

/**
 * Funkcja znajdująca inode pliku znajdującego się w katalogu reprezentowanym przez dany inode
 * @return znaleziony inode
 */
inode* _get_inode_in_dir(int fd, inode* parent_inode, char* name, master_block* masterblock, unsigned long* inode_no) {
    DEBUG("_get_inode_in_dir %c\n", parent_inode->type);

    if(parent_inode->type != INODE_DIR) {
        return NULL;
    }
    block* dir_block = _read_block(fd, parent_inode->first_data_block, masterblock->data_start_block, masterblock->block_size);
    DEBUG("Next data block: %d\n", dir_block->next_data_block);
    long i;
    while(1) {
        DEBUG("finding dir: next data block: %d\n", dir_block->next_data_block);
        for(i = 0; i <= masterblock->block_size - sizeof(file_signature) - sizeof(long); i += sizeof(file_signature)) {
            DEBUG("indeks %d\n", i);
            file_signature* signature = (file_signature*) (dir_block->data + i * sizeof(char));
            if(strcmp(name, signature->name) == 0 && signature->inode_no != 0) {
                //calculate number of inode table block, where we'll find the right node
                long block_to_read = signature->inode_no * sizeof(inode) / masterblock->block_size;
                //zapameitujemy numer inode w tablicy inodow
                *inode_no = signature->inode_no;
                DEBUG("Zapamiętanie w tablicy inodów: %lu\n", signature->inode_no);
                char* inode_block = malloc(masterblock->block_size); // = (char*) _read_block(fd, block_to_read, masterblock->first_inode_table_block, masterblock->block_size);
                lseek(fd, (masterblock->first_inode_table_block + block_to_read) * masterblock->block_size, SEEK_SET);
                read(fd, inode_block, masterblock->block_size);
                inode* result_inode = malloc(sizeof(inode));
                memcpy(result_inode, inode_block + ((signature->inode_no * sizeof(inode)) % masterblock->block_size), sizeof(inode));
                free(dir_block);
                free(inode_block);
                return result_inode;
            }
        }
        if(dir_block->next_data_block == 0) {
            DEBUG("Nie znaleziony inode!\n");
            free(dir_block);
            return NULL;
        }
        dir_block = _read_block(fd, dir_block->next_data_block, masterblock->data_start_block, masterblock->block_size);
    }
    DEBUG("wyjscie z get inode in dir\n");
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
        DEBUG("In _get_inode_by_path - get root inode. Block size is %d\n", masterblock->block_size);
        return _get_root_inode(fd, masterblock);
    }
    unsigned path_index = 1;
    char* path_part = malloc(strlen(path) * sizeof(char));
    DEBUG("Path_part %c\n", path_part);
    inode* current_inode = _get_root_inode(fd, masterblock);
    while(1) {
        unsigned i = 0;
        while(path[path_index] != '/' && path[path_index] != '\0') {
            path_part[i++] = path[path_index++];
        }
        path_part[i] = '\0';
        inode* new_inode = _get_inode_in_dir(fd, current_inode, path_part, masterblock, inode_no); //inode_no sie nie zmieni jak sie okaze ze path_part nie jest juz katalogiem
        free(current_inode);
        if(new_inode == NULL || new_inode->type == INODE_EMPTY) {
            DEBUG("Nie znaleziony inode!\n");
            free(path_part);
            return NULL;
        }
        current_inode = new_inode;
        if(path[path_index] == '\0') {
            break;
        }
        path_index++;
    }
    free(path_part);
    return current_inode;
}

/**
 * Blokuje first free block (ekskluzywnie).
 *
 * @param fsfd deksryptor systemu plików
 * @param fl niewypełniona struktura flock
 */
int _block_first_free_block(int fsfd, struct flock * fl) {
    /* F_RDLCK, F_WRLCK, F_UNLCK    */
    fl->l_type = F_WRLCK;
    /* SEEK_SET, SEEK_CUR, SEEK_END */
    fl->l_whence = SEEK_SET;
    fl->l_start = 32;
    fl->l_len = 8;
    fl->l_pid = getpid();
    return fcntl(fsfd, F_SETLKW, fl);
}

/**
 * Odblokowuje first free block (ekskluzywnie).
 *
 * @param fd deksryptor systemu plików
 * @param fl niewypełniona struktura flock
 */
int _unblock_first_free_block(int fsfd, struct flock * fl) {
    fl->l_type = F_UNLCK;
    return fcntl(fsfd, F_SETLK, fl);
}

/**
 * Wyszukuje dostępne wolne bloki, nie jest cross-process-safe. Zwraca pierwszy przetwarzany number bloku dla pliku lub
 * jaroslaw_błąd (NO_FREE_BLOCKS).
 *
 * @param initialized_structures_pointer wskaźnik na zainicjalizowane struktury systemu plików
 * @param number_of_free_blocks liczba wolnych bloków do wyszukania
 * @param free_blocks tablica, gdzie zostaną zapisane identyfikatory znalezionych bloków
 */
long _find_free_blocks(int fsfd, initialized_structures * initialized_structures_pointer, unsigned long number_of_free_blocks,
                      unsigned long * free_blocks) {
    DEBUG("In _find_free_blocks. free blocks = %d\n", number_of_free_blocks);
    master_block * master_block = initialized_structures_pointer->master_block_pointer;
    if (master_block->number_of_free_blocks <= number_of_free_blocks) {
        return NO_FREE_BLOCKS;
    }
    unsigned long first_free_block_number = master_block->first_free_block_number;
    char * block_bitmap_pointer = initialized_structures_pointer->block_bitmap_pointer;
    DEBUG("block bitmap pointer = %X\n", block_bitmap_pointer);

    // wolny blok zapisany na wypadek, gdyby poszukiwanie wolnych bloków nie mogło się poprawnie zakończyć (brakuje miejsca)
    unsigned long saved_first_free_block_number = first_free_block_number;
    unsigned long free_block_idx = 0;
    for (free_block_idx; free_block_idx < number_of_free_blocks; free_block_idx++) {
        DEBUG("przebieg petli: %d\n", free_block_idx );

        // zaznaczenie bloku jako zajętego
        DEBUG("bitmap pointer %d\n", block_bitmap_pointer);
        char * bitmap_byte = block_bitmap_pointer + ((master_block->first_free_block_number / 8) * sizeof(char));
        unsigned int bit_in_byte = master_block->first_free_block_number % 8;
        DEBUG("bitmap byte evaluates to %X bit in byte %d\n", *bitmap_byte, bit_in_byte);
        (*bitmap_byte) |= (1 << bit_in_byte);

        DEBUG("Po zapisie\n");
        // zapisanie numeru wolnego bloku do tablicy
        *(free_blocks + free_block_idx) = master_block->first_free_block_number;

        DEBUG("Zmniejszenie liczby wolnych blokow\n");
        // zmniejszenie liczby wolnych bloków
        master_block->number_of_free_blocks--;
        DEBUG("master_block->number_of_free_blocks = %d\n", master_block->number_of_free_blocks);

        // następny blok jest pretendentem na wolny blok
        master_block->first_free_block_number++;
        DEBUG("Nastepny blok pretendednt %d, liczba wszystkich = %d\n", master_block->first_free_block_number, master_block->number_of_blocks);

        DEBUG("przed whilem\n");
        // wyszkanie następnego wolnego bloku, a w nim wolnego bitu
        while (TRUE) {
            unsigned char bytes_mask = (0xFF << ((master_block->first_free_block_number % 8) - 1));
            DEBUG("Maska bitowa dla bajtu: %x, bajt= %x\n", bytes_mask, (*bitmap_byte));
            DEBUG("Maska bitwa nalozona na bajt: %x, first free block number%d\n", ((*bitmap_byte) & bytes_mask), master_block->first_free_block_number);
            if (bytes_mask == 0xFF) {
                // przesuwamy się na następne miejsce w bitmapie (niewyrównane bloki zostaną pominięte)
                bitmap_byte = block_bitmap_pointer + ((master_block->first_free_block_number / 8) * sizeof(char));
            }

            // jeśli istnieje wolny bit w bitmapie
            if ((((*bitmap_byte) & bytes_mask) != bytes_mask) && master_block->first_free_block_number < master_block->number_of_blocks) {
                char bitmap_byte_to_parse = (*bitmap_byte);
                while (bitmap_byte_to_parse & (1 << (master_block->first_free_block_number % 8))) {
                    master_block->first_free_block_number++;
                }
                break;
            } else if (master_block->first_free_block_number >= master_block->number_of_blocks) {
                // dotarcie do końca wszystkich bloków (pierwszy blok to plik .lock)
                master_block->first_free_block_number = master_block->data_start_block + 1;
                bitmap_byte = block_bitmap_pointer + 1;
            } else {
                master_block->first_free_block_number++;
            }
        }
        DEBUG("zakonczenie wyszukiwania wolnych blokow!\n");
    }
    DEBUG("wyjscie z find free blocks!\n");
    return (long) saved_first_free_block_number;
}

/**
 * Pobiera strukturę pliku na podstawie podanego identyfikatora pliku.
 */
file * _get_file_by_fd(int fd) {
    file* file_found;
    HASH_FIND_INT( open_files, &fd, file_found);
    DEBUG("Found file. mode = %d\n", file_found->mode);
    return file_found;
}

/**
 * Ładuje i-node dla podanej struktury pliku.
 */
inode * _load_inode_from_file_structure(initialized_structures * initialized_structures_pointer, file * file_pointer) {
    if (file_pointer == NULL) {
        return NULL;
    }
    return initialized_structures_pointer->inode_table + file_pointer->inode_no;
}

/**
 * Pobiera numery bloków, które aktualnie posiada plik. Jeśli number_of_free_blocks != 0 dodatkowo aktualizuje blok.
 */
int _get_blocks_numbers_taken_by_file(int fsfd, unsigned long first_block_no, master_block * master_block_pointer,
                                      int (*for_each_record)(void*, int, void*), void * additional_param,
                                      unsigned long * blocks_table) {
    DEBUG("\n**** _get_blocks_numbers_taken_by_file ****\n");
    DEBUG("Pierwszy blok: %u\n", first_block_no);

    block * block_pointer = NULL;
    int i = 0;
    unsigned long block_no = first_block_no;
    do {
        block_pointer = _read_block(fsfd, block_no, master_block_pointer->data_start_block, master_block_pointer->block_size);

        // wywołanie funkcji sprawdzającej block
        if (for_each_record != NULL && for_each_record(block_pointer, master_block_pointer->block_size, additional_param) == 0) {
            return -2;
        }
        blocks_table[i++] = block_no;
        DEBUG("Zapisany numer bloku do tablicy: %d\n", block_no);
        block_no = block_pointer->next_data_block;
        DEBUG("nastepny blok danych: %u\n", block_no);
    } while (block_pointer->next_data_block != 0);
    DEBUG("Wyjscie z **** _get_blocks_numbers_taken_by_file ****\n");
    return 0;
}

/**
 * Blokuje wybrane locki.
 */
void _lock_file_blocks(int fsfd, master_block * master_block_pointer, unsigned long * blocks_table,
                       unsigned long number_of_blocks, struct flock * flock_structures) {
    unsigned long idx = 0;
    DEBUG("\n\n******** _lock_file_blocks ********\n");
    DEBUG("Liczba blokow: %d\n", number_of_blocks);
    for (idx; idx < number_of_blocks; idx++) {
        unsigned long block_offset = _get_block_offset(master_block_pointer, blocks_table[idx]);
        DEBUG("Offset dla bloku: %u, wynosi: %u", blocks_table[idx], block_offset);
        struct flock * flock_str = flock_structures + idx;
        flock_str->l_type = F_WRLCK;
        flock_str->l_whence = SEEK_SET;
        flock_str->l_start = block_offset;
        flock_str->l_len = master_block_pointer->block_size;
        flock_str->l_pid = getpid();
        fcntl(fsfd, F_SETLKW, flock_str);
    }
}

/**
 * Odblokowuje locki założone na bloki plików.
 */
void _unlock_file_blocks(int fsfd, struct flock * flock_structures, unsigned long number_of_flocks) {
    unsigned int idx = 0;
    for (idx; idx < number_of_flocks; idx++) {
        struct flock * flock_str = flock_structures + idx;
        flock_str->l_type = F_UNLCK;
        fcntl(fsfd, F_SETLK, flock_str);
    }
}

/**
 * Funkcja przeprowadza rzeczywisty zapis do pliku reprezentującego system plików.
 * Zakładana jest odpowiednia wielkość wypełnionej tablicy {blocks_table}, tak aby dane mogły zostać zapisane.
 */
void _save_buffer_to_file(initialized_structures * initialized_structures_pointer, write_params * params,
                          unsigned long * blocks_table, unsigned long real_file_offset, unsigned long number_of_all_blocks) {
    DEBUG("\n****** save buffer to file *******\n");
    DEBUG("params->data length = %d, file_offset =  %d\n", params->data_length, params->file_offset);
    master_block * master_block_pointer = initialized_structures_pointer->master_block_pointer;
    unsigned int real_block_size = master_block_pointer->block_size - sizeof(long);
    unsigned long block_to_start = real_file_offset / real_block_size;
    unsigned int number_of_all_blocks_be_written = block_to_start + 1 + ((params->data_length - 1) / real_block_size);

    unsigned int additional_block_offset = real_file_offset % real_block_size;
    unsigned int data_offset = 0;

    // jeżeli istnieje poprzedni blok (istnieje możliwa dziura), to trzeba w nim zapisać info o nowym bloku
    if (block_to_start > 0) {
        unsigned long previous_block_to_update = blocks_table[block_to_start - 1];
        lseek(params->fsfd, _get_block_offset(master_block_pointer, previous_block_to_update) + real_block_size, SEEK_SET);
        write(params->fsfd, &blocks_table[block_to_start], sizeof(unsigned long));
    }

    for (block_to_start; block_to_start < number_of_all_blocks_be_written; block_to_start++) {
        unsigned long block_number = blocks_table[block_to_start];

        unsigned long block_offset = _get_block_offset(master_block_pointer, block_number);
        lseek(params->fsfd, block_offset + additional_block_offset, SEEK_SET);

        unsigned long data_length_for_block = real_block_size - additional_block_offset;
        if (params->data_length - data_offset < real_block_size - additional_block_offset) {
            data_length_for_block = params->data_length - data_offset;
        }
        DEBUG("Suspicious write. params->data = %X, data_offset = %d, data_length_for_block = %d, additional_block_offset = %d\n",
                params->data, data_offset, data_length_for_block, additional_block_offset);
        DEBUG("Sizeof file signature is %d\n", sizeof(file_signature));
        write(params->fsfd, params->data + sizeof(char) * data_offset, data_length_for_block);
        additional_block_offset = 0;
        data_offset += data_length_for_block;

        // przesunięcie wskaznika na koniec bloku dla zapisania informacji o następnym numerze bloku
        lseek(params->fsfd, block_offset + real_block_size, SEEK_SET);

        //DEBUG("\nZapis wskaznika na nastepny number bloku, block_to_start = %d, number_of_all_blocks = %d, blocks_table[block_to_start + 1] = %d\n", block_to_start, number_of_all_blocks, blocks_table[block_to_start + 1]);
        // zapisanie wskaznika na następny numer bloku
        if (block_to_start != number_of_all_blocks - 1) {
            DEBUG("Zapis!\n");
            write(params->fsfd, &blocks_table[block_to_start + 1], sizeof(unsigned long));
        } else {
            // zapisanie zera jako następny numer bloku
            unsigned long zero = 0;
            write(params->fsfd, &zero, sizeof(unsigned long));
        }
    }
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
    unsigned int real_block_size = master_block_pointer->block_size - sizeof(long);

    // załadowanie odpowiedniej struktury inode
    file * file_structure = _get_file_by_fd(params.fd);
    inode * file_inode = _load_inode_from_file_structure(initialized_structures_pointer, file_structure);
    unsigned long file_size = file_inode->size;
    DEBUG("_write_unsafe. Filze size = %d\n", file_size);

    // sprawdzenie poprawności dostępu do pliku
    if (file_structure->mode == READ_MODE) {
        _unblock_first_free_block(params.fsfd, &flock_structure);
        DEBUG("Wrong mode\n");
        return WRONG_MODE;
    }

    // wyznaczenie prawdziwego offsetu dla pliku (< 0 => append)
    unsigned int real_file_offset = 0;
    if (params.file_offset < 0) {
        real_file_offset = file_size;
        unsigned long diff = real_block_size - file_size % real_block_size;
        // specjalny przypadek - dodatkowo przy appendzie sprawdzamy czy blok danych do zapisu się zmieści, jeśli nie to robimy tam 'dziurę'
        if (diff < params.data_length) {
            real_file_offset += diff;
        }
    } else {
        real_file_offset = file_structure->position;
    }

    // wyznaczenie ile aktualnie zajmuje plik, a ile może zajmować po operacji zapisu
    unsigned int number_of_all_taken_blocks_by_file = file_size / real_block_size;
    if (file_size != 0) {
        number_of_all_taken_blocks_by_file++;
    }
    unsigned int number_of_blocks_to_be_taken_by_file = 1 + ((real_file_offset + params.data_length - 1) / real_block_size);

    // przypadek specjalny - pierwszy zapis do nowo utworzonego pliku
    if (number_of_blocks_to_be_taken_by_file == 0) {
        number_of_blocks_to_be_taken_by_file = 1;
    }
    unsigned long * blocks_table = NULL;
    if (number_of_all_taken_blocks_by_file >= number_of_blocks_to_be_taken_by_file) {
        blocks_table = (unsigned long *) malloc(sizeof(unsigned long) * number_of_all_taken_blocks_by_file);
    } else {
        blocks_table = (unsigned long *) malloc(sizeof(unsigned long) * number_of_blocks_to_be_taken_by_file);
    }
    DEBUG("\n\n************\nLiczba wszystkich blokow zajmowanych przez plik: %d, liczba blokow do zajecia: %d\n\n", number_of_all_taken_blocks_by_file, number_of_blocks_to_be_taken_by_file);
    // czy trzeba wyszukać nowe bloki danych dla pliku
    unsigned int number_of_free_blocks = 0;
    if ((number_of_blocks_to_be_taken_by_file > number_of_all_taken_blocks_by_file)
        || file_inode->first_data_block == 0) {
        // wyszukanie nowych bloków danych
        number_of_free_blocks = number_of_blocks_to_be_taken_by_file - number_of_all_taken_blocks_by_file;
        //nie wiem czemu tak
        long first_operated_block = _find_free_blocks(params.fsfd, initialized_structures_pointer, number_of_free_blocks,
                          blocks_table + number_of_all_taken_blocks_by_file);
        if (first_operated_block == NO_FREE_BLOCKS) {
            DEBUG("zle!");
            _unblock_first_free_block(params.fsfd, &flock_structure);
            free(blocks_table);
            return NO_FREE_BLOCKS;
        } else if (file_inode->first_data_block == 0) {
            DEBUG("First operated block: %d\n", first_operated_block);
            file_inode->first_data_block = first_operated_block;
        }
    }

    unsigned long number_of_flocks = number_of_blocks_to_be_taken_by_file;
    if (number_of_all_taken_blocks_by_file >= number_of_blocks_to_be_taken_by_file) {
        number_of_flocks = number_of_all_taken_blocks_by_file;
    } else {
        number_of_flocks = number_of_blocks_to_be_taken_by_file;
    }

    struct flock * flock_structures = (struct flock *) malloc(sizeof(struct flock) * number_of_flocks);
    // czy zablokować dodatkowo wszystkie bloki danych, do których funkcja będzie zapisywać dane
    DEBUG("Czy blokowac bloki: %d, dla liczby blokow: %d\n", params.lock_blocks, number_of_flocks);
    if (_get_blocks_numbers_taken_by_file(params.fsfd, file_inode->first_data_block, master_block_pointer,
                                          params.for_each_record, params.additional_param, blocks_table) == -2) {
        return -2;
    }
    if (params.lock_blocks) {
        _lock_file_blocks(params.fsfd, master_block_pointer, blocks_table, number_of_flocks, flock_structures);
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
    _save_buffer_to_file(initialized_structures_pointer, &params, blocks_table, real_file_offset, number_of_flocks);

    // zwiększenie pozycji w strukturze file
    file_structure->position += params.data_length;
    DEBUG("Nowa pozycja w strukturze file: %d", file_structure->position);

    // odblokowanie zablokowanych bloków danych (jeśli było to żądane)
    if (params.lock_blocks) {
        _unlock_file_blocks(params.fsfd, flock_structures, number_of_flocks);
    }
    free(flock_structures);
    free(blocks_table);
    return 0;
}

/**
 * Funkcja odczytująca master block z dysku
 * @return odczytany master block
 */
master_block* _get_master_block(int fsfd) {
    DEBUG("get master block. fd = %d\n", fsfd);
    lseek(fsfd, 0, SEEK_SET);
    master_block* masterblock = malloc(sizeof(master_block));
    read(fsfd, masterblock, sizeof(master_block));
    DEBUG("Read first inode table block: %d\n", masterblock->first_inode_table_block);
    DEBUG("Sizeof block_size is %d\n", sizeof(masterblock->block_size));
    return masterblock;
}

/**
 * Funkcja zwracająca podaną ścieżkę skróconą o ostatnią część - sluży więc do oddzielenia istniejącej ścieżki
 * od nazwy pliku (folderu), który ma być utworzony
 */
char* _get_path_for_file(char* full_path) {
    char* path = malloc(strlen(full_path) * sizeof(char) + 1);
    strcpy(path, full_path);
    int i;
    for(i = strlen(path) - 1; i >= 0; i--) {
        if(path[i] == '/') {
            if(i == 0) {
                path[i+1] = '\0';
            } else {
                path[i] = '\0';
            }
            return path;
        }
    }
    return NULL;
}

/**
 * Funkcja umieszczająca bezpiecznie inode w pierwszym wolnym miejscu
 * @return numer umieszczonego inode'u
 */
unsigned long _insert_new_inode(inode* new_inode, initialized_structures* structures, int fsfd) {
    struct flock lock;
    lock.l_type = F_RDLCK + F_WRLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = FIRST_FREE_INODE_OFFSET;
    lock.l_len = sizeof(structures->master_block_pointer->first_free_inode);
    lock.l_pid = getpid();
DEBUG("Writing new inode: masterblock pointer: %d\n", structures->master_block_pointer);
    //lock first free inode in master block
    fcntl(fsfd, F_SETLKW, &lock);
    DEBUG("Writing new inode: masterblock pointer: %d\n", structures->master_block_pointer);
    unsigned long inode_no = structures->master_block_pointer->first_free_inode;
    if(inode_no == 0) {
        lock.l_type = F_UNLCK;
        fcntl(fsfd, F_SETLK, &lock);
        return 0;
    }
    structures->inode_table[inode_no] = *new_inode;
    //now need to find new next free inode
    unsigned long i;
    for(i = inode_no + 1; i < (structures->master_block_pointer->number_of_inode_table_blocks * structures->master_block_pointer->block_size)
            / sizeof(inode); i++) {
        if(structures->inode_table[i].type == INODE_EMPTY) {
            structures->master_block_pointer->first_free_inode = i;
            lock.l_type = F_UNLCK;
            fcntl(fsfd, F_SETLK, &lock);
            return inode_no;
        }
    }
    //no free inodes!
    //unlock
    structures->master_block_pointer->first_free_inode = 0;
    lock.l_type = F_UNLCK;
    fcntl(fsfd, F_SETLK, &lock);
    return inode_no;
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
    file_signature* signature = (file_signature*) data_block->data;
    DEBUG("in check_duplicat file names. data_block->data = %X, liczba sygnatur na plik:%d\n", data_block->data, block_size / sizeof(file_signature));
    int i;
    for(i = 0; i < block_size / sizeof(file_signature); i++) {
        DEBUG("wchodze");
        DEBUG("signature node: %d, signature name %s\n", signature->inode_no, signature->name);
        if(signature->inode_no == 0) {
            signature++;
            continue;
        }
        if(signature->inode_no != 0 && !strcmp(signature->name, (char*) name)) {
            //exists!
            return FALSE;
        }
        DEBUG("Adding %d to signature\n", sizeof(file_signature));

        signature++;
    }
    DEBUG("wychodze");
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

    int fd = open(path, O_RDONLY, 0644);
    if(fd != -1) {
        return FILE_ALREADY_EXISTS;
    }

    fd = open(path, O_RDWR | O_CREAT, 0644);
    if(fd == -1) {
        return HOST_FILE_ACCESS_ERROR;
    }

    //get master block
    master_block masterblock = get_initial_master_block(block_size, number_of_blocks);
    unsigned fs_size = (1 + masterblock.number_of_bitmap_blocks + masterblock.number_of_inode_table_blocks +
            masterblock.number_of_blocks) * masterblock.block_size;

    //insert master block
    write(fd, &masterblock, sizeof(master_block));
    DEBUG("Masterblock written\n");

    //mark first block as taken
    lseek(fd, masterblock.block_size, SEEK_SET);
    char one = 0x01;
    write(fd, &one, sizeof(char));

    //insert space for bitmap
    lseek(fd, (1 + masterblock.number_of_bitmap_blocks) * masterblock.block_size, SEEK_SET);

    //insert root inode
    inode root_inode;
    memset(&root_inode, 0, sizeof(inode));
    strcpy(root_inode.filename, "/");
    root_inode.type = INODE_DIR;
    write(fd, &root_inode, sizeof(inode));

    //insert .lock inode
    inode lock_inode;
    memset(&lock_inode, 0, sizeof(inode));
    strcpy(lock_inode.filename, ".lock");
    lock_inode.type = INODE_FILE;
    write(fd, &lock_inode, sizeof(inode));

    //allocate space for data
    lseek(fd, fs_size - 1, SEEK_SET);
    write(fd, "\0", 1);
    DEBUG("Allocated %d bytes\n", fs_size);

    close(fd);
    return 0;
}

int simplefs_openfs(char *path) { //Adam
    int fd = open(path, O_RDWR, 0644);
    DEBUG("OPEN FS. fd = %d\n", fd);
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
    DEBUG("\nSimplefs open - rozpoczęcie poszukiwania inoda.\n");
    inode* file_inode = _get_inode_by_path(name, masterblock, fsfd, &tmp);
    if (file_inode == NULL) {
        return FILE_DOESNT_EXIST;
    }
    DEBUG("\n\nSimplefs open, pobrany inode: typ = %c size = %d\n", file_inode->type, file_inode->size);
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
    new_file->mode = (char) mode;
    HASH_ADD_INT(open_files, fd, new_file);
    pthread_mutex_unlock(&open_files_write_mutex);
    free(masterblock);
    return i;
}

int _free_data_block(initialized_structures* structures, unsigned long block_no) {
    //free block in bitmap
    unsigned long bitmap_block_no = block_no / (8 * structures->master_block_pointer->block_size);
    DEBUG("\n\n                      Numer bitmapy do zwolnienia %d\n\n", bitmap_block_no);
    char bit_offset = block_no % 8;
    structures->block_bitmap_pointer[bitmap_block_no] &= ~(1 << bit_offset);
    structures->master_block_pointer->number_of_free_blocks++;
    if(block_no < structures->master_block_pointer->first_free_block_number) {
        //update first free block no
        structures->master_block_pointer->first_free_block_number = block_no;
    }
}

int simplefs_unlink(char *name, int fsfd) { //Michal
    //extract file name
    int path_length = strlen(name);
    if(path_length < 1) {
        return FILE_DOESNT_EXIST;
    }
    int filename_position = path_length - 1;
    while(name[filename_position] != '/') {
        if(--filename_position < 0) {
            return FILE_DOESNT_EXIST;
        }
    }
    initialized_structures* structures = _initialize_structures(fsfd, 1);
    _lock_lock_inode(structures->master_block_pointer, fsfd);
    _lock_lock_file(structures->master_block_pointer, fsfd);
    char* path = _get_path_for_file(name);
    unsigned long inode_no;
    inode* file_inode = _get_inode_by_path(name, structures->master_block_pointer, fsfd, &inode_no);
    if(file_inode == NULL) {
        _unlock_lock_file(structures->master_block_pointer, fsfd);
        _unlock_lock_inode(structures->master_block_pointer, fsfd);
        _uninitilize_structures(structures);
        return FILE_DOESNT_EXIST;
    }

    //jeśli to katalog, tu sprawdź, czy nie ma w nim plików!
    if(file_inode->type == INODE_DIR) {
        int file_fd = simplefs_open(name, READ_AND_WRITE, fsfd);
        int bytes_read = 0;
        while(1) {
            file_signature signature;
            bytes_read = _read_unsafe(file_fd, &signature, sizeof(file_signature), fsfd,
                                        _load_inode_from_file_structure(structures, _get_file_by_fd(file_fd))->size);
            if(bytes_read < sizeof(file_signature)) {
                break;
            }
            if(signature.inode_no != 0) {
                _unlock_lock_file(structures->master_block_pointer, fsfd);
                _unlock_lock_inode(structures->master_block_pointer, fsfd);
                _uninitilize_structures(structures);
                simplefs_close(file_fd);
                return DIR_NOT_EMPTY;
            }
        }
        simplefs_close(file_fd);
    }
    //zwolnienie bloków
    unsigned long blocks_freed = 0;
    unsigned long current_block_no = file_inode->first_data_block;
    unsigned long zero = 0;
    while(current_block_no != 0) {
        //update no of free blocks in mb
        structures->master_block_pointer->number_of_free_blocks++;

        _free_data_block(structures, current_block_no);
        block* current_block = _read_block(fsfd, current_block_no, structures->master_block_pointer->data_start_block,
                                        structures->master_block_pointer->block_size);
        unsigned long old_block_no = current_block_no;
        current_block_no = current_block->next_data_block;

        //clear next data block pointer
        lseek(fsfd, (structures->master_block_pointer->data_start_block + old_block_no + 1) *
                        structures->master_block_pointer->block_size - sizeof(unsigned long), SEEK_SET);
        write(fsfd, &zero, sizeof(unsigned long));
        blocks_freed++;

        free(current_block);
    }
    structures->master_block_pointer->number_of_free_blocks += blocks_freed;
    _mark_inode_as_empty(structures, inode_no);

    //remove file signature from parent directory
    char* dir_path = _get_path_for_file(name);
    int dir_fd = simplefs_open(dir_path, READ_AND_WRITE, fsfd);
    unsigned block_data_size = structures->master_block_pointer->block_size - sizeof(long);
    unsigned signatures_in_block = block_data_size / sizeof(file_signature);
    unsigned dir_block_padding = block_data_size % sizeof(file_signature);
    int i;
    for(i = 0;;i++) {
        file_signature signature;
        _read_unsafe(dir_fd, (char*) &signature, sizeof(file_signature), fsfd,
                    _load_inode_from_file_structure(structures, _get_file_by_fd(dir_fd))->size);
        DEBUG("\n\n\n%d. signature.inode_no = %d, strcmp = %d, signature.name = %s, name = %s\n\n\n", i, signature.inode_no, strcmp(signature.name, name + filename_position + 1), signature.name, name + filename_position + 1);
        if(strcmp(signature.name, name + filename_position + 1) == 0 && signature.inode_no != 0) {
            //now navigate to the last signature in this dir so we can replace the old one with that
            file* dir_file_struct;
            HASH_FIND_INT(open_files, &dir_fd, dir_file_struct);
            unsigned long saved_position = dir_file_struct->position;
            unsigned long dir_inode_no;
            inode* dir_inode = _get_inode_by_path(dir_path, structures->master_block_pointer, fsfd, &dir_inode_no);
            if(saved_position != dir_inode->size) {
                //not the last signature to remove, need to replace
                simplefs_lseek_unsafe(dir_fd, structures, SEEK_SET, dir_inode->size - sizeof(file_signature), fsfd);
                file_signature last_signature;
                _read_unsafe(dir_fd, &last_signature, sizeof(file_signature), fsfd,
                            _load_inode_from_file_structure(structures, _get_file_by_fd(dir_fd))->size);

                //clear the last signature
                simplefs_lseek_unsafe(dir_fd, structures, SEEK_CUR, -sizeof(unsigned long), fsfd);
                unsigned long zero = 0;

                write_params params;
                params.for_each_record = NULL;
                params.additional_param = NULL;
                params.data = &zero;
                params.data_length = sizeof(unsigned long);
                params.fd = dir_fd;
                params.file_offset = _get_file_by_fd(dir_fd)->position;
                params.fsfd = fsfd;
                params.lock_blocks = 0;
                _write_unsafe(structures, params);

                //write in place of the old inode
                simplefs_lseek_unsafe(dir_fd, structures, SEEK_SET, saved_position - sizeof(file_signature), fsfd);

                params.data = &last_signature;
                params.data_length = sizeof(file_signature);
                params.file_offset = _get_file_by_fd(dir_fd)->position;
                _write_unsafe(structures, params);
            } else {
                //just last signature to remove
                //clear the last signature
                simplefs_lseek_unsafe(dir_fd, structures, SEEK_CUR, -sizeof(unsigned long), fsfd);
                unsigned long zero = 0;
                write_params params;
                params.for_each_record = NULL;
                params.additional_param = NULL;
                params.data = &zero;
                params.data_length = sizeof(unsigned long);
                params.fd = dir_fd;
                params.file_offset = _get_file_by_fd(dir_fd)->position;
                params.fsfd = fsfd;
                params.lock_blocks = 0;
                _write_unsafe(structures, params);
            }
            structures->inode_table[dir_inode_no].size -= sizeof(file_signature);
            //we may need to free the block
            if(structures->inode_table[dir_inode_no].size % structures->master_block_pointer->block_size == 0) {
                unsigned long current_dir_block_no = dir_inode->first_data_block;
                if(current_dir_block_no != 0) {
                    unsigned long previous_dir_block_no = 0;
                    while(current_dir_block_no != 0) {
                        block* current_dir_block = _read_block(fsfd, current_dir_block_no, structures->master_block_pointer->data_start_block, structures->master_block_pointer->block_size);
                        previous_dir_block_no = current_dir_block_no;
                        current_dir_block_no = current_dir_block->next_data_block;
                        free(current_dir_block);
                    }
                    _free_data_block(structures, current_dir_block_no);
                    if(previous_dir_block_no == 0) {
                        structures->inode_table[dir_inode_no].first_data_block = 0;
                    }
                    else {
                        lseek(fsfd, (structures->master_block_pointer->data_start_block + previous_dir_block_no + 1)
                                                    * structures->master_block_pointer->block_size - sizeof(unsigned long), SEEK_SET);
                        unsigned long zero = 0;
                        write(fsfd, &zero, sizeof(unsigned long));
                    }
                }
                simplefs_lseek_unsafe(dir_fd, structures, SEEK_SET, 0, fsfd);
                char* dir_block = malloc(structures->master_block_pointer->block_size - sizeof(unsigned long));
                _read_unsafe(dir_fd, &dir_block, sizeof(block), fsfd,
                            _load_inode_from_file_structure(structures, _get_file_by_fd(dir_fd))->size);
            }

            free(dir_inode);
            /*unsigned long last_dir_block_no = dir_inode->size / block_data_size;
            block* last_dir_block = _read_block(fsfd, last_dir_block_no, structures->master_block_pointer->data_start_block, structures->master_block_pointer->block_size);
            unsigned last_inode_block_offset = (dir_inode->size % block_data_size) - sizeof(file_signature);


            //clear
            signature.inode_no = 0;
            simplefs_lseek(dir_fd, SEEK_CUR, -sizeof(file_signature), fsfd);
            write_params params;
            params.data = (char*) &signature;
            params.data_length = sizeof(file_signature);
            params.fd = dir_fd;
            params.file_offset = i * sizeof(file_signature);
            params.fsfd = fsfd;
            params.lock_blocks = 0;
            params.additional_param = NULL;
            params.for_each_record = NULL;
            _write_unsafe(structures, params);*/
            break;
        }
        //kiedy w bloku nie mieści się więcej sygnatur, przeskocz o różnicę
        if((i + 1) % signatures_in_block == 0) {
            simplefs_lseek_unsafe(dir_fd, structures, SEEK_CUR, dir_block_padding, fsfd);
        }
    }
    /*unsigned long dir_inode_no;
    inode* dir_inode = _get_inode_by_path(dir_path, structures->master_block_pointer, fsfd, &dir_inode_no);
    free(dir_path);*/

    _unlock_lock_file(structures->master_block_pointer, fsfd);
    _unlock_lock_inode(structures->master_block_pointer, fsfd);
    if(_get_lock_counter(fsfd, structures->master_block_pointer) != 0) {
        _try_lock_lock_inode(structures->master_block_pointer, fsfd);
    }
    _uninitilize_structures(structures);
    simplefs_close(dir_fd);
    return OK;
}

int simplefs_mkdir(char *name, int fsfd) { //Michal
    return _create_file_or_dir(name, fsfd, TRUE);
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

    if (_write_unsafe(structures, params) == NO_FREE_BLOCKS) {
        // TODO
    }

    free(structures);
    simplefs_close(fd);
    return 0;*/
}

void _try_lock_lock_inode(master_block * mb, int fsfd) {
    //lock inode
    struct flock lock_inode;
    lock_inode.l_type = F_WRLCK;
    lock_inode.l_whence = SEEK_SET;
    lock_inode.l_start = (mb->first_inode_table_block * mb->block_size + sizeof(inode) * 1);
    lock_inode.l_len = sizeof(inode);
    lock_inode.l_pid = getpid();
    fcntl(fsfd, F_SETLK, &lock_inode);
}

void _lock_lock_inode(master_block * mb, int fsfd) {
    //lock inode
    struct flock lock_inode;
    lock_inode.l_type = F_WRLCK;
    lock_inode.l_whence = SEEK_SET;
    lock_inode.l_start = (mb->first_inode_table_block * mb->block_size + sizeof(inode) * 1);
    lock_inode.l_len = sizeof(inode);
    lock_inode.l_pid = getpid();
    fcntl(fsfd, F_SETLKW, &lock_inode);
}

void _unlock_lock_inode(master_block * mb, int fsfd) {
    //lock inode
    struct flock lock_inode;
    lock_inode.l_type = F_UNLCK;
    lock_inode.l_whence = SEEK_SET;
    lock_inode.l_start = (mb->first_inode_table_block * mb->block_size + sizeof(inode) * 1);
    lock_inode.l_len = sizeof(inode);
    lock_inode.l_pid = getpid();
    fcntl(fsfd, F_SETLK, &lock_inode);
}

void _lock_lock_file(master_block * mb, int fsfd) {
    _try_lock_lock_inode(mb, fsfd);
    //lock lock block
    struct flock lock_block;
    lock_block.l_type = F_WRLCK;
    lock_block.l_whence = SEEK_SET;
    lock_block.l_start = (mb->data_start_block);
    lock_block.l_len = mb->block_size;
    lock_block.l_pid = getpid();
    fcntl(fsfd, F_SETLKW, &lock_block);
    unsigned delta;
    int * counter = (int *) mmap_enhanced(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED,
                    fsfd, mb->data_start_block * mb->block_size, &delta);
    DEBUG("Printing master block data. data start block = %d, block size = %d\n", mb->data_start_block, mb->block_size);
    DEBUG("mmapped counter, address = %d\n", counter);
    DEBUG("Co wychodzi? %X\n", (mb->data_start_block * mb->block_size) & ~(sysconf(_SC_PAGE_SIZE) - 1));
    DEBUG("Counter = %d", *counter);
    (*counter)++;
    DEBUG("Counter = %d", *counter);
    DEBUG("Munmap result = %d\n", munmap_enhanced(counter, sizeof(int), delta));
    //unlock lock block
    lock_block.l_type = F_UNLCK;
    fcntl(fsfd, F_SETLK, &lock_block);
}

void _unlock_lock_file(master_block * mb, int fsfd) {
    //lock lock block
    struct flock lock_block;
    lock_block.l_type = F_WRLCK;
    lock_block.l_whence = SEEK_SET;
    lock_block.l_start = (mb->data_start_block);
    lock_block.l_len = mb->block_size;
    lock_block.l_pid = getpid();
    fcntl(fsfd, F_SETLKW, &lock_block);
    unsigned delta;
    int * counter = (int *) mmap_enhanced(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED,
                    fsfd, mb->data_start_block * mb->block_size, &delta);
    (*counter)--;
    int value = *counter;
    munmap_enhanced(counter, sizeof(int), delta);
    if(value != 0) {
        _try_lock_lock_inode(mb, fsfd);
    } else {
        _unlock_lock_inode(mb, fsfd);
    }
    //unlock lock block
    lock_block.l_type = F_UNLCK;
    fcntl(fsfd, F_SETLK, &lock_block);
}

int _get_lock_counter(int fsfd, master_block* masterblock) {
    block* lock_block = _read_block(fsfd, 0, masterblock->data_start_block, masterblock->block_size);
    int counter = *((int*) lock_block->data);
    free(lock_block);
    return counter;
}

/**
 * Funkcja oznaczajaca inode jako pusty i uaktualniająca w masterblocku wpis pierwszego wolnego inode'u, jeśli to konieczne
 */
void _mark_inode_as_empty(initialized_structures* structures, unsigned long inode_no) {
    //mark inode as empty
    structures->inode_table[inode_no].type = INODE_EMPTY;
    //update first free inode if applicable
    if(inode_no < structures->master_block_pointer->first_free_inode || structures->master_block_pointer->first_free_inode == 0) {
        structures->master_block_pointer->first_free_inode = inode_no;
    }
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

int _create_file_or_dir(char *name, int fsfd, int is_dir) {

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
        DEBUG("Name TOO LONG!");
        return NAME_TOO_LONG;
    } else {
        file_name_length = full_path_length - path_length;
    }
    strncpy(file_name, name + path_length, full_path_length - path_length);
    path[path_length] = '\0';

    //for files deeper than in the root dir, remove the slash at the end
    if(path_length != 1 && path[path_length-1] == '/') {
        path[path_length-1] = '\0';
    }

    file_name[file_name_length] = '\0';
    DEBUG("%d", path_length);
    DEBUG("%s\n", path);
    DEBUG("%s\n", file_name);
    //blokujemy plik .lock
    initialized_structures * is = _initialize_structures(fsfd, 1);
    DEBUG("masterblock pointer: %d\n", is->master_block_pointer);
    inode * parent_node;
    DEBUG("Przed seg fault: is = %d\n", is);
    _lock_lock_file(is->master_block_pointer, fsfd);
    do {
        unsigned long tmp;
        parent_node = _get_inode_by_path(path, is->master_block_pointer, fsfd, &tmp);
        if(parent_node == NULL) {
            result = DIR_DOESNT_EXIST;
            DEBUG("Dir deousn exist\n");
            break;
        }
        DEBUG("%d", tmp);
        DEBUG("\n%s\n", parent_node->filename);
        inode new_file;
        strcpy(new_file.filename, file_name);
        new_file.type = (is_dir ? 'D' : 'F');
        new_file.size = 0;
        new_file.first_data_block = 0;
        unsigned long inode_no = _insert_new_inode(&new_file, is, fsfd);
        if(inode_no == 0) {
            result =  NO_FREE_INODES;
            DEBUG("no free inodes!\n");
            break;
        }
        DEBUG("Inserted new inofde: %lu\n", inode_no);
        path[path_length] = '\0'; //przerobic katalog na plik
        write_params write_params_value;
        write_params_value.fsfd = fsfd;
        int fd = simplefs_open(path, READ_AND_WRITE, fsfd);
        write_params_value.fd = fd;
        file_signature fs;
        memset(&fs, 0, sizeof(file_signature));
        strcpy(fs.name, file_name);
        fs.inode_no = inode_no;
        write_params_value.data = &fs;
        write_params_value.data_length = sizeof(file_signature);
        write_params_value.lock_blocks = TRUE;
        write_params_value.file_offset = -1; //append
        write_params_value.for_each_record = _check_duplicate_file_names_in_block;
        write_params_value.additional_param = file_name;
        int write_result = _write_unsafe(is, write_params_value);
        if (write_result == NO_FREE_BLOCKS) {
            result = NO_FREE_BLOCKS;
        } else if(write_result == -2) {
            result = FILE_ALREADY_EXISTS;
        }
        //rolback inode got but eventually not used
        if(result == NO_FREE_BLOCKS || result == FILE_ALREADY_EXISTS) {
            _mark_inode_as_empty(is, inode_no);
        }
        simplefs_close(fd);
    } while( FALSE );
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
int simplefs_creat(char *name, int fsfd) { //Adam
    return _create_file_or_dir(name, fsfd, FALSE);
}

int _read_unsafe(int fd, char* buf, int len, int fsfd, unsigned long file_size) {
    initialized_structures * initialized_structures_pointer = _initialize_structures(fsfd, 1);
    file * file_pointer = _get_file_by_fd(fd);
    master_block * masterblock = initialized_structures_pointer->master_block_pointer;
    if(initialized_structures_pointer == NULL) {
        return FILE_SYSTEM_ERROR;
    }
    if(file_pointer == NULL) {
        return FD_NOT_FOUND;
    }

    unsigned long position = file_pointer->position;
    unsigned long current_block_number = _load_inode_from_file_structure(initialized_structures_pointer, file_pointer)->first_data_block;
    unsigned long current_position = 0;
    block t;
    unsigned long block_data_size =  masterblock->block_size - sizeof(t.next_data_block); //realny rozmiar bloku
    //przesuwamu sie przez dane które nasz position ignoruje
    while (current_position + block_data_size <= position && current_block_number != 0 && current_position + block_data_size <= file_size) {
        current_block_number = _find_next_block(fsfd, current_block_number,  masterblock->data_start_block, masterblock->block_size);
        current_position += block_data_size;
    }
    unsigned long data_read = 0;
    block * current_block;
    unsigned first_block = 1;
    while (current_block_number != 0 && data_read < len && current_position < file_size) { //dopoki mozna czytac
        current_block = _read_block(fsfd, current_block_number, masterblock->data_start_block, masterblock->block_size);
        if (first_block) { //pierwszy czytany blok
            unsigned long position_in_read_block = (position == current_position ? 0 : position - current_position);
            current_position += position_in_read_block;
            unsigned long portion_to_read = block_data_size - position_in_read_block; //ile chcemy wczytac
            if (portion_to_read > len) { //jak wiecej niz powinnismy
                portion_to_read = len;
            }
            if (current_position + portion_to_read > file_size) { //jak wyskakuje poza to obicnamy
                portion_to_read = file_size - current_position;
            }

            memcpy(buf + data_read, current_block->data + position_in_read_block, portion_to_read);
            data_read += portion_to_read;
            current_position += portion_to_read;
            first_block = 0;
        } else { //tutaj czytamy od poczatku bloku
            unsigned long portion_to_read = block_data_size;
            if(data_read + portion_to_read > file_size) {
                portion_to_read = file_size - data_read;
            }
            if(data_read + portion_to_read > len) {
                portion_to_read = len - data_read;
            }
            memcpy(buf + data_read, current_block->data, portion_to_read);
            data_read += portion_to_read;
            current_position += portion_to_read;
        }
        current_block_number = current_block->next_data_block;
        free(current_block->data);
        free(current_block);
    }
    file_pointer->position += data_read;
    _uninitilize_structures(initialized_structures_pointer);
    return data_read;
}

int simplefs_read(int fd, char *buf, int len, int fsfd) { //Adam
    initialized_structures * initialized_structures_pointer = _initialize_structures(fsfd, 1);
    file * file_pointer = _get_file_by_fd(fd);
    _lock_lock_file(initialized_structures_pointer->master_block_pointer, fsfd);
    unsigned long file_size = _load_inode_from_file_structure(initialized_structures_pointer, file_pointer)->size;
    _unlock_lock_file(initialized_structures_pointer->master_block_pointer, fsfd);
    _uninitilize_structures(initialized_structures_pointer);
    return _read_unsafe(fd, buf, len, fsfd, file_size);
}

int simplefs_write(int fd, char *buf, int len, int fsfd) { //Mateusz
    initialized_structures * initialized_structures_pointer = _initialize_structures(fsfd, 1);
    if (initialized_structures_pointer == NULL) {
        DEBUG("Blad");
        return -1;
    }
    file * file_pointer = _get_file_by_fd(fd);
    if (file_pointer == NULL) {
        return FD_NOT_FOUND;
    }
    _try_lock_lock_inode(initialized_structures_pointer->master_block_pointer, fsfd);
    _lock_lock_file(initialized_structures_pointer->master_block_pointer, fsfd);

    write_params write_params_structure;
    write_params_structure.data_length = len;
    write_params_structure.data = buf;
    write_params_structure.fsfd = fsfd;
    write_params_structure.fd = fd;
    write_params_structure.lock_blocks = 0;
    write_params_structure.file_offset = file_pointer->position;
    write_params_structure.for_each_record = NULL;
    int result = _write_unsafe(initialized_structures_pointer, write_params_structure);

    _unlock_lock_file(initialized_structures_pointer->master_block_pointer, fsfd);
    _unlock_lock_inode(initialized_structures_pointer->master_block_pointer, fsfd);

    _uninitilize_structures(initialized_structures_pointer);
    return result;
}

/**
 * Funkcja zakłada poprawną inicjalizację struktur.
 */
int simplefs_lseek_unsafe(int fd, initialized_structures * initialized_structures_pointer, int whence, int offset, int fsfd) {

    file * file_pointer =_get_file_by_fd(fd);
    if (file_pointer == NULL) {
        _unlock_lock_file(initialized_structures_pointer->master_block_pointer, fsfd);
        return FD_NOT_FOUND;
    }
    int effective_offset = 0;
    unsigned long file_size = _load_inode_from_file_structure(initialized_structures_pointer, file_pointer)->size;
    switch(whence) {
        case SEEK_SET:
            effective_offset = offset;
            break;
        case SEEK_CUR:
            effective_offset = file_pointer->position + offset;
            break;
        case SEEK_END:
            effective_offset = file_size + offset;

            break;
        default:
            _unlock_lock_file(initialized_structures_pointer->master_block_pointer, fsfd);
            return - 1;
    }
    if (effective_offset < 0) {
        effective_offset = 0;
    } else if (effective_offset > file_size) {
        effective_offset = file_size;
    }
    file_pointer->position = effective_offset;
}

int simplefs_lseek(int fd, int whence, int offset, int fsfd) { //Mateusz
    initialized_structures * initialized_structures_pointer = _initialize_structures(fsfd, 1);
    if (initialized_structures_pointer == NULL) {
        DEBUG("Blad");
        return -1;
    }
    _lock_lock_file(initialized_structures_pointer->master_block_pointer, fsfd);
    simplefs_lseek_unsafe(fd, initialized_structures_pointer, whence, offset, fsfd);
    _unlock_lock_file(initialized_structures_pointer->master_block_pointer, fsfd);
    _uninitilize_structures(initialized_structures_pointer);
    return 0;
}
