/**
 * @file simplefs.h
 * @author Mateusz Kamiński
 * @author Adam Mościcki
 * @author Michał Pluta 
 * @brief Definicje funkcji systemu plików 
 */

#ifndef _SIMPLEFS_H
#define _SIMPLEFS_H

/**
 * Tworzy system plików pod zadaną ścieżkę
 * @param path - ścieżka do tworzonego systemu plików
 * @param ::block_size - rozmar bloku w bajtach
 * @param number_of_blocks - liczba bloków
 *
 * @return {0} sukces, {-1} błąd
 */
int simplefs_init(char *path, int block_size, int number_of_blocks);

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
 * @param whene - jedna z trzech wartości (patrz niżej)
 * @param offset - liczba bajtów, o które chcamy się przesunąć
 * @param fsfd - deskryptor do systemu plików
 *
 * @return {0} sukces, {-1} bład
 */

int simplefs_lseek(int fd, int whence, int offset, int fsfd);

//Whence
#define SEEK_SET 0 //ustawienie pozycji za początkiem pliku
#define SEEK_CUR 1 //ustawienie pozycji po aktualnej pozycji
#define SEEK_END 2 //ustawienie pozycji za końcem pliku

#define DIR 'D'
#define FILE 'F'

/**
 * Struktura metryczki dla pliku na dysku.
 * 
 */
typedef struct inode_t {
    char filename[255];
    char type;
    char is_open;
    char mode; /* tryb dostepu */
    int first_block_number;
    int file_position;
    int size;
} inode;

typedef struct master_block_t {
    int block_size;
    int number_of_blocks;               //1 ster, n/floor[block_size/sizeof(inode)], n blokow_uzytkowych n/liczbę blokow_użytkowych, 
    int number_of_free_blocks;          //Wielkość systemu plików = number_of_blocks + number_of_bitmap_blocks + number_of_inode_table_blocks + 1
    int first_free_block_number;
    int number_of_bitmap_blocks;
    int number_of_inode_table_blocks; 
    int magic_number;
    /* TODO struct inode root_node; */
} master_block;

typedef struct block_bitmap_t {
    //bitmap length is block-size
	char* bitmap; 
} block_bitmap;

typedef struct block_t {
	char is_empty;
	//data length is block_size - 1
	char* data;
	int next_free_block;
} block;

#endif //_SIMPLEFS_H