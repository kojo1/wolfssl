
# TEST IDE Make                    #
#      IDE/IAR-EWARM               #
#      IDE/IAR-EWARM/Projects/lib  #
#      IDE/IAR-EWARM/Projects/test #

add_definitions(-DWOLFSSL_USER_SETTINGS)
include_directories(${INCLUDE_PATH})
include_directories(${WOLFSSL_ROOT}/IDE/IAR-EWARM/Projects)

add_library(libwolfssl_static STATIC ${LIBWOLFSSL_SRCS} ${LIBWOLFCRYPT_SRCS})
set_target_properties(libwolfssl_static PROPERTIES OUTPUT_NAME wolfssl)


#      IDE/IAR-EWARM/Projects/test #

set(IDE_PATH ${WOLFSSL_ROOT}/IDE/IAR-EWARM)
set(PROJ_TEST_SRCS 
    ${WOLFSSL_ROOT}/wolfcrypt/test/test.c
    ${IDE_PATH}/Projects/test/test-main.c
)

add_definitions(-DWOLFSSL_USER_SETTINGS)
include_directories(${INCLUDE_PATH})
include_directories(${WOLFSSL_ROOT}/IDE/IAR-EWARM/Projects)

add_executable(testwolfcrypt  ${PROJ_TEST_SRCS})
target_link_libraries(testwolfcrypt libwolfssl_static)
