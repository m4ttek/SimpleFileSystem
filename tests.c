#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "CUnit/Basic.h"
#include "CUnit/CUnit.h"
#include "simplefs.h"

/* Pointer to the file used by the tests. */
static FILE* temp_file = NULL;

int fsfd = -1;

/* The suite initialization function.
 * Opens the temporary file used by the tests.
 * Returns zero on success, non-zero otherwise.
 */
int init_suite1(void)
{
   if (NULL == (temp_file = fopen("temp.txt", "w+"))) {
      return -1;
   }
   else {
      return 0;
   }
}

/* The suite cleanup function.
 * Closes the temporary file used by the tests.
 * Returns zero on success, non-zero otherwise.
 */
int clean_suite1(void)
{
   if (0 != fclose(temp_file)) {
      return -1;
   }
   else {
      temp_file = NULL;
      return 0;
   }
}

int init_suite2(void) {
    remove("testfs2");
    return 0;
}

int clean_suite2(void) {
    return 0;
}


/* Simple test of fprintf().
 * Writes test data to the temporary file and checks
 * whether the expected number of bytes were written.
 */
void testFPRINTF(void)
{
   int i1 = 10;

   if (NULL != temp_file) {
      CU_ASSERT(0 == fprintf(temp_file, ""));
      CU_ASSERT(2 == fprintf(temp_file, "Q\n"));
      CU_ASSERT(7 == fprintf(temp_file, "i1 = %d", i1));
   }
}

/* Simple test of fread().
 * Reads the data previously written by testFPRINTF()
 * and checks whether the expected characters are present.
 * Must be run after testFPRINTF().
 */
void testFREAD(void)
{
   unsigned char buffer[20];

   if (NULL != temp_file) {
      rewind(temp_file);
      CU_ASSERT(9 == fread(buffer, sizeof(unsigned char), 20, temp_file));
      CU_ASSERT(0 == strncmp(buffer, "Q\ni1 = 10", 9));
   }
}

void test_initfs() {
   // CU_ASSERT(0 == simplefs_init("testfs", 4096, 1024));
   simplefs_init("testfs", 4096, 16);
}

void test_openfs() {
    CU_ASSERT(-1 == simplefs_openfs("fakefs"));
    CU_ASSERT(-1 != (fsfd = simplefs_openfs("testfs")));
}

void test_creat() {
    CU_ASSERT(OK == simplefs_creat("/testfile", fsfd));
    CU_ASSERT(FILE_ALREADY_EXISTS == simplefs_creat("/testfile", fsfd));

    CU_ASSERT(OK == simplefs_creat("/testfile1", fsfd));
    CU_ASSERT(FILE_ALREADY_EXISTS == simplefs_creat("/testfile", fsfd));
    int response;
    CU_ASSERT(FILE_ALREADY_EXISTS == (response = simplefs_creat("/testfile1", fsfd)));
    if(response != FILE_ALREADY_EXISTS) {
        printf("Niedobrze");
    }
}

void test_mkdir() {
    CU_ASSERT(FILE_ALREADY_EXISTS == simplefs_mkdir("/testfile", fsfd));
    CU_ASSERT(OK == simplefs_mkdir("/testdir", fsfd));
}

void test_create_file_in_dir() {
    CU_ASSERT(OK == simplefs_creat("/testdir/file_in_dir", fsfd));
    CU_ASSERT(FILE_ALREADY_EXISTS == simplefs_creat("/testdir/file_in_dir", fsfd));
}

void test_unlink() {
    CU_ASSERT(OK == simplefs_unlink("/testfile", fsfd));
    CU_ASSERT(FILE_DOESNT_EXIST == simplefs_unlink("/testfile", fsfd));

    CU_ASSERT(OK == simplefs_unlink("/testfile1", fsfd));
    CU_ASSERT(FILE_DOESNT_EXIST == simplefs_unlink("/testfile", fsfd));
    CU_ASSERT(FILE_DOESNT_EXIST == simplefs_unlink("/testfile1", fsfd));

    //unlink dir
    CU_ASSERT(DIR_NOT_EMPTY == simplefs_unlink("/testdir", fsfd));
    //right, need to remove all the files in the dir
    CU_ASSERT(OK == simplefs_unlink("/testdir/file_in_dir", fsfd));
    CU_ASSERT(OK == simplefs_unlink("/testdir", fsfd));

    CU_ASSERT(FILE_DOESNT_EXIST == simplefs_unlink("/testdir", fsfd));

}

void test_creat_and_unlink() {
    int i = 0;
    while(i < 10000) {
        int result;
        CU_ASSERT(OK == (result = simplefs_creat("/ads", fsfd)));
        if (result != OK) {
            break;
        }
        CU_ASSERT(OK == (result = simplefs_unlink("/ads", fsfd)));
        if (result != OK) {
            break;
        }
        i++;
    }
}

