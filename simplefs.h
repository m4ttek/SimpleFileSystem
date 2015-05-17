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
 * @param block_size - rozmar bloku w bajtach
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
int simpelfs_openfs(char *path);


/**
 * Zamyka system plików - należy ją wywołać po zakończeniu pracy z systemem plików
 * @param fsfd - deskryptor systemu plików zwrócony przez simplefs_openfs
 *
 * @return {0} sukces, {-1} błąd
 */
int simpelfs_closefs(int fsfd);

/**
 * Otwiera plik o podanej nazzwie w danym trybie, w systemie z danego deskryptora
 * @param name - nazwa pliku
 * @param mode - tryb {patrz niżej}
 * @param fsfd - deskryptor
 * @return {deskryptor} sukces, {-1, -2} bład (patrz niżej)
 */
int simplefs_open(char *name, int mode, int fsfd);

//Tryby
#define READ_MODE 0x04
#define WRITE_MODE 0x02
#define READ_AND_WRITE 0x6
//Błędy
#define FILE_DOESNT_EXIST -1
#define WRONG_MODE -2



#endif //_SIMPLEFS_H
