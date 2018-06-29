cmake_minimum_required(VERSION 3.5)

project(SQLiteTest)

set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_STANDARD 11) 

add_executable(${PROJECT_NAME} sqlite3.c vfs.c test.cpp procvfs.cpp)
target_compile_options(${PROJECT_NAME} PRIVATE -Werror)
target_link_libraries(${PROJECT_NAME} dl gmock gtest gtest_main pthread)

