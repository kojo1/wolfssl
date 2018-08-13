cmake_minimum_required(VERSION 3.2)

set(PROMECT_NAME wolfssl)
set(WOLFSSL_ROOT .)
set(WOLFSSL_ROOT .)
set(INCLUDE_PATH ${WOLFSSL_ROOT})
set(LIBWOLFSSL wolfssl)
set(LIBWOLCRYPT wolfcrypt)
set(TESTWOLFCRYPT testwolfcrypt)

project(${PROJECT_NAME} C)

include_directories(${INCLUDE_PATH})

add_definitions(--include wolfssl/options.h)

add_subdirectory(wolfcrypt/src)
add_subdirectory(src)
add_subdirectory(wolfcrypt/test)

