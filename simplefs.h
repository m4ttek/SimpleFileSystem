/**
 * @file simplefs.h
 * @author Mateusz Kamiński
 * @author Adam Mościcki
 * @author Michał Pluta 
 * @brief Definicje funkcji systemu plików 
 */

#ifndef _SIMPLEFS_H
#define _SIMPLEFSH_

#include "uthash.h"

#define TRUE 1
#define FALSE 0

#define SIMPLEFS_MAGIC_NUMBER 0x4A5B
#define FILE_NAME_LENGTH (256 - 2 * sizeof(long) - 2 * sizeof(char))

/**
 * Tworzy system plików pod zadaną ścieżkę
 * @param path - ścieżka do tworzonego systemu plików
 * @param block_size - rozmiar bloku w bajtach - minimalnie 1024 B
 * @param number_of_blocks - liczba bloków
 *
 * @return {0} sukces, {-1} błąd
 */
int simplefs_init(char *path, unsigned block_size, unsigned number_of_blocks);

//Błędy
#define HOST_FILE_ACCESS_ERROR -1
#define BLOCK_SIZE_TOO_SMALL -2
#define NUMBER_OF_BLOCKS_ZERO -3

/**
 * Otwiera plik zawierający system plików spod zadanej ścieżki 
 * @param path - ścieżka do systemu plików
 *
 * @return {deskryptor} sukces, {-1} błąd
 */
int simplefs_openfs(char *path);


/**
 * Zamyka system plików - należy ją wywołać po zakończeniu pracy z systemem plików
 * @param fsfd - deskryptor systemu plików zwrócony przez simplefs_openfs
 *
 * @return {0} sukces, {-1} błąd
 */
int simplefs_closefs(int fsfd);

/**
 * Otwiera plik o podanej nazzwie w danym trybie, w systemie z danego deskryptora
 * @param name - nazwa pliku
 * @param mode - tryb {patrz niżej}
 * @param fsfd - deskryptor do systemu plików
 *
 * @return {deskryptor} sukces, {-1, -2} bład (patrz niżej)
 */
int simplefs_open(char *name, int mode, int fsfd);

//Tryby
#define READ_MODE 0x04
#define WRITE_MODE 0x02
#define READ_AND_WRITE 0x06
//Błędy
#define FILE_DOESNT_EXIST -1
#define WRONG_MODE -2

/**
 * Usuwa plik o podanej nazwie w systemie plików, w systemie z danego deskryptor
 * @param name - nazwa pliku
 * @param fsfd - deskryptor do systemu plików
 *
 * @return {0} sukces, {-1} brak pliku
 */
int simplefs_unlink(char *name, int fsfd);

//Zwracane
#define OK 0
#define FILE_DOESNT_EXIST -1

/**
 * Tworzy katalog o pełnej ścieżce, gdzie kolejne katalogi są oddzielone znakiem ‘/’,  różne od ‘.’ oraz ‘..’
 * @param name - nazwa katalogu
 * @param fsfd - deskryptor do systemu plików
 *
 * @return {0} sukces, {-1, -2} błąd (patrz niżej)
 */
int simplefs_mkdir (char *name, int fsfd);

#define OK 0
#define PARENT_DIR_DOESNT_EXIST -1
#define NAME_ALREADY_IN_USE -2

/**
 * Tworzy plik o podanej nazwie (razem ze ścieżką oraz trybie praw, zapis/odczyt)
 * @param name - nazwa pliku wraz ze ścieżką
 * @param mode - tryb
 * @param fsfd - deskryptor do systemu plików
 *
 * @return {0} sukces, {-1, -2, -3, -4, -5} błąd (patrz niżej)
 */

int simplefs_creat (char *name, int mode, int fsfd);

//Zwracane
#define OK 0 
#define FILE_ALREADY_EXISTS -1 
#define WRONG_MODE -2 
#define DIR_DOESNT_EXIST -3 
#define NAME_TOO_LONG -4
#define NO_FREE_BLOCKS -5

