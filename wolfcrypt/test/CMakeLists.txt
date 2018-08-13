set(TEST_SRC test.c)
link_directories(../../wolfcrypt/src ../../src)
include_directories(${INCLUDE_PATH})
add_executable(${TESTWOLFCRYPT} ${TEST_SRC})
target_link_libraries(${TESTWOLFCRYPT} ${LIBWOLFCRYPT} ${LIBWOLFSSL} )



