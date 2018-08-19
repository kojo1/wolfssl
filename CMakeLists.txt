cmake_minimum_required(VERSION 3.8)

set(PROMECT_NAME wolfssl)
set(WOLFSSL_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(INCLUDE_PATH ${WOLFSSL_ROOT})

project(${PROJECT_NAME} C)

project(wolfssl)
set(LIBWOLFSSL_SRCS
    src/keys.c
    src/sniffer.c
    src/tls.c
    src/wolfio.c
    src/crl.c
    src/internal.c
    src/ocsp.c
    src/ssl.c
    src/tls13.c
)
set(LIBWOLFSSL_HEADERS
    wolfssl/callbacks.h
    wolfssl/certs_test.h
    wolfssl/crl.h
    wolfssl/error-ssl.h
    wolfssl/internal.h
    wolfssl/ocsp.h
    wolfssl/sniffer.h
    wolfssl/sniffer_error.h
    wolfssl/ssl.h
    wolfssl/test.h
    wolfssl/version.h
)
set(LIBWOLFSSL_OPENSSL_HEADERS
    wolfssl/openssl/aes.h
    wolfssl/openssl/asn1.h
    wolfssl/openssl/bio.h
    wolfssl/openssl/bn.h
    wolfssl/openssl/buffer.h
    wolfssl/openssl/conf.h
    wolfssl/openssl/crypto.h
    wolfssl/openssl/des.h
    wolfssl/openssl/dh.h
    wolfssl/openssl/dsa.h
    wolfssl/openssl/ec.h
    wolfssl/openssl/ec25519.h
    wolfssl/openssl/ecdh.h
    wolfssl/openssl/ecdsa.h
    wolfssl/openssl/ed25519.h
    wolfssl/openssl/engine.h
    wolfssl/openssl/err.h
    wolfssl/openssl/evp.h
    wolfssl/openssl/hmac.h
    wolfssl/openssl/lhash.h
    wolfssl/openssl/md4.h
    wolfssl/openssl/md5.h
    wolfssl/openssl/objects.h
    wolfssl/openssl/ocsp.h
    wolfssl/openssl/opensslconf.h
    wolfssl/openssl/opensslv.h
    wolfssl/openssl/ossl_typ.h
    wolfssl/openssl/pem.h
    wolfssl/openssl/pkcs12.h
    wolfssl/openssl/rand.h
    wolfssl/openssl/rc4.h
    wolfssl/openssl/ripemd.h
    wolfssl/openssl/rsa.h
    wolfssl/openssl/sha.h
    wolfssl/openssl/ssl.h
    wolfssl/openssl/ssl23.h
    wolfssl/openssl/stack.h
    wolfssl/openssl/ui.h
    wolfssl/openssl/x509.h
    wolfssl/openssl/x509v3.h
)
set(LIBWOLFCRYPT_SRCS
    wolfcrypt/src/aes.c
    wolfcrypt/src/arc4.c
    wolfcrypt/src/asm.c
    wolfcrypt/src/asn.c
    wolfcrypt/src/blake2b.c
    wolfcrypt/src/camellia.c
    wolfcrypt/src/chacha.c
    wolfcrypt/src/chacha20_poly1305.c
    wolfcrypt/src/cmac.c
    wolfcrypt/src/coding.c
    wolfcrypt/src/compress.c
    wolfcrypt/src/cpuid.c
    wolfcrypt/src/cryptodev.c
    wolfcrypt/src/curve25519.c
    wolfcrypt/src/des3.c
    wolfcrypt/src/dh.c
    wolfcrypt/src/dsa.c
    wolfcrypt/src/ecc.c
    wolfcrypt/src/ecc_fp.c
    wolfcrypt/src/ed25519.c
    wolfcrypt/src/error.c
    wolfcrypt/src/fe_low_mem.c
    wolfcrypt/src/fe_operations.c
    wolfcrypt/src/ge_low_mem.c
    wolfcrypt/src/ge_operations.c
    wolfcrypt/src/hash.c
    wolfcrypt/src/hc128.c
    wolfcrypt/src/hmac.c
    wolfcrypt/src/idea.c
    wolfcrypt/src/integer.c
    wolfcrypt/src/logging.c
    wolfcrypt/src/md2.c
    wolfcrypt/src/md4.c
    wolfcrypt/src/md5.c
    wolfcrypt/src/memory.c
    wolfcrypt/src/pkcs12.c
    wolfcrypt/src/pkcs7.c
    wolfcrypt/src/poly1305.c
    wolfcrypt/src/pwdbased.c
    wolfcrypt/src/rabbit.c
    wolfcrypt/src/random.c
    wolfcrypt/src/ripemd.c
    wolfcrypt/src/rsa.c
    wolfcrypt/src/sha.c
    wolfcrypt/src/sha256.c
    wolfcrypt/src/sha3.c
    wolfcrypt/src/sha512.c
    wolfcrypt/src/signature.c
    wolfcrypt/src/sp_arm32.c
    wolfcrypt/src/sp_arm64.c
    wolfcrypt/src/sp_c32.c
    wolfcrypt/src/sp_c64.c
    wolfcrypt/src/sp_int.c
    wolfcrypt/src/sp_x86_64.c
    wolfcrypt/src/srp.c
    wolfcrypt/src/tfm.c
    wolfcrypt/src/wc_encrypt.c
    wolfcrypt/src/wc_port.c
    wolfcrypt/src/wolfevent.c
    wolfcrypt/src/wolfmath.c
)
set(LIBWOLFCRYPT_HEADERS
    wolfssl/wolfcrypt/aes.h
    wolfssl/wolfcrypt/arc4.h
    wolfssl/wolfcrypt/asn.h
    wolfssl/wolfcrypt/asn_public.h
    wolfssl/wolfcrypt/blake2-impl.h
    wolfssl/wolfcrypt/blake2-int.h
    wolfssl/wolfcrypt/blake2.h
    wolfssl/wolfcrypt/camellia.h
    wolfssl/wolfcrypt/chacha.h
    wolfssl/wolfcrypt/chacha20_poly1305.h
    wolfssl/wolfcrypt/cmac.h
    wolfssl/wolfcrypt/coding.h
    wolfssl/wolfcrypt/compress.h
    wolfssl/wolfcrypt/cpuid.h
    wolfssl/wolfcrypt/cryptodev.h
    wolfssl/wolfcrypt/curve25519.h
    wolfssl/wolfcrypt/des3.h
    wolfssl/wolfcrypt/dh.h
    wolfssl/wolfcrypt/dsa.h
    wolfssl/wolfcrypt/ecc.h
    wolfssl/wolfcrypt/ed25519.h
    wolfssl/wolfcrypt/error-crypt.h
    wolfssl/wolfcrypt/fe_operations.h
    wolfssl/wolfcrypt/fips_test.h
    wolfssl/wolfcrypt/ge_operations.h
    wolfssl/wolfcrypt/hash.h
    wolfssl/wolfcrypt/hc128.h
    wolfssl/wolfcrypt/hmac.h
    wolfssl/wolfcrypt/idea.h
    wolfssl/wolfcrypt/integer.h
    wolfssl/wolfcrypt/logging.h
    wolfssl/wolfcrypt/md2.h
    wolfssl/wolfcrypt/md4.h
    wolfssl/wolfcrypt/md5.h
    wolfssl/wolfcrypt/mem_track.h
    wolfssl/wolfcrypt/memory.h
    wolfssl/wolfcrypt/misc.h
    wolfssl/wolfcrypt/mpi_class.h
    wolfssl/wolfcrypt/mpi_superclass.h
    wolfssl/wolfcrypt/pkcs12.h
    wolfssl/wolfcrypt/pkcs7.h
    wolfssl/wolfcrypt/poly1305.h
)


# TEST IDE Make                   #
#      IDE/IAR-EWARM              #
#      IDE/IAR-EWARM/Projects/lib #

add_definitions(-DWOLFSSL_USER_SETTINGS)
include_directories(${INCLUDE_PATH})
include_directories(${WOLFSSL_ROOT}/IDE/IAR-EWARM/Projects)

add_library(libwolfssl_static STATIC ${LIBWOLFSSL_SRCS} ${LIBWOLFCRYPT_SRCS})
set_target_properties(libwolfssl_static PROPERTIES OUTPUT_NAME wolfssl)


#      IDE/IAR-EWARM/Projects/test #

set(IDE_PATH ${WOLFSSL_ROOT}/IDE/IAR-EWARM)
set(PROJ_TEST_SRCS 
    wolfcrypt/test/test.c
    ${IDE_PATH}/Projects/test/test-main.c
)

add_definitions(-DWOLFSSL_USER_SETTINGS)
include_directories(${INCLUDE_PATH})
include_directories(${WOLFSSL_ROOT}/IDE/IAR-EWARM/Projects)

add_executable(testwolfcrypt  ${PROJ_TEST_SRCS})
target_link_libraries(testwolfcrypt libwolfssl_static)