/*
 * Funkcje testujące należące do suite 2.
 */
void test_write() {
    printf ("\n*********** TEST WRITE ***********\n");
    simplefs_init("testfs2", 4096, 5);
    unsigned long data_block_start = 1 + ceil((double) 5 / (4096 * 8)) + ceil((double) 5 / floor((double) 4096 / sizeof(inode)));
    printf("\n\nData block %d , byte start = %d\n\n", data_block_start, (1 + data_block_start) * 4096 );
    CU_ASSERT(-1 != (fsfd = simplefs_openfs("testfs2")));
   master_block* master_block = _get_master_block(fsfd);

    int fd = -1;
    CU_ASSERT(OK == simplefs_creat("/testfile", fsfd));
    CU_ASSERT(0 <= (fd = simplefs_open("/testfile", WRITE_MODE, fsfd)));
    if (fd < 0) {
        return;
    }
    char *buf = "testing file save";
    printf("\n\nPierwszy zapis do pliku\n\n");
    CU_ASSERT(OK == simplefs_write(fd, buf, 17, fsfd));
    block * block_pointer = (block *) _read_block(fsfd, 2, data_block_start, 4096);

    CU_ASSERT('t' == block_pointer->data[0]);

    // append do istniejącego asserta
    printf("\n\nDrugi zapis do pliku\n\n");
    CU_ASSERT(OK == simplefs_write(fd, buf, 17, fsfd));
    block_pointer = (block *) _read_block(fsfd, 2, data_block_start, 4096);
    CU_ASSERT('t' == block_pointer->data[0]);
    CU_ASSERT('t' == block_pointer->data[17]);

    // append na tyle długi żeby przeszedł na drugi blok
    char second_bufs[4096];
    int i;
    for(i = 0; i < 4096; i++) {
        second_bufs[i] = 1;
    }

    printf("\n\nZapis dużej dawki jedynek\n\n");
    CU_ASSERT(OK == simplefs_write(fd, second_bufs, 4096, fsfd));
    // sprawdzenie czy dane zapisane poprawnie:
    block_pointer = (block *) _read_block(fsfd, 2, data_block_start, 4096);
    block * second_block_pointer = (block *) _read_block(fsfd, 3, data_block_start, 4096);
    // pierwszy blok
    for (i = 2 * 17; i < 4096 - sizeof(long); i++) {
        CU_ASSERT(1 == block_pointer->data[i]);
        if (1 != block_pointer->data[i]) {
            break;
        }
    }
    // drugi blok
    for (i = 0; i < 2 * 17 + sizeof(long); i++) {
        CU_ASSERT(1 == second_block_pointer->data[i]);
        if (1 != second_block_pointer->data[i]) {
            break;
        }
    }

    printf("\n\nZapis następnej dawki jedynek, który nie powinien zostać zapisany z powodu niewystarczającej ilości miejsca\n\n");
    CU_ASSERT(NO_FREE_BLOCKS == simplefs_write(fd, second_bufs, 4096, fsfd));
    block * third_block_pointer = (block *) _read_block(fsfd, 3, data_block_start, 4096);
    for (i = 2 * 17 + sizeof(long); i < 4096 - sizeof(long); i++) {
        CU_ASSERT(1 != third_block_pointer->data[i]);
        if (1 == third_block_pointer->data[i]) {
            break;
        }
    }

    //clean
    CU_ASSERT(OK == simplefs_unlink("/testfile", fsfd));
}


#define READ_TEST_LEN 16000

//tworzy system plikow do testu reada
int init_suite3(void)
{
    simplefs_init("testfs3", 4096, 8);
}

//usuwa system plikow po tescie reada
int clean_suite3(void)
{
   remove("testfs3");
}

/*
 * Funkcje testujące należące do suite 3.
 */
