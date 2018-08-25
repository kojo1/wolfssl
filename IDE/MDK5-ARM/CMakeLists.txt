
# TEST IDE Make                    #
#      IDE/MDK5-ARM               #

set(IDE_PATH ${WOLFSSL_ROOT}/IDE/MDK5-ARM)
add_definitions(-DWOLFSSL_USER_SETTINGS)
include_directories(${INCLUDE_PATH})
include_directories(${IDE_PATH}/Conf)

add_library(libwolfssl_static STATIC ${LIBWOLFSSL_SRCS} ${LIBWOLFCRYPT_SRCS})
set_target_properties(libwolfssl_static PROPERTIES OUTPUT_NAME wolfssl)


#      IDE/IAR-EWARM/Projects/test #

set(PROJ_TEST_SRCS 
    ${WOLFSSL_ROOT}/wolfcrypt/test/test.c
    ${IDE_PATH}/Projects/CryptTest/main.c
)

add_definitions(-DWOLFSSL_USER_SETTINGS -DWOLFSSL_NO_PACK)
include_directories(${INCLUDE_PATH})
include_directories(${IDE_PATH}/Projects)

add_executable(testwolfcrypt  ${PROJ_TEST_SRCS})
target_link_libraries(testwolfcrypt libwolfssl_static)
