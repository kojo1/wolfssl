add_executable(testwolfcrypt test.c)
target_link_libraries(testwolfcrypt wolfssl)
target_link_libraries(testwolfcrypt wolfcrypt)