void test_read() {
    printf ("\n*********** TEST READE ***********\n");

    int fdfs = simplefs_openfs("testfs3");
    CU_ASSERT(fdfs > 0);
    CU_ASSERT(OK == simplefs_creat("/a.txt", fdfs));
    int fd = simplefs_open("/a.txt", READ_AND_WRITE, fdfs);
    printf("\nFD %d\n", fd);
    CU_ASSERT(0 <= fd);
    if (fd < 0) {
        return;
    }
    CU_ASSERT(OK == simplefs_write(fd, "adam to glupi programista", 15, fdfs));
    char result[20];
    simplefs_lseek(fd, SEEK_SET,0,fdfs);
    CU_ASSERT(10 == simplefs_read(fd, result, 10, fdfs));
    result[10] = '\0';
    CU_ASSERT(strcmp("adam to gl", result) == 0);
    CU_ASSERT(OK == simplefs_creat("/b.txt", fdfs));
    int fd2 = simplefs_open("/b.txt", READ_AND_WRITE, fdfs);
    CU_ASSERT(0 < fd2);
    char message[READ_TEST_LEN], read[READ_TEST_LEN];
    int i = 0;
    for(i = 0; i < READ_TEST_LEN; ++i) {
        message[i] = 'a' + i % 26;
        read[i] = 0;
    }
    CU_ASSERT(OK == simplefs_write(fd2, message, READ_TEST_LEN, fdfs));
    simplefs_lseek(fd2, SEEK_SET, 0, fdfs);
    CU_ASSERT(READ_TEST_LEN == simplefs_read(fd2, read, READ_TEST_LEN, fdfs));
    for(i = 0; i < READ_TEST_LEN; ++i) {
        if(read[i] != message[i]) {
            CU_ASSERT(0 == 1);
            break;
        }
    }

    //test odczytu z bloku
    simplefs_lseek(fd2, SEEK_SET, (4096/26+1)*26, fdfs);
    char alphabet[26];
    CU_ASSERT(26 == simplefs_read(fd2, alphabet, 26, fdfs));
    for(i = 0; i < 26; ++i) {
        if(alphabet[i] != 'a' + i) {
            CU_ASSERT(1 == 2);
        }
    }


    CU_ASSERT(OK == simplefs_unlink("/a.txt", fdfs));
    CU_ASSERT(OK == simplefs_unlink("/b.txt", fdfs));
}

void test_create_100_files() {
    /*simplefs_init("testfs3", 4096, 1024);
    int fsfd;
    CU_ASSERT(-1 != (fsfd = simplefs_openfs("testfs3")));
    char buf[100];
    int i;
    for(i = 0; i < 1; i++) {
        sprintf(buf,"/%d",i);
        int result = simplefs_creat(buf, fsfd);
        if(result != OK) {
            printf("Nie poszło");
        }
        CU_ASSERT(OK == result);
    }*/
}

int main()
{
   CU_pSuite pSuite = NULL;

   /* initialize the CUnit test registry */
   if (CUE_SUCCESS != CU_initialize_registry())
      return CU_get_error();

   /* add a suite to the registry */
   pSuite = CU_add_suite("Suite_1", init_suite1, clean_suite1);
   if (NULL == pSuite) {
      CU_cleanup_registry();
      return CU_get_error();
   }

   /* add the tests to the suite */
   /* NOTE - ORDER IS IMPORTANT - MUST TEST fread() AFTER fprintf() */
   if ((NULL == CU_add_test(pSuite, "test of fprintf()", testFPRINTF)) ||
       (NULL == CU_add_test(pSuite, "test of fread()", testFREAD)) ||
       (NULL == CU_add_test(pSuite, "test of simplefs_init", test_initfs)) ||
       (NULL == CU_add_test(pSuite, "test of simplefs openfs", test_openfs)) ||
       (NULL == CU_add_test(pSuite, "test of simplefs creat", test_creat)) ||
       (NULL == CU_add_test(pSuite, "test of simplefs mkdir", test_mkdir)) ||
       (NULL == CU_add_test(pSuite, "test of creating file in a directory", test_create_file_in_dir)) ||
       (NULL == CU_add_test(pSuite, "test of simplefs unlink", test_unlink)) ||
       (NULL == CU_add_test(pSuite, "test of creating 100 files", test_create_100_files)) ||
       (NULL == CU_add_test(pSuite, "test of creating and droping till bugg", test_creat_and_unlink)))
   {
      CU_cleanup_registry();
      return CU_get_error();
   }

    /* add a suite to the registry */
    pSuite = CU_add_suite("Suite_2", init_suite2, clean_suite2);
    if (NULL == pSuite) {
        CU_cleanup_registry();
        return CU_get_error();
    }

    /* add the tests to the suite 2 */
    if ((NULL == CU_add_test(pSuite, "test of simplefs write", test_write)))
    {
        CU_cleanup_registry();
        return CU_get_error();
    }

    /* Test reada */
    pSuite = CU_add_suite("Suite_3", init_suite3, clean_suite3);
    if ((NULL == CU_add_test(pSuite, "test of simplefs_read operation", test_read)))
    {
        CU_cleanup_registry();
        return CU_get_error();
    }



    /* add a suite to the registry */
    /*pSuite = CU_add_suite("Suite_3", init_suite1, clean_suite1);
    if (NULL == pSuite) {
        CU_cleanup_registry();
        return CU_get_error();
    }*/

    /* add the tests to the suite 2 */
    /*if ((NULL == CU_add_test(pSuite, "test of creating 100 files", test_create_100_files)));
    {
        CU_cleanup_registry();
        return CU_get_error();
    }*/

   /* Run all tests using the CUnit Basic interface */
   CU_basic_set_mode(CU_BRM_VERBOSE);
   CU_basic_run_tests();
   CU_cleanup_registry();
   return CU_get_error();
}
