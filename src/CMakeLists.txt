set(SOURCE_SRC
crl.c
internal.c
keys.c
ocsp.c
sniffer.c
ssl.c
tls.c
tls13.c
wolfio.c
)
#link_directories(${TESTWOLFCRYPT} ${WOLFSSL_ROOT}/src)
add_library(${LIBWOLFSSL}  SHARED ${SOURCE_SRC})