/**
 * 	Czyta plik do podanego bufora o podanej długości.
 * @param fd - deskryptor do pliku
 * @param buf - bufor, do którego zostanie wczytana zawartość pliku
 * @param len - rozmiar bufora
 * @param fsfd - deskryptor do systemu plików
 *
 * @return {wczytana wilkosc} jesli wczytana wielkosc < len => koniec pliku, 
 */
int simplefs_read(int fd, char *buf, int len, int fsfd);

/**
 * Zapisuje z zawartość bufora o podanej długości do pliku określonego przez deskryptor
 * @param fd - deskryptor do pliku
 * @param buf - bufor, z którego będzie zapisywana informacja
 * @param len - rozmiar bufora
 * @param fsfd - deskryptor do systemu plików
 *
 * @return {0} sukces, {-1} bład (patrz niżej)
 */
int simplefs_write(int fd, char *buf, int len, int fsfd);

//Zwracane
#define OK 0 
#define CANNOT_EXTEND_FILE -1 

/**
 * Przesuwa pozycję o podany offset w pliku, pod warunkami określonymi przez whence
 * @param fd - deskryptor pliku
 * @param whence - jedna z trzech wartości (patrz niżej)
 * @param offset - liczba bajtów, o które chcemy się przesunąć
 * @param fsfd - deskryptor do systemu plików
 *
 * @return {0} sukces, {-1} bład
 */

int simplefs_lseek(int fd, int whence, int offset, int fsfd);

//Whence
#define SEEK_SET 0 //ustawienie pozycji za początkiem pliku
#define SEEK_CUR 1 //ustawienie pozycji po aktualnej pozycji
#define SEEK_END 2 //ustawienie pozycji za końcem pliku

#define INODE_DIR 'D'
#define INODE_FILE 'F'

/**
 * Struktura metryczki dla pliku na dysku.
 */
typedef struct inode_t {
    char filename[FILE_NAME_LENGTH];
    char type;
    char mode; /* tryb dostepu */
    unsigned long size;
    unsigned long first_data_block;
} inode;

/**
 * Struktura pierwszego bloku na dysku.
 */
typedef struct master_block_t {
    unsigned int block_size;
    unsigned long number_of_blocks;               //1 master, n/floor[block_size/sizeof(inode)], n blokow_uzytkowych n/liczbę blokow_użytkowych,
    unsigned long number_of_free_blocks;          //Wielkość systemu plików = number_of_blocks + number_of_bitmap_blocks + number_of_inode_table_blocks + 1
    unsigned int data_start_block;               //numer bloku w całym systemie plików, który jest pierwszym blokiem danych
    unsigned long first_free_block_number;        // pierwszy wolny blok
    unsigned long number_of_bitmap_blocks;        // ilość bloków bitmapowych
    unsigned long number_of_inode_table_blocks;
    unsigned long first_inode_table_block;
    unsigned int magic_number;
    /* TODO struct inode root_node; */
} master_block;

/**
 * Struktura bloku bitmapowego, zawierająca bity zajętości bloków.
 */
typedef struct block_bitmap_t {
    //bitmap length is block-size
	char* bitmap;
} block_bitmap;

/**
 * Struktura reprezentująca blok zawierający fragment danych jednego pliku.
 */
typedef struct block_t {
	//data length is block_size - 8 (next_data_block)
	char* data;
    unsigned long next_data_block;
} block;

typedef struct file_signature_t {
    char name[FILE_NAME_LENGTH];
    unsigned long inode_no;
} file_signature;

/**
 * Struktura reprezentująca unixową strukturę file - tutaj zawiera pozycję w otwartym pliku. Dla każdego wywołania
 * simplefs_open() będzie tworzona nowa taka struktura. Kolejne instancje tej strukturą będą przechowywane w mapie haszującej
 * Jak używać mapy hashującej: http://troydhanson.github.io/uthash/
 */
typedef struct file_t {
    int fd;
    long position;
    UT_hash_handle hh; //makes the struct hashable
} file;

#endif //_SIMPLEFS_H