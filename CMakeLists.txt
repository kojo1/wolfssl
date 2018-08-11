cmake_minimum_required(VERSION 2.8)

include_directories( . )

add_subdirectory(wolfcrypt/src)
add_subdirectory(src)
add_subdirectory(wolfcrypt/test)
