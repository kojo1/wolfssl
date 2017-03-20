/* bio.h for openssl */


#ifndef WOLFSSL_BIO_H_
#define WOLFSSL_BIO_H_

#include <wolfssl/openssl/ssl.h>


#ifdef __cplusplus
    extern "C" {
#endif

#define BIO_find_type wolfSSL_BIO_find_type
#define BIO_next      wolfSSL_BIO_next
#define BIO_gets      wolfSSL_BIO_gets


#define BIO_TYPE_FILE WOLFSSL_BIO_FILE
#define BIO_TYPE_BIO  WOLFSSL_BIO_BIO
#define BIO_TYPE_MEM  WOLFSSL_BIO_MEMORY


#ifdef __cplusplus
    }  /* extern "C" */ 
#endif


#endif /* WOLFSSL_BIO_H_ */

