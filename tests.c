#include <stdio.h>
#include <string.h>
#include "CUnit/Basic.h"
#include "CUnit/CUnit.h"
#include "simplefs.h"

/* Pointer to the file used by the tests. */
static FILE* temp_file = NULL;

master_block* initial_masterblock;

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
   simplefs_init("testfs", 4096, 1024);
}

void test_openfs() {
    CU_ASSERT(-1 == simplefs_openfs("fakefs"));
    CU_ASSERT(-1 != (fsfd = simplefs_openfs("testfs")));
    initial_masterblock = _get_master_block(fsfd);
}

void test_creat() {
    CU_ASSERT(OK == simplefs_creat("/testfile", fsfd));
    CU_ASSERT(FILE_ALREADY_EXISTS == simplefs_creat("/testfile", fsfd));

    CU_ASSERT(OK == simplefs_creat("/testfile1", fsfd));
    CU_ASSERT(FILE_ALREADY_EXISTS == simplefs_creat("/testfile", fsfd));
    CU_ASSERT(FILE_ALREADY_EXISTS == simplefs_creat("/testfile1", fsfd));

    master_block* masterblock = _get_master_block(fsfd);
    CU_ASSERT(initial_masterblock->number_of_free_blocks == masterblock->number_of_free_blocks + 1);
    free(masterblock);
}

void test_mkdir() {
    CU_ASSERT(FILE_ALREADY_EXISTS == simplefs_mkdir("/testfile", fsfd));
    CU_ASSERT(OK == simplefs_mkdir("/testdir", fsfd));
}

void test_create_file_in_dir() {
    CU_ASSERT(OK == simplefs_creat("/testdir/file_in_dir", fsfd));
    CU_ASSERT(FILE_ALREADY_EXISTS == simplefs_creat("/testdir/file_in_dir", fsfd));

    master_block* masterblock = _get_master_block(fsfd);
    CU_ASSERT(initial_masterblock->number_of_free_blocks == masterblock->number_of_free_blocks + 2);
    free(masterblock);
}

void test_unlink() {
    CU_ASSERT(OK == simplefs_unlink("/testfile", fsfd));
    master_block* masterblock = _get_master_block(fsfd);
    CU_ASSERT(initial_masterblock->first_free_inode == masterblock->first_free_inode);
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
       (NULL == CU_add_test(pSuite, "test of simplefs unlink", test_unlink)))
   {
      CU_cleanup_registry();
      return CU_get_error();
   }

   /* Run all tests using the CUnit Basic interface */
   CU_basic_set_mode(CU_BRM_VERBOSE);
   CU_basic_run_tests();
   CU_cleanup_registry();
   return CU_get_error();
}
