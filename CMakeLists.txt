cmake_minimum_required(VERSION 3.2)
project(SimpleFileSystem)

add_custom_target(mytest COMMAND make -C ${mytest_SOURCE_DIR}
                         CLION_EXE_DIR=${PROJECT_BINARY_DIR})