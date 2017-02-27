/* ssl.c
 *
 * Copyright (C) 2006-2016 wolfSSL Inc.
 *
 * This file is part of wolfSSL.
 *
 * wolfSSL is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * wolfSSL is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
 */


#ifdef HAVE_CONFIG_H
    #include <config.h>
#endif

#include <wolfssl/wolfcrypt/settings.h>

#ifndef WOLFCRYPT_ONLY

#ifdef HAVE_ERRNO_H
    #include <errno.h>
#endif

#include <wolfssl/internal.h>
#include <wolfssl/error-ssl.h>
#include <wolfssl/wolfcrypt/coding.h>
#ifdef NO_INLINE
    #include <wolfssl/wolfcrypt/misc.h>
#else
    #define WOLFSSL_MISC_INCLUDED
    #include <wolfcrypt/src/misc.c>
#endif


#ifndef WOLFSSL_ALLOW_NO_SUITES
    #if defined(NO_DH) && !defined(HAVE_ECC) && !defined(WOLFSSL_STATIC_RSA) \
                  && !defined(WOLFSSL_STATIC_DH) && !defined(WOLFSSL_STATIC_PSK)
        #error "No cipher suites defined because DH disabled, ECC disabled, and no static suites defined. Please see top of README"
    #endif
#endif

#if defined(OPENSSL_EXTRA) || defined(HAVE_WEBSERVER) || \
                              defined(WOLFSSL_KEY_GEN)
    #include <wolfssl/openssl/evp.h>
    /* openssl headers end, wolfssl internal headers next */
    #include <wolfssl/wolfcrypt/wc_encrypt.h>
#endif

#ifdef OPENSSL_EXTRA
    /* openssl headers begin */
    #include <wolfssl/openssl/aes.h>
    #include <wolfssl/openssl/hmac.h>
    #include <wolfssl/openssl/crypto.h>
    #include <wolfssl/openssl/des.h>
    #include <wolfssl/openssl/bn.h>
    #include <wolfssl/openssl/buffer.h>
    #include <wolfssl/openssl/dh.h>
    #include <wolfssl/openssl/rsa.h>
    #include <wolfssl/openssl/pem.h>
    #include <wolfssl/openssl/ec.h>
    #include <wolfssl/openssl/ec25519.h>
    #include <wolfssl/openssl/ed25519.h>
    #include <wolfssl/openssl/ecdsa.h>
    #include <wolfssl/openssl/ecdh.h>
    #include <wolfssl/openssl/rc4.h>
    /* openssl headers end, wolfssl internal headers next */
    #include <wolfssl/wolfcrypt/hmac.h>
    #include <wolfssl/wolfcrypt/random.h>
    #include <wolfssl/wolfcrypt/des3.h>
    #include <wolfssl/wolfcrypt/md4.h>
    #include <wolfssl/wolfcrypt/md5.h>
    #include <wolfssl/wolfcrypt/arc4.h>
    #include <wolfssl/wolfcrypt/idea.h>
    #include <wolfssl/wolfcrypt/curve25519.h>
    #include <wolfssl/wolfcrypt/ed25519.h>
    #ifdef HAVE_STUNNEL
        #include <wolfssl/openssl/ocsp.h>
    #endif /* WITH_STUNNEL */
    #ifdef WOLFSSL_SHA512
        #include <wolfssl/wolfcrypt/sha512.h>
    #endif
#endif

#ifdef NO_ASN
    #include <wolfssl/wolfcrypt/dh.h>
#endif

#ifndef NO_FILESYSTEM
    #if !defined(USE_WINDOWS_API) && !defined(NO_WOLFSSL_DIR) \
            && !defined(EBSNET)
        #include <dirent.h>
        #include <sys/stat.h>
    #endif
    #ifdef EBSNET
        #include "vfapi.h"
        #include "vfile.h"
    #endif
#endif /* NO_FILESYSTEM */


#if defined(WOLFSSL_DTLS) && !defined(WOLFSSL_HAVE_MAX)
#define WOLFSSL_HAVE_MAX

    static INLINE word32 max(word32 a, word32 b)
    {
        return a > b ? a : b;
    }

#endif /* WOLFSSL_DTLS && !WOLFSSL_HAVE_MAX */


#ifndef WOLFSSL_LEANPSK
char* mystrnstr(const char* s1, const char* s2, unsigned int n)
{
    unsigned int s2_len = (unsigned int)XSTRLEN(s2);

    if (s2_len == 0)
        return (char*)s1;

    while (n >= s2_len && s1[0]) {
        if (s1[0] == s2[0])
            if (XMEMCMP(s1, s2, s2_len) == 0)
                return (char*)s1;
        s1++;
        n--;
    }

    return NULL;
}
#endif

#ifdef WOLFSSL_SESSION_EXPORT
#ifdef WOLFSSL_DTLS
int wolfSSL_dtls_import(WOLFSSL* ssl, unsigned char* buf, unsigned int sz)
{
    WOLFSSL_ENTER("wolfSSL_session_import");

    if (ssl == NULL || buf == NULL) {
        return BAD_FUNC_ARG;
    }

    /* sanity checks on buffer and protocol are done in internal function */
    return wolfSSL_dtls_import_internal(ssl, buf, sz);
}


/* Sets the function to call for serializing the session. This function is
 * called right after the handshake is completed. */
int wolfSSL_CTX_dtls_set_export(WOLFSSL_CTX* ctx, wc_dtls_export func)
{

    WOLFSSL_ENTER("wolfSSL_CTX_dtls_set_export");

    /* purposefully allow func to be NULL */
    if (ctx == NULL) {
        return BAD_FUNC_ARG;
    }

    ctx->dtls_export = func;

    return SSL_SUCCESS;
}


/* Sets the function in WOLFSSL struct to call for serializing the session. This
 * function is called right after the handshake is completed. */
int wolfSSL_dtls_set_export(WOLFSSL* ssl, wc_dtls_export func)
{

    WOLFSSL_ENTER("wolfSSL_dtls_set_export");

    /* purposefully allow func to be NULL */
    if (ssl == NULL) {
        return BAD_FUNC_ARG;
    }

    ssl->dtls_export = func;

    return SSL_SUCCESS;
}


/* This function allows for directly serializing a session rather than using
 * callbacks. It has less overhead by removing a temporary buffer and gives
 * control over when the session gets serialized. When using callbacks the
 * session is always serialized immediatly after the handshake is finished.
 *
 * buf is the argument to contain the serialized session
 * sz  is the size of the buffer passed in
 * ssl is the WOLFSSL struct to serialize
 * returns the size of serialized session on success, 0 on no action, and
 *         negative value on error */
int wolfSSL_dtls_export(WOLFSSL* ssl, unsigned char* buf, unsigned int* sz)
{
    WOLFSSL_ENTER("wolfSSL_dtls_export");

    if (ssl == NULL || sz == NULL) {
        return BAD_FUNC_ARG;
    }

    if (buf == NULL) {
        *sz = MAX_EXPORT_BUFFER;
        return 0;
    }

    /* if not DTLS do nothing */
    if (!ssl->options.dtls) {
        WOLFSSL_MSG("Currently only DTLS export is supported");
        return 0;
    }

    /* copy over keys, options, and dtls state struct */
    return wolfSSL_dtls_export_internal(ssl, buf, *sz);
}


/* returns 0 on success */
int wolfSSL_send_session(WOLFSSL* ssl)
{
    int ret;
    byte* buf;
    word16 bufSz = MAX_EXPORT_BUFFER;

    WOLFSSL_ENTER("wolfSSL_send_session");

    if (ssl == NULL) {
        return BAD_FUNC_ARG;
    }

    buf = (byte*)XMALLOC(bufSz, ssl->heap, DYNAMIC_TYPE_TMP_BUFFER);
    if (buf == NULL) {
        return MEMORY_E;
    }

    /* if not DTLS do nothing */
    if (!ssl->options.dtls) {
        XFREE(buf, ssl->heap, DYNAMIC_TYPE_TMP_BUFFER);
        WOLFSSL_MSG("Currently only DTLS export is supported");
        return 0;
    }

    /* copy over keys, options, and dtls state struct */
    ret = wolfSSL_dtls_export_internal(ssl, buf, bufSz);
    if (ret < 0) {
        XFREE(buf, ssl->heap, DYNAMIC_TYPE_TMP_BUFFER);
        return ret;
    }

    /* if no error ret has size of buffer */
    ret = ssl->dtls_export(ssl, buf, ret, NULL);
    if (ret != SSL_SUCCESS) {
        XFREE(buf, ssl->heap, DYNAMIC_TYPE_TMP_BUFFER);
        return ret;
    }

    XFREE(buf, ssl->heap, DYNAMIC_TYPE_TMP_BUFFER);
    return 0;
}
#endif /* WOLFSSL_DTLS */
#endif /* WOLFSSL_SESSION_EXPORT */


/* prevent multiple mutex initializations */
static volatile int initRefCount = 0;
static wolfSSL_Mutex count_mutex;   /* init ref count mutex */


/* Create a new WOLFSSL_CTX struct and return the pointer to created struct.
   WOLFSSL_METHOD pointer passed in is given to ctx to manage.
   This function frees the passed in WOLFSSL_METHOD struct on failure and on
   success is freed when ctx is freed.
 */
WOLFSSL_CTX* wolfSSL_CTX_new_ex(WOLFSSL_METHOD* method, void* heap)
{
    WOLFSSL_CTX* ctx = NULL;

    WOLFSSL_ENTER("WOLFSSL_CTX_new_ex");

    if (initRefCount == 0) {
        /* user no longer forced to call Init themselves */
        int ret = wolfSSL_Init();
        if (ret != SSL_SUCCESS) {
            WOLFSSL_MSG("wolfSSL_Init failed");
            WOLFSSL_LEAVE("WOLFSSL_CTX_new", 0);
            if (method != NULL) {
                XFREE(method, heap, DYNAMIC_TYPE_METHOD);
            }
            return NULL;
        }
    }

    if (method == NULL)
        return ctx;

    ctx = (WOLFSSL_CTX*) XMALLOC(sizeof(WOLFSSL_CTX), heap, DYNAMIC_TYPE_CTX);
    if (ctx) {
        if (InitSSL_Ctx(ctx, method, heap) < 0) {
            WOLFSSL_MSG("Init CTX failed");
            wolfSSL_CTX_free(ctx);
            ctx = NULL;
        }
    }
    else {
        WOLFSSL_MSG("Alloc CTX failed, method freed");
        XFREE(method, heap, DYNAMIC_TYPE_METHOD);
    }

    WOLFSSL_LEAVE("WOLFSSL_CTX_new", 0);
    return ctx;
}


WOLFSSL_CTX* wolfSSL_CTX_new(WOLFSSL_METHOD* method)
{
#ifdef WOLFSSL_HEAP_TEST
    /* if testing the heap hint then set top level CTX to have test value */
    return wolfSSL_CTX_new_ex(method, (void*)WOLFSSL_HEAP_TEST);
#else
    return wolfSSL_CTX_new_ex(method, NULL);
#endif
}


void wolfSSL_CTX_free(WOLFSSL_CTX* ctx)
{
    WOLFSSL_ENTER("SSL_CTX_free");
    if (ctx)
        FreeSSL_Ctx(ctx);
    WOLFSSL_LEAVE("SSL_CTX_free", 0);
}


#ifdef SINGLE_THREADED
/* no locking in single threaded mode, allow a CTX level rng to be shared with
 * WOLFSSL objects, SSL_SUCCESS on ok */
int wolfSSL_CTX_new_rng(WOLFSSL_CTX* ctx)
{
    WC_RNG* rng;
    int     ret;

    if (ctx == NULL) {
        return BAD_FUNC_ARG;
    }

    rng = XMALLOC(sizeof(WC_RNG), ctx->heap, DYNAMIC_TYPE_RNG);
    if (rng == NULL) {
        return MEMORY_E;
    }

#ifndef HAVE_FIPS
    ret = wc_InitRng_ex(rng, ctx->heap);
#else
    ret = wc_InitRng(rng);
#endif
    if (ret != 0) {
        XFREE(rng, ctx->heap, DYNAMIC_TYPE_RNG);
        return ret;
    }

    ctx->rng = rng;
    return SSL_SUCCESS;
}
#endif


WOLFSSL* wolfSSL_new(WOLFSSL_CTX* ctx)
{
    WOLFSSL* ssl = NULL;
    int ret = 0;

    (void)ret;
    WOLFSSL_ENTER("SSL_new");

    if (ctx == NULL)
        return ssl;

    ssl = (WOLFSSL*) XMALLOC(sizeof(WOLFSSL), ctx->heap, DYNAMIC_TYPE_SSL);
    if (ssl)
        if ( (ret = InitSSL(ssl, ctx)) < 0) {
            FreeSSL(ssl, ctx->heap);
            ssl = 0;
        }

    WOLFSSL_LEAVE("SSL_new", ret);
    return ssl;
}


void wolfSSL_free(WOLFSSL* ssl)
{
    WOLFSSL_ENTER("SSL_free");
    if (ssl)
        FreeSSL(ssl, ssl->ctx->heap);
    WOLFSSL_LEAVE("SSL_free", 0);
}

#ifdef HAVE_POLY1305
/* set if to use old poly 1 for yes 0 to use new poly */
int wolfSSL_use_old_poly(WOLFSSL* ssl, int value)
{
    WOLFSSL_ENTER("SSL_use_old_poly");
    WOLFSSL_MSG("Warning SSL connection auto detects old/new and this function"
            "is depriciated");
    ssl->options.oldPoly = (word16)value;
    WOLFSSL_LEAVE("SSL_use_old_poly", 0);
    return 0;
}
#endif


int wolfSSL_set_fd(WOLFSSL* ssl, int fd)
{
    int ret;

    WOLFSSL_ENTER("SSL_set_fd");

    if (ssl == NULL) {
        return BAD_FUNC_ARG;
    }

    ret = wolfSSL_set_read_fd(ssl, fd);
    if (ret == SSL_SUCCESS) {
        ret = wolfSSL_set_write_fd(ssl, fd);
    }

    return ret;
}


int wolfSSL_set_read_fd(WOLFSSL* ssl, int fd)
{
    WOLFSSL_ENTER("SSL_set_read_fd");

    if (ssl == NULL) {
        return BAD_FUNC_ARG;
    }

    ssl->rfd = fd;      /* not used directly to allow IO callbacks */
    ssl->IOCB_ReadCtx  = &ssl->rfd;

    #ifdef WOLFSSL_DTLS
        if (ssl->options.dtls) {
            ssl->IOCB_ReadCtx = &ssl->buffers.dtlsCtx;
            ssl->buffers.dtlsCtx.rfd = fd;
        }
    #endif

    WOLFSSL_LEAVE("SSL_set_read_fd", SSL_SUCCESS);
    return SSL_SUCCESS;
}


int wolfSSL_set_write_fd(WOLFSSL* ssl, int fd)
{
    WOLFSSL_ENTER("SSL_set_write_fd");

    if (ssl == NULL) {
        return BAD_FUNC_ARG;
    }

    ssl->wfd = fd;      /* not used directly to allow IO callbacks */
    ssl->IOCB_WriteCtx  = &ssl->wfd;

    #ifdef WOLFSSL_DTLS
        if (ssl->options.dtls) {
            ssl->IOCB_WriteCtx = &ssl->buffers.dtlsCtx;
            ssl->buffers.dtlsCtx.wfd = fd;
        }
    #endif

    WOLFSSL_LEAVE("SSL_set_write_fd", SSL_SUCCESS);
    return SSL_SUCCESS;
}


/**
  * Get the name of cipher at priority level passed in.
  */
char* wolfSSL_get_cipher_list(int priority)
{
    const char* const* ciphers = GetCipherNames();

    if (priority >= GetCipherNamesSize() || priority < 0) {
        return 0;
    }

    return (char*)ciphers[priority];
}


int wolfSSL_get_ciphers(char* buf, int len)
{
    const char* const* ciphers = GetCipherNames();
    int  totalInc = 0;
    int  step     = 0;
    char delim    = ':';
    int  size     = GetCipherNamesSize();
    int  i;

    if (buf == NULL || len <= 0)
        return BAD_FUNC_ARG;

    /* Add each member to the buffer delimited by a : */
    for (i = 0; i < size; i++) {
        step = (int)(XSTRLEN(ciphers[i]) + 1);  /* delimiter */
        totalInc += step;

        /* Check to make sure buf is large enough and will not overflow */
        if (totalInc < len) {
            XSTRNCPY(buf, ciphers[i], XSTRLEN(ciphers[i]));
            buf += XSTRLEN(ciphers[i]);

            if (i < size - 1)
                *buf++ = delim;
            else
                *buf++ = '\0';
        }
        else
            return BUFFER_E;
    }
    return SSL_SUCCESS;
}


int wolfSSL_get_fd(const WOLFSSL* ssl)
{
    WOLFSSL_ENTER("SSL_get_fd");
    WOLFSSL_LEAVE("SSL_get_fd", ssl->rfd);
    return ssl->rfd;
}


int wolfSSL_get_using_nonblock(WOLFSSL* ssl)
{
    WOLFSSL_ENTER("wolfSSL_get_using_nonblock");
    WOLFSSL_LEAVE("wolfSSL_get_using_nonblock", ssl->options.usingNonblock);
    return ssl->options.usingNonblock;
}


int wolfSSL_dtls(WOLFSSL* ssl)
{
    return ssl->options.dtls;
}


#ifndef WOLFSSL_LEANPSK
void wolfSSL_set_using_nonblock(WOLFSSL* ssl, int nonblock)
{
    WOLFSSL_ENTER("wolfSSL_set_using_nonblock");
    ssl->options.usingNonblock = (nonblock != 0);
}


int wolfSSL_dtls_set_peer(WOLFSSL* ssl, void* peer, unsigned int peerSz)
{
#ifdef WOLFSSL_DTLS
    void* sa = (void*)XMALLOC(peerSz, ssl->heap, DYNAMIC_TYPE_SOCKADDR);
    if (sa != NULL) {
        if (ssl->buffers.dtlsCtx.peer.sa != NULL)
            XFREE(ssl->buffers.dtlsCtx.peer.sa,ssl->heap,DYNAMIC_TYPE_SOCKADDR);
        XMEMCPY(sa, peer, peerSz);
        ssl->buffers.dtlsCtx.peer.sa = sa;
        ssl->buffers.dtlsCtx.peer.sz = peerSz;
        return SSL_SUCCESS;
    }
    return SSL_FAILURE;
#else
    (void)ssl;
    (void)peer;
    (void)peerSz;
    return SSL_NOT_IMPLEMENTED;
#endif
}

int wolfSSL_dtls_get_peer(WOLFSSL* ssl, void* peer, unsigned int* peerSz)
{
#ifdef WOLFSSL_DTLS
    if (ssl == NULL) {
        return SSL_FAILURE;
    }

    if (peer != NULL && peerSz != NULL
            && *peerSz >= ssl->buffers.dtlsCtx.peer.sz
            && ssl->buffers.dtlsCtx.peer.sa != NULL) {
        *peerSz = ssl->buffers.dtlsCtx.peer.sz;
        XMEMCPY(peer, ssl->buffers.dtlsCtx.peer.sa, *peerSz);
        return SSL_SUCCESS;
    }
    return SSL_FAILURE;
#else
    (void)ssl;
    (void)peer;
    (void)peerSz;
    return SSL_NOT_IMPLEMENTED;
#endif
}


#if defined(WOLFSSL_SCTP) && defined(WOLFSSL_DTLS)

int wolfSSL_CTX_dtls_set_sctp(WOLFSSL_CTX* ctx)
{
    WOLFSSL_ENTER("wolfSSL_CTX_dtls_set_sctp()");

    if (ctx == NULL)
        return BAD_FUNC_ARG;

    ctx->dtlsSctp = 1;
    return SSL_SUCCESS;
}


int wolfSSL_dtls_set_sctp(WOLFSSL* ssl)
{
    WOLFSSL_ENTER("wolfSSL_dtls_set_sctp()");

    if (ssl == NULL)
        return BAD_FUNC_ARG;

    ssl->options.dtlsSctp = 1;
    return SSL_SUCCESS;
}


int wolfSSL_CTX_dtls_set_mtu(WOLFSSL_CTX* ctx, word16 newMtu)
{
    if (ctx == NULL || newMtu > MAX_RECORD_SIZE)
        return BAD_FUNC_ARG;

    ctx->dtlsMtuSz = newMtu;
    return SSL_SUCCESS;
}


int wolfSSL_dtls_set_mtu(WOLFSSL* ssl, word16 newMtu)
{
    if (ssl == NULL)
        return BAD_FUNC_ARG;

    if (newMtu > MAX_RECORD_SIZE) {
        ssl->error = BAD_FUNC_ARG;
        return SSL_FAILURE;
    }

    ssl->dtlsMtuSz = newMtu;
    return SSL_SUCCESS;
}


#endif /* WOLFSSL_DTLS && WOLFSSL_SCTP */

#endif /* WOLFSSL_LEANPSK */


/* return underlying connect or accept, SSL_SUCCESS on ok */
int wolfSSL_negotiate(WOLFSSL* ssl)
{
    int err = SSL_FATAL_ERROR;

    WOLFSSL_ENTER("wolfSSL_negotiate");
#ifndef NO_WOLFSSL_SERVER
    if (ssl->options.side == WOLFSSL_SERVER_END)
        err = wolfSSL_accept(ssl);
#endif

#ifndef NO_WOLFSSL_CLIENT
    if (ssl->options.side == WOLFSSL_CLIENT_END)
        err = wolfSSL_connect(ssl);
#endif

    WOLFSSL_LEAVE("wolfSSL_negotiate", err);

    return err;
}


WC_RNG* wolfSSL_GetRNG(WOLFSSL* ssl)
{
    if (ssl) {
        return ssl->rng;
    }

    return NULL;
}


#ifndef WOLFSSL_LEANPSK
/* object size based on build */
int wolfSSL_GetObjectSize(void)
{
#ifdef SHOW_SIZES
    printf("sizeof suites           = %lu\n", sizeof(Suites));
    printf("sizeof ciphers(2)       = %lu\n", sizeof(Ciphers));
#ifndef NO_RC4
    printf("    sizeof arc4         = %lu\n", sizeof(Arc4));
#endif
    printf("    sizeof aes          = %lu\n", sizeof(Aes));
#ifndef NO_DES3
    printf("    sizeof des3         = %lu\n", sizeof(Des3));
#endif
#ifndef NO_RABBIT
    printf("    sizeof rabbit       = %lu\n", sizeof(Rabbit));
#endif
#ifdef HAVE_CHACHA
    printf("    sizeof chacha       = %lu\n", sizeof(ChaCha));
#endif
    printf("sizeof cipher specs     = %lu\n", sizeof(CipherSpecs));
    printf("sizeof keys             = %lu\n", sizeof(Keys));
    printf("sizeof Hashes(2)        = %lu\n", sizeof(Hashes));
#ifndef NO_MD5
    printf("    sizeof MD5          = %lu\n", sizeof(Md5));
#endif
#ifndef NO_SHA
    printf("    sizeof SHA          = %lu\n", sizeof(Sha));
#endif
#ifdef WOLFSSL_SHA224
    printf("    sizeof SHA224       = %lu\n", sizeof(Sha224));
#endif
#ifndef NO_SHA256
    printf("    sizeof SHA256       = %lu\n", sizeof(Sha256));
#endif
#ifdef WOLFSSL_SHA384
    printf("    sizeof SHA384       = %lu\n", sizeof(Sha384));
#endif
#ifdef WOLFSSL_SHA384
    printf("    sizeof SHA512       = %lu\n", sizeof(Sha512));
#endif
    printf("sizeof Buffers          = %lu\n", sizeof(Buffers));
    printf("sizeof Options          = %lu\n", sizeof(Options));
    printf("sizeof Arrays           = %lu\n", sizeof(Arrays));
#ifndef NO_RSA
    printf("sizeof RsaKey           = %lu\n", sizeof(RsaKey));
#endif
#ifdef HAVE_ECC
    printf("sizeof ecc_key          = %lu\n", sizeof(ecc_key));
#endif
    printf("sizeof WOLFSSL_CIPHER    = %lu\n", sizeof(WOLFSSL_CIPHER));
    printf("sizeof WOLFSSL_SESSION   = %lu\n", sizeof(WOLFSSL_SESSION));
    printf("sizeof WOLFSSL           = %lu\n", sizeof(WOLFSSL));
    printf("sizeof WOLFSSL_CTX       = %lu\n", sizeof(WOLFSSL_CTX));
#endif

    return sizeof(WOLFSSL);
}
#endif


#ifdef WOLFSSL_STATIC_MEMORY

int wolfSSL_CTX_load_static_memory(WOLFSSL_CTX** ctx, wolfSSL_method_func method,
                                   unsigned char* buf, unsigned int sz,
                                   int flag, int max)
{
    WOLFSSL_HEAP*      heap;
    WOLFSSL_HEAP_HINT* hint;
    word32 idx = 0;

    if (ctx == NULL || buf == NULL) {
        return BAD_FUNC_ARG;
    }

    if (*ctx == NULL && method == NULL) {
        return BAD_FUNC_ARG;
    }

    if (*ctx == NULL || (*ctx)->heap == NULL) {
        if (sizeof(WOLFSSL_HEAP) + sizeof(WOLFSSL_HEAP_HINT) > sz - idx) {
            return BUFFER_E; /* not enough memory for structures */
        }
        heap = (WOLFSSL_HEAP*)buf;
        idx += sizeof(WOLFSSL_HEAP);
        if (wolfSSL_init_memory_heap(heap) != 0) {
            return SSL_FAILURE;
        }
        hint = (WOLFSSL_HEAP_HINT*)(buf + idx);
        idx += sizeof(WOLFSSL_HEAP_HINT);
        XMEMSET(hint, 0, sizeof(WOLFSSL_HEAP_HINT));
        hint->memory = heap;

        if (*ctx && (*ctx)->heap == NULL) {
            (*ctx)->heap = (void*)hint;
        }
    }
    else {
#ifdef WOLFSSL_HEAP_TEST
        /* do not load in memory if test has been set */
        if ((*ctx)->heap == (void*)WOLFSSL_HEAP_TEST) {
            return SSL_SUCCESS;
        }
#endif
        hint = (WOLFSSL_HEAP_HINT*)((*ctx)->heap);
        heap = hint->memory;
    }

    if (wolfSSL_load_static_memory(buf + idx, sz - idx, flag, heap) != 1) {
        WOLFSSL_MSG("Error partitioning memory");
        return SSL_FAILURE;
    }

    /* create ctx if needed */
    if (*ctx == NULL) {
        *ctx = wolfSSL_CTX_new_ex(method(hint), hint);
        if (*ctx == NULL) {
            WOLFSSL_MSG("Error creating ctx");
            return SSL_FAILURE;
        }
    }

    /* determine what max applies too */
    if (flag & WOLFMEM_IO_POOL || flag & WOLFMEM_IO_POOL_FIXED) {
        heap->maxIO = max;
    }
    else { /* general memory used in handshakes */
        heap->maxHa = max;
    }

    heap->flag |= flag;

    (void)max;
    (void)method;

    return SSL_SUCCESS;
}


int wolfSSL_is_static_memory(WOLFSSL* ssl, WOLFSSL_MEM_CONN_STATS* mem_stats)
{
    if (ssl == NULL) {
        return BAD_FUNC_ARG;
    }
    WOLFSSL_ENTER("wolfSSL_is_static_memory");

    /* fill out statistics if wanted and WOLFMEM_TRACK_STATS flag */
    if (mem_stats != NULL && ssl->heap != NULL) {
        WOLFSSL_HEAP_HINT* hint = ((WOLFSSL_HEAP_HINT*)(ssl->heap));
        WOLFSSL_HEAP* heap      = hint->memory;
        if (heap->flag & WOLFMEM_TRACK_STATS && hint->stats != NULL) {
            XMEMCPY(mem_stats, hint->stats, sizeof(WOLFSSL_MEM_CONN_STATS));
        }
    }

    return (ssl->heap) ? 1 : 0;
}


int wolfSSL_CTX_is_static_memory(WOLFSSL_CTX* ctx, WOLFSSL_MEM_STATS* mem_stats)
{
    if (ctx == NULL) {
        return BAD_FUNC_ARG;
    }
    WOLFSSL_ENTER("wolfSSL_CTX_is_static_memory");

    /* fill out statistics if wanted */
    if (mem_stats != NULL && ctx->heap != NULL) {
        WOLFSSL_HEAP* heap = ((WOLFSSL_HEAP_HINT*)(ctx->heap))->memory;
        if (wolfSSL_GetMemStats(heap, mem_stats) != 1) {
            return MEMORY_E;
        }
    }

    return (ctx->heap) ? 1 : 0;
}

#endif /* WOLFSSL_STATIC_MEMORY */


/* return max record layer size plaintext input size */
int wolfSSL_GetMaxOutputSize(WOLFSSL* ssl)
{
    int maxSize = OUTPUT_RECORD_SIZE;

    WOLFSSL_ENTER("wolfSSL_GetMaxOutputSize");

    if (ssl == NULL)
        return BAD_FUNC_ARG;

    if (ssl->options.handShakeState != HANDSHAKE_DONE) {
        WOLFSSL_MSG("Handshake not complete yet");
        return BAD_FUNC_ARG;
    }

#ifdef HAVE_MAX_FRAGMENT
    maxSize = min(maxSize, ssl->max_fragment);
#endif

#ifdef WOLFSSL_DTLS
    if (ssl->options.dtls) {
        maxSize = min(maxSize, MAX_UDP_SIZE);
    }
#endif

    return maxSize;
}


/* return record layer size of plaintext input size */
int wolfSSL_GetOutputSize(WOLFSSL* ssl, int inSz)
{
    int maxSize;

    WOLFSSL_ENTER("wolfSSL_GetOutputSize");

    if (inSz < 0)
        return BAD_FUNC_ARG;

    maxSize = wolfSSL_GetMaxOutputSize(ssl);
    if (maxSize < 0)
        return maxSize;   /* error */
    if (inSz > maxSize)
        return INPUT_SIZE_E;

    return BuildMessage(ssl, NULL, 0, NULL, inSz, application_data, 0, 1);
}


#ifdef HAVE_ECC
int wolfSSL_CTX_SetMinEccKey_Sz(WOLFSSL_CTX* ctx, short keySz)
{
    if (ctx == NULL || keySz < 0 || keySz % 8 != 0) {
        WOLFSSL_MSG("Key size must be divisable by 8 or ctx was null");
        return BAD_FUNC_ARG;
    }

    ctx->minEccKeySz     = keySz / 8;
    ctx->cm->minEccKeySz = keySz / 8;
    return SSL_SUCCESS;
}


int wolfSSL_SetMinEccKey_Sz(WOLFSSL* ssl, short keySz)
{
    if (ssl == NULL || keySz < 0 || keySz % 8 != 0) {
        WOLFSSL_MSG("Key size must be divisable by 8 or ssl was null");
        return BAD_FUNC_ARG;
    }

    ssl->options.minEccKeySz = keySz / 8;
    return SSL_SUCCESS;
}

#endif /* !NO_RSA */

#ifndef NO_RSA
int wolfSSL_CTX_SetMinRsaKey_Sz(WOLFSSL_CTX* ctx, short keySz)
{
    if (ctx == NULL || keySz < 0 || keySz % 8 != 0) {
        WOLFSSL_MSG("Key size must be divisable by 8 or ctx was null");
        return BAD_FUNC_ARG;
    }

    ctx->minRsaKeySz     = keySz / 8;
    ctx->cm->minRsaKeySz = keySz / 8;
    return SSL_SUCCESS;
}


int wolfSSL_SetMinRsaKey_Sz(WOLFSSL* ssl, short keySz)
{
    if (ssl == NULL || keySz < 0 || keySz % 8 != 0) {
        WOLFSSL_MSG("Key size must be divisable by 8 or ssl was null");
        return BAD_FUNC_ARG;
    }

    ssl->options.minRsaKeySz = keySz / 8;
    return SSL_SUCCESS;
}
#endif /* !NO_RSA */

#ifndef NO_DH
/* server Diffie-Hellman parameters, SSL_SUCCESS on ok */
int wolfSSL_SetTmpDH(WOLFSSL* ssl, const unsigned char* p, int pSz,
                    const unsigned char* g, int gSz)
{
    word16 havePSK = 0;
    word16 haveRSA = 1;

    WOLFSSL_ENTER("wolfSSL_SetTmpDH");
    if (ssl == NULL || p == NULL || g == NULL) return BAD_FUNC_ARG;

    if (pSz < ssl->options.minDhKeySz)
        return DH_KEY_SIZE_E;

    if (ssl->options.side != WOLFSSL_SERVER_END)
        return SIDE_ERROR;

    if (ssl->buffers.serverDH_P.buffer && ssl->buffers.weOwnDH) {
        XFREE(ssl->buffers.serverDH_P.buffer, ssl->heap, DYNAMIC_TYPE_DH);
        ssl->buffers.serverDH_P.buffer = NULL;
    }
    if (ssl->buffers.serverDH_G.buffer && ssl->buffers.weOwnDH) {
        XFREE(ssl->buffers.serverDH_G.buffer, ssl->heap, DYNAMIC_TYPE_DH);
        ssl->buffers.serverDH_G.buffer = NULL;
    }

    ssl->buffers.weOwnDH = 1;  /* SSL owns now */
    ssl->buffers.serverDH_P.buffer = (byte*)XMALLOC(pSz, ssl->heap,
                                                    DYNAMIC_TYPE_DH);
    if (ssl->buffers.serverDH_P.buffer == NULL)
        return MEMORY_E;

    ssl->buffers.serverDH_G.buffer = (byte*)XMALLOC(gSz, ssl->heap,
                                                    DYNAMIC_TYPE_DH);
    if (ssl->buffers.serverDH_G.buffer == NULL) {
        XFREE(ssl->buffers.serverDH_P.buffer, ssl->heap, DYNAMIC_TYPE_DH);
        ssl->buffers.serverDH_P.buffer = NULL;
        return MEMORY_E;
    }

    ssl->buffers.serverDH_P.length = pSz;
    ssl->buffers.serverDH_G.length = gSz;

    XMEMCPY(ssl->buffers.serverDH_P.buffer, p, pSz);
    XMEMCPY(ssl->buffers.serverDH_G.buffer, g, gSz);

    ssl->options.haveDH = 1;
    #ifndef NO_PSK
        havePSK = ssl->options.havePSK;
    #endif
    #ifdef NO_RSA
        haveRSA = 0;
    #endif
    InitSuites(ssl->suites, ssl->version, haveRSA, havePSK, ssl->options.haveDH,
               ssl->options.haveNTRU, ssl->options.haveECDSAsig,
               ssl->options.haveECC, ssl->options.haveStaticECC,
               ssl->options.side);

    WOLFSSL_LEAVE("wolfSSL_SetTmpDH", 0);
    return SSL_SUCCESS;
}

/* server ctx Diffie-Hellman parameters, SSL_SUCCESS on ok */
int wolfSSL_CTX_SetTmpDH(WOLFSSL_CTX* ctx, const unsigned char* p, int pSz,
                         const unsigned char* g, int gSz)
{
    WOLFSSL_ENTER("wolfSSL_CTX_SetTmpDH");
    if (ctx == NULL || p == NULL || g == NULL) return BAD_FUNC_ARG;

    if (pSz < ctx->minDhKeySz)
        return DH_KEY_SIZE_E;

    XFREE(ctx->serverDH_P.buffer, ctx->heap, DYNAMIC_TYPE_DH);
    XFREE(ctx->serverDH_G.buffer, ctx->heap, DYNAMIC_TYPE_DH);

    ctx->serverDH_P.buffer = (byte*)XMALLOC(pSz, ctx->heap, DYNAMIC_TYPE_DH);
    if (ctx->serverDH_P.buffer == NULL)
       return MEMORY_E;

    ctx->serverDH_G.buffer = (byte*)XMALLOC(gSz, ctx->heap, DYNAMIC_TYPE_DH);
    if (ctx->serverDH_G.buffer == NULL) {
        XFREE(ctx->serverDH_P.buffer, ctx->heap, DYNAMIC_TYPE_DH);
        return MEMORY_E;
    }

    ctx->serverDH_P.length = pSz;
    ctx->serverDH_G.length = gSz;

    XMEMCPY(ctx->serverDH_P.buffer, p, pSz);
    XMEMCPY(ctx->serverDH_G.buffer, g, gSz);

    ctx->haveDH = 1;

    WOLFSSL_LEAVE("wolfSSL_CTX_SetTmpDH", 0);
    return SSL_SUCCESS;
}


int wolfSSL_CTX_SetMinDhKey_Sz(WOLFSSL_CTX* ctx, word16 keySz)
{
    if (ctx == NULL || keySz > 16000 || keySz % 8 != 0)
        return BAD_FUNC_ARG;

    ctx->minDhKeySz = keySz / 8;
    return SSL_SUCCESS;
}


int wolfSSL_SetMinDhKey_Sz(WOLFSSL* ssl, word16 keySz)
{
    if (ssl == NULL || keySz > 16000 || keySz % 8 != 0)
        return BAD_FUNC_ARG;

    ssl->options.minDhKeySz = keySz / 8;
    return SSL_SUCCESS;
}


int wolfSSL_GetDhKey_Sz(WOLFSSL* ssl)
{
    if (ssl == NULL)
        return BAD_FUNC_ARG;

    return (ssl->options.dhKeySz * 8);
}

#endif /* !NO_DH */


int wolfSSL_write(WOLFSSL* ssl, const void* data, int sz)
{
    int ret;

    WOLFSSL_ENTER("SSL_write()");

    if (ssl == NULL || data == NULL || sz < 0)
        return BAD_FUNC_ARG;

#ifdef HAVE_ERRNO_H
    errno = 0;
#endif

    ret = SendData(ssl, data, sz);

    WOLFSSL_LEAVE("SSL_write()", ret);

    if (ret < 0)
        return SSL_FATAL_ERROR;
    else
        return ret;
}


static int wolfSSL_read_internal(WOLFSSL* ssl, void* data, int sz, int peek)
{
    int ret;

    WOLFSSL_ENTER("wolfSSL_read_internal()");

    if (ssl == NULL || data == NULL || sz < 0)
        return BAD_FUNC_ARG;

#ifdef HAVE_ERRNO_H
        errno = 0;
#endif

#ifdef WOLFSSL_DTLS
    if (ssl->options.dtls) {
        ssl->dtls_expected_rx = max(sz + 100, MAX_MTU);
#ifdef WOLFSSL_SCTP
        if (ssl->options.dtlsSctp)
            ssl->dtls_expected_rx = max(ssl->dtls_expected_rx, ssl->dtlsMtuSz);
#endif
    }
#endif

    sz = min(sz, OUTPUT_RECORD_SIZE);
#ifdef HAVE_MAX_FRAGMENT
    sz = min(sz, ssl->max_fragment);
#endif
    ret = ReceiveData(ssl, (byte*)data, sz, peek);

    WOLFSSL_LEAVE("wolfSSL_read_internal()", ret);

    if (ret < 0)
        return SSL_FATAL_ERROR;
    else
        return ret;
}


int wolfSSL_peek(WOLFSSL* ssl, void* data, int sz)
{
    WOLFSSL_ENTER("wolfSSL_peek()");

    return wolfSSL_read_internal(ssl, data, sz, TRUE);
}


int wolfSSL_read(WOLFSSL* ssl, void* data, int sz)
{
    WOLFSSL_ENTER("wolfSSL_read()");

    return wolfSSL_read_internal(ssl, data, sz, FALSE);
}


#ifdef WOLFSSL_ASYNC_CRYPT

/* let's use async hardware, SSL_SUCCESS on ok */
int wolfSSL_UseAsync(WOLFSSL* ssl, int devId)
{
    if (ssl == NULL)
        return BAD_FUNC_ARG;

    ssl->devId = devId;

    return SSL_SUCCESS;
}


/* let's use async hardware, SSL_SUCCESS on ok */
int wolfSSL_CTX_UseAsync(WOLFSSL_CTX* ctx, int devId)
{
    if (ctx == NULL)
        return BAD_FUNC_ARG;

    ctx->devId = devId;

    return SSL_SUCCESS;
}

#endif /* WOLFSSL_ASYNC_CRYPT */

#ifdef HAVE_SNI

int wolfSSL_UseSNI(WOLFSSL* ssl, byte type, const void* data, word16 size)
{
    if (ssl == NULL)
        return BAD_FUNC_ARG;

    return TLSX_UseSNI(&ssl->extensions, type, data, size, ssl->heap);
}


int wolfSSL_CTX_UseSNI(WOLFSSL_CTX* ctx, byte type, const void* data,
                                                                    word16 size)
{
    if (ctx == NULL)
        return BAD_FUNC_ARG;

    return TLSX_UseSNI(&ctx->extensions, type, data, size, ctx->heap);
}

#ifndef NO_WOLFSSL_SERVER

void wolfSSL_SNI_SetOptions(WOLFSSL* ssl, byte type, byte options)
{
    if (ssl && ssl->extensions)
        TLSX_SNI_SetOptions(ssl->extensions, type, options);
}


void wolfSSL_CTX_SNI_SetOptions(WOLFSSL_CTX* ctx, byte type, byte options)
{
    if (ctx && ctx->extensions)
        TLSX_SNI_SetOptions(ctx->extensions, type, options);
}


byte wolfSSL_SNI_Status(WOLFSSL* ssl, byte type)
{
    return TLSX_SNI_Status(ssl ? ssl->extensions : NULL, type);
}


word16 wolfSSL_SNI_GetRequest(WOLFSSL* ssl, byte type, void** data)
{
    if (data)
        *data = NULL;

    if (ssl && ssl->extensions)
        return TLSX_SNI_GetRequest(ssl->extensions, type, data);

    return 0;
}


int wolfSSL_SNI_GetFromBuffer(const byte* clientHello, word32 helloSz,
                              byte type, byte* sni, word32* inOutSz)
{
    if (clientHello && helloSz > 0 && sni && inOutSz && *inOutSz > 0)
        return TLSX_SNI_GetFromBuffer(clientHello, helloSz, type, sni, inOutSz);

    return BAD_FUNC_ARG;
}

#endif /* NO_WOLFSSL_SERVER */

#endif /* HAVE_SNI */


#ifdef HAVE_MAX_FRAGMENT
#ifndef NO_WOLFSSL_CLIENT

int wolfSSL_UseMaxFragment(WOLFSSL* ssl, byte mfl)
{
    if (ssl == NULL)
        return BAD_FUNC_ARG;

    return TLSX_UseMaxFragment(&ssl->extensions, mfl, ssl->heap);
}


int wolfSSL_CTX_UseMaxFragment(WOLFSSL_CTX* ctx, byte mfl)
{
    if (ctx == NULL)
        return BAD_FUNC_ARG;

    return TLSX_UseMaxFragment(&ctx->extensions, mfl, ctx->heap);
}

#endif /* NO_WOLFSSL_CLIENT */
#endif /* HAVE_MAX_FRAGMENT */

#ifdef HAVE_TRUNCATED_HMAC
#ifndef NO_WOLFSSL_CLIENT

int wolfSSL_UseTruncatedHMAC(WOLFSSL* ssl)
{
    if (ssl == NULL)
        return BAD_FUNC_ARG;

    return TLSX_UseTruncatedHMAC(&ssl->extensions, ssl->heap);
}


int wolfSSL_CTX_UseTruncatedHMAC(WOLFSSL_CTX* ctx)
{
    if (ctx == NULL)
        return BAD_FUNC_ARG;

    return TLSX_UseTruncatedHMAC(&ctx->extensions, ctx->heap);
}

#endif /* NO_WOLFSSL_CLIENT */
#endif /* HAVE_TRUNCATED_HMAC */

#ifdef HAVE_CERTIFICATE_STATUS_REQUEST

int wolfSSL_UseOCSPStapling(WOLFSSL* ssl, byte status_type, byte options)
{
    if (ssl == NULL || ssl->options.side != WOLFSSL_CLIENT_END)
        return BAD_FUNC_ARG;

    return TLSX_UseCertificateStatusRequest(&ssl->extensions, status_type,
                                                            options, ssl->heap);
}


int wolfSSL_CTX_UseOCSPStapling(WOLFSSL_CTX* ctx, byte status_type,
                                                                   byte options)
{
    if (ctx == NULL || ctx->method->side != WOLFSSL_CLIENT_END)
        return BAD_FUNC_ARG;

    return TLSX_UseCertificateStatusRequest(&ctx->extensions, status_type,
                                                            options, ctx->heap);
}

#endif /* HAVE_CERTIFICATE_STATUS_REQUEST */

#ifdef HAVE_CERTIFICATE_STATUS_REQUEST_V2

int wolfSSL_UseOCSPStaplingV2(WOLFSSL* ssl, byte status_type, byte options)
{
    if (ssl == NULL || ssl->options.side != WOLFSSL_CLIENT_END)
        return BAD_FUNC_ARG;

    return TLSX_UseCertificateStatusRequestV2(&ssl->extensions, status_type,
                                                            options, ssl->heap);
}


int wolfSSL_CTX_UseOCSPStaplingV2(WOLFSSL_CTX* ctx,
                                                 byte status_type, byte options)
{
    if (ctx == NULL || ctx->method->side != WOLFSSL_CLIENT_END)
        return BAD_FUNC_ARG;

    return TLSX_UseCertificateStatusRequestV2(&ctx->extensions, status_type,
                                                            options, ctx->heap);
}

#endif /* HAVE_CERTIFICATE_STATUS_REQUEST_V2 */

/* Elliptic Curves */
#ifdef HAVE_SUPPORTED_CURVES
#ifndef NO_WOLFSSL_CLIENT

int wolfSSL_UseSupportedCurve(WOLFSSL* ssl, word16 name)
{
    if (ssl == NULL)
        return BAD_FUNC_ARG;

    switch (name) {
        case WOLFSSL_ECC_SECP160K1:
        case WOLFSSL_ECC_SECP160R1:
        case WOLFSSL_ECC_SECP160R2:
        case WOLFSSL_ECC_SECP192K1:
        case WOLFSSL_ECC_SECP192R1:
        case WOLFSSL_ECC_SECP224K1:
        case WOLFSSL_ECC_SECP224R1:
        case WOLFSSL_ECC_SECP256K1:
        case WOLFSSL_ECC_SECP256R1:
        case WOLFSSL_ECC_SECP384R1:
        case WOLFSSL_ECC_SECP521R1:
        case WOLFSSL_ECC_BRAINPOOLP256R1:
        case WOLFSSL_ECC_BRAINPOOLP384R1:
        case WOLFSSL_ECC_BRAINPOOLP512R1:
            break;

        default:
            return BAD_FUNC_ARG;
    }

    ssl->options.userCurves = 1;

    return TLSX_UseSupportedCurve(&ssl->extensions, name, ssl->heap);
}


int wolfSSL_CTX_UseSupportedCurve(WOLFSSL_CTX* ctx, word16 name)
{
    if (ctx == NULL)
        return BAD_FUNC_ARG;

    switch (name) {
        case WOLFSSL_ECC_SECP160K1:
        case WOLFSSL_ECC_SECP160R1:
        case WOLFSSL_ECC_SECP160R2:
        case WOLFSSL_ECC_SECP192K1:
        case WOLFSSL_ECC_SECP192R1:
        case WOLFSSL_ECC_SECP224K1:
        case WOLFSSL_ECC_SECP224R1:
        case WOLFSSL_ECC_SECP256K1:
        case WOLFSSL_ECC_SECP256R1:
        case WOLFSSL_ECC_SECP384R1:
        case WOLFSSL_ECC_SECP521R1:
        case WOLFSSL_ECC_BRAINPOOLP256R1:
        case WOLFSSL_ECC_BRAINPOOLP384R1:
        case WOLFSSL_ECC_BRAINPOOLP512R1:
            break;

        default:
            return BAD_FUNC_ARG;
    }

    ctx->userCurves = 1;

    return TLSX_UseSupportedCurve(&ctx->extensions, name, ctx->heap);
}

#endif /* NO_WOLFSSL_CLIENT */
#endif /* HAVE_SUPPORTED_CURVES */

/* QSH quantum safe handshake */
#ifdef HAVE_QSH
/* returns 1 if QSH has been used 0 otherwise */
int wolfSSL_isQSH(WOLFSSL* ssl)
{
    /* if no ssl struct than QSH was not used */
    if (ssl == NULL)
        return 0;

    return ssl->isQSH;
}


int wolfSSL_UseSupportedQSH(WOLFSSL* ssl, word16 name)
{
    if (ssl == NULL)
        return BAD_FUNC_ARG;

    switch (name) {
    #ifdef HAVE_NTRU
        case WOLFSSL_NTRU_EESS439:
        case WOLFSSL_NTRU_EESS593:
        case WOLFSSL_NTRU_EESS743:
            break;
    #endif
        default:
            return BAD_FUNC_ARG;
    }

    ssl->user_set_QSHSchemes = 1;

    return TLSX_UseQSHScheme(&ssl->extensions, name, NULL, 0, ssl->heap);
}

#ifndef NO_WOLFSSL_CLIENT
    /* user control over sending client public key in hello
       when flag = 1 will send keys if flag is 0 or function is not called
       then will not send keys in the hello extension
       return 0 on success
     */
    int wolfSSL_UseClientQSHKeys(WOLFSSL* ssl, unsigned char flag)
    {
        if (ssl == NULL)
            return BAD_FUNC_ARG;

        ssl->sendQSHKeys = flag;

        return 0;
    }
#endif /* NO_WOLFSSL_CLIENT */
#endif /* HAVE_QSH */


/* Application-Layer Protocol Negotiation */
#ifdef HAVE_ALPN

int wolfSSL_UseALPN(WOLFSSL* ssl, char *protocol_name_list,
                    word32 protocol_name_listSz, byte options)
{
    char    *list, *ptr, *token[10];
    word16  len;
    int     idx = 0;
    int     ret = SSL_FAILURE;

    WOLFSSL_ENTER("wolfSSL_UseALPN");

    if (ssl == NULL || protocol_name_list == NULL)
        return BAD_FUNC_ARG;

    if (protocol_name_listSz > (WOLFSSL_MAX_ALPN_NUMBER *
                                WOLFSSL_MAX_ALPN_PROTO_NAME_LEN +
                                WOLFSSL_MAX_ALPN_NUMBER)) {
        WOLFSSL_MSG("Invalid arguments, protocol name list too long");
        return BAD_FUNC_ARG;
    }

    if (!(options & WOLFSSL_ALPN_CONTINUE_ON_MISMATCH) &&
        !(options & WOLFSSL_ALPN_FAILED_ON_MISMATCH)) {
            WOLFSSL_MSG("Invalid arguments, options not supported");
            return BAD_FUNC_ARG;
        }


    list = (char *)XMALLOC(protocol_name_listSz+1, ssl->heap,
                           DYNAMIC_TYPE_TMP_BUFFER);
    if (list == NULL) {
        WOLFSSL_MSG("Memory failure");
        return MEMORY_ERROR;
    }

    XMEMSET(list, 0, protocol_name_listSz+1);
    XSTRNCPY(list, protocol_name_list, protocol_name_listSz);

    /* read all protocol name from the list */
    token[idx] = XSTRTOK(list, ",", &ptr);
    while (token[idx] != NULL)
        token[++idx] = XSTRTOK(NULL, ",", &ptr);

    /* add protocol name list in the TLS extension in reverse order */
    while ((idx--) > 0) {
        len = (word16)XSTRLEN(token[idx]);

        ret = TLSX_UseALPN(&ssl->extensions, token[idx], len, options,
                                                                     ssl->heap);
        if (ret != SSL_SUCCESS) {
            WOLFSSL_MSG("TLSX_UseALPN failure");
            break;
        }
    }

    XFREE(list, ssl->heap, DYNAMIC_TYPE_TMP_BUFFER);

    return ret;
}

int wolfSSL_ALPN_GetProtocol(WOLFSSL* ssl, char **protocol_name, word16 *size)
{
    return TLSX_ALPN_GetRequest(ssl ? ssl->extensions : NULL,
                               (void **)protocol_name, size);
}

int wolfSSL_ALPN_GetPeerProtocol(WOLFSSL* ssl, char **list, word16 *listSz)
{
    if (list == NULL || listSz == NULL)
        return BAD_FUNC_ARG;

    if (ssl->alpn_client_list == NULL)
        return BUFFER_ERROR;

    *listSz = (word16)XSTRLEN(ssl->alpn_client_list);
    if (*listSz == 0)
        return BUFFER_ERROR;

    *list = (char *)XMALLOC((*listSz)+1, ssl->heap, DYNAMIC_TYPE_TLSX);
    if (*list == NULL)
        return MEMORY_ERROR;

    XSTRNCPY(*list, ssl->alpn_client_list, (*listSz)+1);
    (*list)[*listSz] = 0;

    return SSL_SUCCESS;
}


/* used to free memory allocated by wolfSSL_ALPN_GetPeerProtocol */
int wolfSSL_ALPN_FreePeerProtocol(WOLFSSL* ssl, char **list)
{
    if (ssl == NULL) {
        return BAD_FUNC_ARG;
    }

    XFREE(*list, ssl->heap, DYNAMIC_TYPE_TLSX);
    *list = NULL;

    return SSL_SUCCESS;
}

#endif /* HAVE_ALPN */

/* Secure Renegotiation */
#ifdef HAVE_SECURE_RENEGOTIATION

/* user is forcing ability to use secure renegotiation, we discourage it */
int wolfSSL_UseSecureRenegotiation(WOLFSSL* ssl)
{
    int ret = BAD_FUNC_ARG;

    if (ssl)
        ret = TLSX_UseSecureRenegotiation(&ssl->extensions, ssl->heap);

    if (ret == SSL_SUCCESS) {
        TLSX* extension = TLSX_Find(ssl->extensions, TLSX_RENEGOTIATION_INFO);

        if (extension)
            ssl->secure_renegotiation = (SecureRenegotiation*)extension->data;
    }

    return ret;
}


/* do a secure renegotiation handshake, user forced, we discourage */
int wolfSSL_Rehandshake(WOLFSSL* ssl)
{
    int ret;

    if (ssl == NULL)
        return BAD_FUNC_ARG;

    if (ssl->secure_renegotiation == NULL) {
        WOLFSSL_MSG("Secure Renegotiation not forced on by user");
        return SECURE_RENEGOTIATION_E;
    }

    if (ssl->secure_renegotiation->enabled == 0) {
        WOLFSSL_MSG("Secure Renegotiation not enabled at extension level");
        return SECURE_RENEGOTIATION_E;
    }

    if (ssl->options.handShakeState != HANDSHAKE_DONE) {
        WOLFSSL_MSG("Can't renegotiate until previous handshake complete");
        return SECURE_RENEGOTIATION_E;
    }

#ifndef NO_FORCE_SCR_SAME_SUITE
    /* force same suite */
    if (ssl->suites) {
        ssl->suites->suiteSz = SUITE_LEN;
        ssl->suites->suites[0] = ssl->options.cipherSuite0;
        ssl->suites->suites[1] = ssl->options.cipherSuite;
    }
#endif

    /* reset handshake states */
    ssl->options.serverState = NULL_STATE;
    ssl->options.clientState = NULL_STATE;
    ssl->options.connectState  = CONNECT_BEGIN;
    ssl->options.acceptState   = ACCEPT_BEGIN;
    ssl->options.handShakeState = NULL_STATE;
    ssl->options.processReply  = 0;  /* TODO, move states in internal.h */

    XMEMSET(&ssl->msgsReceived, 0, sizeof(ssl->msgsReceived));

    ssl->secure_renegotiation->cache_status = SCR_CACHE_NEEDED;

#ifndef NO_OLD_TLS
#ifndef NO_MD5
    wc_InitMd5(&ssl->hsHashes->hashMd5);
#endif
#ifndef NO_SHA
    ret = wc_InitSha(&ssl->hsHashes->hashSha);
    if (ret !=0)
        return ret;
#endif
#endif /* NO_OLD_TLS */
#ifndef NO_SHA256
    ret = wc_InitSha256(&ssl->hsHashes->hashSha256);
    if (ret !=0)
        return ret;
#endif
#ifdef WOLFSSL_SHA384
    ret = wc_InitSha384(&ssl->hsHashes->hashSha384);
    if (ret !=0)
        return ret;
#endif
#ifdef WOLFSSL_SHA512
    ret = wc_InitSha512(&ssl->hsHashes->hashSha512);
    if (ret !=0)
        return ret;
#endif

    ret = wolfSSL_negotiate(ssl);
    return ret;
}

#endif /* HAVE_SECURE_RENEGOTIATION */

/* Session Ticket */
#if !defined(NO_WOLFSSL_SERVER) && defined(HAVE_SESSION_TICKET)
/* SSL_SUCCESS on ok */
int wolfSSL_CTX_set_TicketEncCb(WOLFSSL_CTX* ctx, SessionTicketEncCb cb)
{
    if (ctx == NULL)
        return BAD_FUNC_ARG;

    ctx->ticketEncCb = cb;

    return SSL_SUCCESS;
}

/* set hint interval, SSL_SUCCESS on ok */
int wolfSSL_CTX_set_TicketHint(WOLFSSL_CTX* ctx, int hint)
{
    if (ctx == NULL)
        return BAD_FUNC_ARG;

    ctx->ticketHint = hint;

    return SSL_SUCCESS;
}

/* set user context, SSL_SUCCESS on ok */
int wolfSSL_CTX_set_TicketEncCtx(WOLFSSL_CTX* ctx, void* userCtx)
{
    if (ctx == NULL)
        return BAD_FUNC_ARG;

    ctx->ticketEncCtx = userCtx;

    return SSL_SUCCESS;
}

#endif /* !defined(NO_WOLFSSL_CLIENT) && defined(HAVE_SESSION_TICKET) */

/* Session Ticket */
#if !defined(NO_WOLFSSL_CLIENT) && defined(HAVE_SESSION_TICKET)
int wolfSSL_UseSessionTicket(WOLFSSL* ssl)
{
    if (ssl == NULL)
        return BAD_FUNC_ARG;

    return TLSX_UseSessionTicket(&ssl->extensions, NULL, ssl->heap);
}

int wolfSSL_CTX_UseSessionTicket(WOLFSSL_CTX* ctx)
{
    if (ctx == NULL)
        return BAD_FUNC_ARG;

    return TLSX_UseSessionTicket(&ctx->extensions, NULL, ctx->heap);
}

WOLFSSL_API int wolfSSL_get_SessionTicket(WOLFSSL* ssl,
                                          byte* buf, word32* bufSz)
{
    if (ssl == NULL || buf == NULL || bufSz == NULL || *bufSz == 0)
        return BAD_FUNC_ARG;

    if (ssl->session.ticketLen <= *bufSz) {
        XMEMCPY(buf, ssl->session.ticket, ssl->session.ticketLen);
        *bufSz = ssl->session.ticketLen;
    }
    else
        *bufSz = 0;

    return SSL_SUCCESS;
}

WOLFSSL_API int wolfSSL_set_SessionTicket(WOLFSSL* ssl, byte* buf, word32 bufSz)
{
    if (ssl == NULL || (buf == NULL && bufSz > 0))
        return BAD_FUNC_ARG;

    if (bufSz > 0) {
        /* Ticket will fit into static ticket */
        if(bufSz <= SESSION_TICKET_LEN) {
            if (ssl->session.isDynamic) {
                XFREE(ssl->session.ticket, ssl->heap, DYNAMIC_TYPE_SESSION_TICK);
                ssl->session.isDynamic = 0;
                ssl->session.ticket = ssl->session.staticTicket;
            }
        } else { /* Ticket requires dynamic ticket storage */
            if (ssl->session.ticketLen < bufSz) { /* is dyn buffer big enough */
                if(ssl->session.isDynamic)
                    XFREE(ssl->session.ticket, ssl->heap,
                            DYNAMIC_TYPE_SESSION_TICK);
                ssl->session.ticket = (byte*)XMALLOC(bufSz, ssl->heap,
                        DYNAMIC_TYPE_SESSION_TICK);
                if(!ssl->session.ticket) {
                    ssl->session.ticket = ssl->session.staticTicket;
                    ssl->session.isDynamic = 0;
                    return MEMORY_ERROR;
                }
                ssl->session.isDynamic = 1;
            }
        }
        XMEMCPY(ssl->session.ticket, buf, bufSz);
    }
    ssl->session.ticketLen = (word16)bufSz;

    return SSL_SUCCESS;
}


WOLFSSL_API int wolfSSL_set_SessionTicket_cb(WOLFSSL* ssl,
                                            CallbackSessionTicket cb, void* ctx)
{
    if (ssl == NULL)
        return BAD_FUNC_ARG;

    ssl->session_ticket_cb = cb;
    ssl->session_ticket_ctx = ctx;

    return SSL_SUCCESS;
}
#endif


#ifdef HAVE_EXTENDED_MASTER
#ifndef NO_WOLFSSL_CLIENT

int wolfSSL_CTX_DisableExtendedMasterSecret(WOLFSSL_CTX* ctx)
{
    if (ctx == NULL)
        return BAD_FUNC_ARG;

    ctx->haveEMS = 0;

    return SSL_SUCCESS;
}


int wolfSSL_DisableExtendedMasterSecret(WOLFSSL* ssl)
{
    if (ssl == NULL)
        return BAD_FUNC_ARG;

    ssl->options.haveEMS = 0;

    return SSL_SUCCESS;
}

#endif
#endif


#ifndef WOLFSSL_LEANPSK

int wolfSSL_send(WOLFSSL* ssl, const void* data, int sz, int flags)
{
    int ret;
    int oldFlags;

    WOLFSSL_ENTER("wolfSSL_send()");

    if (ssl == NULL || data == NULL || sz < 0)
        return BAD_FUNC_ARG;

    oldFlags = ssl->wflags;

    ssl->wflags = flags;
    ret = wolfSSL_write(ssl, data, sz);
    ssl->wflags = oldFlags;

    WOLFSSL_LEAVE("wolfSSL_send()", ret);

    return ret;
}


int wolfSSL_recv(WOLFSSL* ssl, void* data, int sz, int flags)
{
    int ret;
    int oldFlags;

    WOLFSSL_ENTER("wolfSSL_recv()");

    if (ssl == NULL || data == NULL || sz < 0)
        return BAD_FUNC_ARG;

    oldFlags = ssl->rflags;

    ssl->rflags = flags;
    ret = wolfSSL_read(ssl, data, sz);
    ssl->rflags = oldFlags;

    WOLFSSL_LEAVE("wolfSSL_recv()", ret);

    return ret;
}
#endif


/* SSL_SUCCESS on ok */
int wolfSSL_shutdown(WOLFSSL* ssl)
{
    int  ret = SSL_FATAL_ERROR;
    byte tmp;
    WOLFSSL_ENTER("SSL_shutdown()");

    if (ssl == NULL)
        return SSL_FATAL_ERROR;

    if (ssl->options.quietShutdown) {
        WOLFSSL_MSG("quiet shutdown, no close notify sent");
        return SSL_SUCCESS;
    }

    /* try to send close notify, not an error if can't */
    if (!ssl->options.isClosed && !ssl->options.connReset &&
                                  !ssl->options.sentNotify) {
        ssl->error = SendAlert(ssl, alert_warning, close_notify);
        if (ssl->error < 0) {
            WOLFSSL_ERROR(ssl->error);
            return SSL_FATAL_ERROR;
        }
        ssl->options.sentNotify = 1;  /* don't send close_notify twice */
        if (ssl->options.closeNotify)
            ret = SSL_SUCCESS;
        else
            ret = SSL_SHUTDOWN_NOT_DONE;

        WOLFSSL_LEAVE("SSL_shutdown()", ret);
        return ret;
    }

    /* call wolfSSL_shutdown again for bidirectional shutdown */
    if (ssl->options.sentNotify && !ssl->options.closeNotify) {
        ret = wolfSSL_read(ssl, &tmp, 0);
        if (ret < 0) {
            WOLFSSL_ERROR(ssl->error);
            ret = SSL_FATAL_ERROR;
        } else if (ssl->options.closeNotify) {
            ssl->error = SSL_ERROR_SYSCALL;   /* simulate OpenSSL behavior */
            ret = SSL_SUCCESS;
        }
    }

    WOLFSSL_LEAVE("SSL_shutdown()", ret);

    return ret;
}


/* get current error state value */
int wolfSSL_state(WOLFSSL* ssl)
{
    if (ssl == NULL) {
        return BAD_FUNC_ARG;
    }

    return ssl->error;
}


int wolfSSL_get_error(WOLFSSL* ssl, int ret)
{
    WOLFSSL_ENTER("SSL_get_error");

    if (ret > 0)
        return SSL_ERROR_NONE;
    if (ssl == NULL)
        return BAD_FUNC_ARG;

    WOLFSSL_LEAVE("SSL_get_error", ssl->error);

    /* make sure converted types are handled in SetErrorString() too */
    if (ssl->error == WANT_READ)
        return SSL_ERROR_WANT_READ;         /* convert to OpenSSL type */
    else if (ssl->error == WANT_WRITE)
        return SSL_ERROR_WANT_WRITE;        /* convert to OpenSSL type */
    else if (ssl->error == ZERO_RETURN)
        return SSL_ERROR_ZERO_RETURN;       /* convert to OpenSSL type */
    return ssl->error;
}


/* retrive alert history, SSL_SUCCESS on ok */
int wolfSSL_get_alert_history(WOLFSSL* ssl, WOLFSSL_ALERT_HISTORY *h)
{
    if (ssl && h) {
        *h = ssl->alert_history;
    }
    return SSL_SUCCESS;
}


/* return TRUE if current error is want read */
int wolfSSL_want_read(WOLFSSL* ssl)
{
    WOLFSSL_ENTER("SSL_want_read");
    if (ssl->error == WANT_READ)
        return 1;

    return 0;
}


/* return TRUE if current error is want write */
int wolfSSL_want_write(WOLFSSL* ssl)
{
    WOLFSSL_ENTER("SSL_want_write");
    if (ssl->error == WANT_WRITE)
        return 1;

    return 0;
}


char* wolfSSL_ERR_error_string(unsigned long errNumber, char* data)
{
    static const char* msg = "Please supply a buffer for error string";

    WOLFSSL_ENTER("ERR_error_string");
    if (data) {
        SetErrorString((int)errNumber, data);
        return data;
    }

    return (char*)msg;
}


void wolfSSL_ERR_error_string_n(unsigned long e, char* buf, unsigned long len)
{
    WOLFSSL_ENTER("wolfSSL_ERR_error_string_n");
    if (len >= WOLFSSL_MAX_ERROR_SZ)
        wolfSSL_ERR_error_string(e, buf);
    else {
        char tmp[WOLFSSL_MAX_ERROR_SZ];

        WOLFSSL_MSG("Error buffer too short, truncating");
        if (len) {
            wolfSSL_ERR_error_string(e, tmp);
            XMEMCPY(buf, tmp, len-1);
            buf[len-1] = '\0';
        }
    }
}


/* don't free temporary arrays at end of handshake */
void wolfSSL_KeepArrays(WOLFSSL* ssl)
{
    if (ssl)
        ssl->options.saveArrays = 1;
}


/* user doesn't need temporary arrays anymore, Free */
void wolfSSL_FreeArrays(WOLFSSL* ssl)
{
    if (ssl && ssl->options.handShakeState == HANDSHAKE_DONE) {
        ssl->options.saveArrays = 0;
        FreeArrays(ssl, 1);
    }
}


const byte* wolfSSL_GetMacSecret(WOLFSSL* ssl, int verify)
{
    if (ssl == NULL)
        return NULL;

    if ( (ssl->options.side == WOLFSSL_CLIENT_END && !verify) ||
         (ssl->options.side == WOLFSSL_SERVER_END &&  verify) )
        return ssl->keys.client_write_MAC_secret;
    else
        return ssl->keys.server_write_MAC_secret;
}


#ifdef ATOMIC_USER

void  wolfSSL_CTX_SetMacEncryptCb(WOLFSSL_CTX* ctx, CallbackMacEncrypt cb)
{
    if (ctx)
        ctx->MacEncryptCb = cb;
}


void  wolfSSL_SetMacEncryptCtx(WOLFSSL* ssl, void *ctx)
{
    if (ssl)
        ssl->MacEncryptCtx = ctx;
}


void* wolfSSL_GetMacEncryptCtx(WOLFSSL* ssl)
{
    if (ssl)
        return ssl->MacEncryptCtx;

    return NULL;
}


void  wolfSSL_CTX_SetDecryptVerifyCb(WOLFSSL_CTX* ctx, CallbackDecryptVerify cb)
{
    if (ctx)
        ctx->DecryptVerifyCb = cb;
}


void  wolfSSL_SetDecryptVerifyCtx(WOLFSSL* ssl, void *ctx)
{
    if (ssl)
        ssl->DecryptVerifyCtx = ctx;
}


void* wolfSSL_GetDecryptVerifyCtx(WOLFSSL* ssl)
{
    if (ssl)
        return ssl->DecryptVerifyCtx;

    return NULL;
}


const byte* wolfSSL_GetClientWriteKey(WOLFSSL* ssl)
{
    if (ssl)
        return ssl->keys.client_write_key;

    return NULL;
}


const byte* wolfSSL_GetClientWriteIV(WOLFSSL* ssl)
{
    if (ssl)
        return ssl->keys.client_write_IV;

    return NULL;
}


const byte* wolfSSL_GetServerWriteKey(WOLFSSL* ssl)
{
    if (ssl)
        return ssl->keys.server_write_key;

    return NULL;
}


const byte* wolfSSL_GetServerWriteIV(WOLFSSL* ssl)
{
    if (ssl)
        return ssl->keys.server_write_IV;

    return NULL;
}

int wolfSSL_GetKeySize(WOLFSSL* ssl)
{
    if (ssl)
        return ssl->specs.key_size;

    return BAD_FUNC_ARG;
}


int wolfSSL_GetIVSize(WOLFSSL* ssl)
{
    if (ssl)
        return ssl->specs.iv_size;

    return BAD_FUNC_ARG;
}


int wolfSSL_GetBulkCipher(WOLFSSL* ssl)
{
    if (ssl)
        return ssl->specs.bulk_cipher_algorithm;

    return BAD_FUNC_ARG;
}


int wolfSSL_GetCipherType(WOLFSSL* ssl)
{
    if (ssl == NULL)
        return BAD_FUNC_ARG;

    if (ssl->specs.cipher_type == block)
        return WOLFSSL_BLOCK_TYPE;
    if (ssl->specs.cipher_type == stream)
        return WOLFSSL_STREAM_TYPE;
    if (ssl->specs.cipher_type == aead)
        return WOLFSSL_AEAD_TYPE;

    return -1;
}


int wolfSSL_GetCipherBlockSize(WOLFSSL* ssl)
{
    if (ssl == NULL)
        return BAD_FUNC_ARG;

    return ssl->specs.block_size;
}


int wolfSSL_GetAeadMacSize(WOLFSSL* ssl)
{
    if (ssl == NULL)
        return BAD_FUNC_ARG;

    return ssl->specs.aead_mac_size;
}


int wolfSSL_IsTLSv1_1(WOLFSSL* ssl)
{
    if (ssl == NULL)
        return BAD_FUNC_ARG;

    if (ssl->options.tls1_1)
        return 1;

    return 0;
}


int wolfSSL_GetSide(WOLFSSL* ssl)
{
    if (ssl)
        return ssl->options.side;

    return BAD_FUNC_ARG;
}


int wolfSSL_GetHmacSize(WOLFSSL* ssl)
{
    /* AEAD ciphers don't have HMAC keys */
    if (ssl)
        return (ssl->specs.cipher_type != aead) ? ssl->specs.hash_size : 0;

    return BAD_FUNC_ARG;
}

#endif /* ATOMIC_USER */

#ifndef NO_CERTS

int AllocDer(DerBuffer** pDer, word32 length, int type, void* heap)
{
    int ret = BAD_FUNC_ARG;
    if (pDer) {
        int dynType = 0;
        DerBuffer* der;

        /* Determine dynamic type */
        switch (type) {
            case CA_TYPE:   dynType = DYNAMIC_TYPE_CA;   break;
            case CERT_TYPE: dynType = DYNAMIC_TYPE_CERT; break;
            case CRL_TYPE:  dynType = DYNAMIC_TYPE_CRL;  break;
            case DSA_TYPE:  dynType = DYNAMIC_TYPE_DSA;  break;
            case ECC_TYPE:  dynType = DYNAMIC_TYPE_ECC;  break;
            case RSA_TYPE:  dynType = DYNAMIC_TYPE_RSA;  break;
            default:        dynType = DYNAMIC_TYPE_KEY;  break;
        }

        /* Setup new buffer */
        *pDer = (DerBuffer*)XMALLOC(sizeof(DerBuffer) + length, heap, dynType);
        if (*pDer == NULL) {
            return MEMORY_ERROR;
        }

        der = *pDer;
        der->type = type;
        der->dynType = dynType; /* Cache this for FreeDer */
        der->heap = heap;
        der->buffer = (byte*)der + sizeof(DerBuffer);
        der->length = length;
        ret = 0; /* Success */
    }
    return ret;
}

void FreeDer(DerBuffer** pDer)
{
    if (pDer && *pDer)
    {
        DerBuffer* der = (DerBuffer*)*pDer;

        /* ForceZero private keys */
        if (der->type == PRIVATEKEY_TYPE) {
            ForceZero(der->buffer, der->length);
        }
        der->buffer = NULL;
        der->length = 0;
        XFREE(der, der->heap, der->dynType);

        *pDer = NULL;
    }
}


WOLFSSL_CERT_MANAGER* wolfSSL_CertManagerNew_ex(void* heap)
{
    WOLFSSL_CERT_MANAGER* cm = NULL;

    WOLFSSL_ENTER("wolfSSL_CertManagerNew");

    cm = (WOLFSSL_CERT_MANAGER*) XMALLOC(sizeof(WOLFSSL_CERT_MANAGER), heap,
                                         DYNAMIC_TYPE_CERT_MANAGER);
    if (cm) {
        XMEMSET(cm, 0, sizeof(WOLFSSL_CERT_MANAGER));

        if (wc_InitMutex(&cm->caLock) != 0) {
            WOLFSSL_MSG("Bad mutex init");
            wolfSSL_CertManagerFree(cm);
            return NULL;
        }

        #ifdef WOLFSSL_TRUST_PEER_CERT
        if (wc_InitMutex(&cm->tpLock) != 0) {
            WOLFSSL_MSG("Bad mutex init");
            wolfSSL_CertManagerFree(cm);
            return NULL;
        }
        #endif

        /* set default minimum key size allowed */
        #ifndef NO_RSA
            cm->minRsaKeySz = MIN_RSAKEY_SZ;
        #endif
        #ifdef HAVE_ECC
            cm->minEccKeySz = MIN_ECCKEY_SZ;
        #endif
            cm->heap = heap;
    }

    return cm;
}


WOLFSSL_CERT_MANAGER* wolfSSL_CertManagerNew(void)
{
    return wolfSSL_CertManagerNew_ex(NULL);
}


void wolfSSL_CertManagerFree(WOLFSSL_CERT_MANAGER* cm)
{
    WOLFSSL_ENTER("wolfSSL_CertManagerFree");

    if (cm) {
        #ifdef HAVE_CRL
            if (cm->crl)
                FreeCRL(cm->crl, 1);
        #endif
        #ifdef HAVE_OCSP
            if (cm->ocsp)
                FreeOCSP(cm->ocsp, 1);
        #if defined(HAVE_CERTIFICATE_STATUS_REQUEST) \
         || defined(HAVE_CERTIFICATE_STATUS_REQUEST_V2)
            if (cm->ocsp_stapling)
                FreeOCSP(cm->ocsp_stapling, 1);
        #endif
        #endif
        FreeSignerTable(cm->caTable, CA_TABLE_SIZE, cm->heap);
        wc_FreeMutex(&cm->caLock);

        #ifdef WOLFSSL_TRUST_PEER_CERT
        FreeTrustedPeerTable(cm->tpTable, TP_TABLE_SIZE, cm->heap);
        wc_FreeMutex(&cm->tpLock);
        #endif

        XFREE(cm, cm->heap, DYNAMIC_TYPE_CERT_MANAGER);
    }

}


/* Unload the CA signer list */
int wolfSSL_CertManagerUnloadCAs(WOLFSSL_CERT_MANAGER* cm)
{
    WOLFSSL_ENTER("wolfSSL_CertManagerUnloadCAs");

    if (cm == NULL)
        return BAD_FUNC_ARG;

    if (wc_LockMutex(&cm->caLock) != 0)
        return BAD_MUTEX_E;

    FreeSignerTable(cm->caTable, CA_TABLE_SIZE, NULL);

    wc_UnLockMutex(&cm->caLock);


    return SSL_SUCCESS;
}


#ifdef WOLFSSL_TRUST_PEER_CERT
int wolfSSL_CertManagerUnload_trust_peers(WOLFSSL_CERT_MANAGER* cm)
{
    WOLFSSL_ENTER("wolfSSL_CertManagerUnload_trust_peers");

    if (cm == NULL)
        return BAD_FUNC_ARG;

    if (wc_LockMutex(&cm->tpLock) != 0)
        return BAD_MUTEX_E;

    FreeTrustedPeerTable(cm->tpTable, TP_TABLE_SIZE, NULL);

    wc_UnLockMutex(&cm->tpLock);


    return SSL_SUCCESS;
}
#endif /* WOLFSSL_TRUST_PEER_CERT */


/* Return bytes written to buff or < 0 for error */
int wolfSSL_CertPemToDer(const unsigned char* pem, int pemSz,
                        unsigned char* buff, int buffSz, int type)
{
    int            eccKey = 0;
    int            ret;
    DerBuffer*     der = NULL;
#ifdef WOLFSSL_SMALL_STACK
    EncryptedInfo* info = NULL;
#else
    EncryptedInfo  info[1];
#endif

    WOLFSSL_ENTER("wolfSSL_CertPemToDer");

    if (pem == NULL || buff == NULL || buffSz <= 0) {
        WOLFSSL_MSG("Bad pem der args");
        return BAD_FUNC_ARG;
    }

    if (type != CERT_TYPE && type != CA_TYPE && type != CERTREQ_TYPE) {
        WOLFSSL_MSG("Bad cert type");
        return BAD_FUNC_ARG;
    }

#ifdef WOLFSSL_SMALL_STACK
    info = (EncryptedInfo*)XMALLOC(sizeof(EncryptedInfo), NULL,
                                   DYNAMIC_TYPE_TMP_BUFFER);
    if (info == NULL)
        return MEMORY_E;
#endif

    info->set      = 0;
    info->ctx      = NULL;
    info->consumed = 0;

    ret = PemToDer(pem, pemSz, type, &der, NULL, info, &eccKey);

#ifdef WOLFSSL_SMALL_STACK
    XFREE(info, NULL, DYNAMIC_TYPE_TMP_BUFFER);
#endif

    if (ret < 0) {
        WOLFSSL_MSG("Bad Pem To Der");
    }
    else {
        if (der->length <= (word32)buffSz) {
            XMEMCPY(buff, der->buffer, der->length);
            ret = der->length;
        }
        else {
            WOLFSSL_MSG("Bad der length");
            ret = BAD_FUNC_ARG;
        }
    }

    FreeDer(&der);
    return ret;
}

#endif /* NO_CERTS */

#if defined(OPENSSL_EXTRA) || defined(HAVE_WEBSERVER)

static struct cipher{
        unsigned char type;
        const char *name;
} cipher_tbl[] = {

#ifndef NO_AES
    {AES_128_CBC_TYPE, "AES-128-CBC"},
    {AES_192_CBC_TYPE, "AES-192-CBC"},
    {AES_256_CBC_TYPE, "AES-256-CBC"},
#if defined(OPENSSL_EXTRA)
        {AES_128_CTR_TYPE, "AES-128-CTR"},
        {AES_192_CTR_TYPE, "AES-192-CTR"},
        {AES_256_CTR_TYPE, "AES-256-CTR"},

        {AES_128_ECB_TYPE, "AES-128-ECB"},
        {AES_192_ECB_TYPE, "AES-192-ECB"},
        {AES_256_ECB_TYPE, "AES-256-ECB"},
#endif

#endif

#ifndef NO_DES3
    {DES_CBC_TYPE, "DES-CBC"},
    {DES_ECB_TYPE, "DES-ECB"},

    {DES_EDE3_CBC_TYPE, "DES-EDE3-CBC"},
    {DES_EDE3_ECB_TYPE, "DES-EDE3-ECB"},
#endif

#ifdef HAVE_IDEA
    {IDEA_CBC_TYPE, "IDEA-CBC"},
#endif
    { 0, NULL}
} ;

const WOLFSSL_EVP_CIPHER *wolfSSL_EVP_get_cipherbyname(const char *name)
{

    static const struct alias {
        const char *name;
        const char *alias;
    } alias_tbl[] =
    {
        {"DES-CBC", "DES"},
        {"DES-CBC", "des"},
        {"DES-EDE3-CBC", "DES3"},
        {"DES-EDE3-CBC", "des3"},
        {"DES-EDE3-ECB", "des-ede3-ecb"},
        {"IDEA-CBC", "IDEA"},
        {"IDEA-CBC", "idea"},
        {"AES-128-CBC", "AES128"},
        {"AES-128-CBC", "aes128"},
        {"AES-192-CBC", "AES192"},
        {"AES-192-CBC", "aes192"},
        {"AES-256-CBC", "AES256"},
        {"AES-256-CBC", "aes256"},
        { NULL, NULL}
    };

    const struct cipher *ent ;
    const struct alias  *al ;

    WOLFSSL_ENTER("EVP_get_cipherbyname");

    for( al = alias_tbl; al->name != NULL; al++)
        if(XSTRNCMP(name, al->alias, XSTRLEN(al->alias)+1) == 0) {
            name = al->name;
            break;
        }

    for( ent = cipher_tbl; ent->name != NULL; ent++)
        if(XSTRNCMP(name, ent->name, XSTRLEN(ent->name)+1) == 0) {
            return (WOLFSSL_EVP_CIPHER *)ent->name;
        }

    return NULL;
}


#ifndef NO_AES
static char *EVP_AES_128_CBC;
static char *EVP_AES_192_CBC;
static char *EVP_AES_256_CBC;
#if defined(OPENSSL_EXTRA)
    static char *EVP_AES_128_CTR;
    static char *EVP_AES_192_CTR;
    static char *EVP_AES_256_CTR;

    static char *EVP_AES_128_ECB;
    static char *EVP_AES_192_ECB;
    static char *EVP_AES_256_ECB;
#endif
static const int  EVP_AES_SIZE = 11;
#endif

#ifndef NO_DES3
static char *EVP_DES_CBC;
static char *EVP_DES_ECB;
static const int  EVP_DES_SIZE = 7;

static char *EVP_DES_EDE3_CBC;
static char *EVP_DES_EDE3_ECB;
static const int  EVP_DES_EDE3_SIZE = 12;
#endif

#ifdef HAVE_IDEA
static char *EVP_IDEA_CBC;
static const int  EVP_IDEA_SIZE = 8;
#endif

void wolfSSL_EVP_init(void)
{
#ifndef NO_AES
    EVP_AES_128_CBC = (char *)EVP_get_cipherbyname("AES-128-CBC");
    EVP_AES_192_CBC = (char *)EVP_get_cipherbyname("AES-192-CBC");
    EVP_AES_256_CBC = (char *)EVP_get_cipherbyname("AES-256-CBC");

#if defined(OPENSSL_EXTRA)
        EVP_AES_128_CTR = (char *)EVP_get_cipherbyname("AES-128-CTR");
        EVP_AES_192_CTR = (char *)EVP_get_cipherbyname("AES-192-CTR");
        EVP_AES_256_CTR = (char *)EVP_get_cipherbyname("AES-256-CTR");

        EVP_AES_128_ECB = (char *)EVP_get_cipherbyname("AES-128-ECB");
        EVP_AES_192_ECB = (char *)EVP_get_cipherbyname("AES-192-ECB");
        EVP_AES_256_ECB = (char *)EVP_get_cipherbyname("AES-256-ECB");
#endif
#endif

#ifndef NO_DES3
    EVP_DES_CBC = (char *)EVP_get_cipherbyname("DES-CBC");
    EVP_DES_ECB = (char *)EVP_get_cipherbyname("DES-ECB");

    EVP_DES_EDE3_CBC = (char *)EVP_get_cipherbyname("DES-EDE3-CBC");
    EVP_DES_EDE3_ECB = (char *)EVP_get_cipherbyname("DES-EDE3-ECB");
#endif

#ifdef HAVE_IDEA
    EVP_IDEA_CBC = (char *)EVP_get_cipherbyname("IDEA-CBC");
#endif
}

/* our KeyPemToDer password callback, password in userData */
static INLINE int OurPasswordCb(char* passwd, int sz, int rw, void* userdata)
{
    (void)rw;

    if (userdata == NULL)
        return 0;

    XSTRNCPY(passwd, (char*)userdata, sz);
    return min((word32)sz, (word32)XSTRLEN((char*)userdata));
}

#endif /* OPENSSL_EXTRA || HAVE_WEBSERVER */

#ifndef NO_CERTS

/* Return bytes written to buff or < 0 for error */
int wolfSSL_KeyPemToDer(const unsigned char* pem, int pemSz,
                        unsigned char* buff, int buffSz, const char* pass)
{
    int            eccKey = 0;
    int            ret;
    DerBuffer*     der = NULL;
#ifdef WOLFSSL_SMALL_STACK
    EncryptedInfo* info = NULL;
#else
    EncryptedInfo  info[1];
#endif

    WOLFSSL_ENTER("wolfSSL_KeyPemToDer");

    if (pem == NULL || buff == NULL || buffSz <= 0) {
        WOLFSSL_MSG("Bad pem der args");
        return BAD_FUNC_ARG;
    }

#ifdef WOLFSSL_SMALL_STACK
    info = (EncryptedInfo*)XMALLOC(sizeof(EncryptedInfo), NULL,
                                   DYNAMIC_TYPE_TMP_BUFFER);
    if (info == NULL)
        return MEMORY_E;
#endif

    info->set      = 0;
    info->ctx      = NULL;
    info->consumed = 0;

#if defined(OPENSSL_EXTRA) || defined(HAVE_WEBSERVER)
    if (pass) {
        info->ctx = wolfSSL_CTX_new(wolfSSLv23_client_method());
        if (info->ctx == NULL) {
        #ifdef WOLFSSL_SMALL_STACK
            XFREE(info, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        #endif
            return MEMORY_E;
        }

        wolfSSL_CTX_set_default_passwd_cb(info->ctx, OurPasswordCb);
        wolfSSL_CTX_set_default_passwd_cb_userdata(info->ctx, (void*)pass);
    }
#else
    (void)pass;
#endif

    ret = PemToDer(pem, pemSz, PRIVATEKEY_TYPE, &der, NULL, info, &eccKey);

    if (info->ctx)
        wolfSSL_CTX_free(info->ctx);

#ifdef WOLFSSL_SMALL_STACK
    XFREE(info, NULL, DYNAMIC_TYPE_TMP_BUFFER);
#endif

    if (ret < 0) {
        WOLFSSL_MSG("Bad Pem To Der");
    }
    else {
        if (der->length <= (word32)buffSz) {
            XMEMCPY(buff, der->buffer, der->length);
            ret = der->length;
        }
        else {
            WOLFSSL_MSG("Bad der length");
            ret = BAD_FUNC_ARG;
        }
    }

    FreeDer(&der);
    return ret;
}

#endif /* !NO_CERTS */



#if !defined(NO_FILESYSTEM) && !defined(NO_STDIO_FILESYSTEM)

void wolfSSL_ERR_print_errors_fp(FILE* fp, int err)
{
    char data[WOLFSSL_MAX_ERROR_SZ + 1];

    WOLFSSL_ENTER("wolfSSL_ERR_print_errors_fp");
    SetErrorString(err, data);
    fprintf(fp, "%s", data);
}

#if defined(OPENSSL_EXTRA) || defined(DEBUG_WOLFSSL_VERBOSE)
void wolfSSL_ERR_dump_errors_fp(FILE* fp)
{
    wc_ERR_print_errors_fp(fp);
}
#endif
#endif


int wolfSSL_pending(WOLFSSL* ssl)
{
    WOLFSSL_ENTER("SSL_pending");
    return ssl->buffers.clearOutputBuffer.length;
}


#ifndef WOLFSSL_LEANPSK
/* turn on handshake group messages for context */
int wolfSSL_CTX_set_group_messages(WOLFSSL_CTX* ctx)
{
    if (ctx == NULL)
       return BAD_FUNC_ARG;

    ctx->groupMessages = 1;

    return SSL_SUCCESS;
}
#endif


#ifndef NO_WOLFSSL_CLIENT
/* connect enough to get peer cert chain */
int wolfSSL_connect_cert(WOLFSSL* ssl)
{
    int  ret;

    if (ssl == NULL)
        return SSL_FAILURE;

    ssl->options.certOnly = 1;
    ret = wolfSSL_connect(ssl);
    ssl->options.certOnly   = 0;

    return ret;
}
#endif


#ifndef WOLFSSL_LEANPSK
/* turn on handshake group messages for ssl object */
int wolfSSL_set_group_messages(WOLFSSL* ssl)
{
    if (ssl == NULL)
       return BAD_FUNC_ARG;

    ssl->options.groupMessages = 1;

    return SSL_SUCCESS;
}


/* make minVersion the internal equivalent SSL version */
static int SetMinVersionHelper(byte* minVersion, int version)
{
#ifdef NO_TLS
    (void)minVersion;
#endif

    switch (version) {
#if defined(WOLFSSL_ALLOW_SSLV3) && !defined(NO_OLD_TLS)
        case WOLFSSL_SSLV3:
            *minVersion = SSLv3_MINOR;
            break;
#endif

#ifndef NO_TLS
    #ifndef NO_OLD_TLS
        case WOLFSSL_TLSV1:
            *minVersion = TLSv1_MINOR;
            break;

        case WOLFSSL_TLSV1_1:
            *minVersion = TLSv1_1_MINOR;
            break;
    #endif
        case WOLFSSL_TLSV1_2:
            *minVersion = TLSv1_2_MINOR;
            break;
#endif

        default:
            WOLFSSL_MSG("Bad function argument");
            return BAD_FUNC_ARG;
    }

    return SSL_SUCCESS;
}


/* Set minimum downgrade version allowed, SSL_SUCCESS on ok */
int wolfSSL_CTX_SetMinVersion(WOLFSSL_CTX* ctx, int version)
{
    WOLFSSL_ENTER("wolfSSL_CTX_SetMinVersion");

    if (ctx == NULL) {
        WOLFSSL_MSG("Bad function argument");
        return BAD_FUNC_ARG;
    }

    return SetMinVersionHelper(&ctx->minDowngrade, version);
}


/* Set minimum downgrade version allowed, SSL_SUCCESS on ok */
int wolfSSL_SetMinVersion(WOLFSSL* ssl, int version)
{
    WOLFSSL_ENTER("wolfSSL_SetMinVersion");

    if (ssl == NULL) {
        WOLFSSL_MSG("Bad function argument");
        return BAD_FUNC_ARG;
    }

    return SetMinVersionHelper(&ssl->options.minDowngrade, version);
}


int wolfSSL_SetVersion(WOLFSSL* ssl, int version)
{
    word16 haveRSA = 1;
    word16 havePSK = 0;

    WOLFSSL_ENTER("wolfSSL_SetVersion");

    if (ssl == NULL) {
        WOLFSSL_MSG("Bad function argument");
        return BAD_FUNC_ARG;
    }

    switch (version) {
#if defined(WOLFSSL_ALLOW_SSLV3) && !defined(NO_OLD_TLS)
        case WOLFSSL_SSLV3:
            ssl->version = MakeSSLv3();
            break;
#endif

#ifndef NO_TLS
    #ifndef NO_OLD_TLS
        case WOLFSSL_TLSV1:
            ssl->version = MakeTLSv1();
            break;

        case WOLFSSL_TLSV1_1:
            ssl->version = MakeTLSv1_1();
            break;
    #endif
        case WOLFSSL_TLSV1_2:
            ssl->version = MakeTLSv1_2();
            break;
#endif

        default:
            WOLFSSL_MSG("Bad function argument");
            return BAD_FUNC_ARG;
    }

    #ifdef NO_RSA
        haveRSA = 0;
    #endif
    #ifndef NO_PSK
        havePSK = ssl->options.havePSK;
    #endif

    InitSuites(ssl->suites, ssl->version, haveRSA, havePSK, ssl->options.haveDH,
                ssl->options.haveNTRU, ssl->options.haveECDSAsig,
                ssl->options.haveECC, ssl->options.haveStaticECC,
                ssl->options.side);

    return SSL_SUCCESS;
}
#endif /* !leanpsk */


#if !defined(NO_CERTS) || !defined(NO_SESSION_CACHE)

/* Make a work from the front of random hash */
static INLINE word32 MakeWordFromHash(const byte* hashID)
{
    return (hashID[0] << 24) | (hashID[1] << 16) | (hashID[2] <<  8) |
            hashID[3];
}

#endif /* !NO_CERTS || !NO_SESSION_CACHE */


#ifndef NO_CERTS

/* hash is the SHA digest of name, just use first 32 bits as hash */
static INLINE word32 HashSigner(const byte* hash)
{
    return MakeWordFromHash(hash) % CA_TABLE_SIZE;
}


/* does CA already exist on signer list */
int AlreadySigner(WOLFSSL_CERT_MANAGER* cm, byte* hash)
{
    Signer* signers;
    int     ret = 0;
    word32  row = HashSigner(hash);

    if (wc_LockMutex(&cm->caLock) != 0)
        return  ret;
    signers = cm->caTable[row];
    while (signers) {
        byte* subjectHash;
        #ifndef NO_SKID
            subjectHash = signers->subjectKeyIdHash;
        #else
            subjectHash = signers->subjectNameHash;
        #endif
        if (XMEMCMP(hash, subjectHash, SIGNER_DIGEST_SIZE) == 0) {
            ret = 1;
            break;
        }
        signers = signers->next;
    }
    wc_UnLockMutex(&cm->caLock);

    return ret;
}


#ifdef WOLFSSL_TRUST_PEER_CERT
/* hash is the SHA digest of name, just use first 32 bits as hash */
static INLINE word32 TrustedPeerHashSigner(const byte* hash)
{
    return MakeWordFromHash(hash) % TP_TABLE_SIZE;
}

/* does trusted peer already exist on signer list */
int AlreadyTrustedPeer(WOLFSSL_CERT_MANAGER* cm, byte* hash)
{
    TrustedPeerCert* tp;
    int     ret = 0;
    word32  row = TrustedPeerHashSigner(hash);

    if (wc_LockMutex(&cm->tpLock) != 0)
        return  ret;
    tp = cm->tpTable[row];
    while (tp) {
        byte* subjectHash;
        #ifndef NO_SKID
            subjectHash = tp->subjectKeyIdHash;
        #else
            subjectHash = tp->subjectNameHash;
        #endif
        if (XMEMCMP(hash, subjectHash, SIGNER_DIGEST_SIZE) == 0) {
            ret = 1;
            break;
        }
        tp = tp->next;
    }
    wc_UnLockMutex(&cm->tpLock);

    return ret;
}


/* return Trusted Peer if found, otherwise NULL
    type is what to match on
 */
TrustedPeerCert* GetTrustedPeer(void* vp, byte* hash, int type)
{
    WOLFSSL_CERT_MANAGER* cm = (WOLFSSL_CERT_MANAGER*)vp;
    TrustedPeerCert* ret = NULL;
    TrustedPeerCert* tp  = NULL;
    word32  row;

    if (cm == NULL || hash == NULL)
        return NULL;

    row = TrustedPeerHashSigner(hash);

    if (wc_LockMutex(&cm->tpLock) != 0)
        return ret;

    tp = cm->tpTable[row];
    while (tp) {
        byte* subjectHash;
        switch (type) {
            #ifndef NO_SKID
            case WC_MATCH_SKID:
                subjectHash = tp->subjectKeyIdHash;
                break;
            #endif
            case WC_MATCH_NAME:
                subjectHash = tp->subjectNameHash;
                break;
            default:
                WOLFSSL_MSG("Unknown search type");
                wc_UnLockMutex(&cm->tpLock);
                return NULL;
        }
        if (XMEMCMP(hash, subjectHash, SIGNER_DIGEST_SIZE) == 0) {
            ret = tp;
            break;
        }
        tp = tp->next;
    }
    wc_UnLockMutex(&cm->tpLock);

    return ret;
}


int MatchTrustedPeer(TrustedPeerCert* tp, DecodedCert* cert)
{
    if (tp == NULL || cert == NULL)
        return BAD_FUNC_ARG;

    /* subject key id or subject hash has been compared when searching
       tpTable for the cert from function GetTrustedPeer */

    /* compare signatures */
    if (tp->sigLen == cert->sigLength) {
        if (XMEMCMP(tp->sig, cert->signature, cert->sigLength)) {
            return SSL_FAILURE;
        }
    }
    else {
        return SSL_FAILURE;
    }

    return SSL_SUCCESS;
}
#endif /* WOLFSSL_TRUST_PEER_CERT */


/* return CA if found, otherwise NULL */
Signer* GetCA(void* vp, byte* hash)
{
    WOLFSSL_CERT_MANAGER* cm = (WOLFSSL_CERT_MANAGER*)vp;
    Signer* ret = NULL;
    Signer* signers;
    word32  row = HashSigner(hash);

    if (cm == NULL)
        return NULL;

    if (wc_LockMutex(&cm->caLock) != 0)
        return ret;

    signers = cm->caTable[row];
    while (signers) {
        byte* subjectHash;
        #ifndef NO_SKID
            subjectHash = signers->subjectKeyIdHash;
        #else
            subjectHash = signers->subjectNameHash;
        #endif
        if (XMEMCMP(hash, subjectHash, SIGNER_DIGEST_SIZE) == 0) {
            ret = signers;
            break;
        }
        signers = signers->next;
    }
    wc_UnLockMutex(&cm->caLock);

    return ret;
}


#ifndef NO_SKID
/* return CA if found, otherwise NULL. Walk through hash table. */
Signer* GetCAByName(void* vp, byte* hash)
{
    WOLFSSL_CERT_MANAGER* cm = (WOLFSSL_CERT_MANAGER*)vp;
    Signer* ret = NULL;
    Signer* signers;
    word32  row;

    if (cm == NULL)
        return NULL;

    if (wc_LockMutex(&cm->caLock) != 0)
        return ret;

    for (row = 0; row < CA_TABLE_SIZE && ret == NULL; row++) {
        signers = cm->caTable[row];
        while (signers && ret == NULL) {
            if (XMEMCMP(hash, signers->subjectNameHash,
                        SIGNER_DIGEST_SIZE) == 0) {
                ret = signers;
            }
            signers = signers->next;
        }
    }
    wc_UnLockMutex(&cm->caLock);

    return ret;
}
#endif


#ifdef WOLFSSL_TRUST_PEER_CERT
/* add a trusted peer cert to linked list */
int AddTrustedPeer(WOLFSSL_CERT_MANAGER* cm, DerBuffer** pDer, int verify)
{
    int ret, row;
    TrustedPeerCert* peerCert;
    DecodedCert* cert = NULL;
    DerBuffer*   der = *pDer;
    byte* subjectHash = NULL;

    WOLFSSL_MSG("Adding a Trusted Peer Cert");

    cert = (DecodedCert*)XMALLOC(sizeof(DecodedCert), cm->heap,
                                 DYNAMIC_TYPE_TMP_BUFFER);
    if (cert == NULL)
        return MEMORY_E;

    InitDecodedCert(cert, der->buffer, der->length, cm->heap);
    if ((ret = ParseCert(cert, TRUSTED_PEER_TYPE, verify, cm)) != 0) {
        XFREE(cert, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        return ret;
    }
    WOLFSSL_MSG("    Parsed new trusted peer cert");

    peerCert = (TrustedPeerCert*)XMALLOC(sizeof(TrustedPeerCert), cm->heap,
                                                             DYNAMIC_TYPE_CERT);
    if (peerCert == NULL) {
        FreeDecodedCert(cert);
        XFREE(cert, cm->heap, DYNAMIC_TYPE_TMP_BUFFER);
        return MEMORY_E;
    }
    XMEMSET(peerCert, 0, sizeof(TrustedPeerCert));

#ifndef NO_SKID
    if (cert->extAuthKeyIdSet) {
        subjectHash = cert->extSubjKeyId;
    }
    else {
        subjectHash = cert->subjectHash;
    }
#else
    subjectHash = cert->subjectHash;
#endif

    #ifndef IGNORE_NAME_CONSTRAINTS
        if (peerCert->permittedNames)
            FreeNameSubtrees(peerCert->permittedNames, cm->heap);
        if (peerCert->excludedNames)
            FreeNameSubtrees(peerCert->excludedNames, cm->heap);
    #endif

    if (AlreadyTrustedPeer(cm, subjectHash)) {
        WOLFSSL_MSG("    Already have this CA, not adding again");
        (void)ret;
    }
    else {
        /* add trusted peer signature */
        peerCert->sigLen = cert->sigLength;
        peerCert->sig = XMALLOC(cert->sigLength, cm->heap,
                                                        DYNAMIC_TYPE_SIGNATURE);
        if (peerCert->sig == NULL) {
            FreeDecodedCert(cert);
            XFREE(cert, cm->heap, DYNAMIC_TYPE_TMP_BUFFER);
            FreeTrustedPeer(peerCert, cm->heap);
            return MEMORY_E;
        }
        XMEMCPY(peerCert->sig, cert->signature, cert->sigLength);

        /* add trusted peer name */
        peerCert->nameLen = cert->subjectCNLen;
        peerCert->name    = cert->subjectCN;
        #ifndef IGNORE_NAME_CONSTRAINTS
            peerCert->permittedNames = cert->permittedNames;
            peerCert->excludedNames  = cert->excludedNames;
        #endif

        /* add SKID when available and hash of name */
        #ifndef NO_SKID
            XMEMCPY(peerCert->subjectKeyIdHash, cert->extSubjKeyId,
                    SIGNER_DIGEST_SIZE);
        #endif
            XMEMCPY(peerCert->subjectNameHash, cert->subjectHash,
                    SIGNER_DIGEST_SIZE);
            peerCert->next    = NULL; /* If Key Usage not set, all uses valid. */
            cert->subjectCN = 0;
        #ifndef IGNORE_NAME_CONSTRAINTS
            cert->permittedNames = NULL;
            cert->excludedNames = NULL;
        #endif

        #ifndef NO_SKID
            if (cert->extAuthKeyIdSet) {
                row = TrustedPeerHashSigner(peerCert->subjectKeyIdHash);
            }
            else {
                row = TrustedPeerHashSigner(peerCert->subjectNameHash);
            }
        #else
            row = TrustedPeerHashSigner(peerCert->subjectNameHash);
        #endif

            if (wc_LockMutex(&cm->tpLock) == 0) {
                peerCert->next = cm->tpTable[row];
                cm->tpTable[row] = peerCert;   /* takes ownership */
                wc_UnLockMutex(&cm->tpLock);
            }
            else {
                WOLFSSL_MSG("    Trusted Peer Cert Mutex Lock failed");
                FreeDecodedCert(cert);
                XFREE(cert, cm->heap, DYNAMIC_TYPE_TMP_BUFFER);
                FreeTrustedPeer(peerCert, cm->heap);
                return BAD_MUTEX_E;
            }
        }

    WOLFSSL_MSG("    Freeing parsed trusted peer cert");
    FreeDecodedCert(cert);
    XFREE(cert, cm->heap, DYNAMIC_TYPE_TMP_BUFFER);
    WOLFSSL_MSG("    Freeing der trusted peer cert");
    FreeDer(&der);
    WOLFSSL_MSG("        OK Freeing der trusted peer cert");
    WOLFSSL_LEAVE("AddTrustedPeer", ret);

    return SSL_SUCCESS;
}
#endif /* WOLFSSL_TRUST_PEER_CERT */


/* owns der, internal now uses too */
/* type flag ids from user or from chain received during verify
   don't allow chain ones to be added w/o isCA extension */
int AddCA(WOLFSSL_CERT_MANAGER* cm, DerBuffer** pDer, int type, int verify)
{
    int         ret;
    Signer*     signer = 0;
    word32      row;
    byte*       subjectHash;
#ifdef WOLFSSL_SMALL_STACK
    DecodedCert* cert = NULL;
#else
    DecodedCert  cert[1];
#endif
    DerBuffer*   der = *pDer;

    WOLFSSL_MSG("Adding a CA");

#ifdef WOLFSSL_SMALL_STACK
    cert = (DecodedCert*)XMALLOC(sizeof(DecodedCert), NULL,
                                 DYNAMIC_TYPE_TMP_BUFFER);
    if (cert == NULL)
        return MEMORY_E;
#endif

    InitDecodedCert(cert, der->buffer, der->length, cm->heap);
    ret = ParseCert(cert, CA_TYPE, verify, cm);
    WOLFSSL_MSG("    Parsed new CA");

#ifndef NO_SKID
    subjectHash = cert->extSubjKeyId;
#else
    subjectHash = cert->subjectHash;
#endif

    /* check CA key size */
    if (verify) {
        switch (cert->keyOID) {
            #ifndef NO_RSA
            case RSAk:
                if (cm->minRsaKeySz < 0 ||
                                   cert->pubKeySize < (word16)cm->minRsaKeySz) {
                    ret = RSA_KEY_SIZE_E;
                    WOLFSSL_MSG("    CA RSA key size error");
                }
                break;
            #endif /* !NO_RSA */
            #ifdef HAVE_ECC
            case ECDSAk:
                if (cm->minEccKeySz < 0 ||
                                   cert->pubKeySize < (word16)cm->minEccKeySz) {
                    ret = ECC_KEY_SIZE_E;
                    WOLFSSL_MSG("    CA ECC key size error");
                }
                break;
            #endif /* HAVE_ECC */

            default:
                WOLFSSL_MSG("    No key size check done on CA");
                break; /* no size check if key type is not in switch */
        }
    }

    if (ret == 0 && cert->isCA == 0 && type != WOLFSSL_USER_CA) {
        WOLFSSL_MSG("    Can't add as CA if not actually one");
        ret = NOT_CA_ERROR;
    }
#ifndef ALLOW_INVALID_CERTSIGN
    else if (ret == 0 && cert->isCA == 1 && type != WOLFSSL_USER_CA &&
             (cert->extKeyUsage & KEYUSE_KEY_CERT_SIGN) == 0) {
        /* Intermediate CA certs are required to have the keyCertSign
        * extension set. User loaded root certs are not. */
        WOLFSSL_MSG("    Doesn't have key usage certificate signing");
        ret = NOT_CA_ERROR;
    }
#endif
    else if (ret == 0 && AlreadySigner(cm, subjectHash)) {
        WOLFSSL_MSG("    Already have this CA, not adding again");
        (void)ret;
    }
    else if (ret == 0) {
        /* take over signer parts */
        signer = MakeSigner(cm->heap);
        if (!signer)
            ret = MEMORY_ERROR;
        else {
            signer->keyOID         = cert->keyOID;
            signer->publicKey      = cert->publicKey;
            signer->pubKeySize     = cert->pubKeySize;
            signer->nameLen        = cert->subjectCNLen;
            signer->name           = cert->subjectCN;
            signer->pathLength     = cert->pathLength;
            signer->pathLengthSet  = cert->pathLengthSet;
        #ifndef IGNORE_NAME_CONSTRAINTS
            signer->permittedNames = cert->permittedNames;
            signer->excludedNames  = cert->excludedNames;
        #endif
        #ifndef NO_SKID
            XMEMCPY(signer->subjectKeyIdHash, cert->extSubjKeyId,
                    SIGNER_DIGEST_SIZE);
        #endif
            XMEMCPY(signer->subjectNameHash, cert->subjectHash,
                    SIGNER_DIGEST_SIZE);
            signer->keyUsage = cert->extKeyUsageSet ? cert->extKeyUsage
                                                    : 0xFFFF;
            signer->next    = NULL; /* If Key Usage not set, all uses valid. */
            cert->publicKey = 0;    /* in case lock fails don't free here.   */
            cert->subjectCN = 0;
        #ifndef IGNORE_NAME_CONSTRAINTS
            cert->permittedNames = NULL;
            cert->excludedNames = NULL;
        #endif

        #ifndef NO_SKID
            row = HashSigner(signer->subjectKeyIdHash);
        #else
            row = HashSigner(signer->subjectNameHash);
        #endif

            if (wc_LockMutex(&cm->caLock) == 0) {
                signer->next = cm->caTable[row];
                cm->caTable[row] = signer;   /* takes ownership */
                wc_UnLockMutex(&cm->caLock);
                if (cm->caCacheCallback)
                    cm->caCacheCallback(der->buffer, (int)der->length, type);
            }
            else {
                WOLFSSL_MSG("    CA Mutex Lock failed");
                ret = BAD_MUTEX_E;
                FreeSigner(signer, cm->heap);
            }
        }
    }

    WOLFSSL_MSG("    Freeing Parsed CA");
    FreeDecodedCert(cert);
#ifdef WOLFSSL_SMALL_STACK
    XFREE(cert, NULL, DYNAMIC_TYPE_TMP_BUFFER);
#endif
    WOLFSSL_MSG("    Freeing der CA");
    FreeDer(pDer);
    WOLFSSL_MSG("        OK Freeing der CA");

    WOLFSSL_LEAVE("AddCA", ret);

    return ret == 0 ? SSL_SUCCESS : ret;
}

#endif /* !NO_CERTS */


#ifndef NO_SESSION_CACHE

    /* basic config gives a cache with 33 sessions, adequate for clients and
       embedded servers

       MEDIUM_SESSION_CACHE allows 1055 sessions, adequate for servers that
       aren't under heavy load, basically allows 200 new sessions per minute

       BIG_SESSION_CACHE yields 20,027 sessions

       HUGE_SESSION_CACHE yields 65,791 sessions, for servers under heavy load,
       allows over 13,000 new sessions per minute or over 200 new sessions per
       second

       SMALL_SESSION_CACHE only stores 6 sessions, good for embedded clients
       or systems where the default of nearly 3kB is too much RAM, this define
       uses less than 500 bytes RAM

       default SESSION_CACHE stores 33 sessions (no XXX_SESSION_CACHE defined)
    */
    #ifdef HUGE_SESSION_CACHE
        #define SESSIONS_PER_ROW 11
        #define SESSION_ROWS 5981
    #elif defined(BIG_SESSION_CACHE)
        #define SESSIONS_PER_ROW 7
        #define SESSION_ROWS 2861
    #elif defined(MEDIUM_SESSION_CACHE)
        #define SESSIONS_PER_ROW 5
        #define SESSION_ROWS 211
    #elif defined(SMALL_SESSION_CACHE)
        #define SESSIONS_PER_ROW 2
        #define SESSION_ROWS 3
    #else
        #define SESSIONS_PER_ROW 3
        #define SESSION_ROWS 11
    #endif

    typedef struct SessionRow {
        int nextIdx;                           /* where to place next one   */
        int totalCount;                        /* sessions ever on this row */
        WOLFSSL_SESSION Sessions[SESSIONS_PER_ROW];
    } SessionRow;

    static SessionRow SessionCache[SESSION_ROWS];

    #if defined(WOLFSSL_SESSION_STATS) && defined(WOLFSSL_PEAK_SESSIONS)
        static word32 PeakSessions;
    #endif

    static wolfSSL_Mutex session_mutex;   /* SessionCache mutex */

    #ifndef NO_CLIENT_CACHE

        typedef struct ClientSession {
            word16 serverRow;            /* SessionCache Row id */
            word16 serverIdx;            /* SessionCache Idx (column) */
        } ClientSession;

        typedef struct ClientRow {
            int nextIdx;                /* where to place next one   */
            int totalCount;             /* sessions ever on this row */
            ClientSession Clients[SESSIONS_PER_ROW];
        } ClientRow;

        static ClientRow ClientCache[SESSION_ROWS];  /* Client Cache */
                                                     /* uses session mutex */
    #endif  /* NO_CLIENT_CACHE */

#endif /* NO_SESSION_CACHE */

int wolfSSL_Init(void)
{
    WOLFSSL_ENTER("wolfSSL_Init");

    if (initRefCount == 0) {
        /* Initialize crypto for use with TLS connection */
        if (wolfCrypt_Init() != 0) {
            WOLFSSL_MSG("Bad wolfCrypt Init");
            return WC_INIT_E;
        }
#ifndef NO_SESSION_CACHE
        if (wc_InitMutex(&session_mutex) != 0) {
            WOLFSSL_MSG("Bad Init Mutex session");
            return BAD_MUTEX_E;
        }
#endif
        if (wc_InitMutex(&count_mutex) != 0) {
            WOLFSSL_MSG("Bad Init Mutex count");
            return BAD_MUTEX_E;
        }
    }

    if (wc_LockMutex(&count_mutex) != 0) {
        WOLFSSL_MSG("Bad Lock Mutex count");
        return BAD_MUTEX_E;
    }

    initRefCount++;
    wc_UnLockMutex(&count_mutex);

    return SSL_SUCCESS;
}


#if (defined(OPENSSL_EXTRA) || defined(HAVE_WEBSERVER)) && !defined(NO_CERTS)

/* SSL_SUCCESS if ok, <= 0 else */
static int wolfssl_decrypt_buffer_key(DerBuffer* der, byte* password,
                                      int passwordSz, EncryptedInfo* info)
{
    int ret = SSL_BAD_FILE;

#ifdef WOLFSSL_SMALL_STACK
    byte* key      = NULL;
#else
    byte  key[AES_256_KEY_SIZE];
#endif

    (void)passwordSz;
    (void)key;

    WOLFSSL_ENTER("wolfssl_decrypt_buffer_key");

    if (der == NULL || password == NULL || info == NULL) {
        WOLFSSL_MSG("bad arguments");
        return SSL_FATAL_ERROR;
    }

    /* use file's salt for key derivation, hex decode first */
    if (Base16_Decode(info->iv, info->ivSz, info->iv, &info->ivSz) != 0) {
        WOLFSSL_MSG("base16 decode failed");
        return SSL_FATAL_ERROR;
    }

#ifndef NO_MD5

#ifdef WOLFSSL_SMALL_STACK
    key = (byte*)XMALLOC(AES_256_KEY_SIZE, NULL, DYNAMIC_TYPE_TMP_BUFFER);
    if (key == NULL) {
        WOLFSSL_MSG("memory failure");
        return SSL_FATAL_ERROR;
    }
#endif /* WOLFSSL_SMALL_STACK */

    if ((ret = wolfSSL_EVP_BytesToKey(info->name, "MD5", info->iv,
                              password, passwordSz, 1, key, NULL)) <= 0) {
        WOLFSSL_MSG("bytes to key failure");
#ifdef WOLFSSL_SMALL_STACK
        XFREE(key, NULL, DYNAMIC_TYPE_TMP_BUFFER);
#endif
        return SSL_FATAL_ERROR;
    }

#endif /* NO_MD5 */

#ifndef NO_DES3
    if (XSTRNCMP(info->name, EVP_DES_CBC, EVP_DES_SIZE) == 0)
        ret = wc_Des_CbcDecryptWithKey(der->buffer, der->buffer, der->length,
                                       key, info->iv);
    else if (XSTRNCMP(info->name, EVP_DES_EDE3_CBC, EVP_DES_EDE3_SIZE) == 0)
        ret = wc_Des3_CbcDecryptWithKey(der->buffer, der->buffer, der->length,
                                        key, info->iv);
#endif /* NO_DES3 */
#if !defined(NO_AES) && defined(HAVE_AES_CBC) && defined(HAVE_AES_DECRYPT)
    if (XSTRNCMP(info->name, EVP_AES_128_CBC, EVP_AES_SIZE) == 0)
        ret = wc_AesCbcDecryptWithKey(der->buffer, der->buffer, der->length,
                                      key, AES_128_KEY_SIZE, info->iv);
    else if (XSTRNCMP(info->name, EVP_AES_192_CBC, EVP_AES_SIZE) == 0)
        ret = wc_AesCbcDecryptWithKey(der->buffer, der->buffer, der->length,
                                      key, AES_192_KEY_SIZE, info->iv);
    else if (XSTRNCMP(info->name, EVP_AES_256_CBC, EVP_AES_SIZE) == 0)
        ret = wc_AesCbcDecryptWithKey(der->buffer, der->buffer, der->length,
                                      key, AES_256_KEY_SIZE, info->iv);
#endif /* !NO_AES && HAVE_AES_CBC && HAVE_AES_DECRYPT */

#ifdef WOLFSSL_SMALL_STACK
    XFREE(key, NULL, DYNAMIC_TYPE_TMP_BUFFER);
#endif

    if (ret == MP_OKAY)
        return SSL_SUCCESS;
    else if (ret == SSL_BAD_FILE)
        return SSL_BAD_FILE;

    return SSL_FATAL_ERROR;
}
#endif /* defined(OPENSSL_EXTRA) || defined(HAVE_WEBSERVER) */


#if defined(WOLFSSL_KEY_GEN) && defined(OPENSSL_EXTRA)
static int wolfssl_encrypt_buffer_key(byte* der, word32 derSz, byte* password,
                                      int passwordSz, EncryptedInfo* info)
{
    int ret = SSL_BAD_FILE;

#ifdef WOLFSSL_SMALL_STACK
    byte* key      = NULL;
#else
    byte  key[AES_256_KEY_SIZE];
#endif

    (void)derSz;
    (void)passwordSz;
    (void)key;

    WOLFSSL_ENTER("wolfssl_encrypt_buffer_key");

    if (der == NULL || password == NULL || info == NULL || info->ivSz == 0) {
        WOLFSSL_MSG("bad arguments");
        return SSL_FATAL_ERROR;
    }

#ifndef NO_MD5

#ifdef WOLFSSL_SMALL_STACK
    key = (byte*)XMALLOC(AES_256_KEY_SIZE, NULL, DYNAMIC_TYPE_TMP_BUFFER);
    if (key == NULL) {
        WOLFSSL_MSG("memory failure");
        return SSL_FATAL_ERROR;
    }
#endif /* WOLFSSL_SMALL_STACK */

    if ((ret = wolfSSL_EVP_BytesToKey(info->name, "MD5", info->iv,
                              password, passwordSz, 1, key, NULL)) <= 0) {
        WOLFSSL_MSG("bytes to key failure");
#ifdef WOLFSSL_SMALL_STACK
        XFREE(key, NULL, DYNAMIC_TYPE_TMP_BUFFER);
#endif
        return SSL_FATAL_ERROR;
    }

#endif /* NO_MD5 */

    if (ret > 0) {
        ret = SSL_BAD_FILE; /* Reset error return */
#ifndef NO_DES3
        if (XSTRNCMP(info->name, EVP_DES_CBC, EVP_DES_SIZE) == 0)
            ret = wc_Des_CbcEncryptWithKey(der, der, derSz, key, info->iv);
        else if (XSTRNCMP(info->name, EVP_DES_EDE3_CBC, EVP_DES_EDE3_SIZE) == 0)
            ret = wc_Des3_CbcEncryptWithKey(der, der, derSz, key, info->iv);
#endif /* NO_DES3 */
#ifndef NO_AES
        if (XSTRNCMP(info->name, EVP_AES_128_CBC, EVP_AES_SIZE) == 0)
            ret = wc_AesCbcEncryptWithKey(der, der, derSz,
                                          key, AES_128_KEY_SIZE, info->iv);
        else if (XSTRNCMP(info->name, EVP_AES_192_CBC, EVP_AES_SIZE) == 0)
            ret = wc_AesCbcEncryptWithKey(der, der, derSz,
                                          key, AES_192_KEY_SIZE, info->iv);
        else if (XSTRNCMP(info->name, EVP_AES_256_CBC, EVP_AES_SIZE) == 0)
            ret = wc_AesCbcEncryptWithKey(der, der, derSz,
                                          key, AES_256_KEY_SIZE, info->iv);
#endif /* NO_AES */
    }

#ifdef WOLFSSL_SMALL_STACK
    XFREE(key, NULL, DYNAMIC_TYPE_TMP_BUFFER);
#endif

    if (ret == MP_OKAY)
        return SSL_SUCCESS;
    else if (ret == SSL_BAD_FILE)
        return SSL_BAD_FILE;

    return SSL_FATAL_ERROR;
}
#endif /* defined(WOLFSSL_KEY_GEN) */


#ifndef NO_CERTS

/* Remove PEM header/footer, convert to ASN1, store any encrypted data
   info->consumed tracks of PEM bytes consumed in case multiple parts */
int PemToDer(const unsigned char* buff, long longSz, int type,
              DerBuffer** pDer, void* heap, EncryptedInfo* info, int* eccKey)
{
    const char* header      = NULL;
    const char* footer      = NULL;
    char*       headerEnd;
    char*       footerEnd;
    char*       consumedEnd;
    char*       bufferEnd   = (char*)(buff + longSz);
    long        neededSz;
    int         ret         = 0;
    int         sz          = (int)longSz;
    int         encrypted_key = 0;
    DerBuffer*  der;

    WOLFSSL_ENTER("PemToDer");

    switch (type) {
        case CA_TYPE:       /* same as below */
        case TRUSTED_PEER_TYPE:
        case CERT_TYPE:      header=BEGIN_CERT;     footer=END_CERT;     break;
        case CRL_TYPE:       header=BEGIN_X509_CRL; footer=END_X509_CRL; break;
        case DH_PARAM_TYPE:  header=BEGIN_DH_PARAM; footer=END_DH_PARAM; break;
        case DSA_PARAM_TYPE: header=BEGIN_DSA_PARAM; footer=END_DSA_PARAM; break;
        case CERTREQ_TYPE:   header=BEGIN_CERT_REQ; footer=END_CERT_REQ; break;
        case DSA_TYPE:       header=BEGIN_DSA_PRIV; footer=END_DSA_PRIV; break;
        case ECC_TYPE:       header=BEGIN_EC_PRIV;  footer=END_EC_PRIV;  break;
        case RSA_TYPE:       header=BEGIN_RSA_PRIV; footer=END_RSA_PRIV; break;
        case PUBLICKEY_TYPE: header=BEGIN_PUB_KEY;  footer=END_PUB_KEY;  break;
        default:             header=BEGIN_RSA_PRIV; footer=END_RSA_PRIV; break;
    }

    /* find header */
    for (;;) {
        headerEnd = XSTRNSTR((char*)buff, header, sz);

        if (headerEnd || type != PRIVATEKEY_TYPE) {
            break;
        } else if (header == BEGIN_RSA_PRIV) {
                   header =  BEGIN_PRIV_KEY;       footer = END_PRIV_KEY;
        } else if (header == BEGIN_PRIV_KEY) {
                   header =  BEGIN_ENC_PRIV_KEY;   footer = END_ENC_PRIV_KEY;
        } else if (header == BEGIN_ENC_PRIV_KEY) {
                   header =  BEGIN_EC_PRIV;        footer = END_EC_PRIV;
        } else if (header == BEGIN_EC_PRIV) {
                   header =  BEGIN_DSA_PRIV;       footer = END_DSA_PRIV;
        } else
            break;
    }

    if (!headerEnd) {
        WOLFSSL_MSG("Couldn't find PEM header");
        return SSL_NO_PEM_HEADER;
    }

    headerEnd += XSTRLEN(header);

    if ((headerEnd + 1) >= bufferEnd)
        return SSL_BAD_FILE;

    /* eat end of line */
    if (headerEnd[0] == '\n')
        headerEnd++;
    else if (headerEnd[1] == '\n')
        headerEnd += 2;
    else {
        if (info)
            info->consumed = (long)(headerEnd+2 - (char*)buff);
        return SSL_BAD_FILE;
    }

    if (type == PRIVATEKEY_TYPE) {
        if (eccKey)
            *eccKey = header == BEGIN_EC_PRIV;
    }

#if defined(OPENSSL_EXTRA) || defined(HAVE_WEBSERVER)
    {
        /* remove encrypted header if there */
        char encHeader[] = "Proc-Type";
        char* line = XSTRNSTR(headerEnd, encHeader, PEM_LINE_LEN);
        if (line) {
            char* newline;
            char* finish;
            char* start  = XSTRNSTR(line, "DES", PEM_LINE_LEN);

            if (!start)
                start = XSTRNSTR(line, "AES", PEM_LINE_LEN);

            if (!start) return SSL_BAD_FILE;
            if (!info)  return SSL_BAD_FILE;

            finish = XSTRNSTR(start, ",", PEM_LINE_LEN);

            if (start && finish && (start < finish)) {
                newline = XSTRNSTR(finish, "\r", PEM_LINE_LEN);

                if (XMEMCPY(info->name, start, finish - start) == NULL)
                    return SSL_FATAL_ERROR;
                info->name[finish - start] = 0;
                if (XMEMCPY(info->iv, finish + 1, sizeof(info->iv)) == NULL)
                    return SSL_FATAL_ERROR;

                if (!newline) newline = XSTRNSTR(finish, "\n", PEM_LINE_LEN);
                if (newline && (newline > finish)) {
                    info->ivSz = (word32)(newline - (finish + 1));
                    info->set = 1;
                }
                else
                    return SSL_BAD_FILE;
            }
            else
                return SSL_BAD_FILE;

            /* eat blank line */
            while (*newline == '\r' || *newline == '\n')
                newline++;
            headerEnd = newline;

            encrypted_key = 1;
        }
    }
#endif /* OPENSSL_EXTRA || HAVE_WEBSERVER */

    /* find footer */
    footerEnd = XSTRNSTR((char*)buff, footer, sz);
    if (!footerEnd) {
        if (info)
            info->consumed = longSz; /* No more certs if no footer */
        return SSL_BAD_FILE;
    }

    consumedEnd = footerEnd + XSTRLEN(footer);

    if (consumedEnd < bufferEnd) {  /* handle no end of line on last line */
        /* eat end of line */
        if (consumedEnd[0] == '\n')
            consumedEnd++;
        else if ((consumedEnd + 1 < bufferEnd) && consumedEnd[1] == '\n')
            consumedEnd += 2;
        else {
            if (info)
                info->consumed = (long)(consumedEnd+2 - (char*)buff);
            return SSL_BAD_FILE;
        }
    }

    if (info)
        info->consumed = (long)(consumedEnd - (char*)buff);

    /* set up der buffer */
    neededSz = (long)(footerEnd - headerEnd);
    if (neededSz > sz || neededSz <= 0)
        return SSL_BAD_FILE;

    ret = AllocDer(pDer, (word32)neededSz, type, heap);
    if (ret < 0) {
        return ret;
    }
    der = *pDer;

    if (Base64_Decode((byte*)headerEnd, (word32)neededSz,
                      der->buffer, &der->length) < 0)
        return SSL_BAD_FILE;

    if (header == BEGIN_PRIV_KEY && !encrypted_key) {
        /* pkcs8 key, convert and adjust length */
        if ((ret = ToTraditional(der->buffer, der->length)) < 0)
            return ret;

        der->length = ret;
        return 0;
    }

#if (defined(OPENSSL_EXTRA) || defined(HAVE_WEBSERVER)) && !defined(NO_PWDBASED)
    if (encrypted_key || header == BEGIN_ENC_PRIV_KEY) {
        int   passwordSz;
    #ifdef WOLFSSL_SMALL_STACK
        char* password = NULL;
    #else
        char  password[80];
    #endif

        if (!info || !info->ctx || !info->ctx->passwd_cb)
            return SSL_BAD_FILE;  /* no callback error */

    #ifdef WOLFSSL_SMALL_STACK
        password = (char*)XMALLOC(80, heap, DYNAMIC_TYPE_TMP_BUFFER);
        if (password == NULL)
            return MEMORY_E;
    #endif
        passwordSz = info->ctx->passwd_cb(password, sizeof(password), 0,
                                          info->ctx->userdata);
        /* convert and adjust length */
        if (header == BEGIN_ENC_PRIV_KEY) {
            ret = ToTraditionalEnc(der->buffer, der->length,
                                   password, passwordSz);
    #ifdef WOLFSSL_SMALL_STACK
            XFREE(password, NULL, DYNAMIC_TYPE_TMP_BUFFER);
    #endif
            if (ret < 0) {
                return ret;
            }

            der->length = ret;
        }
        /* decrypt the key */
        else {
            ret = wolfssl_decrypt_buffer_key(der, (byte*)password,
                                             passwordSz, info);
    #ifdef WOLFSSL_SMALL_STACK
            XFREE(password, heap, DYNAMIC_TYPE_TMP_BUFFER);
    #endif
            if (ret != SSL_SUCCESS) {
                return ret;
            }
        }
    }
#endif  /* OPENSSL_EXTRA || HAVE_WEBSERVER || NO_PWDBASED */

    return 0;
}



/* process user cert chain to pass during the handshake */
static int ProcessUserChain(WOLFSSL_CTX* ctx, const unsigned char* buff,
                         long sz, int format, int type, WOLFSSL* ssl,
                         long* used, EncryptedInfo* info)
{
    int ret = 0;
    void* heap = ctx ? ctx->heap : ((ssl) ? ssl->heap : NULL);

    /* we may have a user cert chain, try to consume */
    if (type == CERT_TYPE && info->consumed < sz) {
    #ifdef WOLFSSL_SMALL_STACK
        byte   staticBuffer[1];                 /* force heap usage */
    #else
        byte   staticBuffer[FILE_BUFFER_SIZE];  /* tmp chain buffer */
    #endif
        byte*  chainBuffer = staticBuffer;
        int    dynamicBuffer = 0;
        word32 bufferSz = sizeof(staticBuffer);
        long   consumed = info->consumed;
        word32 idx = 0;
        int    gotOne = 0;

        if ( (sz - consumed) > (int)bufferSz) {
            WOLFSSL_MSG("Growing Tmp Chain Buffer");
            bufferSz = (word32)(sz - consumed);
                       /* will shrink to actual size */
            chainBuffer = (byte*)XMALLOC(bufferSz, heap, DYNAMIC_TYPE_FILE);
            if (chainBuffer == NULL) {
                return MEMORY_E;
            }
            dynamicBuffer = 1;
        }

        WOLFSSL_MSG("Processing Cert Chain");
        while (consumed < sz) {
            int eccKey = 0;
            DerBuffer* part = NULL;
            word32 remain = (word32)(sz - consumed);
            info->consumed = 0;

            if (format == SSL_FILETYPE_PEM) {
                ret = PemToDer(buff + consumed, remain, type, &part,
                               heap, info, &eccKey);
            }
            else {
                int length = remain;
                if (format == SSL_FILETYPE_ASN1) {
                    /* get length of der (read sequence) */
                    word32 inOutIdx = 0;
                    if (GetSequence(buff + consumed, &inOutIdx, &length, remain) < 0) {
                        ret = SSL_NO_PEM_HEADER;
                    }
                    length += inOutIdx; /* include leading squence */
                }
                info->consumed = length;
                if (ret == 0) {
                    ret = AllocDer(&part, length, type, heap);
                    if (ret == 0) {
                        XMEMCPY(part->buffer, buff + consumed, length);
                    }
                }
            }
            if (ret == 0) {
                gotOne = 1;
                if ((idx + part->length) > bufferSz) {
                    WOLFSSL_MSG("   Cert Chain bigger than buffer");
                    ret = BUFFER_E;
                }
                else {
                    c32to24(part->length, &chainBuffer[idx]);
                    idx += CERT_HEADER_SZ;
                    XMEMCPY(&chainBuffer[idx], part->buffer, part->length);
                    idx += part->length;
                    consumed  += info->consumed;
                    if (used)
                        *used += info->consumed;
                }
            }
            FreeDer(&part);

            if (ret == SSL_NO_PEM_HEADER && gotOne) {
                WOLFSSL_MSG("We got one good cert, so stuff at end ok");
                break;
            }

            if (ret < 0) {
                WOLFSSL_MSG("   Error in Cert in Chain");
                if (dynamicBuffer)
                    XFREE(chainBuffer, heap, DYNAMIC_TYPE_FILE);
                return ret;
            }
            WOLFSSL_MSG("   Consumed another Cert in Chain");
        }
        WOLFSSL_MSG("Finished Processing Cert Chain");

        /* only retain actual size used */
        ret = 0;
        if (idx > 0) {
            if (ssl) {
                if (ssl->buffers.weOwnCertChain) {
                    FreeDer(&ssl->buffers.certChain);
                }
                ret = AllocDer(&ssl->buffers.certChain, idx, type, heap);
                if (ret == 0) {
                    XMEMCPY(ssl->buffers.certChain->buffer, chainBuffer, idx);
                    ssl->buffers.weOwnCertChain = 1;
                }
            } else if (ctx) {
                FreeDer(&ctx->certChain);
                ret = AllocDer(&ctx->certChain, idx, type, heap);
                if (ret == 0) {
                    XMEMCPY(ctx->certChain->buffer, chainBuffer, idx);
                }
            }
        }

        if (dynamicBuffer)
            XFREE(chainBuffer, heap, DYNAMIC_TYPE_FILE);
    }

    return ret;
}
/* process the buffer buff, length sz, into ctx of format and type
   used tracks bytes consumed, userChain specifies a user cert chain
   to pass during the handshake */
int ProcessBuffer(WOLFSSL_CTX* ctx, const unsigned char* buff,
                         long sz, int format, int type, WOLFSSL* ssl,
                         long* used, int userChain)
{
    DerBuffer*    der = NULL;        /* holds DER or RAW (for NTRU) */
    int           ret = 0;
    int           eccKey = 0;
    int           rsaKey = 0;
    void*         heap = ctx ? ctx->heap : ((ssl) ? ssl->heap : NULL);
#ifdef WOLFSSL_SMALL_STACK
    EncryptedInfo* info = NULL;
#else
    EncryptedInfo  info[1];
#endif

    (void)rsaKey;

    if (used)
        *used = sz;     /* used bytes default to sz, PEM chain may shorten*/

    /* check args */
    if (format != SSL_FILETYPE_ASN1 && format != SSL_FILETYPE_PEM
                                    && format != SSL_FILETYPE_RAW)
        return SSL_BAD_FILETYPE;

    if (ctx == NULL && ssl == NULL)
        return BAD_FUNC_ARG;

#ifdef WOLFSSL_SMALL_STACK
    info = (EncryptedInfo*)XMALLOC(sizeof(EncryptedInfo), heap,
                                   DYNAMIC_TYPE_TMP_BUFFER);
    if (info == NULL)
        return MEMORY_E;
#endif

    info->set      = 0;
    info->ctx      = ctx;
    info->consumed = 0;

    if (format == SSL_FILETYPE_PEM) {
        ret = PemToDer(buff, sz, type, &der, heap, info, &eccKey);
    }
    else {  /* ASN1 (DER) or RAW (NTRU) */
        int length = (int)sz;
        if (format == SSL_FILETYPE_ASN1) {
            /* get length of der (read sequence) */
            word32 inOutIdx = 0;
            if (GetSequence(buff, &inOutIdx, &length, (word32)sz) < 0) {
                ret = ASN_PARSE_E;
            }
            length += inOutIdx; /* include leading squence */
        }
        info->consumed = length;
        if (ret == 0) {
            ret = AllocDer(&der, (word32)length, type, heap);
            if (ret == 0) {
                XMEMCPY(der->buffer, buff, length);
            }
        }
    }

    if (used) {
        *used = info->consumed;
    }

    /* process user chain */
    if (ret >= 0) {
        if (userChain) {
            ret = ProcessUserChain(ctx, buff, sz, format, type, ssl, used, info);
        }
    }

    /* check for error */
    if (ret < 0) {
    #ifdef WOLFSSL_SMALL_STACK
        XFREE(info, heap, DYNAMIC_TYPE_TMP_BUFFER);
    #endif
        FreeDer(&der);
        return ret;
    }

#if defined(OPENSSL_EXTRA) || defined(HAVE_WEBSERVER)
    /* for SSL_FILETYPE_PEM, PemToDer manage the decryption if required */
    if (info->set && (format != SSL_FILETYPE_PEM)) {
        /* decrypt */
        int   passwordSz;
#ifdef WOLFSSL_SMALL_STACK
        char* password = NULL;
#else
        char  password[80];
#endif

    #ifdef WOLFSSL_SMALL_STACK
        password = (char*)XMALLOC(80, heap, DYNAMIC_TYPE_TMP_BUFFER);
        if (password == NULL)
            ret = MEMORY_E;
        else
    #endif
        if (!ctx || !ctx->passwd_cb) {
            ret = NO_PASSWORD;
        }
        else {
            passwordSz = ctx->passwd_cb(password, sizeof(password),
                                        0, ctx->userdata);

            /* decrypt the key */
            ret = wolfssl_decrypt_buffer_key(der, (byte*)password,
                                             passwordSz, info);
        }

    #ifdef WOLFSSL_SMALL_STACK
        XFREE(password, heap, DYNAMIC_TYPE_TMP_BUFFER);
    #endif

        if (ret != SSL_SUCCESS) {
        #ifdef WOLFSSL_SMALL_STACK
            XFREE(info, heap, DYNAMIC_TYPE_TMP_BUFFER);
        #endif
            FreeDer(&der);
            return ret;
        }
    }
#endif /* OPENSSL_EXTRA || HAVE_WEBSERVER */

#ifdef WOLFSSL_SMALL_STACK
    XFREE(info, heap, DYNAMIC_TYPE_TMP_BUFFER);
#endif

    /* Handle DER owner */
    if (type == CA_TYPE) {
        if (ctx == NULL) {
            WOLFSSL_MSG("Need context for CA load");
            FreeDer(&der);
            return BAD_FUNC_ARG;
        }
        /* verify CA unless user set to no verify */
        return AddCA(ctx->cm, &der, WOLFSSL_USER_CA, !ctx->verifyNone);
    }
#ifdef WOLFSSL_TRUST_PEER_CERT
    else if (type == TRUSTED_PEER_TYPE) {
        if (ctx == NULL) {
            WOLFSSL_MSG("Need context for trusted peer cert load");
            FreeDer(&der);
            return BAD_FUNC_ARG;
        }
        /* add trusted peer cert */
        return AddTrustedPeer(ctx->cm, &der, !ctx->verifyNone);
    }
#endif /* WOLFSSL_TRUST_PEER_CERT */
    else if (type == CERT_TYPE) {
        if (ssl) {
             /* Make sure previous is free'd */
            if (ssl->buffers.weOwnCert) {
                FreeDer(&ssl->buffers.certificate);
                #ifdef KEEP_OUR_CERT
                    FreeX509(ssl->ourCert);
                    if (ssl->ourCert) {
                        XFREE(ssl->ourCert, ssl->heap, DYNAMIC_TYPE_X509);
                        ssl->ourCert = NULL;
                    }
                #endif
            }
            ssl->buffers.certificate = der;
            #ifdef KEEP_OUR_CERT
                ssl->keepCert = 1; /* hold cert for ssl lifetime */
            #endif
            ssl->buffers.weOwnCert = 1;
        }
        else if (ctx) {
            FreeDer(&ctx->certificate); /* Make sure previous is free'd */
        #ifdef KEEP_OUR_CERT
            FreeX509(ctx->ourCert);
            if (ctx->ourCert) {
                XFREE(ctx->ourCert, ctx->heap, DYNAMIC_TYPE_X509);
                ctx->ourCert = NULL;
            }
        #endif
            ctx->certificate = der;
        }
    }
    else if (type == PRIVATEKEY_TYPE) {
        if (ssl) {
             /* Make sure previous is free'd */
            if (ssl->buffers.weOwnKey) {
                FreeDer(&ssl->buffers.key);
            }
            ssl->buffers.key = der;
            ssl->buffers.weOwnKey = 1;
        }
        else if (ctx) {
            FreeDer(&ctx->privateKey);
            ctx->privateKey = der;
        }
    }
    else {
        FreeDer(&der);
        return SSL_BAD_CERTTYPE;
    }

    if (type == PRIVATEKEY_TYPE && format != SSL_FILETYPE_RAW) {
    #ifndef NO_RSA
        if (!eccKey) {
            /* make sure RSA key can be used */
            word32 idx = 0;
        #ifdef WOLFSSL_SMALL_STACK
            RsaKey* key = NULL;
        #else
            RsaKey  key[1];
        #endif

        #ifdef WOLFSSL_SMALL_STACK
            key = (RsaKey*)XMALLOC(sizeof(RsaKey), heap,
                                   DYNAMIC_TYPE_TMP_BUFFER);
            if (key == NULL)
                return MEMORY_E;
        #endif

            ret = wc_InitRsaKey(key, 0);
            if (ret == 0) {
                if (wc_RsaPrivateKeyDecode(der->buffer, &idx, key, der->length)
                    != 0) {
                #ifdef HAVE_ECC
                    /* could have DER ECC (or pkcs8 ecc), no easy way to tell */
                    eccKey = 1;  /* so try it out */
                #endif
                    if (!eccKey)
                        ret = SSL_BAD_FILE;
                } else {
                    /* check that the size of the RSA key is enough */
                    int RsaSz = wc_RsaEncryptSize((RsaKey*)key);
                    if (ssl) {
                        if (RsaSz < ssl->options.minRsaKeySz) {
                            ret = RSA_KEY_SIZE_E;
                            WOLFSSL_MSG("Private Key size too small");
                        }
                    }
                    else if(ctx) {
                        if (RsaSz < ctx->minRsaKeySz) {
                            ret = RSA_KEY_SIZE_E;
                            WOLFSSL_MSG("Private Key size too small");
                        }
                    }
                    rsaKey = 1;
                    (void)rsaKey;  /* for no ecc builds */
                }
            }

            wc_FreeRsaKey(key);

        #ifdef WOLFSSL_SMALL_STACK
            XFREE(key, heap, DYNAMIC_TYPE_TMP_BUFFER);
        #endif

            if (ret != 0)
                return ret;
        }
    #endif
    #ifdef HAVE_ECC
        if (!rsaKey) {
            /* make sure ECC key can be used */
            word32  idx = 0;
            ecc_key key;

            wc_ecc_init(&key);
            if (wc_EccPrivateKeyDecode(der->buffer, &idx, &key,
                                                        der->length) != 0) {
                wc_ecc_free(&key);
                return SSL_BAD_FILE;
            }

            /* check for minimum ECC key size and then free */
            if (ssl) {
                if (wc_ecc_size(&key) < ssl->options.minEccKeySz) {
                    wc_ecc_free(&key);
                    WOLFSSL_MSG("ECC private key too small");
                    return ECC_KEY_SIZE_E;
                }
            }
            else if (ctx) {
                if (wc_ecc_size(&key) < ctx->minEccKeySz) {
                    wc_ecc_free(&key);
                    WOLFSSL_MSG("ECC private key too small");
                    return ECC_KEY_SIZE_E;
                }
            }

            wc_ecc_free(&key);
            eccKey = 1;
            if (ctx)
                ctx->haveStaticECC = 1;
            if (ssl)
                ssl->options.haveStaticECC = 1;
        }
    #endif /* HAVE_ECC */
    }
    else if (type == CERT_TYPE) {
    #ifdef WOLFSSL_SMALL_STACK
        DecodedCert* cert = NULL;
    #else
        DecodedCert  cert[1];
    #endif

    #ifdef WOLFSSL_SMALL_STACK
        cert = (DecodedCert*)XMALLOC(sizeof(DecodedCert), heap,
                                     DYNAMIC_TYPE_TMP_BUFFER);
        if (cert == NULL)
            return MEMORY_E;
    #endif

        WOLFSSL_MSG("Checking cert signature type");
        InitDecodedCert(cert, der->buffer, der->length, heap);

        if (DecodeToKey(cert, 0) < 0) {
            WOLFSSL_MSG("Decode to key failed");
            FreeDecodedCert(cert);
        #ifdef WOLFSSL_SMALL_STACK
            XFREE(cert, heap, DYNAMIC_TYPE_TMP_BUFFER);
        #endif
            return SSL_BAD_FILE;
        }
        switch (cert->signatureOID) {
            case CTC_SHAwECDSA:
            case CTC_SHA256wECDSA:
            case CTC_SHA384wECDSA:
            case CTC_SHA512wECDSA:
                WOLFSSL_MSG("ECDSA cert signature");
                if (ctx)
                    ctx->haveECDSAsig = 1;
                if (ssl)
                    ssl->options.haveECDSAsig = 1;
                break;
            default:
                WOLFSSL_MSG("Not ECDSA cert signature");
                break;
        }

    #ifdef HAVE_ECC
        if (ctx) {
            ctx->pkCurveOID = cert->pkCurveOID;
        #ifndef WC_STRICT_SIG
            if (cert->keyOID == ECDSAk) {
                ctx->haveECC = 1;
            }
        #else
            ctx->haveECC = ctx->haveECDSAsig;
        #endif
        }
        if (ssl) {
            ssl->pkCurveOID = cert->pkCurveOID;
        #ifndef WC_STRICT_SIG
            if (cert->keyOID == ECDSAk) {
                ssl->options.haveECC = 1;
            }
        #else
            ssl->options.haveECC = ssl->options.haveECDSAsig;
        #endif
        }
    #endif

        /* check key size of cert unless specified not to */
        switch (cert->keyOID) {
        #ifndef NO_RSA
            case RSAk:
                if (ssl && !ssl->options.verifyNone) {
                    if (ssl->options.minRsaKeySz < 0 ||
                          cert->pubKeySize < (word16)ssl->options.minRsaKeySz) {
                        ret = RSA_KEY_SIZE_E;
                        WOLFSSL_MSG("Certificate RSA key size too small");
                    }
                }
                else if (ctx && !ctx->verifyNone) {
                    if (ctx->minRsaKeySz < 0 ||
                                  cert->pubKeySize < (word16)ctx->minRsaKeySz) {
                        ret = RSA_KEY_SIZE_E;
                        WOLFSSL_MSG("Certificate RSA key size too small");
                    }
                }
                break;
            #endif /* !NO_RSA */
        #ifdef HAVE_ECC
            case ECDSAk:
                if (ssl && !ssl->options.verifyNone) {
                    if (ssl->options.minEccKeySz < 0 ||
                          cert->pubKeySize < (word16)ssl->options.minEccKeySz) {
                        ret = ECC_KEY_SIZE_E;
                        WOLFSSL_MSG("Certificate ECC key size error");
                    }
                }
                else if (ctx && !ctx->verifyNone) {
                    if (ctx->minEccKeySz < 0 ||
                                  cert->pubKeySize < (word16)ctx->minEccKeySz) {
                        ret = ECC_KEY_SIZE_E;
                        WOLFSSL_MSG("Certificate ECC key size error");
                    }
                }
                break;
            #endif /* HAVE_ECC */

            default:
                WOLFSSL_MSG("No key size check done on certificate");
                break; /* do no check if not a case for the key */
        }

        FreeDecodedCert(cert);
    #ifdef WOLFSSL_SMALL_STACK
        XFREE(cert, heap, DYNAMIC_TYPE_TMP_BUFFER);
    #endif

        if (ret != 0) {
            return ret;
        }
    }

    return SSL_SUCCESS;
}


/* CA PEM file for verification, may have multiple/chain certs to process */
static int ProcessChainBuffer(WOLFSSL_CTX* ctx, const unsigned char* buff,
                            long sz, int format, int type, WOLFSSL* ssl)
{
    long used   = 0;
    int  ret    = 0;
    int  gotOne = 0;

    WOLFSSL_MSG("Processing CA PEM file");
    while (used < sz) {
        long consumed = 0;

        ret = ProcessBuffer(ctx, buff + used, sz - used, format, type, ssl,
                            &consumed, 0);

        if (ret < 0)
        {
            if(consumed > 0) { /* Made progress in file */
                WOLFSSL_ERROR(ret);
                WOLFSSL_MSG("CA Parse failed, with progress in file.");
                WOLFSSL_MSG("Search for other certs in file");
            } else {
                WOLFSSL_MSG("CA Parse failed, no progress in file.");
                WOLFSSL_MSG("Do not continue search for other certs in file");
                break;
            }
        } else {
            WOLFSSL_MSG("   Processed a CA");
            gotOne = 1;
        }
        used += consumed;
    }

    if(gotOne)
    {
        WOLFSSL_MSG("Processed at least one valid CA. Other stuff OK");
        return SSL_SUCCESS;
    }
    return ret;
}


static INLINE WOLFSSL_METHOD* cm_pick_method(void)
{
    #ifndef NO_WOLFSSL_CLIENT
        #if defined(WOLFSSL_ALLOW_SSLV3) && !defined(NO_OLD_TLS)
            return wolfSSLv3_client_method();
        #else
            return wolfTLSv1_2_client_method();
        #endif
    #elif !defined(NO_WOLFSSL_SERVER)
        #if defined(WOLFSSL_ALLOW_SSLV3) && !defined(NO_OLD_TLS)
            return wolfSSLv3_server_method();
        #else
            return wolfTLSv1_2_server_method();
        #endif
    #else
        return NULL;
    #endif
}


/* like load verify locations, 1 for success, < 0 for error */
int wolfSSL_CertManagerLoadCABuffer(WOLFSSL_CERT_MANAGER* cm,
                                   const unsigned char* in, long sz, int format)
{
    int ret = SSL_FATAL_ERROR;
    WOLFSSL_CTX* tmp;

    WOLFSSL_ENTER("wolfSSL_CertManagerLoadCABuffer");

    if (cm == NULL) {
        WOLFSSL_MSG("No CertManager error");
        return ret;
    }
    tmp = wolfSSL_CTX_new(cm_pick_method());

    if (tmp == NULL) {
        WOLFSSL_MSG("CTX new failed");
        return ret;
    }

    /* for tmp use */
    wolfSSL_CertManagerFree(tmp->cm);
    tmp->cm = cm;

    ret = wolfSSL_CTX_load_verify_buffer(tmp, in, sz, format);

    /* don't loose our good one */
    tmp->cm = NULL;
    wolfSSL_CTX_free(tmp);

    return ret;
}

#ifdef HAVE_CRL

int wolfSSL_CertManagerLoadCRLBuffer(WOLFSSL_CERT_MANAGER* cm,
                                   const unsigned char* buff, long sz, int type)
{
    WOLFSSL_ENTER("wolfSSL_CertManagerLoadCRLBuffer");
    if (cm == NULL)
        return BAD_FUNC_ARG;

    if (cm->crl == NULL) {
        if (wolfSSL_CertManagerEnableCRL(cm, 0) != SSL_SUCCESS) {
            WOLFSSL_MSG("Enable CRL failed");
            return SSL_FATAL_ERROR;
        }
    }

    return BufferLoadCRL(cm->crl, buff, sz, type);
}


int wolfSSL_CTX_LoadCRLBuffer(WOLFSSL_CTX* ctx, const unsigned char* buff,
                              long sz, int type)
{
    WOLFSSL_ENTER("wolfSSL_CTX_LoadCRLBuffer");

    if (ctx == NULL)
        return BAD_FUNC_ARG;

    return wolfSSL_CertManagerLoadCRLBuffer(ctx->cm, buff, sz, type);
}


int wolfSSL_LoadCRLBuffer(WOLFSSL* ssl, const unsigned char* buff,
                          long sz, int type)
{
    WOLFSSL_ENTER("wolfSSL_LoadCRLBuffer");

    if (ssl == NULL || ssl->ctx == NULL)
        return BAD_FUNC_ARG;

    return wolfSSL_CertManagerLoadCRLBuffer(ssl->ctx->cm, buff, sz, type);
}


#endif /* HAVE_CRL */

/* turn on CRL if off and compiled in, set options */
int wolfSSL_CertManagerEnableCRL(WOLFSSL_CERT_MANAGER* cm, int options)
{
    int ret = SSL_SUCCESS;

    (void)options;

    WOLFSSL_ENTER("wolfSSL_CertManagerEnableCRL");
    if (cm == NULL)
        return BAD_FUNC_ARG;

    #ifdef HAVE_CRL
        if (cm->crl == NULL) {
            cm->crl = (WOLFSSL_CRL*)XMALLOC(sizeof(WOLFSSL_CRL), cm->heap,
                                           DYNAMIC_TYPE_CRL);
            if (cm->crl == NULL)
                return MEMORY_E;

            if (InitCRL(cm->crl, cm) != 0) {
                WOLFSSL_MSG("Init CRL failed");
                FreeCRL(cm->crl, 1);
                cm->crl = NULL;
                return SSL_FAILURE;
            }
        }
        cm->crlEnabled = 1;
        if (options & WOLFSSL_CRL_CHECKALL)
            cm->crlCheckAll = 1;
    #else
        ret = NOT_COMPILED_IN;
    #endif

    return ret;
}


int wolfSSL_CertManagerDisableCRL(WOLFSSL_CERT_MANAGER* cm)
{
    WOLFSSL_ENTER("wolfSSL_CertManagerDisableCRL");
    if (cm == NULL)
        return BAD_FUNC_ARG;

    cm->crlEnabled = 0;

    return SSL_SUCCESS;
}
/* Verify the certificate, SSL_SUCCESS for ok, < 0 for error */
int wolfSSL_CertManagerVerifyBuffer(WOLFSSL_CERT_MANAGER* cm, const byte* buff,
                                    long sz, int format)
{
    int ret = 0;
    DerBuffer* der = NULL;
#ifdef WOLFSSL_SMALL_STACK
    DecodedCert* cert = NULL;
#else
    DecodedCert  cert[1];
#endif

    WOLFSSL_ENTER("wolfSSL_CertManagerVerifyBuffer");

#ifdef WOLFSSL_SMALL_STACK
    cert = (DecodedCert*)XMALLOC(sizeof(DecodedCert), cm->heap,
                                 DYNAMIC_TYPE_TMP_BUFFER);
    if (cert == NULL)
        return MEMORY_E;
#endif

    if (format == SSL_FILETYPE_PEM) {
        int eccKey = 0; /* not used */
    #ifdef WOLFSSL_SMALL_STACK
        EncryptedInfo* info = NULL;
    #else
        EncryptedInfo  info[1];
    #endif

    #ifdef WOLFSSL_SMALL_STACK
        info = (EncryptedInfo*)XMALLOC(sizeof(EncryptedInfo), cm->heap,
                                       DYNAMIC_TYPE_TMP_BUFFER);
        if (info == NULL) {
            XFREE(cert, cm->heap, DYNAMIC_TYPE_TMP_BUFFER);
            return MEMORY_E;
        }
    #endif

        info->set      = 0;
        info->ctx      = NULL;
        info->consumed = 0;

        ret = PemToDer(buff, sz, CERT_TYPE, &der, cm->heap, info, &eccKey);
        if (ret != 0) {
            FreeDer(&der);
            #ifdef WOLFSSL_SMALL_STACK
                XFREE(info, cm->heap, DYNAMIC_TYPE_TMP_BUFFER);
            #endif
            return ret;
        }
        InitDecodedCert(cert, der->buffer, der->length, cm->heap);

    #ifdef WOLFSSL_SMALL_STACK
        XFREE(info, cm->heap, DYNAMIC_TYPE_TMP_BUFFER);
    #endif
    }
    else
        InitDecodedCert(cert, (byte*)buff, (word32)sz, cm->heap);

    if (ret == 0)
        ret = ParseCertRelative(cert, CERT_TYPE, 1, cm);

#ifdef HAVE_CRL
    if (ret == 0 && cm->crlEnabled)
        ret = CheckCertCRL(cm->crl, cert);
#endif

    FreeDecodedCert(cert);
    FreeDer(&der);
#ifdef WOLFSSL_SMALL_STACK
    XFREE(cert, cm->heap, DYNAMIC_TYPE_TMP_BUFFER);
#endif

    return ret == 0 ? SSL_SUCCESS : ret;
}


/* turn on OCSP if off and compiled in, set options */
int wolfSSL_CertManagerEnableOCSP(WOLFSSL_CERT_MANAGER* cm, int options)
{
    int ret = SSL_SUCCESS;

    (void)options;

    WOLFSSL_ENTER("wolfSSL_CertManagerEnableOCSP");
    if (cm == NULL)
        return BAD_FUNC_ARG;

    #ifdef HAVE_OCSP
        if (cm->ocsp == NULL) {
            cm->ocsp = (WOLFSSL_OCSP*)XMALLOC(sizeof(WOLFSSL_OCSP), cm->heap,
                                              DYNAMIC_TYPE_OCSP);
            if (cm->ocsp == NULL)
                return MEMORY_E;

            if (InitOCSP(cm->ocsp, cm) != 0) {
                WOLFSSL_MSG("Init OCSP failed");
                FreeOCSP(cm->ocsp, 1);
                cm->ocsp = NULL;
                return SSL_FAILURE;
            }
        }
        cm->ocspEnabled = 1;
        if (options & WOLFSSL_OCSP_URL_OVERRIDE)
            cm->ocspUseOverrideURL = 1;
        if (options & WOLFSSL_OCSP_NO_NONCE)
            cm->ocspSendNonce = 0;
        else
            cm->ocspSendNonce = 1;
        if (options & WOLFSSL_OCSP_CHECKALL)
            cm->ocspCheckAll = 1;
        #ifndef WOLFSSL_USER_IO
            cm->ocspIOCb = EmbedOcspLookup;
            cm->ocspRespFreeCb = EmbedOcspRespFree;
            cm->ocspIOCtx = cm->heap;
        #endif /* WOLFSSL_USER_IO */
    #else
        ret = NOT_COMPILED_IN;
    #endif

    return ret;
}


int wolfSSL_CertManagerDisableOCSP(WOLFSSL_CERT_MANAGER* cm)
{
    WOLFSSL_ENTER("wolfSSL_CertManagerDisableOCSP");
    if (cm == NULL)
        return BAD_FUNC_ARG;

    cm->ocspEnabled = 0;

    return SSL_SUCCESS;
}

/* turn on OCSP Stapling if off and compiled in, set options */
int wolfSSL_CertManagerEnableOCSPStapling(WOLFSSL_CERT_MANAGER* cm)
{
    int ret = SSL_SUCCESS;

    WOLFSSL_ENTER("wolfSSL_CertManagerEnableOCSPStapling");
    if (cm == NULL)
        return BAD_FUNC_ARG;

    #if defined(HAVE_CERTIFICATE_STATUS_REQUEST) \
     || defined(HAVE_CERTIFICATE_STATUS_REQUEST_V2)
        if (cm->ocsp_stapling == NULL) {
            cm->ocsp_stapling = (WOLFSSL_OCSP*)XMALLOC(sizeof(WOLFSSL_OCSP),
                                                   cm->heap, DYNAMIC_TYPE_OCSP);
            if (cm->ocsp_stapling == NULL)
                return MEMORY_E;

            if (InitOCSP(cm->ocsp_stapling, cm) != 0) {
                WOLFSSL_MSG("Init OCSP failed");
                FreeOCSP(cm->ocsp_stapling, 1);
                cm->ocsp_stapling = NULL;
                return SSL_FAILURE;
            }
        }
        cm->ocspStaplingEnabled = 1;

        #ifndef WOLFSSL_USER_IO
            cm->ocspIOCb = EmbedOcspLookup;
            cm->ocspRespFreeCb = EmbedOcspRespFree;
            cm->ocspIOCtx = cm->heap;
        #endif /* WOLFSSL_USER_IO */
    #else
        ret = NOT_COMPILED_IN;
    #endif

    return ret;
}


#ifdef HAVE_OCSP


/* check CRL if enabled, SSL_SUCCESS  */
int wolfSSL_CertManagerCheckOCSP(WOLFSSL_CERT_MANAGER* cm, byte* der, int sz)
{
    int ret;
#ifdef WOLFSSL_SMALL_STACK
    DecodedCert* cert = NULL;
#else
    DecodedCert  cert[1];
#endif

    WOLFSSL_ENTER("wolfSSL_CertManagerCheckOCSP");

    if (cm == NULL)
        return BAD_FUNC_ARG;

    if (cm->ocspEnabled == 0)
        return SSL_SUCCESS;

#ifdef WOLFSSL_SMALL_STACK
    cert = (DecodedCert*)XMALLOC(sizeof(DecodedCert), NULL,
                                 DYNAMIC_TYPE_TMP_BUFFER);
    if (cert == NULL)
        return MEMORY_E;
#endif

    InitDecodedCert(cert, der, sz, NULL);

    if ((ret = ParseCertRelative(cert, CERT_TYPE, VERIFY_OCSP, cm)) != 0) {
        WOLFSSL_MSG("ParseCert failed");
    }
    else if ((ret = CheckCertOCSP(cm->ocsp, cert, NULL)) != 0) {
        WOLFSSL_MSG("CheckCertOCSP failed");
    }

    FreeDecodedCert(cert);
#ifdef WOLFSSL_SMALL_STACK
    XFREE(cert, NULL, DYNAMIC_TYPE_TMP_BUFFER);
#endif

    return ret == 0 ? SSL_SUCCESS : ret;
}


int wolfSSL_CertManagerSetOCSPOverrideURL(WOLFSSL_CERT_MANAGER* cm,
                                          const char* url)
{
    WOLFSSL_ENTER("wolfSSL_CertManagerSetOCSPOverrideURL");
    if (cm == NULL)
        return BAD_FUNC_ARG;

    XFREE(cm->ocspOverrideURL, cm->heap, DYNAMIC_TYPE_URL);
    if (url != NULL) {
        int urlSz = (int)XSTRLEN(url) + 1;
        cm->ocspOverrideURL = (char*)XMALLOC(urlSz, cm->heap, DYNAMIC_TYPE_URL);
        if (cm->ocspOverrideURL != NULL) {
            XMEMCPY(cm->ocspOverrideURL, url, urlSz);
        }
        else
            return MEMORY_E;
    }
    else
        cm->ocspOverrideURL = NULL;

    return SSL_SUCCESS;
}


int wolfSSL_CertManagerSetOCSP_Cb(WOLFSSL_CERT_MANAGER* cm,
                        CbOCSPIO ioCb, CbOCSPRespFree respFreeCb, void* ioCbCtx)
{
    WOLFSSL_ENTER("wolfSSL_CertManagerSetOCSP_Cb");
    if (cm == NULL)
        return BAD_FUNC_ARG;

    cm->ocspIOCb = ioCb;
    cm->ocspRespFreeCb = respFreeCb;
    cm->ocspIOCtx = ioCbCtx;

    return SSL_SUCCESS;
}


int wolfSSL_EnableOCSP(WOLFSSL* ssl, int options)
{
    WOLFSSL_ENTER("wolfSSL_EnableOCSP");
    if (ssl)
        return wolfSSL_CertManagerEnableOCSP(ssl->ctx->cm, options);
    else
        return BAD_FUNC_ARG;
}


int wolfSSL_DisableOCSP(WOLFSSL* ssl)
{
    WOLFSSL_ENTER("wolfSSL_DisableOCSP");
    if (ssl)
        return wolfSSL_CertManagerDisableOCSP(ssl->ctx->cm);
    else
        return BAD_FUNC_ARG;
}


int wolfSSL_SetOCSP_OverrideURL(WOLFSSL* ssl, const char* url)
{
    WOLFSSL_ENTER("wolfSSL_SetOCSP_OverrideURL");
    if (ssl)
        return wolfSSL_CertManagerSetOCSPOverrideURL(ssl->ctx->cm, url);
    else
        return BAD_FUNC_ARG;
}


int wolfSSL_SetOCSP_Cb(WOLFSSL* ssl,
                        CbOCSPIO ioCb, CbOCSPRespFree respFreeCb, void* ioCbCtx)
{
    WOLFSSL_ENTER("wolfSSL_SetOCSP_Cb");
    if (ssl)
        return wolfSSL_CertManagerSetOCSP_Cb(ssl->ctx->cm,
                                             ioCb, respFreeCb, ioCbCtx);
    else
        return BAD_FUNC_ARG;
}


int wolfSSL_CTX_EnableOCSP(WOLFSSL_CTX* ctx, int options)
{
    WOLFSSL_ENTER("wolfSSL_CTX_EnableOCSP");
    if (ctx)
        return wolfSSL_CertManagerEnableOCSP(ctx->cm, options);
    else
        return BAD_FUNC_ARG;
}


int wolfSSL_CTX_DisableOCSP(WOLFSSL_CTX* ctx)
{
    WOLFSSL_ENTER("wolfSSL_CTX_DisableOCSP");
    if (ctx)
        return wolfSSL_CertManagerDisableOCSP(ctx->cm);
    else
        return BAD_FUNC_ARG;
}


int wolfSSL_CTX_SetOCSP_OverrideURL(WOLFSSL_CTX* ctx, const char* url)
{
    WOLFSSL_ENTER("wolfSSL_SetOCSP_OverrideURL");
    if (ctx)
        return wolfSSL_CertManagerSetOCSPOverrideURL(ctx->cm, url);
    else
        return BAD_FUNC_ARG;
}


int wolfSSL_CTX_SetOCSP_Cb(WOLFSSL_CTX* ctx, CbOCSPIO ioCb,
                           CbOCSPRespFree respFreeCb, void* ioCbCtx)
{
    WOLFSSL_ENTER("wolfSSL_CTX_SetOCSP_Cb");
    if (ctx)
        return wolfSSL_CertManagerSetOCSP_Cb(ctx->cm, ioCb,
                                             respFreeCb, ioCbCtx);
    else
        return BAD_FUNC_ARG;
}

#if defined(HAVE_CERTIFICATE_STATUS_REQUEST) \
 || defined(HAVE_CERTIFICATE_STATUS_REQUEST_V2)
int wolfSSL_CTX_EnableOCSPStapling(WOLFSSL_CTX* ctx)
{
    WOLFSSL_ENTER("wolfSSL_CTX_EnableOCSPStapling");
    if (ctx)
        return wolfSSL_CertManagerEnableOCSPStapling(ctx->cm);
    else
        return BAD_FUNC_ARG;
}
#endif

#endif /* HAVE_OCSP */


#ifndef NO_FILESYSTEM

/* process a file with name fname into ctx of format and type
   userChain specifies a user certificate chain to pass during handshake */
int ProcessFile(WOLFSSL_CTX* ctx, const char* fname, int format, int type,
                WOLFSSL* ssl, int userChain, WOLFSSL_CRL* crl)
{
#ifdef WOLFSSL_SMALL_STACK
    byte   staticBuffer[1]; /* force heap usage */
#else
    byte   staticBuffer[FILE_BUFFER_SIZE];
#endif
    byte*  myBuffer = staticBuffer;
    int    dynamic = 0;
    int    ret;
    long   sz = 0;
    XFILE  file;
    void*  heapHint = ctx ? ctx->heap : ((ssl) ? ssl->heap : NULL);

    (void)crl;
    (void)heapHint;

    if (fname == NULL) return SSL_BAD_FILE;

    file = XFOPEN(fname, "rb");
    if (file == XBADFILE) return SSL_BAD_FILE;
    XFSEEK(file, 0, XSEEK_END);
    sz = XFTELL(file);
    XREWIND(file);

    if (sz > (long)sizeof(staticBuffer)) {
        WOLFSSL_MSG("Getting dynamic buffer");
        myBuffer = (byte*)XMALLOC(sz, heapHint, DYNAMIC_TYPE_FILE);
        if (myBuffer == NULL) {
            XFCLOSE(file);
            return SSL_BAD_FILE;
        }
        dynamic = 1;
    }
    else if (sz < 0) {
        XFCLOSE(file);
        return SSL_BAD_FILE;
    }

    if ( (ret = (int)XFREAD(myBuffer, sz, 1, file)) < 0)
        ret = SSL_BAD_FILE;
    else {
        if ((type == CA_TYPE || type == TRUSTED_PEER_TYPE)
                                                  && format == SSL_FILETYPE_PEM)
            ret = ProcessChainBuffer(ctx, myBuffer, sz, format, type, ssl);
#ifdef HAVE_CRL
        else if (type == CRL_TYPE)
            ret = BufferLoadCRL(crl, myBuffer, sz, format);
#endif
        else
            ret = ProcessBuffer(ctx, myBuffer, sz, format, type, ssl, NULL,
                                userChain);
    }

    XFCLOSE(file);
    if (dynamic)
        XFREE(myBuffer, heapHint, DYNAMIC_TYPE_FILE);

    return ret;
}


/* loads file then loads each file in path, no c_rehash */
int wolfSSL_CTX_load_verify_locations(WOLFSSL_CTX* ctx, const char* file,
                                     const char* path)
{
    int ret = SSL_SUCCESS;

    WOLFSSL_ENTER("wolfSSL_CTX_load_verify_locations");
    (void)path;

    if (ctx == NULL || (file == NULL && path == NULL) )
        return SSL_FAILURE;

    if (file)
        ret = ProcessFile(ctx, file, SSL_FILETYPE_PEM, CA_TYPE, NULL, 0, NULL);

    if (ret == SSL_SUCCESS && path) {
        /* try to load each regular file in path */
    #ifdef USE_WINDOWS_API
        WIN32_FIND_DATAA FindFileData;
        HANDLE hFind;
    #ifdef WOLFSSL_SMALL_STACK
        char*  name = NULL;
    #else
        char   name[MAX_FILENAME_SZ];
    #endif

    #ifdef WOLFSSL_SMALL_STACK
        name = (char*)XMALLOC(MAX_FILENAME_SZ, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        if (name == NULL)
            return MEMORY_E;
    #endif

        XMEMSET(name, 0, MAX_FILENAME_SZ);
        XSTRNCPY(name, path, MAX_FILENAME_SZ - 4);
        XSTRNCAT(name, "\\*", 3);

        hFind = FindFirstFileA(name, &FindFileData);
        if (hFind == INVALID_HANDLE_VALUE) {
            WOLFSSL_MSG("FindFirstFile for path verify locations failed");
        #ifdef WOLFSSL_SMALL_STACK
            XFREE(name, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        #endif
            return BAD_PATH_ERROR;
        }

        do {
            if (FindFileData.dwFileAttributes != FILE_ATTRIBUTE_DIRECTORY) {
                XSTRNCPY(name, path, MAX_FILENAME_SZ/2 - 3);
                XSTRNCAT(name, "\\", 2);
                XSTRNCAT(name, FindFileData.cFileName, MAX_FILENAME_SZ/2);

                ret = ProcessFile(ctx, name, SSL_FILETYPE_PEM, CA_TYPE,
                                  NULL, 0, NULL);
            }
        } while (ret == SSL_SUCCESS && FindNextFileA(hFind, &FindFileData));

    #ifdef WOLFSSL_SMALL_STACK
        XFREE(name, NULL, DYNAMIC_TYPE_TMP_BUFFER);
    #endif

        FindClose(hFind);
    #elif !defined(NO_WOLFSSL_DIR)
        struct dirent* entry;
        DIR*   dir = opendir(path);
    #ifdef WOLFSSL_SMALL_STACK
        char*  name = NULL;
    #else
        char   name[MAX_FILENAME_SZ];
    #endif

        if (dir == NULL) {
            WOLFSSL_MSG("opendir path verify locations failed");
            return BAD_PATH_ERROR;
        }

    #ifdef WOLFSSL_SMALL_STACK
        name = (char*)XMALLOC(MAX_FILENAME_SZ, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        if (name == NULL) {
            closedir(dir);
            return MEMORY_E;
        }
    #endif

        while ( ret == SSL_SUCCESS && (entry = readdir(dir)) != NULL) {
            struct stat s;

            XMEMSET(name, 0, MAX_FILENAME_SZ);
            XSTRNCPY(name, path, MAX_FILENAME_SZ/2 - 2);
            XSTRNCAT(name, "/", 1);
            XSTRNCAT(name, entry->d_name, MAX_FILENAME_SZ/2);

            if (stat(name, &s) != 0) {
                WOLFSSL_MSG("stat on name failed");
                ret = BAD_PATH_ERROR;
            } else if (s.st_mode & S_IFREG)
                ret = ProcessFile(ctx, name, SSL_FILETYPE_PEM, CA_TYPE,
                                  NULL, 0, NULL);
        }

    #ifdef WOLFSSL_SMALL_STACK
        XFREE(name, NULL, DYNAMIC_TYPE_TMP_BUFFER);
    #endif

        closedir(dir);
    #endif
    }

    return ret;
}


#ifdef WOLFSSL_TRUST_PEER_CERT
/* Used to specify a peer cert to match when connecting
    ctx : the ctx structure to load in peer cert
    file: the string name of cert file
    type: type of format such as PEM/DER
 */
int wolfSSL_CTX_trust_peer_cert(WOLFSSL_CTX* ctx, const char* file, int type)
{
    WOLFSSL_ENTER("wolfSSL_CTX_trust_peer_cert");

    if (ctx == NULL || file == NULL) {
        return SSL_FAILURE;
    }

    return ProcessFile(ctx, file, type, TRUSTED_PEER_TYPE, NULL, 0, NULL);
}
#endif /* WOLFSSL_TRUST_PEER_CERT */


/* Verify the certificate, SSL_SUCCESS for ok, < 0 for error */
int wolfSSL_CertManagerVerify(WOLFSSL_CERT_MANAGER* cm, const char* fname,
                             int format)
{
    int    ret = SSL_FATAL_ERROR;
#ifdef WOLFSSL_SMALL_STACK
    byte   staticBuffer[1]; /* force heap usage */
#else
    byte   staticBuffer[FILE_BUFFER_SIZE];
#endif
    byte*  myBuffer = staticBuffer;
    int    dynamic = 0;
    long   sz = 0;
    XFILE  file = XFOPEN(fname, "rb");

    WOLFSSL_ENTER("wolfSSL_CertManagerVerify");

    if (file == XBADFILE) return SSL_BAD_FILE;
    XFSEEK(file, 0, XSEEK_END);
    sz = XFTELL(file);
    XREWIND(file);

    if (sz > MAX_WOLFSSL_FILE_SIZE || sz < 0) {
        WOLFSSL_MSG("CertManagerVerify file bad size");
        XFCLOSE(file);
        return SSL_BAD_FILE;
    }

    if (sz > (long)sizeof(staticBuffer)) {
        WOLFSSL_MSG("Getting dynamic buffer");
        myBuffer = (byte*) XMALLOC(sz, cm->heap, DYNAMIC_TYPE_FILE);
        if (myBuffer == NULL) {
            XFCLOSE(file);
            return SSL_BAD_FILE;
        }
        dynamic = 1;
    }

    if ( (ret = (int)XFREAD(myBuffer, sz, 1, file)) < 0)
        ret = SSL_BAD_FILE;
    else
        ret = wolfSSL_CertManagerVerifyBuffer(cm, myBuffer, sz, format);

    XFCLOSE(file);
    if (dynamic)
        XFREE(myBuffer, cm->heap, DYNAMIC_TYPE_FILE);

    return ret;
}


/* like load verify locations, 1 for success, < 0 for error */
int wolfSSL_CertManagerLoadCA(WOLFSSL_CERT_MANAGER* cm, const char* file,
                             const char* path)
{
    int ret = SSL_FATAL_ERROR;
    WOLFSSL_CTX* tmp;

    WOLFSSL_ENTER("wolfSSL_CertManagerLoadCA");

    if (cm == NULL) {
        WOLFSSL_MSG("No CertManager error");
        return ret;
    }
    tmp = wolfSSL_CTX_new(cm_pick_method());

    if (tmp == NULL) {
        WOLFSSL_MSG("CTX new failed");
        return ret;
    }

    /* for tmp use */
    wolfSSL_CertManagerFree(tmp->cm);
    tmp->cm = cm;

    ret = wolfSSL_CTX_load_verify_locations(tmp, file, path);

    /* don't loose our good one */
    tmp->cm = NULL;
    wolfSSL_CTX_free(tmp);

    return ret;
}




int wolfSSL_CTX_check_private_key(WOLFSSL_CTX* ctx)
{
    /* TODO: check private against public for RSA match */
    (void)ctx;
    WOLFSSL_ENTER("SSL_CTX_check_private_key");
    return SSL_SUCCESS;
}


#ifdef HAVE_CRL


/* check CRL if enabled, SSL_SUCCESS  */
int wolfSSL_CertManagerCheckCRL(WOLFSSL_CERT_MANAGER* cm, byte* der, int sz)
{
    int ret = 0;
#ifdef WOLFSSL_SMALL_STACK
    DecodedCert* cert = NULL;
#else
    DecodedCert  cert[1];
#endif

    WOLFSSL_ENTER("wolfSSL_CertManagerCheckCRL");

    if (cm == NULL)
        return BAD_FUNC_ARG;

    if (cm->crlEnabled == 0)
        return SSL_SUCCESS;

#ifdef WOLFSSL_SMALL_STACK
    cert = (DecodedCert*)XMALLOC(sizeof(DecodedCert), NULL,
                                 DYNAMIC_TYPE_TMP_BUFFER);
    if (cert == NULL)
        return MEMORY_E;
#endif

    InitDecodedCert(cert, der, sz, NULL);

    if ((ret = ParseCertRelative(cert, CERT_TYPE, VERIFY_CRL, cm)) != 0) {
        WOLFSSL_MSG("ParseCert failed");
    }
    else if ((ret = CheckCertCRL(cm->crl, cert)) != 0) {
        WOLFSSL_MSG("CheckCertCRL failed");
    }

    FreeDecodedCert(cert);
#ifdef WOLFSSL_SMALL_STACK
    XFREE(cert, NULL, DYNAMIC_TYPE_TMP_BUFFER);
#endif

    return ret == 0 ? SSL_SUCCESS : ret;
}


int wolfSSL_CertManagerSetCRL_Cb(WOLFSSL_CERT_MANAGER* cm, CbMissingCRL cb)
{
    WOLFSSL_ENTER("wolfSSL_CertManagerSetCRL_Cb");
    if (cm == NULL)
        return BAD_FUNC_ARG;

    cm->cbMissingCRL = cb;

    return SSL_SUCCESS;
}


int wolfSSL_CertManagerLoadCRL(WOLFSSL_CERT_MANAGER* cm, const char* path,
                              int type, int monitor)
{
    WOLFSSL_ENTER("wolfSSL_CertManagerLoadCRL");
    if (cm == NULL)
        return BAD_FUNC_ARG;

    if (cm->crl == NULL) {
        if (wolfSSL_CertManagerEnableCRL(cm, 0) != SSL_SUCCESS) {
            WOLFSSL_MSG("Enable CRL failed");
            return SSL_FATAL_ERROR;
        }
    }

    return LoadCRL(cm->crl, path, type, monitor);
}


int wolfSSL_EnableCRL(WOLFSSL* ssl, int options)
{
    WOLFSSL_ENTER("wolfSSL_EnableCRL");
    if (ssl)
        return wolfSSL_CertManagerEnableCRL(ssl->ctx->cm, options);
    else
        return BAD_FUNC_ARG;
}


int wolfSSL_DisableCRL(WOLFSSL* ssl)
{
    WOLFSSL_ENTER("wolfSSL_DisableCRL");
    if (ssl)
        return wolfSSL_CertManagerDisableCRL(ssl->ctx->cm);
    else
        return BAD_FUNC_ARG;
}


int wolfSSL_LoadCRL(WOLFSSL* ssl, const char* path, int type, int monitor)
{
    WOLFSSL_ENTER("wolfSSL_LoadCRL");
    if (ssl)
        return wolfSSL_CertManagerLoadCRL(ssl->ctx->cm, path, type, monitor);
    else
        return BAD_FUNC_ARG;
}


int wolfSSL_SetCRL_Cb(WOLFSSL* ssl, CbMissingCRL cb)
{
    WOLFSSL_ENTER("wolfSSL_SetCRL_Cb");
    if (ssl)
        return wolfSSL_CertManagerSetCRL_Cb(ssl->ctx->cm, cb);
    else
        return BAD_FUNC_ARG;
}


int wolfSSL_CTX_EnableCRL(WOLFSSL_CTX* ctx, int options)
{
    WOLFSSL_ENTER("wolfSSL_CTX_EnableCRL");
    if (ctx)
        return wolfSSL_CertManagerEnableCRL(ctx->cm, options);
    else
        return BAD_FUNC_ARG;
}


int wolfSSL_CTX_DisableCRL(WOLFSSL_CTX* ctx)
{
    WOLFSSL_ENTER("wolfSSL_CTX_DisableCRL");
    if (ctx)
        return wolfSSL_CertManagerDisableCRL(ctx->cm);
    else
        return BAD_FUNC_ARG;
}


int wolfSSL_CTX_LoadCRL(WOLFSSL_CTX* ctx, const char* path,
                        int type, int monitor)
{
    WOLFSSL_ENTER("wolfSSL_CTX_LoadCRL");
    if (ctx)
        return wolfSSL_CertManagerLoadCRL(ctx->cm, path, type, monitor);
    else
        return BAD_FUNC_ARG;
}


int wolfSSL_CTX_SetCRL_Cb(WOLFSSL_CTX* ctx, CbMissingCRL cb)
{
    WOLFSSL_ENTER("wolfSSL_CTX_SetCRL_Cb");
    if (ctx)
        return wolfSSL_CertManagerSetCRL_Cb(ctx->cm, cb);
    else
        return BAD_FUNC_ARG;
}


#endif /* HAVE_CRL */


#ifdef WOLFSSL_DER_LOAD

/* Add format parameter to allow DER load of CA files */
int wolfSSL_CTX_der_load_verify_locations(WOLFSSL_CTX* ctx, const char* file,
                                          int format)
{
    WOLFSSL_ENTER("wolfSSL_CTX_der_load_verify_locations");
    if (ctx == NULL || file == NULL)
        return SSL_FAILURE;

    if (ProcessFile(ctx, file, format, CA_TYPE, NULL, 0, NULL) == SSL_SUCCESS)
        return SSL_SUCCESS;

    return SSL_FAILURE;
}

#endif /* WOLFSSL_DER_LOAD */


#ifdef WOLFSSL_CERT_GEN

/* load pem cert from file into der buffer, return der size or error */
int wolfSSL_PemCertToDer(const char* fileName, unsigned char* derBuf, int derSz)
{
#ifdef WOLFSSL_SMALL_STACK
    EncryptedInfo* info = NULL;
    byte   staticBuffer[1]; /* force XMALLOC */
#else
    EncryptedInfo info[1];
    byte   staticBuffer[FILE_BUFFER_SIZE];
#endif
    byte*  fileBuf = staticBuffer;
    int    dynamic = 0;
    int    ret     = 0;
    int    ecc     = 0;
    long   sz      = 0;
    XFILE  file    = XFOPEN(fileName, "rb");
    DerBuffer* converted = NULL;

    WOLFSSL_ENTER("wolfSSL_PemCertToDer");

    if (file == XBADFILE) {
        ret = SSL_BAD_FILE;
    }
    else {
        XFSEEK(file, 0, XSEEK_END);
        sz = XFTELL(file);
        XREWIND(file);

        if (sz < 0) {
            ret = SSL_BAD_FILE;
        }
        else if (sz > (long)sizeof(staticBuffer)) {
        #ifdef WOLFSSL_STATIC_MEMORY
            WOLFSSL_MSG("File was larger then static buffer");
            return MEMORY_E;
        #endif
            fileBuf = (byte*)XMALLOC(sz, 0, DYNAMIC_TYPE_FILE);
            if (fileBuf == NULL)
                ret = MEMORY_E;
            else
                dynamic = 1;
        }

        if (ret == 0) {
            if ( (ret = (int)XFREAD(fileBuf, sz, 1, file)) < 0) {
                ret = SSL_BAD_FILE;
            }
            else {
            #ifdef WOLFSSL_SMALL_STACK
                info = (EncryptedInfo*)XMALLOC(sizeof(EncryptedInfo), NULL,
                                               DYNAMIC_TYPE_TMP_BUFFER);
                if (info == NULL)
                    ret = MEMORY_E;
                else
            #endif
                {
                    ret = PemToDer(fileBuf, sz, CA_TYPE, &converted,
                                   0, info, &ecc);
                #ifdef WOLFSSL_SMALL_STACK
                    XFREE(info, NULL, DYNAMIC_TYPE_TMP_BUFFER);
                #endif
                }
            }

            if (ret == 0) {
                if (converted->length < (word32)derSz) {
                    XMEMCPY(derBuf, converted->buffer, converted->length);
                    ret = converted->length;
                }
                else
                    ret = BUFFER_E;
            }

            FreeDer(&converted);
        }

        XFCLOSE(file);
        if (dynamic)
            XFREE(fileBuf, 0, DYNAMIC_TYPE_FILE);
    }

    return ret;
}

#endif /* WOLFSSL_CERT_GEN */

#ifdef WOLFSSL_CERT_EXT
#ifndef NO_FILESYSTEM
/* load pem public key from file into der buffer, return der size or error */
int wolfSSL_PemPubKeyToDer(const char* fileName,
                           unsigned char* derBuf, int derSz)
{
#ifdef WOLFSSL_SMALL_STACK
    byte   staticBuffer[1]; /* force XMALLOC */
#else
    byte   staticBuffer[FILE_BUFFER_SIZE];
#endif
    byte*  fileBuf = staticBuffer;
    int    dynamic = 0;
    int    ret     = 0;
    long   sz      = 0;
    XFILE  file    = XFOPEN(fileName, "rb");
    DerBuffer* converted = NULL;

    WOLFSSL_ENTER("wolfSSL_PemPubKeyToDer");

    if (file == XBADFILE) {
        ret = SSL_BAD_FILE;
    }
    else {
        XFSEEK(file, 0, XSEEK_END);
        sz = XFTELL(file);
        XREWIND(file);

        if (sz < 0) {
            ret = SSL_BAD_FILE;
        }
        else if (sz > (long)sizeof(staticBuffer)) {
        #ifdef WOLFSSL_STATIC_MEMORY
            WOLFSSL_MSG("File was larger then static buffer");
            return MEMORY_E;
        #endif
            fileBuf = (byte*)XMALLOC(sz, 0, DYNAMIC_TYPE_FILE);
            if (fileBuf == NULL)
                ret = MEMORY_E;
            else
                dynamic = 1;
        }
        if (ret == 0) {
            if ( (ret = (int)XFREAD(fileBuf, sz, 1, file)) < 0)
                ret = SSL_BAD_FILE;
            else
                ret = PemToDer(fileBuf, sz, PUBLICKEY_TYPE, &converted,
                               0, NULL, NULL);

            if (ret == 0) {
                if (converted->length < (word32)derSz) {
                    XMEMCPY(derBuf, converted->buffer, converted->length);
                    ret = converted->length;
                }
                else
                    ret = BUFFER_E;
            }

            FreeDer(&converted);
        }

        XFCLOSE(file);
        if (dynamic)
            XFREE(fileBuf, 0, DYNAMIC_TYPE_FILE);
    }

    return ret;
}
#endif /* NO_FILESYSTEM */

/* Return bytes written to buff or < 0 for error */
int wolfSSL_PubKeyPemToDer(const unsigned char* pem, int pemSz,
                           unsigned char* buff, int buffSz)
{
    int ret;
    DerBuffer* der = NULL;

    WOLFSSL_ENTER("wolfSSL_PubKeyPemToDer");

    if (pem == NULL || buff == NULL || buffSz <= 0) {
        WOLFSSL_MSG("Bad pem der args");
        return BAD_FUNC_ARG;
    }

    ret = PemToDer(pem, pemSz, PUBLICKEY_TYPE, &der, NULL, NULL, NULL);
    if (ret < 0) {
        WOLFSSL_MSG("Bad Pem To Der");
    }
    else {
        if (der->length <= (word32)buffSz) {
            XMEMCPY(buff, der->buffer, der->length);
            ret = der->length;
        }
        else {
            WOLFSSL_MSG("Bad der length");
            ret = BAD_FUNC_ARG;
        }
    }

    FreeDer(&der);
    return ret;
}

#endif /* WOLFSSL_CERT_EXT */

int wolfSSL_CTX_use_certificate_file(WOLFSSL_CTX* ctx, const char* file,
                                     int format)
{
    WOLFSSL_ENTER("wolfSSL_CTX_use_certificate_file");
    if (ProcessFile(ctx, file, format, CERT_TYPE, NULL, 0, NULL) == SSL_SUCCESS)
        return SSL_SUCCESS;

    return SSL_FAILURE;
}


int wolfSSL_CTX_use_PrivateKey_file(WOLFSSL_CTX* ctx, const char* file,
                                    int format)
{
    WOLFSSL_ENTER("wolfSSL_CTX_use_PrivateKey_file");
    if (ProcessFile(ctx, file, format, PRIVATEKEY_TYPE, NULL, 0, NULL)
                    == SSL_SUCCESS)
        return SSL_SUCCESS;

    return SSL_FAILURE;
}


/* get cert chaining depth using ssl struct */
long wolfSSL_get_verify_depth(WOLFSSL* ssl)
{
    if(ssl == NULL) {
        return BAD_FUNC_ARG;
    }
    return MAX_CHAIN_DEPTH;
}


/* get cert chaining depth using ctx struct */
long wolfSSL_CTX_get_verify_depth(WOLFSSL_CTX* ctx)
{
    if(ctx == NULL) {
        return BAD_FUNC_ARG;
    }
    return MAX_CHAIN_DEPTH;
}


int wolfSSL_CTX_use_certificate_chain_file(WOLFSSL_CTX* ctx, const char* file)
{
   /* process up to MAX_CHAIN_DEPTH plus subject cert */
   WOLFSSL_ENTER("wolfSSL_CTX_use_certificate_chain_file");
   if (ProcessFile(ctx, file, SSL_FILETYPE_PEM,CERT_TYPE,NULL,1, NULL)
                   == SSL_SUCCESS)
       return SSL_SUCCESS;

   return SSL_FAILURE;
}


#ifndef NO_DH

/* server Diffie-Hellman parameters */
static int wolfSSL_SetTmpDH_file_wrapper(WOLFSSL_CTX* ctx, WOLFSSL* ssl,
                                        const char* fname, int format)
{
#ifdef WOLFSSL_SMALL_STACK
    byte   staticBuffer[1]; /* force heap usage */
#else
    byte   staticBuffer[FILE_BUFFER_SIZE];
#endif
    byte*  myBuffer = staticBuffer;
    int    dynamic = 0;
    int    ret;
    long   sz = 0;
    XFILE  file;

    if (ctx == NULL || fname == NULL)
        return BAD_FUNC_ARG;

    file = XFOPEN(fname, "rb");
    if (file == XBADFILE) return SSL_BAD_FILE;
    XFSEEK(file, 0, XSEEK_END);
    sz = XFTELL(file);
    XREWIND(file);

    if (sz > (long)sizeof(staticBuffer)) {
        WOLFSSL_MSG("Getting dynamic buffer");
        myBuffer = (byte*) XMALLOC(sz, ctx->heap, DYNAMIC_TYPE_FILE);
        if (myBuffer == NULL) {
            XFCLOSE(file);
            return SSL_BAD_FILE;
        }
        dynamic = 1;
    }
    else if (sz < 0) {
        XFCLOSE(file);
        return SSL_BAD_FILE;
    }

    if ( (ret = (int)XFREAD(myBuffer, sz, 1, file)) < 0)
        ret = SSL_BAD_FILE;
    else {
        if (ssl)
            ret = wolfSSL_SetTmpDH_buffer(ssl, myBuffer, sz, format);
        else
            ret = wolfSSL_CTX_SetTmpDH_buffer(ctx, myBuffer, sz, format);
    }

    XFCLOSE(file);
    if (dynamic)
        XFREE(myBuffer, ctx->heap, DYNAMIC_TYPE_FILE);

    return ret;
}

/* server Diffie-Hellman parameters */
int wolfSSL_SetTmpDH_file(WOLFSSL* ssl, const char* fname, int format)
{
    if (ssl == NULL)
        return BAD_FUNC_ARG;

    return wolfSSL_SetTmpDH_file_wrapper(ssl->ctx, ssl, fname, format);
}


/* server Diffie-Hellman parameters */
int wolfSSL_CTX_SetTmpDH_file(WOLFSSL_CTX* ctx, const char* fname, int format)
{
    return wolfSSL_SetTmpDH_file_wrapper(ctx, NULL, fname, format);
}

#endif /* NO_DH */


#endif /* NO_FILESYSTEM */
#ifdef OPENSSL_EXTRA
/* put SSL type in extra for now, not very common */

WOLFSSL_PKCS8_PRIV_KEY_INFO* wolfSSL_d2i_PKCS8_PKEY_bio(WOLFSSL_BIO* bio,
        WOLFSSL_PKCS8_PRIV_KEY_INFO** pkey)
{
    const unsigned char* mem;
    int memSz;
    int keySz;

    WOLFSSL_PKCS8_PRIV_KEY_INFO* pkcs8;

    WOLFSSL_MSG("wolfSSL_d2i_PKCS8_PKEY_bio()");

    if (bio == NULL) {
        return NULL;
    }

    if ((memSz = wolfSSL_BIO_get_mem_data(bio, &mem)) < 0) {
        return NULL;
    }

    if ((keySz = wolfSSL_KeyPemToDer(mem, memSz, (unsigned char*)mem, memSz,
                    NULL)) < 0) {
        WOLFSSL_MSG("Not PEM format");
        keySz = memSz;
        if ((keySz = ToTraditional((byte*)mem, (word32)keySz)) < 0) {
            return NULL;
        }
    }

    pkcs8 = wolfSSL_PKEY_new();
    pkcs8->pkey.ptr = (char*)XMALLOC(keySz, NULL, DYNAMIC_TYPE_PUBLIC_KEY);
    if (pkcs8->pkey.ptr == NULL) {
        wolfSSL_EVP_PKEY_free(pkcs8);
        return NULL;
    }
    XMEMCPY(pkcs8->pkey.ptr, mem, keySz);
    pkcs8->pkey_sz = keySz;

    if (pkey != NULL) {
        *pkey = pkcs8;
    }

    return pkcs8;
}


/* expecting DER format public key */
WOLFSSL_EVP_PKEY* wolfSSL_d2i_PUBKEY_bio(WOLFSSL_BIO* bio,
                                         WOLFSSL_EVP_PKEY** out)
{
    const unsigned char* mem;
    int memSz;
    WOLFSSL_EVP_PKEY* pkey = NULL;

    WOLFSSL_ENTER("wolfSSL_d2i_PUBKEY_bio()");

    if (bio == NULL) {
        return NULL;
    }
    (void)out;

    if ((memSz = wolfSSL_BIO_get_mem_data(bio, &mem)) < 0) {
        return NULL;
    }
    if (mem == NULL) {
        return NULL;
    }

    #if !defined(NO_RSA)
    {
        RsaKey rsa;
        word32 keyIdx = 0;

        /* test if RSA key */
        if (wc_InitRsaKey(&rsa, NULL) == 0 &&
            wc_RsaPublicKeyDecode(mem, &keyIdx, &rsa, memSz) == 0) {
            wc_FreeRsaKey(&rsa);
            pkey = wolfSSL_PKEY_new();
            if (pkey != NULL) {
                pkey->pkey_sz = keyIdx;
                pkey->pkey.ptr = (char*)XMALLOC(memSz, NULL,
                        DYNAMIC_TYPE_PUBLIC_KEY);
                if (pkey->pkey.ptr == NULL) {
                    wolfSSL_EVP_PKEY_free(pkey);
                    return NULL;
                }
                XMEMCPY(pkey->pkey.ptr, mem, keyIdx);
                pkey->type = RSAk;
                if (out != NULL) {
                    *out = pkey;
                }
                return pkey;
            }
        }
        wc_FreeRsaKey(&rsa);
    }
    #endif /* NO_RSA */

    #ifdef HAVE_ECC
    {
        word32  keyIdx = 0;
        ecc_key ecc;

        if (wc_ecc_init(&ecc) == 0 &&
            wc_EccPublicKeyDecode(mem, &keyIdx, &ecc, memSz) == 0) {
            wc_ecc_free(&ecc);
            pkey = wolfSSL_PKEY_new();
            if (pkey != NULL) {
                pkey->pkey_sz = keyIdx;
                pkey->pkey.ptr = (char*)XMALLOC(keyIdx, NULL,
                        DYNAMIC_TYPE_PUBLIC_KEY);
                if (pkey->pkey.ptr == NULL) {
                    wolfSSL_EVP_PKEY_free(pkey);
                    return NULL;
                }
                XMEMCPY(pkey->pkey.ptr, mem, keyIdx);
                pkey->type = ECDSAk;
                if (out != NULL) {
                    *out = pkey;
                }
                return pkey;
            }
        }
        wc_ecc_free(&ecc);
    }
    #endif /* HAVE_ECC */

    return NULL;
}


WOLFSSL_EVP_PKEY* wolfSSL_d2i_PUBKEY(WOLFSSL_EVP_PKEY** out, unsigned char** in,
        long inSz)
{
    WOLFSSL_EVP_PKEY* local;

    WOLFSSL_ENTER("wolfSSL_d2i_PUBKEY");

    if (in == NULL || inSz < 0) {
        WOLFSSL_MSG("Bad argument");
        return NULL;
    }

    local = wolfSSL_PKEY_new();
    if (local == NULL) {
        return NULL;
    }

    local->pkey_sz  = (int)inSz;
    local->pkey.ptr = (char*)XMALLOC(inSz, NULL, DYNAMIC_TYPE_PUBLIC_KEY);
    if (local->pkey.ptr == NULL) {
        wolfSSL_EVP_PKEY_free(local);
        local = NULL;
    }
    else {
        XMEMCPY(local->pkey.ptr, *in, inSz);
    }

    if (out != NULL) {
        *out = local;
    }

    /* creation of RSA and ECC struct here */


    return local;

}


WOLFSSL_EVP_PKEY* wolfSSL_d2i_PrivateKey(int type, WOLFSSL_EVP_PKEY** out,
        const unsigned char **in, long inSz)
{
    WOLFSSL_EVP_PKEY* local;

    WOLFSSL_ENTER("wolfSSL_d2i_PrivateKey");

    if (in == NULL || inSz < 0) {
        WOLFSSL_MSG("Bad argument");
        return NULL;
    }

    if (out != NULL && *out != NULL) {
        wolfSSL_EVP_PKEY_free(*out);
    }
    local = wolfSSL_PKEY_new();

    if (local == NULL) {
        return NULL;
    }

    local->type     = type;
    local->pkey_sz  = (int)inSz;
    local->pkey.ptr = (char*)XMALLOC(inSz, NULL, DYNAMIC_TYPE_PUBLIC_KEY);
    if (local->pkey.ptr == NULL) {
        wolfSSL_EVP_PKEY_free(local);
        local = NULL;
    }
    else {
        XMEMCPY(local->pkey.ptr, *in, inSz);
    }

    if (out != NULL) {
        *out = local;
    }

#ifndef NO_RSA
    if (type == EVP_PKEY_RSA){
        local->ownRsa = 1;
        local->rsa = wolfSSL_RSA_new();
        if (local->rsa == NULL) {
            XFREE(local, NULL, DYNAMIC_TYPE_PUBLIC_KEY);
            return NULL;
        }
        if (wolfSSL_RSA_LoadDer_ex(local->rsa,
                  (const unsigned char*)local->pkey.ptr, local->pkey_sz,
                  WOLFSSL_RSA_LOAD_PRIVATE) != SSL_SUCCESS) {
            XFREE(local, NULL, DYNAMIC_TYPE_PUBLIC_KEY);
            wolfSSL_RSA_free(local->rsa);
            return NULL;
      }
    }
#endif /* NO_RSA */

    return local;
}


long wolfSSL_ctrl(WOLFSSL* ssl, int cmd, long opt, void* pt)
{
    WOLFSSL_STUB("wolfSSL_ctrl");
    (void)ssl;
    (void)cmd;
    (void)opt;
    (void)pt;
    return SSL_FAILURE;
}


long wolfSSL_CTX_ctrl(WOLFSSL_CTX* ctx, int cmd, long opt, void* pt)
{
    WOLFSSL_STUB("wolfSSL_CTX_ctrl");
    (void)ctx;
    (void)cmd;
    (void)opt;
    (void)pt;
    return SSL_FAILURE;
}

#ifndef NO_CERTS
int wolfSSL_check_private_key(const WOLFSSL* ssl)
{
    DecodedCert der;
    word32 size;
    byte*  buff;
    int    ret;

    if (ssl == NULL) {
        return SSL_FAILURE;
    }

    size = ssl->buffers.certificate->length;
    buff = ssl->buffers.certificate->buffer;
    InitDecodedCert(&der, buff, size, ssl->heap);
    if (ParseCertRelative(&der, CERT_TYPE, NO_VERIFY, NULL) != 0) {
        FreeDecodedCert(&der);
        return SSL_FAILURE;
    }

    size = ssl->buffers.key->length;
    buff = ssl->buffers.key->buffer;
    ret  = wc_CheckPrivateKey(buff, size, &der);
    FreeDecodedCert(&der);
    return ret;
}


/* Looks for the extension matching the passed in nid
 *
 * c   : if not null then is set to status value -2 if multiple occurances
 *       of the extension are found, -1 if not found, 0 if found and not
 *       critical, and 1 if found and critical.
 * nid : Extension OID to be found.
 * idx : if NULL return first extension found match, otherwise start search at
 *       idx location and set idx to the location of extension returned.
 * returns NULL or a pointer to an WOLFSSL_STACK holding extension structure
 *
 * NOTE code for decoding extensions is in asn.c DecodeCertExtensions --
 * use already decoded extension in this function to avoid decoding twice.
 * Currently we do not make use of idx since getting pre decoded extensions.
 */
void* wolfSSL_X509_get_ext_d2i(const WOLFSSL_X509* x509,
                                                     int nid, int* c, int* idx)
{
    WOLFSSL_STACK* sk = NULL;
    WOLFSSL_ASN1_OBJECT* obj = NULL;

    WOLFSSL_ENTER("wolfSSL_X509_get_ext_d2i");

    if (x509 == NULL) {
        return NULL;
    }

    if (c != NULL) {
        *c = -1; /* default to not found */
    }

    sk = (STACK_OF(WOLFSSL_ASN1_OBJECT)*)XMALLOC(
                sizeof(STACK_OF(WOLFSSL_ASN1_OBJECT)), NULL, DYNAMIC_TYPE_ASN1);
    if (sk == NULL) {
        return NULL;
    }
    XMEMSET(sk, 0, sizeof(STACK_OF(WOLFSSL_ASN1_OBJECT)));

    switch (nid) {
        case BASIC_CA_OID:
            if (x509->basicConstSet) {
                obj = wolfSSL_ASN1_OBJECT_new();
                if (c != NULL) {
                    *c = x509->basicConstCrit;
                }
                obj->type = BASIC_CA_OID;
            }
            else {
                WOLFSSL_MSG("No Basic Constraint set");
            }
            break;

        case ALT_NAMES_OID:
            {
                DNS_entry* dns;

                if (x509->subjAltNameSet && x509->altNames != NULL) {
                    /* alt names are DNS_entry structs */
                    if (c != NULL) {
                        if (x509->altNames->next != NULL) {
                            *c = -2; /* more then one found */
                        }
                        else {
                            *c = x509->subjAltNameCrit;
                        }
                    }

                    dns = x509->altNames;
                    while (dns != NULL) {
                        obj = wolfSSL_ASN1_OBJECT_new();
                        obj->type = ALT_NAMES_OID;
                        obj->obj  = (byte*)dns->name;
                        dns = dns->next;
                        /* last dns in list add at end of function */
                        if (dns != NULL) {
                            if (wolfSSL_sk_ASN1_OBJECT_push(sk, obj) !=
                                                                  SSL_SUCCESS) {
                            WOLFSSL_MSG("Error pushing ASN1 object onto stack");
                            wolfSSL_ASN1_OBJECT_free(obj);
                            wolfSSL_sk_ASN1_OBJECT_free(sk);
                            sk = NULL;
                            }
                        }
                    }
                }
                else {
                    WOLFSSL_MSG("No Alt Names set");
                }
            }
            break;

        case CRL_DIST_OID:
            if (x509->CRLdistSet && x509->CRLInfo != NULL) {
                if (c != NULL) {
                    *c = x509->CRLdistCrit;
                }
                obj = wolfSSL_ASN1_OBJECT_new();
                obj->type  = CRL_DIST_OID;
                obj->obj   = x509->CRLInfo;
                obj->objSz = x509->CRLInfoSz;
            }
            else {
                WOLFSSL_MSG("No CRL dist set");
            }
            break;

        case AUTH_INFO_OID:
            if (x509->authInfoSet && x509->authInfo != NULL) {
                if (c != NULL) {
                    *c = x509->authInfoCrit;
                }
                obj = wolfSSL_ASN1_OBJECT_new();
                obj->type  = AUTH_INFO_OID;
                obj->obj   = x509->authInfo;
                obj->objSz = x509->authInfoSz;
            }
            else {
                WOLFSSL_MSG("No Auth Info set");
            }
            break;

        case AUTH_KEY_OID:
            if (x509->authKeyIdSet) {
                if (c != NULL) {
                    *c = x509->authKeyIdCrit;
                }
                obj = wolfSSL_ASN1_OBJECT_new();
                obj->type  = AUTH_KEY_OID;
                obj->obj   = x509->authKeyId;
                obj->objSz = x509->authKeyIdSz;
            }
            else {
                WOLFSSL_MSG("No Auth Key set");
            }
            break;

        case SUBJ_KEY_OID:
            if (x509->subjKeyIdSet) {
                if (c != NULL) {
                    *c = x509->subjKeyIdCrit;
                }
                obj = wolfSSL_ASN1_OBJECT_new();
                obj->type  = SUBJ_KEY_OID;
                obj->obj   = x509->subjKeyId;
                obj->objSz = x509->subjKeyIdSz;
            }
            else {
                WOLFSSL_MSG("No Subject Key set");
            }
            break;

        case CERT_POLICY_OID:
            #ifdef WOLFSSL_CERT_EXT
            {
                int i;

                if (x509->certPoliciesNb > 0) {
                    if (c != NULL) {
                        if (x509->certPoliciesNb > 1) {
                            *c = -2;
                        }
                        else {
                            *c = 0;
                        }
                    }

                    for (i = 0; i < x509->certPoliciesNb - 1; i++) {
                        obj = wolfSSL_ASN1_OBJECT_new();
                        obj->type  = CERT_POLICY_OID;
                        obj->obj   = (byte*)(x509->certPolicies[i]);
                        obj->objSz = MAX_CERTPOL_SZ;
                        if (wolfSSL_sk_ASN1_OBJECT_push(sk, obj)
                                                               != SSL_SUCCESS) {
                            WOLFSSL_MSG("Error pushing ASN1 object onto stack");
                            wolfSSL_ASN1_OBJECT_free(obj);
                            wolfSSL_sk_ASN1_OBJECT_free(sk);
                            sk = NULL;
                        }
                    }
                    obj = wolfSSL_ASN1_OBJECT_new();
                    obj->type  = CERT_POLICY_OID;
                    obj->obj   = (byte*)(x509->certPolicies[i]);
                    obj->objSz = MAX_CERTPOL_SZ;
                }
                else {
                    WOLFSSL_MSG("No Cert Policy set");
                }
            }
            #else
                #ifdef WOLFSSL_SEP
                if (x509->certPolicySet) {
                    if (c != NULL) {
                        *c = x509->certPolicyCrit;
                    }
                    obj = wolfSSL_ASN1_OBJECT_new();
                    obj->type  = CERT_POLICY_OID;
                }
                else {
                    WOLFSSL_MSG("No Cert Policy set");
                }
                #else
                WOLFSSL_MSG("wolfSSL not built with WOLFSSL_SEP or WOLFSSL_CERT_EXT");
                #endif /* WOLFSSL_SEP */
            #endif /* WOLFSSL_CERT_EXT */
            break;

        case KEY_USAGE_OID:
            if (x509->keyUsageSet) {
                if (c != NULL) {
                    *c = x509->keyUsageCrit;
                }
                obj = wolfSSL_ASN1_OBJECT_new();
                obj->type  = KEY_USAGE_OID;
                obj->obj   = (byte*)&(x509->keyUsage);
                obj->objSz = sizeof(word16);
            }
            else {
                WOLFSSL_MSG("No Key Usage set");
            }
            break;

        case INHIBIT_ANY_OID:
            WOLFSSL_MSG("INHIBIT ANY extension not supported");
            break;

        case EXT_KEY_USAGE_OID:
            if (x509->extKeyUsageSrc != NULL) {
                if (c != NULL) {
                    if (x509->extKeyUsageCount > 1) {
                        *c = -2;
                    }
                    else {
                        *c = x509->extKeyUsageCrit;
                    }
                }
                obj = wolfSSL_ASN1_OBJECT_new();
                obj->type  = EXT_KEY_USAGE_OID;
                obj->obj   = x509->extKeyUsageSrc;
                obj->objSz = x509->extKeyUsageSz;
            }
            else {
                WOLFSSL_MSG("No Extended Key Usage set");
            }
            break;

        case NAME_CONS_OID:
            WOLFSSL_MSG("Name Constraint OID extension not supported");
            break;

        case PRIV_KEY_USAGE_PERIOD_OID:
            WOLFSSL_MSG("Private Key Usage Period extension not supported");
            break;

        case SUBJECT_INFO_ACCESS:
            WOLFSSL_MSG("Subject Info Access extension not supported");
            break;

        case POLICY_MAP_OID:
            WOLFSSL_MSG("Policy Map extension not supported");
            break;

        case POLICY_CONST_OID:
            WOLFSSL_MSG("Policy Constraint extension not supported");
            break;

        case ISSUE_ALT_NAMES_OID:
            WOLFSSL_MSG("Issue Alt Names extension not supported");
            break;

        case TLS_FEATURE_OID:
            WOLFSSL_MSG("TLS Feature extension not supported");
            break;

        default:
            WOLFSSL_MSG("Unsupported/Unknown extension OID");
    }

    if (obj != NULL) {
        if (wolfSSL_sk_ASN1_OBJECT_push(sk, obj) != SSL_SUCCESS) {
            WOLFSSL_MSG("Error pushing ASN1 object onto stack");
            wolfSSL_ASN1_OBJECT_free(obj);
            wolfSSL_sk_ASN1_OBJECT_free(sk);
            sk = NULL;
        }
    }
    else { /* no ASN1 object found for extension, free stack */
        wolfSSL_sk_ASN1_OBJECT_free(sk);
        sk = NULL;
    }

    (void)idx;

    return sk;
}


/* this function makes the assumption that out buffer is big enough for digest*/
static int wolfSSL_EVP_Digest(unsigned char* in, int inSz, unsigned char* out,
                              unsigned int* outSz, const WOLFSSL_EVP_MD* evp,
                              WOLFSSL_ENGINE* eng)
{
    enum wc_HashType hash = WC_HASH_TYPE_NONE;
    int  hashSz;

    if (XSTRLEN(evp) < 3) {
        /* do not try comparing strings if size is too small */
        return SSL_FAILURE;
    }

    if (XSTRNCMP("SHA", evp, 3) == 0) {
        if (XSTRLEN(evp) > 3) {
            if (XSTRNCMP("SHA256", evp, 6) == 0) {
                hash = WC_HASH_TYPE_SHA256;
            }
            else if (XSTRNCMP("SHA384", evp, 6) == 0) {
                hash = WC_HASH_TYPE_SHA384;
            }
            else if (XSTRNCMP("SHA512", evp, 6) == 0) {
                hash = WC_HASH_TYPE_SHA512;
            }
            else {
                WOLFSSL_MSG("Unknown SHA hash");
            }
        }
        else {
            hash = WC_HASH_TYPE_SHA;
        }
    }
    else if (XSTRNCMP("MD2", evp, 3) == 0) {
        hash = WC_HASH_TYPE_MD2;
    }
    else if (XSTRNCMP("MD4", evp, 3) == 0) {
        hash = WC_HASH_TYPE_MD4;
    }
    else if (XSTRNCMP("MD5", evp, 3) == 0) {
        hash = WC_HASH_TYPE_MD5;
    }

    hashSz = wc_HashGetDigestSize(hash);
    if (hashSz < 0) {
        WOLFSSL_LEAVE("wolfSSL_EVP_Digest", hashSz);
        return SSL_FAILURE;
    }
    *outSz = hashSz;

    (void)eng;
    if (wc_Hash(hash, in, inSz, out, *outSz) == 0) {
        return SSL_SUCCESS;
    }
    else {
        return SSL_FAILURE;
    }
}


int wolfSSL_X509_digest(const WOLFSSL_X509* x509, const WOLFSSL_EVP_MD* digest,
        unsigned char* buf, unsigned int* len)
{
    WOLFSSL_ENTER("wolfSSL_X509_digest");

    if (x509 == NULL || digest == NULL) {
        return SSL_FAILURE;
    }

    return wolfSSL_EVP_Digest(x509->derCert->buffer, x509->derCert->length, buf,
                              len, digest, NULL);
}


int wolfSSL_use_PrivateKey(WOLFSSL* ssl, WOLFSSL_EVP_PKEY* pkey)
{
    WOLFSSL_ENTER("wolfSSL_use_PrivateKey");
    if (ssl == NULL || pkey == NULL ) {
        return SSL_FAILURE;
    }

    return wolfSSL_use_PrivateKey_buffer(ssl, (unsigned char*)pkey->pkey.ptr,
                                         pkey->pkey_sz, SSL_FILETYPE_ASN1);
}


int wolfSSL_use_PrivateKey_ASN1(int pri, WOLFSSL* ssl, unsigned char* der,
                                long derSz)
{
    WOLFSSL_ENTER("wolfSSL_use_PrivateKey_ASN1");
    if (ssl == NULL || der == NULL ) {
        return SSL_FAILURE;
    }

    (void)pri; /* type of private key */
    return wolfSSL_use_PrivateKey_buffer(ssl, der, derSz, SSL_FILETYPE_ASN1);
}


#ifndef NO_RSA
int wolfSSL_use_RSAPrivateKey_ASN1(WOLFSSL* ssl, unsigned char* der, long derSz)
{
    WOLFSSL_ENTER("wolfSSL_use_RSAPrivateKey_ASN1");
    if (ssl == NULL || der == NULL ) {
        return SSL_FAILURE;
    }

    return wolfSSL_use_PrivateKey_buffer(ssl, der, derSz, SSL_FILETYPE_ASN1);
}
#endif

int wolfSSL_use_certificate_ASN1(WOLFSSL* ssl, unsigned char* der, int derSz)
{
    long idx;

    WOLFSSL_ENTER("wolfSSL_use_certificate_ASN1");
    if (der != NULL && ssl != NULL) {
        if (ProcessBuffer(NULL, der, derSz, SSL_FILETYPE_ASN1, CERT_TYPE, ssl,
                                                        &idx, 0) == SSL_SUCCESS)
            return SSL_SUCCESS;
    }

    (void)idx;
    return SSL_FAILURE;
}


int wolfSSL_use_certificate(WOLFSSL* ssl, WOLFSSL_X509* x509)
{
    long idx;

    WOLFSSL_ENTER("wolfSSL_use_certificate");
    if (x509 != NULL && ssl != NULL && x509->derCert != NULL) {
        if (ProcessBuffer(NULL, x509->derCert->buffer, x509->derCert->length,
                     SSL_FILETYPE_ASN1, CERT_TYPE, ssl, &idx, 0) == SSL_SUCCESS)
            return SSL_SUCCESS;
    }

    (void)idx;
    return SSL_FAILURE;
}
#endif /* NO_CERTS */

#ifndef NO_FILESYSTEM

int wolfSSL_use_certificate_file(WOLFSSL* ssl, const char* file, int format)
{
    WOLFSSL_ENTER("wolfSSL_use_certificate_file");
    if (ProcessFile(ssl->ctx, file, format, CERT_TYPE,
                    ssl, 0, NULL) == SSL_SUCCESS)
        return SSL_SUCCESS;

    return SSL_FAILURE;
}


int wolfSSL_use_PrivateKey_file(WOLFSSL* ssl, const char* file, int format)
{
    WOLFSSL_ENTER("wolfSSL_use_PrivateKey_file");
    if (ProcessFile(ssl->ctx, file, format, PRIVATEKEY_TYPE,
                    ssl, 0, NULL) == SSL_SUCCESS)
        return SSL_SUCCESS;

    return SSL_FAILURE;
}


int wolfSSL_use_certificate_chain_file(WOLFSSL* ssl, const char* file)
{
   /* process up to MAX_CHAIN_DEPTH plus subject cert */
   WOLFSSL_ENTER("wolfSSL_use_certificate_chain_file");
   if (ProcessFile(ssl->ctx, file, SSL_FILETYPE_PEM, CERT_TYPE,
                   ssl, 1, NULL) == SSL_SUCCESS)
       return SSL_SUCCESS;

   return SSL_FAILURE;
}


#ifdef HAVE_ECC

/* Set Temp CTX EC-DHE size in octets, should be 20 - 66 for 160 - 521 bit */
int wolfSSL_CTX_SetTmpEC_DHE_Sz(WOLFSSL_CTX* ctx, word16 sz)
{
    if (ctx == NULL || sz < ECC_MINSIZE || sz > ECC_MAXSIZE)
        return BAD_FUNC_ARG;

    ctx->eccTempKeySz = sz;

    return SSL_SUCCESS;
}


/* Set Temp SSL EC-DHE size in octets, should be 20 - 66 for 160 - 521 bit */
int wolfSSL_SetTmpEC_DHE_Sz(WOLFSSL* ssl, word16 sz)
{
    if (ssl == NULL || sz < ECC_MINSIZE || sz > ECC_MAXSIZE)
        return BAD_FUNC_ARG;

    ssl->eccTempKeySz = sz;

    return SSL_SUCCESS;
}

#endif /* HAVE_ECC */




int wolfSSL_CTX_use_RSAPrivateKey_file(WOLFSSL_CTX* ctx,const char* file,
                                   int format)
{
    WOLFSSL_ENTER("SSL_CTX_use_RSAPrivateKey_file");

    return wolfSSL_CTX_use_PrivateKey_file(ctx, file, format);
}


int wolfSSL_use_RSAPrivateKey_file(WOLFSSL* ssl, const char* file, int format)
{
    WOLFSSL_ENTER("wolfSSL_use_RSAPrivateKey_file");

    return wolfSSL_use_PrivateKey_file(ssl, file, format);
}

#endif /* NO_FILESYSTEM */


/* Copies the master secret over to out buffer. If outSz is 0 returns the size
 * of master secret.
 *
 * ses : a session from completed TLS/SSL handshake
 * out : buffer to hold copy of master secret
 * outSz : size of out buffer
 * returns : number of bytes copied into out buffer on success
 *           less then or equal to 0 is considered a failure case
 */
int wolfSSL_SESSION_get_master_key(const WOLFSSL_SESSION* ses,
        unsigned char* out, int outSz)
{
    int size;

    if (outSz == 0) {
        return SECRET_LEN;
    }

    if (ses == NULL || out == NULL || outSz < 0) {
        return 0;
    }

    if (outSz > SECRET_LEN) {
        size = SECRET_LEN;
    }
    else {
        size = outSz;
    }

    XMEMCPY(out, ses->masterSecret, size);
    return size;
}


int wolfSSL_SESSION_get_master_key_length(const WOLFSSL_SESSION* ses)
{
    (void)ses;
    return SECRET_LEN;
}

#endif /* OPENSSL_EXTRA */

#ifndef NO_FILESYSTEM
#ifdef HAVE_NTRU

int wolfSSL_CTX_use_NTRUPrivateKey_file(WOLFSSL_CTX* ctx, const char* file)
{
    WOLFSSL_ENTER("wolfSSL_CTX_use_NTRUPrivateKey_file");
    if (ctx == NULL)
        return SSL_FAILURE;

    if (ProcessFile(ctx, file, SSL_FILETYPE_RAW, PRIVATEKEY_TYPE, NULL, 0, NULL)
                         == SSL_SUCCESS) {
        ctx->haveNTRU = 1;
        return SSL_SUCCESS;
    }

    return SSL_FAILURE;
}

#endif /* HAVE_NTRU */


#endif /* NO_FILESYSTEM */


void wolfSSL_CTX_set_verify(WOLFSSL_CTX* ctx, int mode, VerifyCallback vc)
{
    WOLFSSL_ENTER("wolfSSL_CTX_set_verify");
    if (mode & SSL_VERIFY_PEER) {
        ctx->verifyPeer = 1;
        ctx->verifyNone = 0;  /* in case previously set */
    }

    if (mode == SSL_VERIFY_NONE) {
        ctx->verifyNone = 1;
        ctx->verifyPeer = 0;  /* in case previously set */
    }

    if (mode & SSL_VERIFY_FAIL_IF_NO_PEER_CERT)
        ctx->failNoCert = 1;

    if (mode & SSL_VERIFY_FAIL_EXCEPT_PSK) {
        ctx->failNoCert    = 0; /* fail on all is set to fail on PSK */
        ctx->failNoCertxPSK = 1;
    }

    ctx->verifyCallback = vc;
}


void wolfSSL_set_verify(WOLFSSL* ssl, int mode, VerifyCallback vc)
{
    WOLFSSL_ENTER("wolfSSL_set_verify");
    if (mode & SSL_VERIFY_PEER) {
        ssl->options.verifyPeer = 1;
        ssl->options.verifyNone = 0;  /* in case previously set */
    }

    if (mode == SSL_VERIFY_NONE) {
        ssl->options.verifyNone = 1;
        ssl->options.verifyPeer = 0;  /* in case previously set */
    }

    if (mode & SSL_VERIFY_FAIL_IF_NO_PEER_CERT)
        ssl->options.failNoCert = 1;

    if (mode & SSL_VERIFY_FAIL_EXCEPT_PSK) {
        ssl->options.failNoCert    = 0; /* fail on all is set to fail on PSK */
        ssl->options.failNoCertxPSK = 1;
    }

    ssl->verifyCallback = vc;
}


/* store user ctx for verify callback */
void wolfSSL_SetCertCbCtx(WOLFSSL* ssl, void* ctx)
{
    WOLFSSL_ENTER("wolfSSL_SetCertCbCtx");
    if (ssl)
        ssl->verifyCbCtx = ctx;
}


/* store context CA Cache addition callback */
void wolfSSL_CTX_SetCACb(WOLFSSL_CTX* ctx, CallbackCACache cb)
{
    if (ctx && ctx->cm)
        ctx->cm->caCacheCallback = cb;
}


#if defined(PERSIST_CERT_CACHE)

#if !defined(NO_FILESYSTEM)

/* Persist cert cache to file */
int wolfSSL_CTX_save_cert_cache(WOLFSSL_CTX* ctx, const char* fname)
{
    WOLFSSL_ENTER("wolfSSL_CTX_save_cert_cache");

    if (ctx == NULL || fname == NULL)
        return BAD_FUNC_ARG;

    return CM_SaveCertCache(ctx->cm, fname);
}


/* Persist cert cache from file */
int wolfSSL_CTX_restore_cert_cache(WOLFSSL_CTX* ctx, const char* fname)
{
    WOLFSSL_ENTER("wolfSSL_CTX_restore_cert_cache");

    if (ctx == NULL || fname == NULL)
        return BAD_FUNC_ARG;

    return CM_RestoreCertCache(ctx->cm, fname);
}

#endif /* NO_FILESYSTEM */

/* Persist cert cache to memory */
int wolfSSL_CTX_memsave_cert_cache(WOLFSSL_CTX* ctx, void* mem,
                                   int sz, int* used)
{
    WOLFSSL_ENTER("wolfSSL_CTX_memsave_cert_cache");

    if (ctx == NULL || mem == NULL || used == NULL || sz <= 0)
        return BAD_FUNC_ARG;

    return CM_MemSaveCertCache(ctx->cm, mem, sz, used);
}


/* Restore cert cache from memory */
int wolfSSL_CTX_memrestore_cert_cache(WOLFSSL_CTX* ctx, const void* mem, int sz)
{
    WOLFSSL_ENTER("wolfSSL_CTX_memrestore_cert_cache");

    if (ctx == NULL || mem == NULL || sz <= 0)
        return BAD_FUNC_ARG;

    return CM_MemRestoreCertCache(ctx->cm, mem, sz);
}


/* get how big the the cert cache save buffer needs to be */
int wolfSSL_CTX_get_cert_cache_memsize(WOLFSSL_CTX* ctx)
{
    WOLFSSL_ENTER("wolfSSL_CTX_get_cert_cache_memsize");

    if (ctx == NULL)
        return BAD_FUNC_ARG;

    return CM_GetCertCacheMemSize(ctx->cm);
}

#endif /* PERSISTE_CERT_CACHE */
#endif /* !NO_CERTS */


#ifndef NO_SESSION_CACHE

WOLFSSL_SESSION* wolfSSL_get_session(WOLFSSL* ssl)
{
    WOLFSSL_ENTER("SSL_get_session");
    if (ssl)
        return GetSession(ssl, 0, 0);

    return NULL;
}


int wolfSSL_set_session(WOLFSSL* ssl, WOLFSSL_SESSION* session)
{
    WOLFSSL_ENTER("SSL_set_session");
    if (session)
        return SetSession(ssl, session);

    return SSL_FAILURE;
}


#ifndef NO_CLIENT_CACHE

/* Associate client session with serverID, find existing or store for saving
   if newSession flag on, don't reuse existing session
   SSL_SUCCESS on ok */
int wolfSSL_SetServerID(WOLFSSL* ssl, const byte* id, int len, int newSession)
{
    WOLFSSL_SESSION* session = NULL;

    WOLFSSL_ENTER("wolfSSL_SetServerID");

    if (ssl == NULL || id == NULL || len <= 0)
        return BAD_FUNC_ARG;

    if (newSession == 0) {
        session = GetSessionClient(ssl, id, len);
        if (session) {
            if (SetSession(ssl, session) != SSL_SUCCESS) {
                WOLFSSL_MSG("SetSession failed");
                session = NULL;
            }
        }
    }

    if (session == NULL) {
        WOLFSSL_MSG("Valid ServerID not cached already");

        ssl->session.idLen = (word16)min(SERVER_ID_LEN, (word32)len);
        XMEMCPY(ssl->session.serverID, id, ssl->session.idLen);
    }

    return SSL_SUCCESS;
}

#endif /* NO_CLIENT_CACHE */

#if defined(PERSIST_SESSION_CACHE)

/* for persistence, if changes to layout need to increment and modify
   save_session_cache() and restore_session_cache and memory versions too */
#define WOLFSSL_CACHE_VERSION 2

/* Session Cache Header information */
typedef struct {
    int version;     /* cache layout version id */
    int rows;        /* session rows */
    int columns;     /* session columns */
    int sessionSz;   /* sizeof WOLFSSL_SESSION */
} cache_header_t;

/* current persistence layout is:

   1) cache_header_t
   2) SessionCache
   3) ClientCache

   update WOLFSSL_CACHE_VERSION if change layout for the following
   PERSISTENT_SESSION_CACHE functions
*/


/* get how big the the session cache save buffer needs to be */
int wolfSSL_get_session_cache_memsize(void)
{
    int sz  = (int)(sizeof(SessionCache) + sizeof(cache_header_t));

    #ifndef NO_CLIENT_CACHE
        sz += (int)(sizeof(ClientCache));
    #endif

    return sz;
}


/* Persist session cache to memory */
int wolfSSL_memsave_session_cache(void* mem, int sz)
{
    int i;
    cache_header_t cache_header;
    SessionRow*    row  = (SessionRow*)((byte*)mem + sizeof(cache_header));
#ifndef NO_CLIENT_CACHE
    ClientRow*     clRow;
#endif

    WOLFSSL_ENTER("wolfSSL_memsave_session_cache");

    if (sz < wolfSSL_get_session_cache_memsize()) {
        WOLFSSL_MSG("Memory buffer too small");
        return BUFFER_E;
    }

    cache_header.version   = WOLFSSL_CACHE_VERSION;
    cache_header.rows      = SESSION_ROWS;
    cache_header.columns   = SESSIONS_PER_ROW;
    cache_header.sessionSz = (int)sizeof(WOLFSSL_SESSION);
    XMEMCPY(mem, &cache_header, sizeof(cache_header));

    if (wc_LockMutex(&session_mutex) != 0) {
        WOLFSSL_MSG("Session cache mutex lock failed");
        return BAD_MUTEX_E;
    }

    for (i = 0; i < cache_header.rows; ++i)
        XMEMCPY(row++, SessionCache + i, sizeof(SessionRow));

#ifndef NO_CLIENT_CACHE
    clRow = (ClientRow*)row;
    for (i = 0; i < cache_header.rows; ++i)
        XMEMCPY(clRow++, ClientCache + i, sizeof(ClientRow));
#endif

    wc_UnLockMutex(&session_mutex);

    WOLFSSL_LEAVE("wolfSSL_memsave_session_cache", SSL_SUCCESS);

    return SSL_SUCCESS;
}


/* Restore the persistent session cache from memory */
int wolfSSL_memrestore_session_cache(const void* mem, int sz)
{
    int    i;
    cache_header_t cache_header;
    SessionRow*    row  = (SessionRow*)((byte*)mem + sizeof(cache_header));
#ifndef NO_CLIENT_CACHE
    ClientRow*     clRow;
#endif

    WOLFSSL_ENTER("wolfSSL_memrestore_session_cache");

    if (sz < wolfSSL_get_session_cache_memsize()) {
        WOLFSSL_MSG("Memory buffer too small");
        return BUFFER_E;
    }

    XMEMCPY(&cache_header, mem, sizeof(cache_header));
    if (cache_header.version   != WOLFSSL_CACHE_VERSION ||
        cache_header.rows      != SESSION_ROWS ||
        cache_header.columns   != SESSIONS_PER_ROW ||
        cache_header.sessionSz != (int)sizeof(WOLFSSL_SESSION)) {

        WOLFSSL_MSG("Session cache header match failed");
        return CACHE_MATCH_ERROR;
    }

    if (wc_LockMutex(&session_mutex) != 0) {
        WOLFSSL_MSG("Session cache mutex lock failed");
        return BAD_MUTEX_E;
    }

    for (i = 0; i < cache_header.rows; ++i)
        XMEMCPY(SessionCache + i, row++, sizeof(SessionRow));

#ifndef NO_CLIENT_CACHE
    clRow = (ClientRow*)row;
    for (i = 0; i < cache_header.rows; ++i)
        XMEMCPY(ClientCache + i, clRow++, sizeof(ClientRow));
#endif

    wc_UnLockMutex(&session_mutex);

    WOLFSSL_LEAVE("wolfSSL_memrestore_session_cache", SSL_SUCCESS);

    return SSL_SUCCESS;
}

#if !defined(NO_FILESYSTEM)

/* Persist session cache to file */
/* doesn't use memsave because of additional memory use */
int wolfSSL_save_session_cache(const char *fname)
{
    XFILE  file;
    int    ret;
    int    rc = SSL_SUCCESS;
    int    i;
    cache_header_t cache_header;

    WOLFSSL_ENTER("wolfSSL_save_session_cache");

    file = XFOPEN(fname, "w+b");
    if (file == XBADFILE) {
        WOLFSSL_MSG("Couldn't open session cache save file");
        return SSL_BAD_FILE;
    }
    cache_header.version   = WOLFSSL_CACHE_VERSION;
    cache_header.rows      = SESSION_ROWS;
    cache_header.columns   = SESSIONS_PER_ROW;
    cache_header.sessionSz = (int)sizeof(WOLFSSL_SESSION);

    /* cache header */
    ret = (int)XFWRITE(&cache_header, sizeof cache_header, 1, file);
    if (ret != 1) {
        WOLFSSL_MSG("Session cache header file write failed");
        XFCLOSE(file);
        return FWRITE_ERROR;
    }

    if (wc_LockMutex(&session_mutex) != 0) {
        WOLFSSL_MSG("Session cache mutex lock failed");
        XFCLOSE(file);
        return BAD_MUTEX_E;
    }

    /* session cache */
    for (i = 0; i < cache_header.rows; ++i) {
        ret = (int)XFWRITE(SessionCache + i, sizeof(SessionRow), 1, file);
        if (ret != 1) {
            WOLFSSL_MSG("Session cache member file write failed");
            rc = FWRITE_ERROR;
            break;
        }
    }

#ifndef NO_CLIENT_CACHE
    /* client cache */
    for (i = 0; i < cache_header.rows; ++i) {
        ret = (int)XFWRITE(ClientCache + i, sizeof(ClientRow), 1, file);
        if (ret != 1) {
            WOLFSSL_MSG("Client cache member file write failed");
            rc = FWRITE_ERROR;
            break;
        }
    }
#endif /* NO_CLIENT_CACHE */

    wc_UnLockMutex(&session_mutex);

    XFCLOSE(file);
    WOLFSSL_LEAVE("wolfSSL_save_session_cache", rc);

    return rc;
}


/* Restore the persistent session cache from file */
/* doesn't use memstore because of additional memory use */
int wolfSSL_restore_session_cache(const char *fname)
{
    XFILE  file;
    int    rc = SSL_SUCCESS;
    int    ret;
    int    i;
    cache_header_t cache_header;

    WOLFSSL_ENTER("wolfSSL_restore_session_cache");

    file = XFOPEN(fname, "rb");
    if (file == XBADFILE) {
        WOLFSSL_MSG("Couldn't open session cache save file");
        return SSL_BAD_FILE;
    }
    /* cache header */
    ret = (int)XFREAD(&cache_header, sizeof cache_header, 1, file);
    if (ret != 1) {
        WOLFSSL_MSG("Session cache header file read failed");
        XFCLOSE(file);
        return FREAD_ERROR;
    }
    if (cache_header.version   != WOLFSSL_CACHE_VERSION ||
        cache_header.rows      != SESSION_ROWS ||
        cache_header.columns   != SESSIONS_PER_ROW ||
        cache_header.sessionSz != (int)sizeof(WOLFSSL_SESSION)) {

        WOLFSSL_MSG("Session cache header match failed");
        XFCLOSE(file);
        return CACHE_MATCH_ERROR;
    }

    if (wc_LockMutex(&session_mutex) != 0) {
        WOLFSSL_MSG("Session cache mutex lock failed");
        XFCLOSE(file);
        return BAD_MUTEX_E;
    }

    /* session cache */
    for (i = 0; i < cache_header.rows; ++i) {
        ret = (int)XFREAD(SessionCache + i, sizeof(SessionRow), 1, file);
        if (ret != 1) {
            WOLFSSL_MSG("Session cache member file read failed");
            XMEMSET(SessionCache, 0, sizeof SessionCache);
            rc = FREAD_ERROR;
            break;
        }
    }

#ifndef NO_CLIENT_CACHE
    /* client cache */
    for (i = 0; i < cache_header.rows; ++i) {
        ret = (int)XFREAD(ClientCache + i, sizeof(ClientRow), 1, file);
        if (ret != 1) {
            WOLFSSL_MSG("Client cache member file read failed");
            XMEMSET(ClientCache, 0, sizeof ClientCache);
            rc = FREAD_ERROR;
            break;
        }
    }

#endif /* NO_CLIENT_CACHE */

    wc_UnLockMutex(&session_mutex);

    XFCLOSE(file);
    WOLFSSL_LEAVE("wolfSSL_restore_session_cache", rc);

    return rc;
}

#endif /* !NO_FILESYSTEM */
#endif /* PERSIST_SESSION_CACHE */
#endif /* NO_SESSION_CACHE */


void wolfSSL_load_error_strings(void)   /* compatibility only */
{}


int wolfSSL_library_init(void)
{
    WOLFSSL_ENTER("SSL_library_init");
    if (wolfSSL_Init() == SSL_SUCCESS)
        return SSL_SUCCESS;
    else
        return SSL_FATAL_ERROR;
}


#ifdef HAVE_SECRET_CALLBACK

int wolfSSL_set_session_secret_cb(WOLFSSL* ssl, SessionSecretCb cb, void* ctx)
{
    WOLFSSL_ENTER("wolfSSL_set_session_secret_cb");
    if (ssl == NULL)
        return SSL_FATAL_ERROR;

    ssl->sessionSecretCb = cb;
    ssl->sessionSecretCtx = ctx;
    /* If using a pre-set key, assume session resumption. */
    ssl->session.sessionIDSz = 0;
    ssl->options.resuming = 1;

    return SSL_SUCCESS;
}

#endif


#ifndef NO_SESSION_CACHE

/* on by default if built in but allow user to turn off */
long wolfSSL_CTX_set_session_cache_mode(WOLFSSL_CTX* ctx, long mode)
{
    WOLFSSL_ENTER("SSL_CTX_set_session_cache_mode");
    if (mode == SSL_SESS_CACHE_OFF)
        ctx->sessionCacheOff = 1;

    if (mode == SSL_SESS_CACHE_NO_AUTO_CLEAR)
        ctx->sessionCacheFlushOff = 1;

    return SSL_SUCCESS;
}

#endif /* NO_SESSION_CACHE */


#if !defined(NO_CERTS)
#if defined(PERSIST_CERT_CACHE)


#define WOLFSSL_CACHE_CERT_VERSION 1

typedef struct {
    int version;                 /* cache cert layout version id */
    int rows;                    /* hash table rows, CA_TABLE_SIZE */
    int columns[CA_TABLE_SIZE];  /* columns per row on list */
    int signerSz;                /* sizeof Signer object */
} CertCacheHeader;

/* current cert persistence layout is:

   1) CertCacheHeader
   2) caTable

   update WOLFSSL_CERT_CACHE_VERSION if change layout for the following
   PERSIST_CERT_CACHE functions
*/


/* Return memory needed to persist this signer, have lock */
static INLINE int GetSignerMemory(Signer* signer)
{
    int sz = sizeof(signer->pubKeySize) + sizeof(signer->keyOID)
           + sizeof(signer->nameLen)    + sizeof(signer->subjectNameHash);

#if !defined(NO_SKID)
        sz += (int)sizeof(signer->subjectKeyIdHash);
#endif

    /* add dynamic bytes needed */
    sz += signer->pubKeySize;
    sz += signer->nameLen;

    return sz;
}


/* Return memory needed to persist this row, have lock */
static INLINE int GetCertCacheRowMemory(Signer* row)
{
    int sz = 0;

    while (row) {
        sz += GetSignerMemory(row);
        row = row->next;
    }

    return sz;
}


/* get the size of persist cert cache, have lock */
static INLINE int GetCertCacheMemSize(WOLFSSL_CERT_MANAGER* cm)
{
    int sz;
    int i;

    sz = sizeof(CertCacheHeader);

    for (i = 0; i < CA_TABLE_SIZE; i++)
        sz += GetCertCacheRowMemory(cm->caTable[i]);

    return sz;
}


/* Store cert cache header columns with number of items per list, have lock */
static INLINE void SetCertHeaderColumns(WOLFSSL_CERT_MANAGER* cm, int* columns)
{
    int     i;
    Signer* row;

    for (i = 0; i < CA_TABLE_SIZE; i++) {
        int count = 0;
        row = cm->caTable[i];

        while (row) {
            ++count;
            row = row->next;
        }
        columns[i] = count;
    }
}


/* Restore whole cert row from memory, have lock, return bytes consumed,
   < 0 on error, have lock */
static INLINE int RestoreCertRow(WOLFSSL_CERT_MANAGER* cm, byte* current,
                                 int row, int listSz, const byte* end)
{
    int idx = 0;

    if (listSz < 0) {
        WOLFSSL_MSG("Row header corrupted, negative value");
        return PARSE_ERROR;
    }

    while (listSz) {
        Signer* signer;
        byte*   start = current + idx;  /* for end checks on this signer */
        int     minSz = sizeof(signer->pubKeySize) + sizeof(signer->keyOID) +
                      sizeof(signer->nameLen) + sizeof(signer->subjectNameHash);
        #ifndef NO_SKID
                minSz += (int)sizeof(signer->subjectKeyIdHash);
        #endif

        if (start + minSz > end) {
            WOLFSSL_MSG("Would overread restore buffer");
            return BUFFER_E;
        }
        signer = MakeSigner(cm->heap);
        if (signer == NULL)
            return MEMORY_E;

        /* pubKeySize */
        XMEMCPY(&signer->pubKeySize, current + idx, sizeof(signer->pubKeySize));
        idx += (int)sizeof(signer->pubKeySize);

        /* keyOID */
        XMEMCPY(&signer->keyOID, current + idx, sizeof(signer->keyOID));
        idx += (int)sizeof(signer->keyOID);

        /* pulicKey */
        if (start + minSz + signer->pubKeySize > end) {
            WOLFSSL_MSG("Would overread restore buffer");
            FreeSigner(signer, cm->heap);
            return BUFFER_E;
        }
        signer->publicKey = (byte*)XMALLOC(signer->pubKeySize, cm->heap,
                                           DYNAMIC_TYPE_KEY);
        if (signer->publicKey == NULL) {
            FreeSigner(signer, cm->heap);
            return MEMORY_E;
        }

        XMEMCPY(signer->publicKey, current + idx, signer->pubKeySize);
        idx += signer->pubKeySize;

        /* nameLen */
        XMEMCPY(&signer->nameLen, current + idx, sizeof(signer->nameLen));
        idx += (int)sizeof(signer->nameLen);

        /* name */
        if (start + minSz + signer->pubKeySize + signer->nameLen > end) {
            WOLFSSL_MSG("Would overread restore buffer");
            FreeSigner(signer, cm->heap);
            return BUFFER_E;
        }
        signer->name = (char*)XMALLOC(signer->nameLen, cm->heap,
                                      DYNAMIC_TYPE_SUBJECT_CN);
        if (signer->name == NULL) {
            FreeSigner(signer, cm->heap);
            return MEMORY_E;
        }

        XMEMCPY(signer->name, current + idx, signer->nameLen);
        idx += signer->nameLen;

        /* subjectNameHash */
        XMEMCPY(signer->subjectNameHash, current + idx, SIGNER_DIGEST_SIZE);
        idx += SIGNER_DIGEST_SIZE;

        #ifndef NO_SKID
            /* subjectKeyIdHash */
            XMEMCPY(signer->subjectKeyIdHash, current + idx,SIGNER_DIGEST_SIZE);
            idx += SIGNER_DIGEST_SIZE;
        #endif

        signer->next = cm->caTable[row];
        cm->caTable[row] = signer;

        --listSz;
    }

    return idx;
}


/* Store whole cert row into memory, have lock, return bytes added */
static INLINE int StoreCertRow(WOLFSSL_CERT_MANAGER* cm, byte* current, int row)
{
    int     added  = 0;
    Signer* list   = cm->caTable[row];

    while (list) {
        XMEMCPY(current + added, &list->pubKeySize, sizeof(list->pubKeySize));
        added += (int)sizeof(list->pubKeySize);

        XMEMCPY(current + added, &list->keyOID,     sizeof(list->keyOID));
        added += (int)sizeof(list->keyOID);

        XMEMCPY(current + added, list->publicKey, list->pubKeySize);
        added += list->pubKeySize;

        XMEMCPY(current + added, &list->nameLen, sizeof(list->nameLen));
        added += (int)sizeof(list->nameLen);

        XMEMCPY(current + added, list->name, list->nameLen);
        added += list->nameLen;

        XMEMCPY(current + added, list->subjectNameHash, SIGNER_DIGEST_SIZE);
        added += SIGNER_DIGEST_SIZE;

        #ifndef NO_SKID
            XMEMCPY(current + added, list->subjectKeyIdHash,SIGNER_DIGEST_SIZE);
            added += SIGNER_DIGEST_SIZE;
        #endif

        list = list->next;
    }

    return added;
}


/* Persist cert cache to memory, have lock */
static INLINE int DoMemSaveCertCache(WOLFSSL_CERT_MANAGER* cm,
                                     void* mem, int sz)
{
    int realSz;
    int ret = SSL_SUCCESS;
    int i;

    WOLFSSL_ENTER("DoMemSaveCertCache");

    realSz = GetCertCacheMemSize(cm);
    if (realSz > sz) {
        WOLFSSL_MSG("Mem output buffer too small");
        ret = BUFFER_E;
    }
    else {
        byte*           current;
        CertCacheHeader hdr;

        hdr.version  = WOLFSSL_CACHE_CERT_VERSION;
        hdr.rows     = CA_TABLE_SIZE;
        SetCertHeaderColumns(cm, hdr.columns);
        hdr.signerSz = (int)sizeof(Signer);

        XMEMCPY(mem, &hdr, sizeof(CertCacheHeader));
        current = (byte*)mem + sizeof(CertCacheHeader);

        for (i = 0; i < CA_TABLE_SIZE; ++i)
            current += StoreCertRow(cm, current, i);
    }

    return ret;
}


#if !defined(NO_FILESYSTEM)

/* Persist cert cache to file */
int CM_SaveCertCache(WOLFSSL_CERT_MANAGER* cm, const char* fname)
{
    XFILE file;
    int   rc = SSL_SUCCESS;
    int   memSz;
    byte* mem;

    WOLFSSL_ENTER("CM_SaveCertCache");

    file = XFOPEN(fname, "w+b");
    if (file == XBADFILE) {
       WOLFSSL_MSG("Couldn't open cert cache save file");
       return SSL_BAD_FILE;
    }

    if (wc_LockMutex(&cm->caLock) != 0) {
        WOLFSSL_MSG("wc_LockMutex on caLock failed");
        XFCLOSE(file);
        return BAD_MUTEX_E;
    }

    memSz = GetCertCacheMemSize(cm);
    mem   = (byte*)XMALLOC(memSz, cm->heap, DYNAMIC_TYPE_TMP_BUFFER);
    if (mem == NULL) {
        WOLFSSL_MSG("Alloc for tmp buffer failed");
        rc = MEMORY_E;
    } else {
        rc = DoMemSaveCertCache(cm, mem, memSz);
        if (rc == SSL_SUCCESS) {
            int ret = (int)XFWRITE(mem, memSz, 1, file);
            if (ret != 1) {
                WOLFSSL_MSG("Cert cache file write failed");
                rc = FWRITE_ERROR;
            }
        }
        XFREE(mem, cm->heap, DYNAMIC_TYPE_TMP_BUFFER);
    }

    wc_UnLockMutex(&cm->caLock);
    XFCLOSE(file);

    return rc;
}


/* Restore cert cache from file */
int CM_RestoreCertCache(WOLFSSL_CERT_MANAGER* cm, const char* fname)
{
    XFILE file;
    int   rc = SSL_SUCCESS;
    int   ret;
    int   memSz;
    byte* mem;

    WOLFSSL_ENTER("CM_RestoreCertCache");

    file = XFOPEN(fname, "rb");
    if (file == XBADFILE) {
       WOLFSSL_MSG("Couldn't open cert cache save file");
       return SSL_BAD_FILE;
    }

    XFSEEK(file, 0, XSEEK_END);
    memSz = (int)XFTELL(file);
    XREWIND(file);

    if (memSz <= 0) {
        WOLFSSL_MSG("Bad file size");
        XFCLOSE(file);
        return SSL_BAD_FILE;
    }

    mem = (byte*)XMALLOC(memSz, cm->heap, DYNAMIC_TYPE_TMP_BUFFER);
    if (mem == NULL) {
        WOLFSSL_MSG("Alloc for tmp buffer failed");
        XFCLOSE(file);
        return MEMORY_E;
    }

    ret = (int)XFREAD(mem, memSz, 1, file);
    if (ret != 1) {
        WOLFSSL_MSG("Cert file read error");
        rc = FREAD_ERROR;
    } else {
        rc = CM_MemRestoreCertCache(cm, mem, memSz);
        if (rc != SSL_SUCCESS) {
            WOLFSSL_MSG("Mem restore cert cache failed");
        }
    }

    XFREE(mem, cm->heap, DYNAMIC_TYPE_TMP_BUFFER);
    XFCLOSE(file);

    return rc;
}

#endif /* NO_FILESYSTEM */


/* Persist cert cache to memory */
int CM_MemSaveCertCache(WOLFSSL_CERT_MANAGER* cm, void* mem, int sz, int* used)
{
    int ret = SSL_SUCCESS;

    WOLFSSL_ENTER("CM_MemSaveCertCache");

    if (wc_LockMutex(&cm->caLock) != 0) {
        WOLFSSL_MSG("wc_LockMutex on caLock failed");
        return BAD_MUTEX_E;
    }

    ret = DoMemSaveCertCache(cm, mem, sz);
    if (ret == SSL_SUCCESS)
        *used  = GetCertCacheMemSize(cm);

    wc_UnLockMutex(&cm->caLock);

    return ret;
}


/* Restore cert cache from memory */
int CM_MemRestoreCertCache(WOLFSSL_CERT_MANAGER* cm, const void* mem, int sz)
{
    int ret = SSL_SUCCESS;
    int i;
    CertCacheHeader* hdr = (CertCacheHeader*)mem;
    byte*            current = (byte*)mem + sizeof(CertCacheHeader);
    byte*            end     = (byte*)mem + sz;  /* don't go over */

    WOLFSSL_ENTER("CM_MemRestoreCertCache");

    if (current > end) {
        WOLFSSL_MSG("Cert Cache Memory buffer too small");
        return BUFFER_E;
    }

    if (hdr->version  != WOLFSSL_CACHE_CERT_VERSION ||
        hdr->rows     != CA_TABLE_SIZE ||
        hdr->signerSz != (int)sizeof(Signer)) {

        WOLFSSL_MSG("Cert Cache Memory header mismatch");
        return CACHE_MATCH_ERROR;
    }

    if (wc_LockMutex(&cm->caLock) != 0) {
        WOLFSSL_MSG("wc_LockMutex on caLock failed");
        return BAD_MUTEX_E;
    }

    FreeSignerTable(cm->caTable, CA_TABLE_SIZE, cm->heap);

    for (i = 0; i < CA_TABLE_SIZE; ++i) {
        int added = RestoreCertRow(cm, current, i, hdr->columns[i], end);
        if (added < 0) {
            WOLFSSL_MSG("RestoreCertRow error");
            ret = added;
            break;
        }
        current += added;
    }

    wc_UnLockMutex(&cm->caLock);

    return ret;
}


/* get how big the the cert cache save buffer needs to be */
int CM_GetCertCacheMemSize(WOLFSSL_CERT_MANAGER* cm)
{
    int sz;

    WOLFSSL_ENTER("CM_GetCertCacheMemSize");

    if (wc_LockMutex(&cm->caLock) != 0) {
        WOLFSSL_MSG("wc_LockMutex on caLock failed");
        return BAD_MUTEX_E;
    }

    sz = GetCertCacheMemSize(cm);

    wc_UnLockMutex(&cm->caLock);

    return sz;
}

#endif /* PERSIST_CERT_CACHE */
#endif /* NO_CERTS */


int wolfSSL_CTX_set_cipher_list(WOLFSSL_CTX* ctx, const char* list)
{
    WOLFSSL_ENTER("wolfSSL_CTX_set_cipher_list");

    /* alloc/init on demand only */
    if (ctx->suites == NULL) {
        ctx->suites = (Suites*)XMALLOC(sizeof(Suites), ctx->heap,
                                       DYNAMIC_TYPE_SUITES);
        if (ctx->suites == NULL) {
            WOLFSSL_MSG("Memory alloc for Suites failed");
            return SSL_FAILURE;
        }
        XMEMSET(ctx->suites, 0, sizeof(Suites));
    }

    return (SetCipherList(ctx->suites, list)) ? SSL_SUCCESS : SSL_FAILURE;
}


int wolfSSL_set_cipher_list(WOLFSSL* ssl, const char* list)
{
    WOLFSSL_ENTER("wolfSSL_set_cipher_list");
    return (SetCipherList(ssl->suites, list)) ? SSL_SUCCESS : SSL_FAILURE;
}


#ifndef WOLFSSL_LEANPSK
#ifdef WOLFSSL_DTLS

int wolfSSL_dtls_get_current_timeout(WOLFSSL* ssl)
{
    (void)ssl;

    return ssl->dtls_timeout;
}


/* user may need to alter init dtls recv timeout, SSL_SUCCESS on ok */
int wolfSSL_dtls_set_timeout_init(WOLFSSL* ssl, int timeout)
{
    if (ssl == NULL || timeout < 0)
        return BAD_FUNC_ARG;

    if (timeout > ssl->dtls_timeout_max) {
        WOLFSSL_MSG("Can't set dtls timeout init greater than dtls timeout max");
        return BAD_FUNC_ARG;
    }

    ssl->dtls_timeout_init = timeout;
    ssl->dtls_timeout = timeout;

    return SSL_SUCCESS;
}


/* user may need to alter max dtls recv timeout, SSL_SUCCESS on ok */
int wolfSSL_dtls_set_timeout_max(WOLFSSL* ssl, int timeout)
{
    if (ssl == NULL || timeout < 0)
        return BAD_FUNC_ARG;

    if (timeout < ssl->dtls_timeout_init) {
        WOLFSSL_MSG("Can't set dtls timeout max less than dtls timeout init");
        return BAD_FUNC_ARG;
    }

    ssl->dtls_timeout_max = timeout;

    return SSL_SUCCESS;
}


int wolfSSL_dtls_got_timeout(WOLFSSL* ssl)
{
    int result = SSL_SUCCESS;

    if (!ssl->options.handShakeDone &&
        (DtlsMsgPoolTimeout(ssl) < 0 || DtlsMsgPoolSend(ssl, 0) < 0)) {

        result = SSL_FATAL_ERROR;
    }
    return result;
}

#endif /* DTLS */
#endif /* LEANPSK */


#if defined(WOLFSSL_DTLS) && !defined(NO_WOLFSSL_SERVER)

/* Not an SSL function, return 0 for success, error code otherwise */
/* Prereq: ssl's RNG needs to be initialized. */
int wolfSSL_DTLS_SetCookieSecret(WOLFSSL* ssl,
                                 const byte* secret, word32 secretSz)
{
    WOLFSSL_ENTER("wolfSSL_DTLS_SetCookieSecret");

    if (ssl == NULL) {
        WOLFSSL_MSG("need a SSL object");
        return BAD_FUNC_ARG;
    }

    if (secret != NULL && secretSz == 0) {
        WOLFSSL_MSG("can't have a new secret without a size");
        return BAD_FUNC_ARG;
    }

    /* If secretSz is 0, use the default size. */
    if (secretSz == 0)
        secretSz = COOKIE_SECRET_SZ;

    if (secretSz != ssl->buffers.dtlsCookieSecret.length) {
        byte* newSecret;

        if (ssl->buffers.dtlsCookieSecret.buffer != NULL) {
            ForceZero(ssl->buffers.dtlsCookieSecret.buffer,
                      ssl->buffers.dtlsCookieSecret.length);
            XFREE(ssl->buffers.dtlsCookieSecret.buffer,
                  ssl->heap, DYNAMIC_TYPE_NONE);
        }

        newSecret = (byte*)XMALLOC(secretSz, ssl->heap,DYNAMIC_TYPE_COOKIE_PWD);
        if (newSecret == NULL) {
            ssl->buffers.dtlsCookieSecret.buffer = NULL;
            ssl->buffers.dtlsCookieSecret.length = 0;
            WOLFSSL_MSG("couldn't allocate new cookie secret");
            return MEMORY_ERROR;
        }
        ssl->buffers.dtlsCookieSecret.buffer = newSecret;
        ssl->buffers.dtlsCookieSecret.length = secretSz;
    }

    /* If the supplied secret is NULL, randomly generate a new secret. */
    if (secret == NULL)
        wc_RNG_GenerateBlock(ssl->rng,
                             ssl->buffers.dtlsCookieSecret.buffer, secretSz);
    else
        XMEMCPY(ssl->buffers.dtlsCookieSecret.buffer, secret, secretSz);

    WOLFSSL_LEAVE("wolfSSL_DTLS_SetCookieSecret", 0);
    return 0;
}

#endif /* WOLFSSL_DTLS && !NO_WOLFSSL_SERVER */

#ifdef OPENSSL_EXTRA
    WOLFSSL_METHOD* wolfSSLv23_method(void) {
        WOLFSSL_METHOD* m;
        WOLFSSL_ENTER("wolfSSLv23_method");
#ifndef NO_WOLFSSL_CLIENT
        m = wolfSSLv23_client_method();
#else
        m = wolfSSLv23_server_method();
#endif
        if (m != NULL) {
            m->side = WOLFSSL_NEITHER_END;
        }

        return m;
    }
#endif /* OPENSSL_EXTRA */

/* client only parts */
#ifndef NO_WOLFSSL_CLIENT

    #if defined(WOLFSSL_ALLOW_SSLV3) && !defined(NO_OLD_TLS)
    WOLFSSL_METHOD* wolfSSLv3_client_method(void)
    {
        WOLFSSL_ENTER("SSLv3_client_method");
        return wolfSSLv3_client_method_ex(NULL);
    }
    #endif

    #ifdef WOLFSSL_DTLS

        #ifndef NO_OLD_TLS
        WOLFSSL_METHOD* wolfDTLSv1_client_method(void)
        {
            WOLFSSL_ENTER("DTLSv1_client_method");
            return wolfDTLSv1_client_method_ex(NULL);
        }
        #endif  /* NO_OLD_TLS */

        WOLFSSL_METHOD* wolfDTLSv1_2_client_method(void)
        {
            WOLFSSL_ENTER("DTLSv1_2_client_method");
            return wolfDTLSv1_2_client_method_ex(NULL);
        }
    #endif

    #if defined(WOLFSSL_ALLOW_SSLV3) && !defined(NO_OLD_TLS)
    WOLFSSL_METHOD* wolfSSLv3_client_method_ex(void* heap)
    {
        WOLFSSL_METHOD* method =
                              (WOLFSSL_METHOD*) XMALLOC(sizeof(WOLFSSL_METHOD),
                                                     heap, DYNAMIC_TYPE_METHOD);
        WOLFSSL_ENTER("SSLv3_client_method_ex");
        if (method)
            InitSSL_Method(method, MakeSSLv3());
        return method;
    }
    #endif

    #ifdef WOLFSSL_DTLS

        #ifndef NO_OLD_TLS
        WOLFSSL_METHOD* wolfDTLSv1_client_method_ex(void* heap)
        {
            WOLFSSL_METHOD* method =
                              (WOLFSSL_METHOD*) XMALLOC(sizeof(WOLFSSL_METHOD),
                                                     heap, DYNAMIC_TYPE_METHOD);
            WOLFSSL_ENTER("DTLSv1_client_method_ex");
            if (method)
                InitSSL_Method(method, MakeDTLSv1());
            return method;
        }
        #endif  /* NO_OLD_TLS */

        WOLFSSL_METHOD* wolfDTLSv1_2_client_method_ex(void* heap)
        {
            WOLFSSL_METHOD* method =
                              (WOLFSSL_METHOD*) XMALLOC(sizeof(WOLFSSL_METHOD),
                                                     heap, DYNAMIC_TYPE_METHOD);
            WOLFSSL_ENTER("DTLSv1_2_client_method_ex");
            if (method)
                InitSSL_Method(method, MakeDTLSv1_2());
            (void)heap;
            return method;
        }
    #endif

    /* If SCTP is not enabled returns the state of the dtls option.
     * If SCTP is enabled returns dtls && !sctp. */
    static INLINE int IsDtlsNotSctpMode(WOLFSSL* ssl)
    {
        int result = ssl->options.dtls;

        if (result) {
        #ifdef WOLFSSL_SCTP
            result = !ssl->options.dtlsSctp;
        #endif
        }

        return result;
    }

    /* please see note at top of README if you get an error from connect */
    int wolfSSL_connect(WOLFSSL* ssl)
    {
        int neededState;

        WOLFSSL_ENTER("SSL_connect()");

        #ifdef HAVE_ERRNO_H
            errno = 0;
        #endif

        if (ssl == NULL)
            return BAD_FUNC_ARG;

        if (ssl->options.side != WOLFSSL_CLIENT_END) {
            WOLFSSL_ERROR(ssl->error = SIDE_ERROR);
            return SSL_FATAL_ERROR;
        }

        #ifdef WOLFSSL_DTLS
            if (ssl->version.major == DTLS_MAJOR) {
                ssl->options.dtls   = 1;
                ssl->options.tls    = 1;
                ssl->options.tls1_1 = 1;
            }
        #endif

        if (ssl->buffers.outputBuffer.length > 0) {
            if ( (ssl->error = SendBuffered(ssl)) == 0) {
                /* fragOffset is non-zero when sending fragments. On the last
                 * fragment, fragOffset is zero again, and the state can be
                 * advanced. */
                if (ssl->fragOffset == 0) {
                    ssl->options.connectState++;
                    WOLFSSL_MSG("connect state: "
                                "Advanced from last buffered fragment send");
                }
                else {
                    WOLFSSL_MSG("connect state: "
                                "Not advanced, more fragments to send");
                }
            }
            else {
                WOLFSSL_ERROR(ssl->error);
                return SSL_FATAL_ERROR;
            }
        }

        switch (ssl->options.connectState) {

        case CONNECT_BEGIN :
            /* always send client hello first */
            if ( (ssl->error = SendClientHello(ssl)) != 0) {
                WOLFSSL_ERROR(ssl->error);
                return SSL_FATAL_ERROR;
            }
            ssl->options.connectState = CLIENT_HELLO_SENT;
            WOLFSSL_MSG("connect state: CLIENT_HELLO_SENT");

        case CLIENT_HELLO_SENT :
            neededState = ssl->options.resuming ? SERVER_FINISHED_COMPLETE :
                                          SERVER_HELLODONE_COMPLETE;
            #ifdef WOLFSSL_DTLS
                /* In DTLS, when resuming, we can go straight to FINISHED,
                 * or do a cookie exchange and then skip to FINISHED, assume
                 * we need the cookie exchange first. */
                if (IsDtlsNotSctpMode(ssl))
                    neededState = SERVER_HELLOVERIFYREQUEST_COMPLETE;
            #endif
            /* get response */
            while (ssl->options.serverState < neededState) {
                if ( (ssl->error = ProcessReply(ssl)) < 0) {
                    WOLFSSL_ERROR(ssl->error);
                    return SSL_FATAL_ERROR;
                }
                /* if resumption failed, reset needed state */
                else if (neededState == SERVER_FINISHED_COMPLETE)
                    if (!ssl->options.resuming) {
                        if (!IsDtlsNotSctpMode(ssl))
                            neededState = SERVER_HELLODONE_COMPLETE;
                        else
                            neededState = SERVER_HELLOVERIFYREQUEST_COMPLETE;
                    }
            }

            ssl->options.connectState = HELLO_AGAIN;
            WOLFSSL_MSG("connect state: HELLO_AGAIN");

        case HELLO_AGAIN :
            if (ssl->options.certOnly)
                return SSL_SUCCESS;

            #ifdef WOLFSSL_DTLS
                if (IsDtlsNotSctpMode(ssl)) {
                    /* re-init hashes, exclude first hello and verify request */
#ifndef NO_OLD_TLS
                    wc_InitMd5(&ssl->hsHashes->hashMd5);
                    if ( (ssl->error = wc_InitSha(&ssl->hsHashes->hashSha))
                                                                         != 0) {
                        WOLFSSL_ERROR(ssl->error);
                        return SSL_FATAL_ERROR;
                    }
#endif
                    if (IsAtLeastTLSv1_2(ssl)) {
                        #ifndef NO_SHA256
                            if ( (ssl->error = wc_InitSha256(
                                            &ssl->hsHashes->hashSha256)) != 0) {
                                WOLFSSL_ERROR(ssl->error);
                                return SSL_FATAL_ERROR;
                            }
                        #endif
                        #ifdef WOLFSSL_SHA384
                            if ( (ssl->error = wc_InitSha384(
                                            &ssl->hsHashes->hashSha384)) != 0) {
                                WOLFSSL_ERROR(ssl->error);
                                return SSL_FATAL_ERROR;
                            }
                        #endif
                        #ifdef WOLFSSL_SHA512
                            if ( (ssl->error = wc_InitSha512(
                                            &ssl->hsHashes->hashSha512)) != 0) {
                                WOLFSSL_ERROR(ssl->error);
                                return SSL_FATAL_ERROR;
                            }
                        #endif
                    }
                    if ( (ssl->error = SendClientHello(ssl)) != 0) {
                        WOLFSSL_ERROR(ssl->error);
                        return SSL_FATAL_ERROR;
                    }
                }
            #endif

            ssl->options.connectState = HELLO_AGAIN_REPLY;
            WOLFSSL_MSG("connect state: HELLO_AGAIN_REPLY");

        case HELLO_AGAIN_REPLY :
            #ifdef WOLFSSL_DTLS
                if (IsDtlsNotSctpMode(ssl)) {
                    neededState = ssl->options.resuming ?
                           SERVER_FINISHED_COMPLETE : SERVER_HELLODONE_COMPLETE;

                    /* get response */
                    while (ssl->options.serverState < neededState) {
                        if ( (ssl->error = ProcessReply(ssl)) < 0) {
                                WOLFSSL_ERROR(ssl->error);
                                return SSL_FATAL_ERROR;
                        }
                        /* if resumption failed, reset needed state */
                        else if (neededState == SERVER_FINISHED_COMPLETE)
                            if (!ssl->options.resuming)
                                neededState = SERVER_HELLODONE_COMPLETE;
                    }
                }
            #endif

            ssl->options.connectState = FIRST_REPLY_DONE;
            WOLFSSL_MSG("connect state: FIRST_REPLY_DONE");

        case FIRST_REPLY_DONE :
            #ifndef NO_CERTS
                if (ssl->options.sendVerify) {
                    if ( (ssl->error = SendCertificate(ssl)) != 0) {
                        WOLFSSL_ERROR(ssl->error);
                        return SSL_FATAL_ERROR;
                    }
                    WOLFSSL_MSG("sent: certificate");
                }

            #endif
            ssl->options.connectState = FIRST_REPLY_FIRST;
            WOLFSSL_MSG("connect state: FIRST_REPLY_FIRST");

        case FIRST_REPLY_FIRST :
            if (!ssl->options.resuming) {
                if ( (ssl->error = SendClientKeyExchange(ssl)) != 0) {
                    WOLFSSL_ERROR(ssl->error);
                    return SSL_FATAL_ERROR;
                }
                WOLFSSL_MSG("sent: client key exchange");
            }

            ssl->options.connectState = FIRST_REPLY_SECOND;
            WOLFSSL_MSG("connect state: FIRST_REPLY_SECOND");

        case FIRST_REPLY_SECOND :
            #ifndef NO_CERTS
                if (ssl->options.sendVerify) {
                    if ( (ssl->error = SendCertificateVerify(ssl)) != 0) {
                        WOLFSSL_ERROR(ssl->error);
                        return SSL_FATAL_ERROR;
                    }
                    WOLFSSL_MSG("sent: certificate verify");
                }
            #endif
            ssl->options.connectState = FIRST_REPLY_THIRD;
            WOLFSSL_MSG("connect state: FIRST_REPLY_THIRD");

        case FIRST_REPLY_THIRD :
            if ( (ssl->error = SendChangeCipher(ssl)) != 0) {
                WOLFSSL_ERROR(ssl->error);
                return SSL_FATAL_ERROR;
            }
            WOLFSSL_MSG("sent: change cipher spec");
            ssl->options.connectState = FIRST_REPLY_FOURTH;
            WOLFSSL_MSG("connect state: FIRST_REPLY_FOURTH");

        case FIRST_REPLY_FOURTH :
            if ( (ssl->error = SendFinished(ssl)) != 0) {
                WOLFSSL_ERROR(ssl->error);
                return SSL_FATAL_ERROR;
            }
            WOLFSSL_MSG("sent: finished");
            ssl->options.connectState = FINISHED_DONE;
            WOLFSSL_MSG("connect state: FINISHED_DONE");

        case FINISHED_DONE :
            /* get response */
            while (ssl->options.serverState < SERVER_FINISHED_COMPLETE)
                if ( (ssl->error = ProcessReply(ssl)) < 0) {
                    WOLFSSL_ERROR(ssl->error);
                    return SSL_FATAL_ERROR;
                }

            ssl->options.connectState = SECOND_REPLY_DONE;
            WOLFSSL_MSG("connect state: SECOND_REPLY_DONE");

        case SECOND_REPLY_DONE:
#ifndef NO_HANDSHAKE_DONE_CB
            if (ssl->hsDoneCb) {
                int cbret = ssl->hsDoneCb(ssl, ssl->hsDoneCtx);
                if (cbret < 0) {
                    ssl->error = cbret;
                    WOLFSSL_MSG("HandShake Done Cb don't continue error");
                    return SSL_FATAL_ERROR;
                }
            }
#endif /* NO_HANDSHAKE_DONE_CB */

            if (!ssl->options.dtls) {
                FreeHandshakeResources(ssl);
            }
#ifdef WOLFSSL_DTLS
            else {
                ssl->options.dtlsHsRetain = 1;
            }
#endif /* WOLFSSL_DTLS */

            WOLFSSL_LEAVE("SSL_connect()", SSL_SUCCESS);
            return SSL_SUCCESS;

        default:
            WOLFSSL_MSG("Unknown connect state ERROR");
            return SSL_FATAL_ERROR; /* unknown connect state */
        }
    }

#endif /* NO_WOLFSSL_CLIENT */


/* server only parts */
#ifndef NO_WOLFSSL_SERVER

    #if defined(WOLFSSL_ALLOW_SSLV3) && !defined(NO_OLD_TLS)
    WOLFSSL_METHOD* wolfSSLv3_server_method(void)
    {
        WOLFSSL_ENTER("SSLv3_server_method");
        return wolfSSLv3_server_method_ex(NULL);
    }
    #endif


    #ifdef WOLFSSL_DTLS

        #ifndef NO_OLD_TLS
        WOLFSSL_METHOD* wolfDTLSv1_server_method(void)
        {
            WOLFSSL_ENTER("DTLSv1_server_method");
            return wolfDTLSv1_server_method_ex(NULL);
        }
        #endif /* NO_OLD_TLS */

        WOLFSSL_METHOD* wolfDTLSv1_2_server_method(void)
        {
            WOLFSSL_ENTER("DTLSv1_2_server_method");
            return wolfDTLSv1_2_server_method_ex(NULL);
        }
    #endif

    #if defined(WOLFSSL_ALLOW_SSLV3) && !defined(NO_OLD_TLS)
    WOLFSSL_METHOD* wolfSSLv3_server_method_ex(void* heap)
    {
        WOLFSSL_METHOD* method =
                              (WOLFSSL_METHOD*) XMALLOC(sizeof(WOLFSSL_METHOD),
                                                     heap, DYNAMIC_TYPE_METHOD);
        WOLFSSL_ENTER("SSLv3_server_method_ex");
        if (method) {
            InitSSL_Method(method, MakeSSLv3());
            method->side = WOLFSSL_SERVER_END;
        }
        return method;
    }
    #endif


    #ifdef WOLFSSL_DTLS

        #ifndef NO_OLD_TLS
        WOLFSSL_METHOD* wolfDTLSv1_server_method_ex(void* heap)
        {
            WOLFSSL_METHOD* method =
                              (WOLFSSL_METHOD*) XMALLOC(sizeof(WOLFSSL_METHOD),
                                                     heap, DYNAMIC_TYPE_METHOD);
            WOLFSSL_ENTER("DTLSv1_server_method_ex");
            if (method) {
                InitSSL_Method(method, MakeDTLSv1());
                method->side = WOLFSSL_SERVER_END;
            }
            return method;
        }
        #endif /* NO_OLD_TLS */

        WOLFSSL_METHOD* wolfDTLSv1_2_server_method_ex(void* heap)
        {
            WOLFSSL_METHOD* method =
                              (WOLFSSL_METHOD*) XMALLOC(sizeof(WOLFSSL_METHOD),
                                                     heap, DYNAMIC_TYPE_METHOD);
            WOLFSSL_ENTER("DTLSv1_2_server_method_ex");
            if (method) {
                InitSSL_Method(method, MakeDTLSv1_2());
                method->side = WOLFSSL_SERVER_END;
            }
            (void)heap;
            return method;
        }
    #endif

    int wolfSSL_accept(WOLFSSL* ssl)
    {
        word16 havePSK = 0;
        word16 haveAnon = 0;
        WOLFSSL_ENTER("SSL_accept()");

        #ifdef HAVE_ERRNO_H
            errno = 0;
        #endif

        #ifndef NO_PSK
            havePSK = ssl->options.havePSK;
        #endif
        (void)havePSK;

        #ifdef HAVE_ANON
            haveAnon = ssl->options.haveAnon;
        #endif
        (void)haveAnon;

        if (ssl->options.side != WOLFSSL_SERVER_END) {
            WOLFSSL_ERROR(ssl->error = SIDE_ERROR);
            return SSL_FATAL_ERROR;
        }

        #ifndef NO_CERTS
            /* in case used set_accept_state after init */
            if (!havePSK && !haveAnon &&
                (!ssl->buffers.certificate ||
                 !ssl->buffers.certificate->buffer ||
                 !ssl->buffers.key ||
                 !ssl->buffers.key->buffer)) {
                WOLFSSL_MSG("accept error: don't have server cert and key");
                ssl->error = NO_PRIVATE_KEY;
                WOLFSSL_ERROR(ssl->error);
                return SSL_FATAL_ERROR;
            }
        #endif

        #ifdef WOLFSSL_DTLS
            if (ssl->version.major == DTLS_MAJOR) {
                ssl->options.dtls   = 1;
                ssl->options.tls    = 1;
                ssl->options.tls1_1 = 1;
            }
        #endif

        if (ssl->buffers.outputBuffer.length > 0) {
            if ( (ssl->error = SendBuffered(ssl)) == 0) {
                /* fragOffset is non-zero when sending fragments. On the last
                 * fragment, fragOffset is zero again, and the state can be
                 * advanced. */
                if (ssl->fragOffset == 0) {
                    ssl->options.acceptState++;
                    WOLFSSL_MSG("accept state: "
                                "Advanced from last buffered fragment send");
                }
                else {
                    WOLFSSL_MSG("accept state: "
                                "Not advanced, more fragments to send");
                }
            }
            else {
                WOLFSSL_ERROR(ssl->error);
                return SSL_FATAL_ERROR;
            }
        }

        switch (ssl->options.acceptState) {

        case ACCEPT_BEGIN :
            /* get response */
            while (ssl->options.clientState < CLIENT_HELLO_COMPLETE)
                if ( (ssl->error = ProcessReply(ssl)) < 0) {
                    WOLFSSL_ERROR(ssl->error);
                    return SSL_FATAL_ERROR;
                }
            ssl->options.acceptState = ACCEPT_CLIENT_HELLO_DONE;
            WOLFSSL_MSG("accept state ACCEPT_CLIENT_HELLO_DONE");

        case ACCEPT_CLIENT_HELLO_DONE :
            ssl->options.acceptState = ACCEPT_FIRST_REPLY_DONE;
            WOLFSSL_MSG("accept state ACCEPT_FIRST_REPLY_DONE");

        case ACCEPT_FIRST_REPLY_DONE :
            if ( (ssl->error = SendServerHello(ssl)) != 0) {
                WOLFSSL_ERROR(ssl->error);
                return SSL_FATAL_ERROR;
            }
            ssl->options.acceptState = SERVER_HELLO_SENT;
            WOLFSSL_MSG("accept state SERVER_HELLO_SENT");

        case SERVER_HELLO_SENT :
            #ifndef NO_CERTS
                if (!ssl->options.resuming)
                    if ( (ssl->error = SendCertificate(ssl)) != 0) {
                        WOLFSSL_ERROR(ssl->error);
                        return SSL_FATAL_ERROR;
                    }
            #endif
            ssl->options.acceptState = CERT_SENT;
            WOLFSSL_MSG("accept state CERT_SENT");

        case CERT_SENT :
            #ifndef NO_CERTS
            if (!ssl->options.resuming)
                if ( (ssl->error = SendCertificateStatus(ssl)) != 0) {
                    WOLFSSL_ERROR(ssl->error);
                    return SSL_FATAL_ERROR;
                }
            #endif
            ssl->options.acceptState = CERT_STATUS_SENT;
            WOLFSSL_MSG("accept state CERT_STATUS_SENT");

        case CERT_STATUS_SENT :
            if (!ssl->options.resuming)
                if ( (ssl->error = SendServerKeyExchange(ssl)) != 0) {
                    WOLFSSL_ERROR(ssl->error);
                    return SSL_FATAL_ERROR;
                }
            ssl->options.acceptState = KEY_EXCHANGE_SENT;
            WOLFSSL_MSG("accept state KEY_EXCHANGE_SENT");

        case KEY_EXCHANGE_SENT :
            #ifndef NO_CERTS
                if (!ssl->options.resuming)
                    if (ssl->options.verifyPeer)
                        if ( (ssl->error = SendCertificateRequest(ssl)) != 0) {
                            WOLFSSL_ERROR(ssl->error);
                            return SSL_FATAL_ERROR;
                        }
            #endif
            ssl->options.acceptState = CERT_REQ_SENT;
            WOLFSSL_MSG("accept state CERT_REQ_SENT");

        case CERT_REQ_SENT :
            if (!ssl->options.resuming)
                if ( (ssl->error = SendServerHelloDone(ssl)) != 0) {
                    WOLFSSL_ERROR(ssl->error);
                    return SSL_FATAL_ERROR;
                }
            ssl->options.acceptState = SERVER_HELLO_DONE;
            WOLFSSL_MSG("accept state SERVER_HELLO_DONE");

        case SERVER_HELLO_DONE :
            if (!ssl->options.resuming) {
                while (ssl->options.clientState < CLIENT_FINISHED_COMPLETE)
                    if ( (ssl->error = ProcessReply(ssl)) < 0) {
                        WOLFSSL_ERROR(ssl->error);
                        return SSL_FATAL_ERROR;
                    }
            }
            ssl->options.acceptState = ACCEPT_SECOND_REPLY_DONE;
            WOLFSSL_MSG("accept state  ACCEPT_SECOND_REPLY_DONE");

        case ACCEPT_SECOND_REPLY_DONE :
#ifdef HAVE_SESSION_TICKET
            if (ssl->options.createTicket) {
                if ( (ssl->error = SendTicket(ssl)) != 0) {
                    WOLFSSL_ERROR(ssl->error);
                    return SSL_FATAL_ERROR;
                }
            }
#endif /* HAVE_SESSION_TICKET */
            ssl->options.acceptState = TICKET_SENT;
            WOLFSSL_MSG("accept state  TICKET_SENT");

        case TICKET_SENT:
            if ( (ssl->error = SendChangeCipher(ssl)) != 0) {
                WOLFSSL_ERROR(ssl->error);
                return SSL_FATAL_ERROR;
            }
            ssl->options.acceptState = CHANGE_CIPHER_SENT;
            WOLFSSL_MSG("accept state  CHANGE_CIPHER_SENT");

        case CHANGE_CIPHER_SENT :
            if ( (ssl->error = SendFinished(ssl)) != 0) {
                WOLFSSL_ERROR(ssl->error);
                return SSL_FATAL_ERROR;
            }

            ssl->options.acceptState = ACCEPT_FINISHED_DONE;
            WOLFSSL_MSG("accept state ACCEPT_FINISHED_DONE");

        case ACCEPT_FINISHED_DONE :
            if (ssl->options.resuming)
                while (ssl->options.clientState < CLIENT_FINISHED_COMPLETE)
                    if ( (ssl->error = ProcessReply(ssl)) < 0) {
                        WOLFSSL_ERROR(ssl->error);
                        return SSL_FATAL_ERROR;
                    }

            ssl->options.acceptState = ACCEPT_THIRD_REPLY_DONE;
            WOLFSSL_MSG("accept state ACCEPT_THIRD_REPLY_DONE");

        case ACCEPT_THIRD_REPLY_DONE :
#ifndef NO_HANDSHAKE_DONE_CB
            if (ssl->hsDoneCb) {
                int cbret = ssl->hsDoneCb(ssl, ssl->hsDoneCtx);
                if (cbret < 0) {
                    ssl->error = cbret;
                    WOLFSSL_MSG("HandShake Done Cb don't continue error");
                    return SSL_FATAL_ERROR;
                }
            }
#endif /* NO_HANDSHAKE_DONE_CB */

            if (!ssl->options.dtls) {
                FreeHandshakeResources(ssl);
            }
#ifdef WOLFSSL_DTLS
            else {
                ssl->options.dtlsHsRetain = 1;
            }
#endif /* WOLFSSL_DTLS */

#ifdef WOLFSSL_SESSION_EXPORT
            if (ssl->dtls_export) {
                if ((ssl->error = wolfSSL_send_session(ssl)) != 0) {
                    WOLFSSL_MSG("Export DTLS session error");
                    WOLFSSL_ERROR(ssl->error);
                    return SSL_FATAL_ERROR;
                }
            }
#endif

            WOLFSSL_LEAVE("SSL_accept()", SSL_SUCCESS);
            return SSL_SUCCESS;

        default :
            WOLFSSL_MSG("Unknown accept state ERROR");
            return SSL_FATAL_ERROR;
        }
    }

#endif /* NO_WOLFSSL_SERVER */


#ifndef NO_HANDSHAKE_DONE_CB

int wolfSSL_SetHsDoneCb(WOLFSSL* ssl, HandShakeDoneCb cb, void* user_ctx)
{
    WOLFSSL_ENTER("wolfSSL_SetHsDoneCb");

    if (ssl == NULL)
        return BAD_FUNC_ARG;

    ssl->hsDoneCb  = cb;
    ssl->hsDoneCtx = user_ctx;


    return SSL_SUCCESS;
}

#endif /* NO_HANDSHAKE_DONE_CB */


int wolfSSL_Cleanup(void)
{
    int ret = SSL_SUCCESS;
    int release = 0;

    WOLFSSL_ENTER("wolfSSL_Cleanup");

    if (initRefCount == 0)
        return ret;  /* possibly no init yet, but not failure either way */

    if (wc_LockMutex(&count_mutex) != 0) {
        WOLFSSL_MSG("Bad Lock Mutex count");
        return BAD_MUTEX_E;
    }

    release = initRefCount-- == 1;
    if (initRefCount < 0)
        initRefCount = 0;

    wc_UnLockMutex(&count_mutex);

    if (!release)
        return ret;

#ifndef NO_SESSION_CACHE
    if (wc_FreeMutex(&session_mutex) != 0)
        ret = BAD_MUTEX_E;
#endif
    if (wc_FreeMutex(&count_mutex) != 0)
        ret = BAD_MUTEX_E;

#ifdef HAVE_ECC
    #ifdef FP_ECC
        wc_ecc_fp_free();
    #endif
    #ifdef ECC_CACHE_CURVE
        wc_ecc_curve_cache_free();
    #endif
#endif

    if (wolfCrypt_Cleanup() != 0) {
        WOLFSSL_MSG("Error with wolfCrypt_Cleanup call");
        ret = WC_CLEANUP_E;
    }

    return ret;
}


#ifndef NO_SESSION_CACHE


/* some session IDs aren't random after all, let's make them random */
static INLINE word32 HashSession(const byte* sessionID, word32 len, int* error)
{
    byte digest[MAX_DIGEST_SIZE];

#ifndef NO_MD5
    *error =  wc_Md5Hash(sessionID, len, digest);
#elif !defined(NO_SHA)
    *error =  wc_ShaHash(sessionID, len, digest);
#elif !defined(NO_SHA256)
    *error =  wc_Sha256Hash(sessionID, len, digest);
#else
    #error "We need a digest to hash the session IDs"
#endif

    return *error == 0 ? MakeWordFromHash(digest) : 0; /* 0 on failure */
}


void wolfSSL_flush_sessions(WOLFSSL_CTX* ctx, long tm)
{
    /* static table now, no flushing needed */
    (void)ctx;
    (void)tm;
}


/* set ssl session timeout in seconds */
int wolfSSL_set_timeout(WOLFSSL* ssl, unsigned int to)
{
    if (ssl == NULL)
        return BAD_FUNC_ARG;

    ssl->timeout = to;

    return SSL_SUCCESS;
}


/* set ctx session timeout in seconds */
int wolfSSL_CTX_set_timeout(WOLFSSL_CTX* ctx, unsigned int to)
{
    if (ctx == NULL)
        return BAD_FUNC_ARG;

    ctx->timeout = to;

    return SSL_SUCCESS;
}


#ifndef NO_CLIENT_CACHE

/* Get Session from Client cache based on id/len, return NULL on failure */
WOLFSSL_SESSION* GetSessionClient(WOLFSSL* ssl, const byte* id, int len)
{
    WOLFSSL_SESSION* ret = NULL;
    word32          row;
    int             idx;
    int             count;
    int             error = 0;

    WOLFSSL_ENTER("GetSessionClient");

    if (ssl->options.side == WOLFSSL_SERVER_END)
        return NULL;

    len = min(SERVER_ID_LEN, (word32)len);
    row = HashSession(id, len, &error) % SESSION_ROWS;
    if (error != 0) {
        WOLFSSL_MSG("Hash session failed");
        return NULL;
    }

    if (wc_LockMutex(&session_mutex) != 0) {
        WOLFSSL_MSG("Lock session mutex failed");
        return NULL;
    }

    /* start from most recently used */
    count = min((word32)ClientCache[row].totalCount, SESSIONS_PER_ROW);
    idx = ClientCache[row].nextIdx - 1;
    if (idx < 0)
        idx = SESSIONS_PER_ROW - 1; /* if back to front, the previous was end */

    for (; count > 0; --count, idx = idx ? idx - 1 : SESSIONS_PER_ROW - 1) {
        WOLFSSL_SESSION* current;
        ClientSession   clSess;

        if (idx >= SESSIONS_PER_ROW || idx < 0) { /* sanity check */
            WOLFSSL_MSG("Bad idx");
            break;
        }

        clSess = ClientCache[row].Clients[idx];

        current = &SessionCache[clSess.serverRow].Sessions[clSess.serverIdx];
        if (XMEMCMP(current->serverID, id, len) == 0) {
            WOLFSSL_MSG("Found a serverid match for client");
            if (LowResTimer() < (current->bornOn + current->timeout)) {
                WOLFSSL_MSG("Session valid");
                ret = current;
                break;
            } else {
                WOLFSSL_MSG("Session timed out");  /* could have more for id */
            }
        } else {
            WOLFSSL_MSG("ServerID not a match from client table");
        }
    }

    wc_UnLockMutex(&session_mutex);

    return ret;
}

#endif /* NO_CLIENT_CACHE */


WOLFSSL_SESSION* GetSession(WOLFSSL* ssl, byte* masterSecret,
        byte restoreSessionCerts)
{
    WOLFSSL_SESSION* ret = 0;
    const byte*  id = NULL;
    word32       row;
    int          idx;
    int          count;
    int          error = 0;

    (void)       restoreSessionCerts;

    if (ssl->options.sessionCacheOff)
        return NULL;

    if (ssl->options.haveSessionId == 0)
        return NULL;

#ifdef HAVE_SESSION_TICKET
    if (ssl->options.side == WOLFSSL_SERVER_END && ssl->options.useTicket == 1)
        return NULL;
#endif

    if (ssl->arrays)
        id = ssl->arrays->sessionID;
    else
        id = ssl->session.sessionID;

    row = HashSession(id, ID_LEN, &error) % SESSION_ROWS;
    if (error != 0) {
        WOLFSSL_MSG("Hash session failed");
        return NULL;
    }

    if (wc_LockMutex(&session_mutex) != 0)
        return 0;

    /* start from most recently used */
    count = min((word32)SessionCache[row].totalCount, SESSIONS_PER_ROW);
    idx = SessionCache[row].nextIdx - 1;
    if (idx < 0)
        idx = SESSIONS_PER_ROW - 1; /* if back to front, the previous was end */

    for (; count > 0; --count, idx = idx ? idx - 1 : SESSIONS_PER_ROW - 1) {
        WOLFSSL_SESSION* current;

        if (idx >= SESSIONS_PER_ROW || idx < 0) { /* sanity check */
            WOLFSSL_MSG("Bad idx");
            break;
        }

        current = &SessionCache[row].Sessions[idx];
        if (XMEMCMP(current->sessionID, id, ID_LEN) == 0) {
            WOLFSSL_MSG("Found a session match");
            if (LowResTimer() < (current->bornOn + current->timeout)) {
                WOLFSSL_MSG("Session valid");
                ret = current;
                if (masterSecret)
                    XMEMCPY(masterSecret, current->masterSecret, SECRET_LEN);
#ifdef SESSION_CERTS
                /* If set, we should copy the session certs into the ssl object
                 * from the session we are returning so we can resume */
                if (restoreSessionCerts) {
                    ssl->session.chain        = ret->chain;
                    ssl->session.version      = ret->version;
                    ssl->session.cipherSuite0 = ret->cipherSuite0;
                    ssl->session.cipherSuite  = ret->cipherSuite;
                }
#endif /* SESSION_CERTS */

            } else {
                WOLFSSL_MSG("Session timed out");
            }
            break;  /* no more sessionIDs whether valid or not that match */
        } else {
            WOLFSSL_MSG("SessionID not a match at this idx");
        }
    }

    wc_UnLockMutex(&session_mutex);

    return ret;
}


static int GetDeepCopySession(WOLFSSL* ssl, WOLFSSL_SESSION* copyFrom)
{
    WOLFSSL_SESSION* copyInto = &ssl->session;
    void* tmpBuff             = NULL;
    int ticketLen             = 0;
    int doDynamicCopy         = 0;
    int ret                   = SSL_SUCCESS;

    (void)ticketLen;
    (void)doDynamicCopy;
    (void)tmpBuff;

    if (!ssl || !copyFrom)
        return BAD_FUNC_ARG;

#ifdef HAVE_SESSION_TICKET
    /* Free old dynamic ticket if we had one to avoid leak */
    if (copyInto->isDynamic) {
        XFREE(copyInto->ticket, ssl->heap, DYNAMIC_TYPE_SESSION_TICK);
        copyInto->ticket = copyInto->staticTicket;
        copyInto->isDynamic = 0;
    }
#endif

    if (wc_LockMutex(&session_mutex) != 0)
        return BAD_MUTEX_E;

#ifdef HAVE_SESSION_TICKET
    /* Size of ticket to alloc if needed; Use later for alloc outside lock */
    doDynamicCopy = copyFrom->isDynamic;
    ticketLen = copyFrom->ticketLen;
#endif

    *copyInto = *copyFrom;

    /* Default ticket to non dynamic. This will avoid crash if we fail below */
#ifdef HAVE_SESSION_TICKET
    copyInto->ticket = copyInto->staticTicket;
    copyInto->isDynamic = 0;
#endif

    if (wc_UnLockMutex(&session_mutex) != 0) {
        return BAD_MUTEX_E;
    }

#ifdef HAVE_SESSION_TICKET
    /* If doing dynamic copy, need to alloc outside lock, then inside a lock
     * confirm the size still matches and memcpy */
    if (doDynamicCopy) {
        tmpBuff = (byte*)XMALLOC(ticketLen, ssl->heap,
                                                     DYNAMIC_TYPE_SESSION_TICK);
        if (!tmpBuff)
            return MEMORY_ERROR;

        if (wc_LockMutex(&session_mutex) != 0) {
            XFREE(tmpBuff, ssl->heap, DYNAMIC_TYPE_SESSION_TICK);
            return BAD_MUTEX_E;
        }

        if (ticketLen != copyFrom->ticketLen) {
            /* Another thread modified the ssl-> session ticket during alloc.
             * Treat as error, since ticket different than when copy requested */
            ret = VAR_STATE_CHANGE_E;
        }

        if (ret == SSL_SUCCESS) {
            copyInto->ticket = (byte*)tmpBuff;
            copyInto->isDynamic = 1;
            XMEMCPY(copyInto->ticket, copyFrom->ticket, ticketLen);
        }
    } else {
        /* Need to ensure ticket pointer gets updated to own buffer
         * and is not pointing to buff of session copied from */
        copyInto->ticket = copyInto->staticTicket;
    }

    if (wc_UnLockMutex(&session_mutex) != 0) {
        if (ret == SSL_SUCCESS)
            ret = BAD_MUTEX_E;
    }

    if (ret != SSL_SUCCESS) {
        /* cleanup */
        if (tmpBuff)
            XFREE(tmpBuff, ssl->heap, DYNAMIC_TYPE_SESSION_TICK);
        copyInto->ticket = copyInto->staticTicket;
        copyInto->isDynamic = 0;
    }
#endif /* HAVE_SESSION_TICKET */
    return ret;
}


int SetSession(WOLFSSL* ssl, WOLFSSL_SESSION* session)
{
    if (ssl->options.sessionCacheOff)
        return SSL_FAILURE;

    if (LowResTimer() < (session->bornOn + session->timeout)) {
        int ret = GetDeepCopySession(ssl, session);
        if (ret == SSL_SUCCESS) {
            ssl->options.resuming = 1;

#ifdef SESSION_CERTS
            ssl->version              = session->version;
            ssl->options.cipherSuite0 = session->cipherSuite0;
            ssl->options.cipherSuite  = session->cipherSuite;
#endif
        }

        return ret;
    }
    return SSL_FAILURE;  /* session timed out */
}


#ifdef WOLFSSL_SESSION_STATS
static int get_locked_session_stats(word32* active, word32* total,
                                    word32* peak);
#endif

int AddSession(WOLFSSL* ssl)
{
    word32 row, idx;
    int    error = 0;
#ifdef HAVE_SESSION_TICKET
    byte*  tmpBuff = NULL;
    int    ticLen  = 0;
#endif

    if (ssl->options.sessionCacheOff)
        return 0;

    if (ssl->options.haveSessionId == 0)
        return 0;

#ifdef HAVE_SESSION_TICKET
    if (ssl->options.side == WOLFSSL_SERVER_END && ssl->options.useTicket == 1)
        return 0;
#endif

    row = HashSession(ssl->arrays->sessionID, ID_LEN, &error) % SESSION_ROWS;
    if (error != 0) {
        WOLFSSL_MSG("Hash session failed");
        return error;
    }

#ifdef HAVE_SESSION_TICKET
    ticLen = ssl->session.ticketLen;
    /* Alloc Memory here so if Malloc fails can exit outside of lock */
    if(ticLen > SESSION_TICKET_LEN) {
        tmpBuff = (byte*)XMALLOC(ticLen, ssl->heap,
                DYNAMIC_TYPE_SESSION_TICK);
        if(!tmpBuff)
            return MEMORY_E;
    }
#endif

    if (wc_LockMutex(&session_mutex) != 0) {
#ifdef HAVE_SESSION_TICKET
        XFREE(tmpBuff, ssl->heap, DYNAMIC_TYPE_SESSION_TICK);
#endif
        return BAD_MUTEX_E;
    }

    idx = SessionCache[row].nextIdx++;
#ifdef SESSION_INDEX
    ssl->sessionIndex = (row << SESSIDX_ROW_SHIFT) | idx;
#endif

    XMEMCPY(SessionCache[row].Sessions[idx].masterSecret,
           ssl->arrays->masterSecret, SECRET_LEN);
    SessionCache[row].Sessions[idx].haveEMS = ssl->options.haveEMS;
    XMEMCPY(SessionCache[row].Sessions[idx].sessionID, ssl->arrays->sessionID,
           ID_LEN);
    SessionCache[row].Sessions[idx].sessionIDSz = ssl->arrays->sessionIDSz;

    SessionCache[row].Sessions[idx].timeout = ssl->timeout;
    SessionCache[row].Sessions[idx].bornOn  = LowResTimer();

#ifdef HAVE_SESSION_TICKET
    /* Check if another thread modified ticket since alloc */
    if (ticLen != ssl->session.ticketLen) {
        error = VAR_STATE_CHANGE_E;
    }

    if (error == 0) {
        /* Cleanup cache row's old Dynamic buff if exists */
        if(SessionCache[row].Sessions[idx].isDynamic) {
            XFREE(SessionCache[row].Sessions[idx].ticket,
                   ssl->heap, DYNAMIC_TYPE_SESSION_TICK);
            SessionCache[row].Sessions[idx].ticket = NULL;
        }

        /* If too large to store in static buffer, use dyn buffer */
        if (ticLen > SESSION_TICKET_LEN) {
            SessionCache[row].Sessions[idx].ticket = tmpBuff;
            SessionCache[row].Sessions[idx].isDynamic = 1;
        } else {
            SessionCache[row].Sessions[idx].ticket =
                    SessionCache[row].Sessions[idx].staticTicket;
            SessionCache[row].Sessions[idx].isDynamic = 0;
        }
    }

    if (error == 0) {
        SessionCache[row].Sessions[idx].ticketLen     = ticLen;
        XMEMCPY(SessionCache[row].Sessions[idx].ticket,
                                   ssl->session.ticket, ticLen);
    } else { /* cleanup, reset state */
        SessionCache[row].Sessions[idx].ticket    =
            SessionCache[row].Sessions[idx].staticTicket;
        SessionCache[row].Sessions[idx].isDynamic = 0;
        SessionCache[row].Sessions[idx].ticketLen = 0;
        if (tmpBuff) {
            XFREE(tmpBuff, ssl->heap, DYNAMIC_TYPE_SESSION_TICK);
            tmpBuff = NULL;
        }
    }
#endif

#ifdef SESSION_CERTS
    if (error == 0) {
        SessionCache[row].Sessions[idx].chain.count = ssl->session.chain.count;
        XMEMCPY(SessionCache[row].Sessions[idx].chain.certs,
               ssl->session.chain.certs, sizeof(x509_buffer) * MAX_CHAIN_DEPTH);

        SessionCache[row].Sessions[idx].version      = ssl->version;
        SessionCache[row].Sessions[idx].cipherSuite0 = ssl->options.cipherSuite0;
        SessionCache[row].Sessions[idx].cipherSuite  = ssl->options.cipherSuite;
    }
#endif /* SESSION_CERTS */
    if (error == 0) {
        SessionCache[row].totalCount++;
        if (SessionCache[row].nextIdx == SESSIONS_PER_ROW)
            SessionCache[row].nextIdx = 0;
    }
#ifndef NO_CLIENT_CACHE
    if (error == 0) {
        if (ssl->options.side == WOLFSSL_CLIENT_END && ssl->session.idLen) {
            word32 clientRow, clientIdx;

            WOLFSSL_MSG("Adding client cache entry");

            SessionCache[row].Sessions[idx].idLen = ssl->session.idLen;
            XMEMCPY(SessionCache[row].Sessions[idx].serverID,
                    ssl->session.serverID, ssl->session.idLen);

            clientRow = HashSession(ssl->session.serverID, ssl->session.idLen,
                                    &error) % SESSION_ROWS;
            if (error != 0) {
                WOLFSSL_MSG("Hash session failed");
            } else {
                clientIdx = ClientCache[clientRow].nextIdx++;

                ClientCache[clientRow].Clients[clientIdx].serverRow =
                                                                   (word16)row;
                ClientCache[clientRow].Clients[clientIdx].serverIdx =
                                                                   (word16)idx;

                ClientCache[clientRow].totalCount++;
                if (ClientCache[clientRow].nextIdx == SESSIONS_PER_ROW)
                    ClientCache[clientRow].nextIdx = 0;
            }
        }
        else
            SessionCache[row].Sessions[idx].idLen = 0;
    }
#endif /* NO_CLIENT_CACHE */

#if defined(WOLFSSL_SESSION_STATS) && defined(WOLFSSL_PEAK_SESSIONS)
    if (error == 0) {
        word32 active = 0;

        error = get_locked_session_stats(&active, NULL, NULL);
        if (error == SSL_SUCCESS) {
            error = 0;  /* back to this function ok */

            if (active > PeakSessions)
                PeakSessions = active;
        }
    }
#endif /* defined(WOLFSSL_SESSION_STATS) && defined(WOLFSSL_PEAK_SESSIONS) */

    if (wc_UnLockMutex(&session_mutex) != 0)
        return BAD_MUTEX_E;

    return error;
}


#ifdef SESSION_INDEX

int wolfSSL_GetSessionIndex(WOLFSSL* ssl)
{
    WOLFSSL_ENTER("wolfSSL_GetSessionIndex");
    WOLFSSL_LEAVE("wolfSSL_GetSessionIndex", ssl->sessionIndex);
    return ssl->sessionIndex;
}


int wolfSSL_GetSessionAtIndex(int idx, WOLFSSL_SESSION* session)
{
    int row, col, result = SSL_FAILURE;

    WOLFSSL_ENTER("wolfSSL_GetSessionAtIndex");

    row = idx >> SESSIDX_ROW_SHIFT;
    col = idx & SESSIDX_IDX_MASK;

    if (wc_LockMutex(&session_mutex) != 0) {
        return BAD_MUTEX_E;
    }

    if (row < SESSION_ROWS &&
        col < (int)min(SessionCache[row].totalCount, SESSIONS_PER_ROW)) {
        XMEMCPY(session,
                 &SessionCache[row].Sessions[col], sizeof(WOLFSSL_SESSION));
        result = SSL_SUCCESS;
    }

    if (wc_UnLockMutex(&session_mutex) != 0)
        result = BAD_MUTEX_E;

    WOLFSSL_LEAVE("wolfSSL_GetSessionAtIndex", result);
    return result;
}

#endif /* SESSION_INDEX */

#if defined(SESSION_INDEX) && defined(SESSION_CERTS)

WOLFSSL_X509_CHAIN* wolfSSL_SESSION_get_peer_chain(WOLFSSL_SESSION* session)
{
    WOLFSSL_X509_CHAIN* chain = NULL;

    WOLFSSL_ENTER("wolfSSL_SESSION_get_peer_chain");
    if (session)
        chain = &session->chain;

    WOLFSSL_LEAVE("wolfSSL_SESSION_get_peer_chain", chain ? 1 : 0);
    return chain;
}

#endif /* SESSION_INDEX && SESSION_CERTS */


#ifdef WOLFSSL_SESSION_STATS

/* requires session_mutex lock held, SSL_SUCCESS on ok */
static int get_locked_session_stats(word32* active, word32* total, word32* peak)
{
    int result = SSL_SUCCESS;
    int i;
    int count;
    int idx;
    word32 now   = 0;
    word32 seen  = 0;
    word32 ticks = LowResTimer();

    (void)peak;

    WOLFSSL_ENTER("get_locked_session_stats");

    for (i = 0; i < SESSION_ROWS; i++) {
        seen += SessionCache[i].totalCount;

        if (active == NULL)
            continue;  /* no need to calculate what we can't set */

        count = min((word32)SessionCache[i].totalCount, SESSIONS_PER_ROW);
        idx   = SessionCache[i].nextIdx - 1;
        if (idx < 0)
            idx = SESSIONS_PER_ROW - 1; /* if back to front previous was end */

        for (; count > 0; --count, idx = idx ? idx - 1 : SESSIONS_PER_ROW - 1) {
            if (idx >= SESSIONS_PER_ROW || idx < 0) {  /* sanity check */
                WOLFSSL_MSG("Bad idx");
                break;
            }

            /* if not expried then good */
            if (ticks < (SessionCache[i].Sessions[idx].bornOn +
                         SessionCache[i].Sessions[idx].timeout) ) {
                now++;
            }
        }
    }

    if (active)
        *active = now;

    if (total)
        *total = seen;

#ifdef WOLFSSL_PEAK_SESSIONS
    if (peak)
        *peak = PeakSessions;
#endif

    WOLFSSL_LEAVE("get_locked_session_stats", result);

    return result;
}


/* return SSL_SUCCESS on ok */
int wolfSSL_get_session_stats(word32* active, word32* total, word32* peak,
                              word32* maxSessions)
{
    int result = SSL_SUCCESS;

    WOLFSSL_ENTER("wolfSSL_get_session_stats");

    if (maxSessions) {
        *maxSessions = SESSIONS_PER_ROW * SESSION_ROWS;

        if (active == NULL && total == NULL && peak == NULL)
            return result;  /* we're done */
    }

    /* user must provide at least one query value */
    if (active == NULL && total == NULL && peak == NULL)
        return BAD_FUNC_ARG;

    if (wc_LockMutex(&session_mutex) != 0) {
        return BAD_MUTEX_E;
    }

    result = get_locked_session_stats(active, total, peak);

    if (wc_UnLockMutex(&session_mutex) != 0)
        result = BAD_MUTEX_E;

    WOLFSSL_LEAVE("wolfSSL_get_session_stats", result);

    return result;
}

#endif /* WOLFSSL_SESSION_STATS */


    #ifdef PRINT_SESSION_STATS

    /* SSL_SUCCESS on ok */
    int wolfSSL_PrintSessionStats(void)
    {
        word32 totalSessionsSeen = 0;
        word32 totalSessionsNow = 0;
        word32 peak = 0;
        word32 maxSessions = 0;
        int    i;
        int    ret;
        double E;               /* expected freq */
        double chiSquare = 0;

        ret = wolfSSL_get_session_stats(&totalSessionsNow, &totalSessionsSeen,
                                        &peak, &maxSessions);
        if (ret != SSL_SUCCESS)
            return ret;
        printf("Total Sessions Seen = %d\n", totalSessionsSeen);
        printf("Total Sessions Now  = %d\n", totalSessionsNow);
#ifdef WOLFSSL_PEAK_SESSIONS
        printf("Peak  Sessions      = %d\n", peak);
#endif
        printf("Max   Sessions      = %d\n", maxSessions);

        E = (double)totalSessionsSeen / SESSION_ROWS;

        for (i = 0; i < SESSION_ROWS; i++) {
            double diff = SessionCache[i].totalCount - E;
            diff *= diff;                /* square    */
            diff /= E;                   /* normalize */

            chiSquare += diff;
        }
        printf("  chi-square = %5.1f, d.f. = %d\n", chiSquare,
                                                     SESSION_ROWS - 1);
        #if (SESSION_ROWS == 11)
            printf(" .05 p value =  18.3, chi-square should be less\n");
        #elif (SESSION_ROWS == 211)
            printf(".05 p value  = 244.8, chi-square should be less\n");
        #elif (SESSION_ROWS == 5981)
            printf(".05 p value  = 6161.0, chi-square should be less\n");
        #elif (SESSION_ROWS == 3)
            printf(".05 p value  =   6.0, chi-square should be less\n");
        #elif (SESSION_ROWS == 2861)
            printf(".05 p value  = 2985.5, chi-square should be less\n");
        #endif
        printf("\n");

        return ret;
    }

    #endif /* SESSION_STATS */

#else  /* NO_SESSION_CACHE */

/* No session cache version */
WOLFSSL_SESSION* GetSession(WOLFSSL* ssl, byte* masterSecret,
        byte restoreSessionCerts)
{
    (void)ssl;
    (void)masterSecret;
    (void)restoreSessionCerts;

    return NULL;
}

#endif /* NO_SESSION_CACHE */


/* call before SSL_connect, if verifying will add name check to
   date check and signature check */
int wolfSSL_check_domain_name(WOLFSSL* ssl, const char* dn)
{
    WOLFSSL_ENTER("wolfSSL_check_domain_name");
    if (ssl->buffers.domainName.buffer)
        XFREE(ssl->buffers.domainName.buffer, ssl->heap, DYNAMIC_TYPE_DOMAIN);

    ssl->buffers.domainName.length = (word32)XSTRLEN(dn) + 1;
    ssl->buffers.domainName.buffer = (byte*) XMALLOC(
                ssl->buffers.domainName.length, ssl->heap, DYNAMIC_TYPE_DOMAIN);

    if (ssl->buffers.domainName.buffer) {
        XSTRNCPY((char*)ssl->buffers.domainName.buffer, dn,
                ssl->buffers.domainName.length);
        return SSL_SUCCESS;
    }
    else {
        ssl->error = MEMORY_ERROR;
        return SSL_FAILURE;
    }
}


/* turn on wolfSSL zlib compression
   returns SSL_SUCCESS for success, else error (not built in)
*/
int wolfSSL_set_compression(WOLFSSL* ssl)
{
    WOLFSSL_ENTER("wolfSSL_set_compression");
    (void)ssl;
#ifdef HAVE_LIBZ
    ssl->options.usingCompression = 1;
    return SSL_SUCCESS;
#else
    return NOT_COMPILED_IN;
#endif
}


#ifndef USE_WINDOWS_API
    #ifndef NO_WRITEV

        /* simulate writev semantics, doesn't actually do block at a time though
           because of SSL_write behavior and because front adds may be small */
        int wolfSSL_writev(WOLFSSL* ssl, const struct iovec* iov, int iovcnt)
        {
        #ifdef WOLFSSL_SMALL_STACK
            byte   staticBuffer[1]; /* force heap usage */
        #else
            byte   staticBuffer[FILE_BUFFER_SIZE];
        #endif
            byte* myBuffer  = staticBuffer;
            int   dynamic   = 0;
            int   sending   = 0;
            int   idx       = 0;
            int   i;
            int   ret;

            WOLFSSL_ENTER("wolfSSL_writev");

            for (i = 0; i < iovcnt; i++)
                sending += (int)iov[i].iov_len;

            if (sending > (int)sizeof(staticBuffer)) {
                myBuffer = (byte*)XMALLOC(sending, ssl->heap,
                                                           DYNAMIC_TYPE_WRITEV);
                if (!myBuffer)
                    return MEMORY_ERROR;

                dynamic = 1;
            }

            for (i = 0; i < iovcnt; i++) {
                XMEMCPY(&myBuffer[idx], iov[i].iov_base, iov[i].iov_len);
                idx += (int)iov[i].iov_len;
            }

            ret = wolfSSL_write(ssl, myBuffer, sending);

            if (dynamic)
                XFREE(myBuffer, ssl->heap, DYNAMIC_TYPE_WRITEV);

            return ret;
        }
    #endif
#endif


#ifdef WOLFSSL_CALLBACKS

    typedef struct itimerval Itimerval;

    /* don't keep calling simple functions while setting up timer and signals
       if no inlining these are the next best */

    #define AddTimes(a, b, c)                       \
        do {                                        \
            c.tv_sec  = a.tv_sec  + b.tv_sec;       \
            c.tv_usec = a.tv_usec + b.tv_usec;      \
            if (c.tv_usec >=  1000000) {            \
                c.tv_sec++;                         \
                c.tv_usec -= 1000000;               \
            }                                       \
        } while (0)


    #define SubtractTimes(a, b, c)                  \
        do {                                        \
            c.tv_sec  = a.tv_sec  - b.tv_sec;       \
            c.tv_usec = a.tv_usec - b.tv_usec;      \
            if (c.tv_usec < 0) {                    \
                c.tv_sec--;                         \
                c.tv_usec += 1000000;               \
            }                                       \
        } while (0)

    #define CmpTimes(a, b, cmp)                     \
        ((a.tv_sec  ==  b.tv_sec) ?                 \
            (a.tv_usec cmp b.tv_usec) :             \
            (a.tv_sec  cmp b.tv_sec))               \


    /* do nothing handler */
    static void myHandler(int signo)
    {
        (void)signo;
        return;
    }


    static int wolfSSL_ex_wrapper(WOLFSSL* ssl, HandShakeCallBack hsCb,
                                 TimeoutCallBack toCb, Timeval timeout)
    {
        int       ret        = SSL_FATAL_ERROR;
        int       oldTimerOn = 0;   /* was timer already on */
        Timeval   startTime;
        Timeval   endTime;
        Timeval   totalTime;
        Itimerval myTimeout;
        Itimerval oldTimeout; /* if old timer adjust from total time to reset */
        struct sigaction act, oact;

        #define ERR_OUT(x) { ssl->hsInfoOn = 0; ssl->toInfoOn = 0; return x; }

        if (hsCb) {
            ssl->hsInfoOn = 1;
            InitHandShakeInfo(&ssl->handShakeInfo, ssl);
        }
        if (toCb) {
            ssl->toInfoOn = 1;
            InitTimeoutInfo(&ssl->timeoutInfo);

            if (gettimeofday(&startTime, 0) < 0)
                ERR_OUT(GETTIME_ERROR);

            /* use setitimer to simulate getitimer, init 0 myTimeout */
            myTimeout.it_interval.tv_sec  = 0;
            myTimeout.it_interval.tv_usec = 0;
            myTimeout.it_value.tv_sec     = 0;
            myTimeout.it_value.tv_usec    = 0;
            if (setitimer(ITIMER_REAL, &myTimeout, &oldTimeout) < 0)
                ERR_OUT(SETITIMER_ERROR);

            if (oldTimeout.it_value.tv_sec || oldTimeout.it_value.tv_usec) {
                oldTimerOn = 1;

                /* is old timer going to expire before ours */
                if (CmpTimes(oldTimeout.it_value, timeout, <)) {
                    timeout.tv_sec  = oldTimeout.it_value.tv_sec;
                    timeout.tv_usec = oldTimeout.it_value.tv_usec;
                }
            }
            myTimeout.it_value.tv_sec  = timeout.tv_sec;
            myTimeout.it_value.tv_usec = timeout.tv_usec;

            /* set up signal handler, don't restart socket send/recv */
            act.sa_handler = myHandler;
            sigemptyset(&act.sa_mask);
            act.sa_flags = 0;
#ifdef SA_INTERRUPT
            act.sa_flags |= SA_INTERRUPT;
#endif
            if (sigaction(SIGALRM, &act, &oact) < 0)
                ERR_OUT(SIGACT_ERROR);

            if (setitimer(ITIMER_REAL, &myTimeout, 0) < 0)
                ERR_OUT(SETITIMER_ERROR);
        }

        /* do main work */
#ifndef NO_WOLFSSL_CLIENT
        if (ssl->options.side == WOLFSSL_CLIENT_END)
            ret = wolfSSL_connect(ssl);
#endif
#ifndef NO_WOLFSSL_SERVER
        if (ssl->options.side == WOLFSSL_SERVER_END)
            ret = wolfSSL_accept(ssl);
#endif

        /* do callbacks */
        if (toCb) {
            if (oldTimerOn) {
                gettimeofday(&endTime, 0);
                SubtractTimes(endTime, startTime, totalTime);
                /* adjust old timer for elapsed time */
                if (CmpTimes(totalTime, oldTimeout.it_value, <))
                    SubtractTimes(oldTimeout.it_value, totalTime,
                                  oldTimeout.it_value);
                else {
                    /* reset value to interval, may be off */
                    oldTimeout.it_value.tv_sec = oldTimeout.it_interval.tv_sec;
                    oldTimeout.it_value.tv_usec =oldTimeout.it_interval.tv_usec;
                }
                /* keep iter the same whether there or not */
            }
            /* restore old handler */
            if (sigaction(SIGALRM, &oact, 0) < 0)
                ret = SIGACT_ERROR;    /* more pressing error, stomp */
            else
                /* use old settings which may turn off (expired or not there) */
                if (setitimer(ITIMER_REAL, &oldTimeout, 0) < 0)
                    ret = SETITIMER_ERROR;

            /* if we had a timeout call callback */
            if (ssl->timeoutInfo.timeoutName[0]) {
                ssl->timeoutInfo.timeoutValue.tv_sec  = timeout.tv_sec;
                ssl->timeoutInfo.timeoutValue.tv_usec = timeout.tv_usec;
                (toCb)(&ssl->timeoutInfo);
            }
            /* clean up */
            FreeTimeoutInfo(&ssl->timeoutInfo, ssl->heap);
            ssl->toInfoOn = 0;
        }
        if (hsCb) {
            FinishHandShakeInfo(&ssl->handShakeInfo);
            (hsCb)(&ssl->handShakeInfo);
            ssl->hsInfoOn = 0;
        }
        return ret;
    }


#ifndef NO_WOLFSSL_CLIENT

    int wolfSSL_connect_ex(WOLFSSL* ssl, HandShakeCallBack hsCb,
                          TimeoutCallBack toCb, Timeval timeout)
    {
        WOLFSSL_ENTER("wolfSSL_connect_ex");
        return wolfSSL_ex_wrapper(ssl, hsCb, toCb, timeout);
    }

#endif


#ifndef NO_WOLFSSL_SERVER

    int wolfSSL_accept_ex(WOLFSSL* ssl, HandShakeCallBack hsCb,
                         TimeoutCallBack toCb,Timeval timeout)
    {
        WOLFSSL_ENTER("wolfSSL_accept_ex");
        return wolfSSL_ex_wrapper(ssl, hsCb, toCb, timeout);
    }

#endif

#endif /* WOLFSSL_CALLBACKS */


#ifndef NO_PSK

    void wolfSSL_CTX_set_psk_client_callback(WOLFSSL_CTX* ctx,
                                         wc_psk_client_callback cb)
    {
        WOLFSSL_ENTER("SSL_CTX_set_psk_client_callback");
        ctx->havePSK = 1;
        ctx->client_psk_cb = cb;
    }


    void wolfSSL_set_psk_client_callback(WOLFSSL* ssl,wc_psk_client_callback cb)
    {
        byte haveRSA = 1;

        WOLFSSL_ENTER("SSL_set_psk_client_callback");
        ssl->options.havePSK = 1;
        ssl->options.client_psk_cb = cb;

        #ifdef NO_RSA
            haveRSA = 0;
        #endif
        InitSuites(ssl->suites, ssl->version, haveRSA, TRUE,
                   ssl->options.haveDH, ssl->options.haveNTRU,
                   ssl->options.haveECDSAsig, ssl->options.haveECC,
                   ssl->options.haveStaticECC, ssl->options.side);
    }


    void wolfSSL_CTX_set_psk_server_callback(WOLFSSL_CTX* ctx,
                                         wc_psk_server_callback cb)
    {
        WOLFSSL_ENTER("SSL_CTX_set_psk_server_callback");
        ctx->havePSK = 1;
        ctx->server_psk_cb = cb;
    }


    void wolfSSL_set_psk_server_callback(WOLFSSL* ssl,wc_psk_server_callback cb)
    {
        byte haveRSA = 1;

        WOLFSSL_ENTER("SSL_set_psk_server_callback");
        ssl->options.havePSK = 1;
        ssl->options.server_psk_cb = cb;

        #ifdef NO_RSA
            haveRSA = 0;
        #endif
        InitSuites(ssl->suites, ssl->version, haveRSA, TRUE,
                   ssl->options.haveDH, ssl->options.haveNTRU,
                   ssl->options.haveECDSAsig, ssl->options.haveECC,
                   ssl->options.haveStaticECC, ssl->options.side);
    }


    const char* wolfSSL_get_psk_identity_hint(const WOLFSSL* ssl)
    {
        WOLFSSL_ENTER("SSL_get_psk_identity_hint");

        if (ssl == NULL || ssl->arrays == NULL)
            return NULL;

        return ssl->arrays->server_hint;
    }


    const char* wolfSSL_get_psk_identity(const WOLFSSL* ssl)
    {
        WOLFSSL_ENTER("SSL_get_psk_identity");

        if (ssl == NULL || ssl->arrays == NULL)
            return NULL;

        return ssl->arrays->client_identity;
    }


    int wolfSSL_CTX_use_psk_identity_hint(WOLFSSL_CTX* ctx, const char* hint)
    {
        WOLFSSL_ENTER("SSL_CTX_use_psk_identity_hint");
        if (hint == 0)
            ctx->server_hint[0] = 0;
        else {
            XSTRNCPY(ctx->server_hint, hint, MAX_PSK_ID_LEN);
            ctx->server_hint[MAX_PSK_ID_LEN - 1] = '\0';
        }
        return SSL_SUCCESS;
    }


    int wolfSSL_use_psk_identity_hint(WOLFSSL* ssl, const char* hint)
    {
        WOLFSSL_ENTER("SSL_use_psk_identity_hint");

        if (ssl == NULL || ssl->arrays == NULL)
            return SSL_FAILURE;

        if (hint == 0)
            ssl->arrays->server_hint[0] = 0;
        else {
            XSTRNCPY(ssl->arrays->server_hint, hint, MAX_PSK_ID_LEN);
            ssl->arrays->server_hint[MAX_PSK_ID_LEN - 1] = '\0';
        }
        return SSL_SUCCESS;
    }

#endif /* NO_PSK */


#ifdef HAVE_ANON

    int wolfSSL_CTX_allow_anon_cipher(WOLFSSL_CTX* ctx)
    {
        WOLFSSL_ENTER("wolfSSL_CTX_allow_anon_cipher");

        if (ctx == NULL)
            return SSL_FAILURE;

        ctx->haveAnon = 1;

        return SSL_SUCCESS;
    }

#endif /* HAVE_ANON */


#ifndef NO_CERTS
/* used to be defined on NO_FILESYSTEM only, but are generally useful */

    /* wolfSSL extension allows DER files to be loaded from buffers as well */
    int wolfSSL_CTX_load_verify_buffer(WOLFSSL_CTX* ctx,
                                       const unsigned char* in,
                                       long sz, int format)
    {
        WOLFSSL_ENTER("wolfSSL_CTX_load_verify_buffer");
        if (format == SSL_FILETYPE_PEM)
            return ProcessChainBuffer(ctx, in, sz, format, CA_TYPE, NULL);
        else
            return ProcessBuffer(ctx, in, sz, format, CA_TYPE, NULL,NULL,0);
    }


#ifdef WOLFSSL_TRUST_PEER_CERT
    int wolfSSL_CTX_trust_peer_buffer(WOLFSSL_CTX* ctx,
                                       const unsigned char* in,
                                       long sz, int format)
    {
        WOLFSSL_ENTER("wolfSSL_CTX_trust_peer_buffer");

        /* sanity check on arguments */
        if (sz < 0 || in == NULL || ctx == NULL) {
            return BAD_FUNC_ARG;
        }

        if (format == SSL_FILETYPE_PEM)
            return ProcessChainBuffer(ctx, in, sz, format,
                                                       TRUSTED_PEER_TYPE, NULL);
        else
            return ProcessBuffer(ctx, in, sz, format, TRUSTED_PEER_TYPE,
                                                                   NULL,NULL,0);
    }
#endif /* WOLFSSL_TRUST_PEER_CERT */


    int wolfSSL_CTX_use_certificate_buffer(WOLFSSL_CTX* ctx,
                                 const unsigned char* in, long sz, int format)
    {
        WOLFSSL_ENTER("wolfSSL_CTX_use_certificate_buffer");
        return ProcessBuffer(ctx, in, sz, format, CERT_TYPE, NULL, NULL, 0);
    }


    int wolfSSL_CTX_use_PrivateKey_buffer(WOLFSSL_CTX* ctx,
                                 const unsigned char* in, long sz, int format)
    {
        WOLFSSL_ENTER("wolfSSL_CTX_use_PrivateKey_buffer");
        return ProcessBuffer(ctx, in, sz, format, PRIVATEKEY_TYPE, NULL,NULL,0);
    }


    int wolfSSL_CTX_use_certificate_chain_buffer_format(WOLFSSL_CTX* ctx,
                                 const unsigned char* in, long sz, int format)
    {
        WOLFSSL_ENTER("wolfSSL_CTX_use_certificate_chain_buffer_format");
        return ProcessBuffer(ctx, in, sz, format, CERT_TYPE, NULL, NULL, 1);
    }

    int wolfSSL_CTX_use_certificate_chain_buffer(WOLFSSL_CTX* ctx,
                                 const unsigned char* in, long sz)
    {
        return wolfSSL_CTX_use_certificate_chain_buffer_format(ctx, in, sz,
                                                            SSL_FILETYPE_PEM);
    }


#ifndef NO_DH

    /* server wrapper for ctx or ssl Diffie-Hellman parameters */
    static int wolfSSL_SetTmpDH_buffer_wrapper(WOLFSSL_CTX* ctx, WOLFSSL* ssl,
                                               const unsigned char* buf,
                                               long sz, int format)
    {
        DerBuffer* der = NULL;
        int    ret      = 0;
        word32 pSz = MAX_DH_SIZE;
        word32 gSz = MAX_DH_SIZE;
    #ifdef WOLFSSL_SMALL_STACK
        byte*  p = NULL;
        byte*  g = NULL;
    #else
        byte   p[MAX_DH_SIZE];
        byte   g[MAX_DH_SIZE];
    #endif

        if (ctx == NULL || buf == NULL)
            return BAD_FUNC_ARG;

        ret = AllocDer(&der, 0, DH_PARAM_TYPE, ctx->heap);
        if (ret != 0) {
            return ret;
        }
        der->buffer = (byte*)buf;
        der->length = (word32)sz;

    #ifdef WOLFSSL_SMALL_STACK
        p = (byte*)XMALLOC(pSz, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        g = (byte*)XMALLOC(gSz, NULL, DYNAMIC_TYPE_TMP_BUFFER);

        if (p == NULL || g == NULL) {
            XFREE(p, NULL, DYNAMIC_TYPE_TMP_BUFFER);
            XFREE(g, NULL, DYNAMIC_TYPE_TMP_BUFFER);
            return MEMORY_E;
        }
    #endif

        if (format != SSL_FILETYPE_ASN1 && format != SSL_FILETYPE_PEM)
            ret = SSL_BAD_FILETYPE;
        else {
            if (format == SSL_FILETYPE_PEM) {
                FreeDer(&der);
                ret = PemToDer(buf, sz, DH_PARAM_TYPE, &der, ctx->heap,
                               NULL, NULL);
            }

            if (ret == 0) {
                if (wc_DhParamsLoad(der->buffer, der->length, p, &pSz, g, &gSz) < 0)
                    ret = SSL_BAD_FILETYPE;
                else if (ssl)
                    ret = wolfSSL_SetTmpDH(ssl, p, pSz, g, gSz);
                else
                    ret = wolfSSL_CTX_SetTmpDH(ctx, p, pSz, g, gSz);
            }
        }

        FreeDer(&der);

    #ifdef WOLFSSL_SMALL_STACK
        XFREE(p, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        XFREE(g, NULL, DYNAMIC_TYPE_TMP_BUFFER);
    #endif

        return ret;
    }


    /* server Diffie-Hellman parameters, SSL_SUCCESS on ok */
    int wolfSSL_SetTmpDH_buffer(WOLFSSL* ssl, const unsigned char* buf, long sz,
                               int format)
    {
        if (ssl == NULL)
            return BAD_FUNC_ARG;

        return wolfSSL_SetTmpDH_buffer_wrapper(ssl->ctx, ssl, buf, sz, format);
    }


    /* server ctx Diffie-Hellman parameters, SSL_SUCCESS on ok */
    int wolfSSL_CTX_SetTmpDH_buffer(WOLFSSL_CTX* ctx, const unsigned char* buf,
                                   long sz, int format)
    {
        return wolfSSL_SetTmpDH_buffer_wrapper(ctx, NULL, buf, sz, format);
    }

#endif /* NO_DH */


    int wolfSSL_use_certificate_buffer(WOLFSSL* ssl,
                                 const unsigned char* in, long sz, int format)
    {
        WOLFSSL_ENTER("wolfSSL_use_certificate_buffer");
        return ProcessBuffer(ssl->ctx, in, sz, format,CERT_TYPE,ssl,NULL,0);
    }


    int wolfSSL_use_PrivateKey_buffer(WOLFSSL* ssl,
                                 const unsigned char* in, long sz, int format)
    {
        WOLFSSL_ENTER("wolfSSL_use_PrivateKey_buffer");
        return ProcessBuffer(ssl->ctx, in, sz, format, PRIVATEKEY_TYPE,
                             ssl, NULL, 0);
    }

    int wolfSSL_use_certificate_chain_buffer_format(WOLFSSL* ssl,
                                 const unsigned char* in, long sz, int format)
    {
        WOLFSSL_ENTER("wolfSSL_use_certificate_chain_buffer_format");
        return ProcessBuffer(ssl->ctx, in, sz, format, CERT_TYPE,
                             ssl, NULL, 1);
    }

    int wolfSSL_use_certificate_chain_buffer(WOLFSSL* ssl,
                                 const unsigned char* in, long sz)
    {
        return wolfSSL_use_certificate_chain_buffer_format(ssl, in, sz,
                                                            SSL_FILETYPE_PEM);
    }


    /* unload any certs or keys that SSL owns, leave CTX as is
       SSL_SUCCESS on ok */
    int wolfSSL_UnloadCertsKeys(WOLFSSL* ssl)
    {
        if (ssl == NULL) {
            WOLFSSL_MSG("Null function arg");
            return BAD_FUNC_ARG;
        }

        if (ssl->buffers.weOwnCert && !ssl->keepCert) {
            WOLFSSL_MSG("Unloading cert");
            FreeDer(&ssl->buffers.certificate);
            #ifdef KEEP_OUR_CERT
                FreeX509(ssl->ourCert);
                if (ssl->ourCert) {
                    XFREE(ssl->ourCert, ssl->heap, DYNAMIC_TYPE_X509);
                    ssl->ourCert = NULL;
                }
            #endif
            ssl->buffers.weOwnCert = 0;
        }

        if (ssl->buffers.weOwnCertChain) {
            WOLFSSL_MSG("Unloading cert chain");
            FreeDer(&ssl->buffers.certChain);
            ssl->buffers.weOwnCertChain = 0;
        }

        if (ssl->buffers.weOwnKey) {
            WOLFSSL_MSG("Unloading key");
            FreeDer(&ssl->buffers.key);
            ssl->buffers.weOwnKey = 0;
        }

        return SSL_SUCCESS;
    }


    int wolfSSL_CTX_UnloadCAs(WOLFSSL_CTX* ctx)
    {
        WOLFSSL_ENTER("wolfSSL_CTX_UnloadCAs");

        if (ctx == NULL)
            return BAD_FUNC_ARG;

        return wolfSSL_CertManagerUnloadCAs(ctx->cm);
    }


#ifdef WOLFSSL_TRUST_PEER_CERT
    int wolfSSL_CTX_Unload_trust_peers(WOLFSSL_CTX* ctx)
    {
        WOLFSSL_ENTER("wolfSSL_CTX_Unload_trust_peers");

        if (ctx == NULL)
            return BAD_FUNC_ARG;

        return wolfSSL_CertManagerUnload_trust_peers(ctx->cm);
    }
#endif /* WOLFSSL_TRUST_PEER_CERT */
/* old NO_FILESYSTEM end */
#endif /* !NO_CERTS */


#if defined(OPENSSL_EXTRA) || defined(GOAHEAD_WS)


    int wolfSSL_add_all_algorithms(void)
    {
        WOLFSSL_ENTER("wolfSSL_add_all_algorithms");
        if (wolfSSL_Init() == SSL_SUCCESS)
            return SSL_SUCCESS;
        else
            return SSL_FATAL_ERROR;
    }


   /* returns previous set cache size which stays constant */
    long wolfSSL_CTX_sess_set_cache_size(WOLFSSL_CTX* ctx, long sz)
    {
        /* cache size fixed at compile time in wolfSSL */
        (void)ctx;
        (void)sz;
        WOLFSSL_MSG("session cache is set at compile time");
        #ifndef NO_SESSION_CACHE
            return SESSIONS_PER_ROW * SESSION_ROWS;
        #else
            return 0;
        #endif
    }


    void wolfSSL_CTX_set_quiet_shutdown(WOLFSSL_CTX* ctx, int mode)
    {
        WOLFSSL_ENTER("wolfSSL_CTX_set_quiet_shutdown");
        if (mode)
            ctx->quietShutdown = 1;
    }


    void wolfSSL_set_quiet_shutdown(WOLFSSL* ssl, int mode)
    {
        WOLFSSL_ENTER("wolfSSL_CTX_set_quiet_shutdown");
        if (mode)
            ssl->options.quietShutdown = 1;
    }


    void wolfSSL_set_bio(WOLFSSL* ssl, WOLFSSL_BIO* rd, WOLFSSL_BIO* wr)
    {
        WOLFSSL_ENTER("SSL_set_bio");
        wolfSSL_set_rfd(ssl, rd->fd);
        wolfSSL_set_wfd(ssl, wr->fd);

        ssl->biord = rd;
        ssl->biowr = wr;
    }


    void wolfSSL_CTX_set_client_CA_list(WOLFSSL_CTX* ctx,
                                       STACK_OF(WOLFSSL_X509_NAME)* names)
    {
        (void)ctx;
        (void)names;
    }


    STACK_OF(WOLFSSL_X509_NAME)* wolfSSL_load_client_CA_file(const char* fname)
    {
        (void)fname;
        return 0;
    }


    int wolfSSL_CTX_set_default_verify_paths(WOLFSSL_CTX* ctx)
    {
        /* TODO:, not needed in goahead */
        (void)ctx;
        return SSL_NOT_IMPLEMENTED;
    }


    /* keyblock size in bytes or -1 */
    int wolfSSL_get_keyblock_size(WOLFSSL* ssl)
    {
        if (ssl == NULL)
            return SSL_FATAL_ERROR;

        return 2 * (ssl->specs.key_size + ssl->specs.iv_size +
                    ssl->specs.hash_size);
    }


    /* store keys returns SSL_SUCCESS or -1 on error */
    int wolfSSL_get_keys(WOLFSSL* ssl, unsigned char** ms, unsigned int* msLen,
                                     unsigned char** sr, unsigned int* srLen,
                                     unsigned char** cr, unsigned int* crLen)
    {
        if (ssl == NULL || ssl->arrays == NULL)
            return SSL_FATAL_ERROR;

        *ms = ssl->arrays->masterSecret;
        *sr = ssl->arrays->serverRandom;
        *cr = ssl->arrays->clientRandom;

        *msLen = SECRET_LEN;
        *srLen = RAN_LEN;
        *crLen = RAN_LEN;

        return SSL_SUCCESS;
    }


    void wolfSSL_set_accept_state(WOLFSSL* ssl)
    {
        word16 haveRSA = 1;
        word16 havePSK = 0;

        WOLFSSL_ENTER("SSL_set_accept_state");
        ssl->options.side = WOLFSSL_SERVER_END;
        /* reset suites in case user switched */

        #ifdef NO_RSA
            haveRSA = 0;
        #endif
        #ifndef NO_PSK
            havePSK = ssl->options.havePSK;
        #endif
        InitSuites(ssl->suites, ssl->version, haveRSA, havePSK,
                   ssl->options.haveDH, ssl->options.haveNTRU,
                   ssl->options.haveECDSAsig, ssl->options.haveECC,
                   ssl->options.haveStaticECC, ssl->options.side);
    }
#endif

    /* return true if connection established */
    int wolfSSL_is_init_finished(WOLFSSL* ssl)
    {
        if (ssl == NULL)
            return 0;

        if (ssl->options.handShakeState == HANDSHAKE_DONE)
            return 1;

        return 0;
    }

#if defined(OPENSSL_EXTRA) || defined(GOAHEAD_WS)
    void wolfSSL_CTX_set_tmp_rsa_callback(WOLFSSL_CTX* ctx,
                                      WOLFSSL_RSA*(*f)(WOLFSSL*, int, int))
    {
        /* wolfSSL verifies all these internally */
        (void)ctx;
        (void)f;
    }


    void wolfSSL_set_shutdown(WOLFSSL* ssl, int opt)
    {
        WOLFSSL_ENTER("wolfSSL_set_shutdown");
        if(ssl==NULL) {
            WOLFSSL_MSG("Shutdown not set. ssl is null");
            return;
        }

        ssl->options.sentNotify =  (opt&SSL_SENT_SHUTDOWN) > 0;
        ssl->options.closeNotify = (opt&SSL_RECEIVED_SHUTDOWN) > 0;
    }


    long wolfSSL_CTX_get_options(WOLFSSL_CTX* ctx)
    {
        (void)ctx;
        WOLFSSL_ENTER("wolfSSL_CTX_get_options");
        WOLFSSL_MSG("wolfSSL options are set through API calls and macros");

        return 0;
    }


    long wolfSSL_CTX_set_options(WOLFSSL_CTX* ctx, long opt)
    {
        /* goahead calls with 0, do nothing */
        WOLFSSL_ENTER("SSL_CTX_set_options");
        (void)ctx;
        return opt;
    }


    int wolfSSL_set_rfd(WOLFSSL* ssl, int rfd)
    {
        WOLFSSL_ENTER("SSL_set_rfd");
        ssl->rfd = rfd;      /* not used directly to allow IO callbacks */

        ssl->IOCB_ReadCtx  = &ssl->rfd;

        return SSL_SUCCESS;
    }


    int wolfSSL_set_wfd(WOLFSSL* ssl, int wfd)
    {
        WOLFSSL_ENTER("SSL_set_wfd");
        ssl->wfd = wfd;      /* not used directly to allow IO callbacks */

        ssl->IOCB_WriteCtx  = &ssl->wfd;

        return SSL_SUCCESS;
    }


    WOLFSSL_RSA* wolfSSL_RSA_generate_key(int len, unsigned long bits,
                                          void(*f)(int, int, void*), void* data)
    {
        /* no tmp key needed, actual generation not supported */
        WOLFSSL_ENTER("RSA_generate_key");
        (void)len;
        (void)bits;
        (void)f;
        (void)data;
        return NULL;
    }


    WOLFSSL_X509_STORE* wolfSSL_CTX_get_cert_store(WOLFSSL_CTX* ctx)
    {
        if (ctx == NULL) {
            return NULL;
        }

        return &(ctx->x509_store);
    }


#ifndef NO_CERTS
    void wolfSSL_CTX_set_cert_store(WOLFSSL_CTX* ctx, WOLFSSL_X509_STORE* str)
    {
        if (ctx == NULL || str == NULL) {
            return;
        }

        /* free cert manager if have one */
        if (ctx->cm != NULL) {
            wolfSSL_CertManagerFree(ctx->cm);
        }
        ctx->cm               = str->cm;
        ctx->x509_store.cache = str->cache;
    }


    WOLFSSL_X509* wolfSSL_X509_STORE_CTX_get_current_cert(
                                                    WOLFSSL_X509_STORE_CTX* ctx)
    {
        WOLFSSL_ENTER("wolfSSL_X509_STORE_CTX_get_current_cert");
        if(ctx)
            return ctx->current_cert;
        return NULL;
    }


    int wolfSSL_X509_STORE_CTX_get_error(WOLFSSL_X509_STORE_CTX* ctx)
    {
        WOLFSSL_ENTER("wolfSSL_X509_STORE_CTX_get_error");
        if (ctx != NULL)
            return ctx->error;
        return 0;
    }


    int wolfSSL_X509_STORE_CTX_get_error_depth(WOLFSSL_X509_STORE_CTX* ctx)
    {
        WOLFSSL_ENTER("wolfSSL_X509_STORE_CTX_get_error_depth");
        if(ctx)
            return ctx->error_depth;
        return SSL_FATAL_ERROR;
    }
#endif


    WOLFSSL_BIO_METHOD* wolfSSL_BIO_f_buffer(void)
    {
        static WOLFSSL_BIO_METHOD meth;

        WOLFSSL_ENTER("BIO_f_buffer");
        meth.type = WOLFSSL_BIO_BUFFER;

        return &meth;
    }


    long wolfSSL_BIO_set_write_buffer_size(WOLFSSL_BIO* bio, long size)
    {
        /* wolfSSL has internal buffer, compatibility only */
        WOLFSSL_ENTER("BIO_set_write_buffer_size");
        (void)bio;
        return size;
    }


    WOLFSSL_BIO_METHOD* wolfSSL_BIO_s_bio(void)
    {
        static WOLFSSL_BIO_METHOD bio_meth;

        WOLFSSL_ENTER("wolfSSL_BIO_f_bio");
        bio_meth.type = WOLFSSL_BIO_BIO;

        return &bio_meth;
    }


#ifndef NO_FILESYSTEM
    WOLFSSL_BIO_METHOD* wolfSSL_BIO_s_file(void)
    {
        static WOLFSSL_BIO_METHOD file_meth;

        WOLFSSL_ENTER("wolfSSL_BIO_f_file");
        file_meth.type = WOLFSSL_BIO_FILE;

        return &file_meth;
    }
#endif


    WOLFSSL_BIO_METHOD* wolfSSL_BIO_f_ssl(void)
    {
        static WOLFSSL_BIO_METHOD meth;

        WOLFSSL_ENTER("BIO_f_ssl");
        meth.type = WOLFSSL_BIO_SSL;

        return &meth;
    }


    WOLFSSL_BIO_METHOD *wolfSSL_BIO_s_socket(void)
    {
        static WOLFSSL_BIO_METHOD meth;

        WOLFSSL_ENTER("BIO_s_socket");
        meth.type = WOLFSSL_BIO_SOCKET;

        return &meth;
    }


    WOLFSSL_BIO* wolfSSL_BIO_new_socket(int sfd, int closeF)
    {
        WOLFSSL_BIO* bio = (WOLFSSL_BIO*) XMALLOC(sizeof(WOLFSSL_BIO), 0,
                                                DYNAMIC_TYPE_OPENSSL);

        WOLFSSL_ENTER("BIO_new_socket");
        if (bio) {
            XMEMSET(bio, 0, sizeof(WOLFSSL_BIO));
            bio->type  = WOLFSSL_BIO_SOCKET;
            bio->close = (byte)closeF;
            bio->fd    = sfd;
            bio->mem   = NULL;
        }
        return bio;
    }


    int wolfSSL_BIO_eof(WOLFSSL_BIO* b)
    {
        WOLFSSL_ENTER("BIO_eof");
        if (b->eof)
            return 1;

        return 0;
    }


    long wolfSSL_BIO_set_ssl(WOLFSSL_BIO* b, WOLFSSL* ssl, int closeF)
    {
        WOLFSSL_ENTER("wolfSSL_BIO_set_ssl");

        if (b != NULL) {
            b->ssl   = ssl;
            b->close = (byte)closeF;
    /* add to ssl for bio free if SSL_free called before/instead of free_all? */
        }

        return 0;
    }


    long wolfSSL_BIO_set_fd(WOLFSSL_BIO* b, int fd, int closeF)
    {
        WOLFSSL_ENTER("wolfSSL_BIO_set_fd");

        if (b != NULL) {
            b->fd    = fd;
            b->close = (byte)closeF;
        }

        return SSL_SUCCESS;
    }


    WOLFSSL_BIO* wolfSSL_BIO_new(WOLFSSL_BIO_METHOD* method)
    {
        WOLFSSL_BIO* bio = (WOLFSSL_BIO*) XMALLOC(sizeof(WOLFSSL_BIO), 0,
                                                DYNAMIC_TYPE_OPENSSL);
        WOLFSSL_ENTER("BIO_new");
        if (bio) {
            XMEMSET(bio, 0, sizeof(WOLFSSL_BIO));
            bio->type   = method->type;
            bio->ssl    = NULL;
            bio->mem    = NULL;
            bio->prev   = NULL;
            bio->next   = NULL;
        }
        return bio;
    }


    int wolfSSL_BIO_get_mem_data(WOLFSSL_BIO* bio, const byte** p)
    {
        if (bio == NULL || p == NULL)
            return SSL_FATAL_ERROR;

        *p = bio->mem;

        return bio->memLen;
    }


    WOLFSSL_BIO* wolfSSL_BIO_new_mem_buf(void* buf, int len)
    {
        WOLFSSL_BIO* bio = NULL;
        if (buf == NULL)
            return bio;

        bio = wolfSSL_BIO_new(wolfSSL_BIO_s_mem());
        if (bio == NULL)
            return bio;

        bio->memLen = bio->wrSz = len;
        bio->mem    = (byte*)XMALLOC(len, 0, DYNAMIC_TYPE_OPENSSL);
        if (bio->mem == NULL) {
            XFREE(bio, 0, DYNAMIC_TYPE_OPENSSL);
            return NULL;
        }

        XMEMCPY(bio->mem, buf, len);

        return bio;
    }


#ifdef USE_WINDOWS_API
    #define CloseSocket(s) closesocket(s)
#elif defined(WOLFSSL_MDK_ARM)  || defined(WOLFSSL_KEIL_TCP_NET)
    #define CloseSocket(s) closesocket(s)
    extern int closesocket(int) ;
#else
    #define CloseSocket(s) close(s)
#endif

    int wolfSSL_BIO_free(WOLFSSL_BIO* bio)
    {
        /* unchain?, doesn't matter in goahead since from free all */
        WOLFSSL_ENTER("wolfSSL_BIO_free");
        if (bio) {
            /* remove from pair by setting the paired bios pair to NULL */
            if (bio->pair != NULL) {
                bio->pair->pair = NULL;
            }

            if (bio->close) {
                if (bio->ssl)
                    wolfSSL_free(bio->ssl);
                if (bio->fd)
                    CloseSocket(bio->fd);
            }

        #ifndef NO_FILESYSTEM
            if (bio->type == WOLFSSL_BIO_FILE && bio->close == BIO_CLOSE) {
                if (bio->file) {
                    XFCLOSE(bio->file);
                }
            }
        #endif

            if (bio->mem)
                XFREE(bio->mem, bio->heap, DYNAMIC_TYPE_OPENSSL);
            XFREE(bio, bio->heap, DYNAMIC_TYPE_OPENSSL);
        }
        return 0;
    }


    int wolfSSL_BIO_free_all(WOLFSSL_BIO* bio)
    {
        WOLFSSL_ENTER("BIO_free_all");
        while (bio) {
            WOLFSSL_BIO* next = bio->next;
            wolfSSL_BIO_free(bio);
            bio = next;
        }
        return 0;
    }


    static int wolfSSL_BIO_BIO_read(WOLFSSL_BIO* bio, void* buf, int len)
    {
        int   sz;
        char* pt;

        sz = wolfSSL_BIO_nread(bio, &pt, len);

        if (sz > 0) {
            XMEMCPY(buf, pt, sz);
        }

        return sz;
    }


    static int wolfSSL_BIO_MEMORY_read(WOLFSSL_BIO* bio, void* buf, int len)
    {
        int   sz;

        sz = wolfSSL_BIO_pending(bio);
        if (sz > 0) {
            const unsigned char* pt = NULL;
            int memSz;

            if (sz > len) {
                sz = len;
            }
            memSz = wolfSSL_BIO_get_mem_data(bio, &pt);
            if (memSz >= sz && pt != NULL) {
                byte* tmp;

                XMEMCPY(buf, (void*)pt, sz);

                if (memSz - sz > 0) {
                    tmp = (byte*)XMALLOC(memSz-sz, bio->heap,
                            DYNAMIC_TYPE_OPENSSL);
                    if (tmp == NULL) {
                        WOLFSSL_MSG("Memory error");
                        return WOLFSSL_BIO_ERROR;
                    }
                    XMEMCPY(tmp, (void*)(pt + sz), memSz - sz);

                    /* reset internal bio->mem */
                    XFREE(bio->mem, bio->heap, DYNAMIC_TYPE_OPENSSL);
                    bio->mem    = tmp;
                    bio->memLen = memSz-sz;
                }
                bio->wrSz  -= sz;
            }
            else {
                WOLFSSL_MSG("Issue with getting bio mem pointer");
                return 0;
            }
        }
        else {
            return WOLFSSL_BIO_ERROR;
        }

        return sz;
    }


    int wolfSSL_BIO_read(WOLFSSL_BIO* bio, void* buf, int len)
    {
        int  ret;
        WOLFSSL* ssl = 0;
        WOLFSSL_BIO* front = bio;

        WOLFSSL_ENTER("wolfSSL_BIO_read");

        if (bio && bio->type == WOLFSSL_BIO_BIO) {
            return wolfSSL_BIO_BIO_read(bio, buf, len);
        }

        if (bio && bio->type == WOLFSSL_BIO_MEMORY) {
            return wolfSSL_BIO_MEMORY_read(bio, buf, len);
        }

    #ifndef NO_FILESYSTEM
        if (bio && bio->type == WOLFSSL_BIO_FILE) {
            return (int)XFREAD(buf, 1, len, bio->file);
        }
    #endif

        /* already got eof, again is error */
        if (bio && front->eof)
            return SSL_FATAL_ERROR;

        while(bio && ((ssl = bio->ssl) == 0) )
            bio = bio->next;

        if (ssl == 0) return BAD_FUNC_ARG;

        ret = wolfSSL_read(ssl, buf, len);
        if (ret == 0)
            front->eof = 1;
        else if (ret < 0) {
            int err = wolfSSL_get_error(ssl, 0);
            if ( !(err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) )
                front->eof = 1;
        }
        return ret;
    }


    static int wolfSSL_BIO_BIO_write(WOLFSSL_BIO* bio, const void* data,
            int len)
    {
        /* internal function where arguments have already been sanity checked */
        int   sz;
        char* buf;

        sz = wolfSSL_BIO_nwrite(bio, &buf, len);

        /* test space for write */
        if (sz <= 0) {
            WOLFSSL_MSG("No room left to write");
            return sz;
        }

        XMEMCPY(buf, data, sz);

        return sz;
    }


    /* for complete compatibility a bio memory write allocs its own memory
     * untill the application runs out ....
     */
    static int wolfSSL_BIO_MEMORY_write(WOLFSSL_BIO* bio, const void* data,
            int len)
    {
        /* internal function where arguments have already been sanity checked */
        int   sz;
        int   ret;
        const unsigned char* buf;

        sz = wolfSSL_BIO_pending(bio);
        if (sz < 0) {
            WOLFSSL_MSG("Error getting memory data");
            return sz;
        }

        if (bio->mem == NULL) {
            bio->mem = (byte*)XMALLOC(len, bio->heap,
                DYNAMIC_TYPE_OPENSSL);
            if (bio->mem == NULL) {
                WOLFSSL_MSG("Error on malloc");
                return SSL_FAILURE;
            }
            bio->memLen = len;
        }

        /* check if will fit in current buffer size */
        if ((ret = wolfSSL_BIO_get_mem_data(bio, &buf)) < sz + len) {
            if (ret <= 0) {
                return WOLFSSL_BIO_ERROR;
            }
            else {
                bio->mem = (byte*)XREALLOC(bio->mem, sz + len, bio->heap,
                    DYNAMIC_TYPE_OPENSSL);
                if (bio->mem == NULL) {
                    WOLFSSL_MSG("Error on realloc");
                    return SSL_FAILURE;
                }
                bio->memLen = sz + len;
            }
        }

        XMEMCPY(bio->mem + sz, data, len);
        bio->wrSz += len;

        return len;
    }


    int wolfSSL_BIO_write(WOLFSSL_BIO* bio, const void* data, int len)
    {
        int  ret;
        WOLFSSL* ssl = 0;
        WOLFSSL_BIO* front = bio;

        WOLFSSL_ENTER("wolfSSL_BIO_write");

        if (bio && bio->type == WOLFSSL_BIO_BIO) {
            return wolfSSL_BIO_BIO_write(bio, data, len);
        }

        if (bio && bio->type == WOLFSSL_BIO_MEMORY) {
            return wolfSSL_BIO_MEMORY_write(bio, data, len);
        }

    #ifndef NO_FILESYSTEM
        if (bio && bio->type == WOLFSSL_BIO_FILE) {
            return (int)XFWRITE(data, 1, len, bio->file);
        }
    #endif

        /* already got eof, again is error */
        if (bio && front->eof)
            return SSL_FATAL_ERROR;

        while(bio && ((ssl = bio->ssl) == 0) )
            bio = bio->next;

        if (ssl == 0) return BAD_FUNC_ARG;

        ret = wolfSSL_write(ssl, data, len);
        if (ret == 0)
            front->eof = 1;
        else if (ret < 0) {
            int err = wolfSSL_get_error(ssl, 0);
            if ( !(err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) )
                front->eof = 1;
        }

        return ret;
    }


    WOLFSSL_BIO* wolfSSL_BIO_push(WOLFSSL_BIO* top, WOLFSSL_BIO* append)
    {
        WOLFSSL_ENTER("BIO_push");
        top->next    = append;
        append->prev = top;

        return top;
    }


    int wolfSSL_BIO_flush(WOLFSSL_BIO* bio)
    {
        /* for wolfSSL no flushing needed */
        WOLFSSL_ENTER("BIO_flush");
        (void)bio;
        return 1;
    }


#endif /* OPENSSL_EXTRA || GOAHEAD_WS */


#if defined(OPENSSL_EXTRA) || defined(HAVE_WEBSERVER)

    void wolfSSL_CTX_set_default_passwd_cb_userdata(WOLFSSL_CTX* ctx,
                                                   void* userdata)
    {
        WOLFSSL_ENTER("SSL_CTX_set_default_passwd_cb_userdata");
        ctx->userdata = userdata;
    }


    void wolfSSL_CTX_set_default_passwd_cb(WOLFSSL_CTX* ctx,pem_password_cb* cb)
    {
        WOLFSSL_ENTER("SSL_CTX_set_default_passwd_cb");
        if (ctx != NULL) {
            ctx->passwd_cb = cb;
        }
    }

    int wolfSSL_num_locks(void)
    {
        return 0;
    }

    void wolfSSL_set_locking_callback(void (*f)(int, int, const char*, int))
    {
        (void)f;
    }

    void wolfSSL_set_id_callback(unsigned long (*f)(void))
    {
        (void)f;
    }

    unsigned long wolfSSL_ERR_get_error(void)
    {
        /* TODO: */
        return 0;
    }

#ifndef NO_MD5

    int wolfSSL_EVP_BytesToKey(const WOLFSSL_EVP_CIPHER* type,
                       const WOLFSSL_EVP_MD* md, const byte* salt,
                       const byte* data, int sz, int count, byte* key, byte* iv)
    {
        int  keyLen = 0;
        int  ivLen  = 0;
        int  j;
        int  keyLeft;
        int  ivLeft;
        int  keyOutput = 0;
        byte digest[MD5_DIGEST_SIZE];
    #ifdef WOLFSSL_SMALL_STACK
        Md5* md5 = NULL;
    #else
        Md5  md5[1];
    #endif

    #ifdef WOLFSSL_SMALL_STACK
        md5 = (Md5*)XMALLOC(sizeof(Md5), NULL, DYNAMIC_TYPE_TMP_BUFFER);
        if (md5 == NULL)
            return 0;
    #endif

        (void)type;

        WOLFSSL_ENTER("wolfSSL_EVP_BytesToKey");
        wc_InitMd5(md5);

        /* only support MD5 for now */
        if (XSTRNCMP(md, "MD5", 3) != 0) return 0;

        /* only support CBC DES and AES for now */
        #ifndef NO_DES3
        if (XSTRNCMP(type, EVP_DES_CBC, EVP_DES_SIZE) == 0) {
            keyLen = DES_KEY_SIZE;
            ivLen  = DES_IV_SIZE;
        }
        else if (XSTRNCMP(type, EVP_DES_EDE3_CBC, EVP_DES_EDE3_SIZE) == 0) {
            keyLen = DES3_KEY_SIZE;
            ivLen  = DES_IV_SIZE;
        }
        else
        #endif /* NO_DES3 */
        #ifndef NO_AES
        if (XSTRNCMP(type, EVP_AES_128_CBC, EVP_AES_SIZE) == 0) {
            keyLen = AES_128_KEY_SIZE;
            ivLen  = AES_IV_SIZE;
        }
        else if (XSTRNCMP(type, EVP_AES_192_CBC, EVP_AES_SIZE) == 0) {
            keyLen = AES_192_KEY_SIZE;
            ivLen  = AES_IV_SIZE;
        }
        else if (XSTRNCMP(type, EVP_AES_256_CBC, EVP_AES_SIZE) == 0) {
            keyLen = AES_256_KEY_SIZE;
            ivLen  = AES_IV_SIZE;
        }
        else
        #endif /* NO_AES */
        {
        #ifdef WOLFSSL_SMALL_STACK
            XFREE(md5, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        #endif
            return 0;
        }

        keyLeft   = keyLen;
        ivLeft    = ivLen;

        while (keyOutput < (keyLen + ivLen)) {
            int digestLeft = MD5_DIGEST_SIZE;
            /* D_(i - 1) */
            if (keyOutput)                      /* first time D_0 is empty */
                wc_Md5Update(md5, digest, MD5_DIGEST_SIZE);
            /* data */
            wc_Md5Update(md5, data, sz);
            /* salt */
            if (salt)
                wc_Md5Update(md5, salt, EVP_SALT_SIZE);
            wc_Md5Final(md5, digest);
            /* count */
            for (j = 1; j < count; j++) {
                wc_Md5Update(md5, digest, MD5_DIGEST_SIZE);
                wc_Md5Final(md5, digest);
            }

            if (keyLeft) {
                int store = min(keyLeft, MD5_DIGEST_SIZE);
                XMEMCPY(&key[keyLen - keyLeft], digest, store);

                keyOutput  += store;
                keyLeft    -= store;
                digestLeft -= store;
            }

            if (ivLeft && digestLeft) {
                int store = min(ivLeft, digestLeft);
                if (iv != NULL)
                    XMEMCPY(&iv[ivLen - ivLeft],
                            &digest[MD5_DIGEST_SIZE - digestLeft], store);
                keyOutput += store;
                ivLeft    -= store;
            }
        }

    #ifdef WOLFSSL_SMALL_STACK
        XFREE(md5, NULL, DYNAMIC_TYPE_TMP_BUFFER);
    #endif

        return keyOutput == (keyLen + ivLen) ? keyOutput : 0;
    }

#endif /* NO_MD5 */

#endif /* OPENSSL_EXTRA || HAVE_WEBSERVER */


#ifdef OPENSSL_EXTRA

#if !defined(NO_WOLFSSL_SERVER)
size_t wolfSSL_get_server_random(const WOLFSSL *ssl, unsigned char *out,
                                                                   size_t outSz)
{
    size_t size;

    /* return max size of buffer */
    if (outSz == 0) {
        return RAN_LEN;
    }

    if (ssl == NULL || out == NULL) {
        return 0;
    }

    if (ssl->options.saveArrays == 0 || ssl->arrays == NULL) {
        WOLFSSL_MSG("Arrays struct not saved after handshake");
        return 0;
    }

    if (outSz > RAN_LEN) {
        size = RAN_LEN;
    }
    else {
        size = outSz;
    }

    XMEMCPY(out, ssl->arrays->serverRandom, size);
    return size;
}
#endif /* !defined(NO_WOLFSSL_SERVER) */


#if !defined(NO_WOLFSSL_CLIENT)
/* Return the amount of random bytes copied over or error case.
 * ssl : ssl struct after handshake
 * out : buffer to hold random bytes
 * outSz : either 0 (return max buffer sz) or size of out buffer
 *
 * NOTE: wolfSSL_KeepArrays(ssl) must be called to retain handshake information.
 */
size_t wolfSSL_get_client_random(const WOLFSSL* ssl, unsigned char* out,
                                                                   size_t outSz)
{
    size_t size;

    /* return max size of buffer */
    if (outSz == 0) {
        return RAN_LEN;
    }

    if (ssl == NULL || out == NULL) {
        return 0;
    }

    if (ssl->options.saveArrays == 0 || ssl->arrays == NULL) {
        WOLFSSL_MSG("Arrays struct not saved after handshake");
        return 0;
    }

    if (outSz > RAN_LEN) {
        size = RAN_LEN;
    }
    else {
        size = outSz;
    }

    XMEMCPY(out, ssl->arrays->clientRandom, size);
    return size;
}
#endif /* !defined(NO_WOLFSSL_CLIENT) */


    unsigned long wolfSSLeay(void)
    {
        return SSLEAY_VERSION_NUMBER;
    }


    const char* wolfSSLeay_version(int type)
    {
        static const char* version = "SSLeay wolfSSL compatibility";
        (void)type;
        return version;
    }


#ifndef NO_MD5
    void wolfSSL_MD5_Init(WOLFSSL_MD5_CTX* md5)
    {
        typedef char md5_test[sizeof(MD5_CTX) >= sizeof(Md5) ? 1 : -1];
        (void)sizeof(md5_test);

        WOLFSSL_ENTER("MD5_Init");
        wc_InitMd5((Md5*)md5);
    }


    void wolfSSL_MD5_Update(WOLFSSL_MD5_CTX* md5, const void* input,
                           unsigned long sz)
    {
        WOLFSSL_ENTER("wolfSSL_MD5_Update");
        wc_Md5Update((Md5*)md5, (const byte*)input, (word32)sz);
    }


    void wolfSSL_MD5_Final(byte* input, WOLFSSL_MD5_CTX* md5)
    {
        WOLFSSL_ENTER("MD5_Final");
        wc_Md5Final((Md5*)md5, input);
    }
#endif /* NO_MD5 */


#ifndef NO_SHA
    void wolfSSL_SHA_Init(WOLFSSL_SHA_CTX* sha)
    {
        typedef char sha_test[sizeof(SHA_CTX) >= sizeof(Sha) ? 1 : -1];
        (void)sizeof(sha_test);

        WOLFSSL_ENTER("SHA_Init");
        wc_InitSha((Sha*)sha);  /* OpenSSL compat, no ret */
    }


    void wolfSSL_SHA_Update(WOLFSSL_SHA_CTX* sha, const void* input,
                           unsigned long sz)
    {
        WOLFSSL_ENTER("SHA_Update");
        wc_ShaUpdate((Sha*)sha, (const byte*)input, (word32)sz);
    }


    void wolfSSL_SHA_Final(byte* input, WOLFSSL_SHA_CTX* sha)
    {
        WOLFSSL_ENTER("SHA_Final");
        wc_ShaFinal((Sha*)sha, input);
    }


    void wolfSSL_SHA1_Init(WOLFSSL_SHA_CTX* sha)
    {
        WOLFSSL_ENTER("SHA1_Init");
        SHA_Init(sha);
    }


    void wolfSSL_SHA1_Update(WOLFSSL_SHA_CTX* sha, const void* input,
                            unsigned long sz)
    {
        WOLFSSL_ENTER("SHA1_Update");
        SHA_Update(sha, input, sz);
    }


    void wolfSSL_SHA1_Final(byte* input, WOLFSSL_SHA_CTX* sha)
    {
        WOLFSSL_ENTER("SHA1_Final");
        SHA_Final(input, sha);
    }
#endif /* NO_SHA */

    #ifdef WOLFSSL_SHA224

    void wolfSSL_SHA224_Init(WOLFSSL_SHA224_CTX* sha)
    {
        typedef char sha_test[sizeof(SHA224_CTX) >= sizeof(Sha224) ? 1 : -1];
        (void)sizeof(sha_test);

        WOLFSSL_ENTER("SHA224_Init");
        XMEMSET(sha, 0, sizeof(WOLFSSL_SHA224_CTX));
        wc_InitSha224((Sha224*)sha);   /* OpenSSL compat, no error */
    }


    void wolfSSL_SHA224_Update(WOLFSSL_SHA224_CTX* sha, const void* input,
                           unsigned long sz)
    {
        WOLFSSL_ENTER("SHA224_Update");
        wc_Sha224Update((Sha224*)sha, (const byte*)input, (word32)sz);
        /* OpenSSL compat, no error */
    }


    void wolfSSL_SHA224_Final(byte* input, WOLFSSL_SHA224_CTX* sha)
    {
        WOLFSSL_ENTER("SHA224_Final");
        wc_Sha224Final((Sha224*)sha, input);
        /* OpenSSL compat, no error */
    }

    #endif /* WOLFSSL_SHA224 */


    void wolfSSL_SHA256_Init(WOLFSSL_SHA256_CTX* sha256)
    {
        typedef char sha_test[sizeof(SHA256_CTX) >= sizeof(Sha256) ? 1 : -1];
        (void)sizeof(sha_test);

        WOLFSSL_ENTER("SHA256_Init");
        wc_InitSha256((Sha256*)sha256);  /* OpenSSL compat, no error */
    }


    void wolfSSL_SHA256_Update(WOLFSSL_SHA256_CTX* sha, const void* input,
                              unsigned long sz)
    {
        WOLFSSL_ENTER("SHA256_Update");
        wc_Sha256Update((Sha256*)sha, (const byte*)input, (word32)sz);
        /* OpenSSL compat, no error */
    }


    void wolfSSL_SHA256_Final(byte* input, WOLFSSL_SHA256_CTX* sha)
    {
        WOLFSSL_ENTER("SHA256_Final");
        wc_Sha256Final((Sha256*)sha, input);
        /* OpenSSL compat, no error */
    }


    #ifdef WOLFSSL_SHA384

    void wolfSSL_SHA384_Init(WOLFSSL_SHA384_CTX* sha)
    {
        typedef char sha_test[sizeof(SHA384_CTX) >= sizeof(Sha384) ? 1 : -1];
        (void)sizeof(sha_test);

        WOLFSSL_ENTER("SHA384_Init");
        wc_InitSha384((Sha384*)sha);   /* OpenSSL compat, no error */
    }


    void wolfSSL_SHA384_Update(WOLFSSL_SHA384_CTX* sha, const void* input,
                           unsigned long sz)
    {
        WOLFSSL_ENTER("SHA384_Update");
        wc_Sha384Update((Sha384*)sha, (const byte*)input, (word32)sz);
        /* OpenSSL compat, no error */
    }


    void wolfSSL_SHA384_Final(byte* input, WOLFSSL_SHA384_CTX* sha)
    {
        WOLFSSL_ENTER("SHA384_Final");
        wc_Sha384Final((Sha384*)sha, input);
        /* OpenSSL compat, no error */
    }

    #endif /* WOLFSSL_SHA384 */


   #ifdef WOLFSSL_SHA512

    void wolfSSL_SHA512_Init(WOLFSSL_SHA512_CTX* sha)
    {
        typedef char sha_test[sizeof(SHA512_CTX) >= sizeof(Sha512) ? 1 : -1];
        (void)sizeof(sha_test);

        WOLFSSL_ENTER("SHA512_Init");
        wc_InitSha512((Sha512*)sha);  /* OpenSSL compat, no error */
    }


    void wolfSSL_SHA512_Update(WOLFSSL_SHA512_CTX* sha, const void* input,
                           unsigned long sz)
    {
        WOLFSSL_ENTER("SHA512_Update");
        wc_Sha512Update((Sha512*)sha, (const byte*)input, (word32)sz);
        /* OpenSSL compat, no error */
    }


    void wolfSSL_SHA512_Final(byte* input, WOLFSSL_SHA512_CTX* sha)
    {
        WOLFSSL_ENTER("SHA512_Final");
        wc_Sha512Final((Sha512*)sha, input);
        /* OpenSSL compat, no error */
    }

    #endif /* WOLFSSL_SHA512 */

    static struct s_ent{
        const unsigned char macType;
        const char *name;
    } md_tbl[] = {
    #ifndef NO_MD5
       {MD5, "MD5"},
    #endif /* NO_MD5 */

    #ifndef NO_SHA
       {SHA, "SHA"},
    #endif /* NO_SHA */

    #ifdef WOLFSSL_SHA224
       {SHA224, "SHA224"},
    #endif /* WOLFSSL_SHA224 */

       {SHA256, "SHA256"},

    #ifdef WOLFSSL_SHA384
       {SHA384, "SHA384"},
    #endif /* WOLFSSL_SHA384 */

    #ifdef WOLFSSL_SHA512
        {SHA512, "SHA512"},
    #endif /* WOLFSSL_SHA512 */

        {0, NULL}
    } ;

const WOLFSSL_EVP_MD *wolfSSL_EVP_get_digestbyname(const char *name)
{
    static const struct alias {
        const char *name;
        const char *alias;
    } alias_tbl[] =
    {
        {"MD5", "ssl3-md5"},
        {"SHA1", "ssl3-sha1"},
        { NULL, NULL}
    };

    const struct alias  *al ;
    const struct s_ent *ent ;

    for( al = alias_tbl; al->name != NULL; al++)
        if(XSTRNCMP(name, al->alias, XSTRLEN(al->alias)+1) == 0) {
            name = al->name;
            break;
        }

    for( ent = md_tbl; ent->name != NULL; ent++)
        if(XSTRNCMP(name, ent->name, XSTRLEN(ent->name)+1) == 0) {
            return (EVP_MD *)ent->name;
        }
    return NULL;
}

static WOLFSSL_EVP_MD *wolfSSL_EVP_get_md(const unsigned char type)
{
    const struct s_ent *ent ;
    WOLFSSL_ENTER("EVP_get_md");
    for( ent = md_tbl; ent->name != NULL; ent++){
        if(type == ent->macType) {
            return (WOLFSSL_EVP_MD *)ent->name;
        }
    }
    return (WOLFSSL_EVP_MD *)"";
}

int wolfSSL_EVP_MD_type(const WOLFSSL_EVP_MD *md)
{
    const struct s_ent *ent ;
    WOLFSSL_ENTER("EVP_MD_type");
    for( ent = md_tbl; ent->name != NULL; ent++){
        if(XSTRNCMP((const char *)md, ent->name, XSTRLEN(ent->name)+1) == 0) {
            return ent->macType;
        }
    }
    return 0;
}


    #ifndef NO_MD5

    const WOLFSSL_EVP_MD* wolfSSL_EVP_md5(void)
    {
        WOLFSSL_ENTER("EVP_md5");
        return EVP_get_digestbyname("MD5");
    }

    #endif /* NO_MD5 */


#ifndef NO_SHA
    const WOLFSSL_EVP_MD* wolfSSL_EVP_sha1(void)
    {
        WOLFSSL_ENTER("EVP_sha1");
        return EVP_get_digestbyname("SHA");
    }
#endif /* NO_SHA */

    #ifdef WOLFSSL_SHA224

    const WOLFSSL_EVP_MD* wolfSSL_EVP_sha224(void)
    {
        WOLFSSL_ENTER("EVP_sha224");
        return EVP_get_digestbyname("SHA224");
    }

    #endif /* WOLFSSL_SHA224 */


    const WOLFSSL_EVP_MD* wolfSSL_EVP_sha256(void)
    {
        WOLFSSL_ENTER("EVP_sha256");
        return EVP_get_digestbyname("SHA256");
    }

    #ifdef WOLFSSL_SHA384

    const WOLFSSL_EVP_MD* wolfSSL_EVP_sha384(void)
    {
        WOLFSSL_ENTER("EVP_sha384");
        return EVP_get_digestbyname("SHA384");
    }

    #endif /* WOLFSSL_SHA384 */

    #ifdef WOLFSSL_SHA512

    const WOLFSSL_EVP_MD* wolfSSL_EVP_sha512(void)
    {
        WOLFSSL_ENTER("EVP_sha512");
        return EVP_get_digestbyname("SHA512");
    }

    #endif /* WOLFSSL_SHA512 */

    WOLFSSL_EVP_MD_CTX *wolfSSL_EVP_MD_CTX_new(void)
    {
        WOLFSSL_EVP_MD_CTX* ctx;
        WOLFSSL_ENTER("EVP_MD_CTX_new");
        ctx = (WOLFSSL_EVP_MD_CTX*)XMALLOC(sizeof *ctx, NULL,
                                                       DYNAMIC_TYPE_TMP_BUFFER);
      	if (ctx){
            wolfSSL_EVP_MD_CTX_init(ctx);
        }
        return ctx;
    }

    WOLFSSL_API void wolfSSL_EVP_MD_CTX_free(WOLFSSL_EVP_MD_CTX *ctx)
    {
        if (ctx) {
            WOLFSSL_ENTER("EVP_MD_CTX_free");
    		    wolfSSL_EVP_MD_CTX_cleanup(ctx);
    		    XFREE(ctx, NULL, DYNAMIC_TYPE_TMP_BUFFER);
    		}
    }

    int wolfSSL_EVP_MD_CTX_type(const WOLFSSL_EVP_MD_CTX *ctx) {
      WOLFSSL_ENTER("EVP_MD_CTX_type");
      return ctx->macType;
    }

    int wolfSSL_EVP_MD_CTX_copy(WOLFSSL_EVP_MD_CTX *out, const WOLFSSL_EVP_MD_CTX *in)
    {
        return EVP_MD_CTX_copy_ex(out, in);
    }

    int wolfSSL_EVP_MD_CTX_copy_ex(WOLFSSL_EVP_MD_CTX *out, const WOLFSSL_EVP_MD_CTX *in)
    {
        if((out == NULL) || (in == NULL))return 0;
        WOLFSSL_ENTER("EVP_CIPHER_MD_CTX_copy_ex");
        XMEMCPY(out, in, sizeof(WOLFSSL_EVP_MD_CTX));
        return 1;
    }

    void wolfSSL_EVP_MD_CTX_init(WOLFSSL_EVP_MD_CTX* ctx)
    {
        WOLFSSL_ENTER("EVP_CIPHER_MD_CTX_init");
        (void)ctx;
        /* do nothing */
    }

    const WOLFSSL_EVP_MD *wolfSSL_EVP_MD_CTX_md(const WOLFSSL_EVP_MD_CTX *ctx)
    {
        if (ctx == NULL)
            return NULL;
        WOLFSSL_ENTER("EVP_MD_CTX_md");
        return (const WOLFSSL_EVP_MD *)wolfSSL_EVP_get_md(ctx->macType);
    }

    #ifndef NO_AES

    const WOLFSSL_EVP_CIPHER* wolfSSL_EVP_aes_128_cbc(void)
    {
        WOLFSSL_ENTER("wolfSSL_EVP_aes_128_cbc");
        return EVP_AES_128_CBC;
    }


    const WOLFSSL_EVP_CIPHER* wolfSSL_EVP_aes_192_cbc(void)
    {
        WOLFSSL_ENTER("wolfSSL_EVP_aes_192_cbc");
        return EVP_AES_192_CBC;
    }


    const WOLFSSL_EVP_CIPHER* wolfSSL_EVP_aes_256_cbc(void)
    {
        WOLFSSL_ENTER("wolfSSL_EVP_aes_256_cbc");
        return EVP_AES_256_CBC;
    }


    const WOLFSSL_EVP_CIPHER* wolfSSL_EVP_aes_128_ctr(void)
    {
        WOLFSSL_ENTER("wolfSSL_EVP_aes_128_ctr");
        return EVP_AES_128_CTR;
    }


    const WOLFSSL_EVP_CIPHER* wolfSSL_EVP_aes_192_ctr(void)
    {
        WOLFSSL_ENTER("wolfSSL_EVP_aes_192_ctr");
        return EVP_AES_192_CTR;
    }


    const WOLFSSL_EVP_CIPHER* wolfSSL_EVP_aes_256_ctr(void)
    {
        WOLFSSL_ENTER("wolfSSL_EVP_aes_256_ctr");
        return EVP_AES_256_CTR;
    }

    const WOLFSSL_EVP_CIPHER* wolfSSL_EVP_aes_128_ecb(void)
    {
        WOLFSSL_ENTER("wolfSSL_EVP_aes_128_ecb");
        return EVP_AES_128_ECB;
    }


    const WOLFSSL_EVP_CIPHER* wolfSSL_EVP_aes_192_ecb(void)
    {
        WOLFSSL_ENTER("wolfSSL_EVP_aes_192_ecb");
        return EVP_AES_192_ECB;
    }


    const WOLFSSL_EVP_CIPHER* wolfSSL_EVP_aes_256_ecb(void)
    {
        WOLFSSL_ENTER("wolfSSL_EVP_aes_256_ecb");
        return EVP_AES_256_ECB;
    }
    #endif /* NO_AES */

#ifndef NO_DES3
    const WOLFSSL_EVP_CIPHER* wolfSSL_EVP_des_cbc(void)
    {
        WOLFSSL_ENTER("wolfSSL_EVP_des_cbc");
        return EVP_DES_CBC;
    }
#ifdef WOLFSSL_DES_ECB
    const WOLFSSL_EVP_CIPHER* wolfSSL_EVP_des_ecb(void)
    {
        WOLFSSL_ENTER("wolfSSL_EVP_des_ecb");
        return EVP_DES_ECB;
    }
#endif
    const WOLFSSL_EVP_CIPHER* wolfSSL_EVP_des_ede3_cbc(void)
    {
        WOLFSSL_ENTER("wolfSSL_EVP_des_ede3_cbc");
        return EVP_DES_EDE3_CBC;
    }
#ifdef WOLFSSL_DES_ECB
    const WOLFSSL_EVP_CIPHER* wolfSSL_EVP_des_ede3_ecb(void)
    {
        WOLFSSL_ENTER("wolfSSL_EVP_des_ede3_ecb");
        return EVP_DES_EDE3_ECB;
    }
#endif
#endif /* NO_DES3 */

    const WOLFSSL_EVP_CIPHER* wolfSSL_EVP_rc4(void)
    {
        static const char* type = "ARC4";
        WOLFSSL_ENTER("wolfSSL_EVP_rc4");
        return type;
    }

#ifdef HAVE_IDEA
    const WOLFSSL_EVP_CIPHER* wolfSSL_EVP_idea_cbc(void)
    {
        WOLFSSL_ENTER("wolfSSL_EVP_idea_cbc");
        return EVP_IDEA_CBC;
    }
#endif
    const WOLFSSL_EVP_CIPHER* wolfSSL_EVP_enc_null(void)
    {
        static const char* type = "NULL";
        WOLFSSL_ENTER("wolfSSL_EVP_enc_null");
        return type;
    }


    int wolfSSL_EVP_MD_CTX_cleanup(WOLFSSL_EVP_MD_CTX* ctx)
    {
        WOLFSSL_ENTER("EVP_MD_CTX_cleanup");
        (void)ctx;
        return 0;
    }



    void wolfSSL_EVP_CIPHER_CTX_init(WOLFSSL_EVP_CIPHER_CTX* ctx)
    {
        WOLFSSL_ENTER("EVP_CIPHER_CTX_init");
        if (ctx) {
            ctx->cipherType = 0xff;   /* no init */
            ctx->keyLen     = 0;
            ctx->enc        = 1;      /* start in encrypt mode */
        }
    }


    /* SSL_SUCCESS on ok */
    int wolfSSL_EVP_CIPHER_CTX_cleanup(WOLFSSL_EVP_CIPHER_CTX* ctx)
    {
        WOLFSSL_ENTER("EVP_CIPHER_CTX_cleanup");
        if (ctx) {
            ctx->cipherType = 0xff;  /* no more init */
            ctx->keyLen     = 0;
        }

        return SSL_SUCCESS;
    }


    /* return SSL_SUCCESS on ok, 0 on failure to match API compatibility */
    int  wolfSSL_EVP_CipherInit(WOLFSSL_EVP_CIPHER_CTX* ctx,
                               const WOLFSSL_EVP_CIPHER* type, byte* key,
                               byte* iv, int enc)
    {
        int ret = -1;  /* failure local, during function 0 means success
                          because internal functions work that way */
        (void)key;
        (void)iv;
        (void)enc;

        WOLFSSL_ENTER("wolfSSL_EVP_CipherInit");
        if (ctx == NULL) {
            WOLFSSL_MSG("no ctx");
            return 0;   /* failure */
        }

        if (type == NULL && ctx->cipherType == 0xff) {
            WOLFSSL_MSG("no type set");
            return 0;   /* failure */
        }
        ctx->bufUsed = 0;
        ctx->lastUsed = 0;
        ctx->flags   = 0;
        ret = 0;S
#ifndef NO_AES
        if (ctx->cipherType == AES_128_CBC_TYPE ||
            (type && XSTRNCMP(type, EVP_AES_128_CBC, EVP_AES_SIZE) == 0)) {
            WOLFSSL_MSG(EVP_AES_128_CBC);
            ctx->cipherType = AES_128_CBC_TYPE;
            ctx->flags      = WOLFSSL_EVP_CIPH_CBC_MODE;
            ctx->keyLen     = 16;
            ctx->block_size = AES_BLOCK_SIZE;
            if (enc == 0 || enc == 1)
                ctx->enc = enc ? 1 : 0;
            if (key) {
                ret = wc_AesSetKey(&ctx->cipher.aes, key, ctx->keyLen, iv,
                                ctx->enc ? AES_ENCRYPTION : AES_DECRYPTION);
                if (ret != 0)
                    return ret;
            }
            if (iv && key == NULL) {
                ret = wc_AesSetIV(&ctx->cipher.aes, iv);
                if (ret != 0)
                    return ret;
            }
        }
        else if (ctx->cipherType == AES_192_CBC_TYPE ||
                 (type && XSTRNCMP(type, EVP_AES_192_CBC, EVP_AES_SIZE) == 0)) {
            WOLFSSL_MSG(EVP_AES_192_CBC);
            ctx->cipherType = AES_192_CBC_TYPE;
            ctx->flags      = WOLFSSL_EVP_CIPH_CBC_MODE;
            ctx->keyLen     = 24;
            ctx->block_size = AES_BLOCK_SIZE;
            if (enc == 0 || enc == 1)
                ctx->enc = enc ? 1 : 0;
            if (key) {
                ret = wc_AesSetKey(&ctx->cipher.aes, key, ctx->keyLen, iv,
                                ctx->enc ? AES_ENCRYPTION : AES_DECRYPTION);
                if (ret != 0)
                    return ret;
            }
            if (iv && key == NULL) {
                ret = wc_AesSetIV(&ctx->cipher.aes, iv);
                if (ret != 0)
                    return ret;
            }
        }
        else if (ctx->cipherType == AES_256_CBC_TYPE ||
                 (type && XSTRNCMP(type, EVP_AES_256_CBC, EVP_AES_SIZE) == 0)) {
            WOLFSSL_MSG(EVP_AES_256_CBC);
            ctx->cipherType = AES_256_CBC_TYPE;
            ctx->flags      = WOLFSSL_EVP_CIPH_CBC_MODE;
            ctx->keyLen     = 32;
            ctx->block_size = AES_BLOCK_SIZE;
            if (enc == 0 || enc == 1)
                ctx->enc = enc ? 1 : 0;
            if (key) {
                ret = wc_AesSetKey(&ctx->cipher.aes, key, ctx->keyLen, iv,
                                ctx->enc ? AES_ENCRYPTION : AES_DECRYPTION);
                if (ret != 0)
                    return ret;
            }
            if (iv && key == NULL) {
                ret = wc_AesSetIV(&ctx->cipher.aes, iv);
                if (ret != 0)
                    return ret;
            }
        }
#ifdef WOLFSSL_AES_COUNTER
        else if (ctx->cipherType == AES_128_CTR_TYPE ||
                 (type && XSTRNCMP(type, EVP_AES_128_CTR, EVP_AES_SIZE) == 0)) {
            WOLFSSL_MSG(EVP_AES_128_CTR);
            ctx->cipherType = AES_128_CTR_TYPE;
            ctx->flags      = WOLFSSL_EVP_CIPH_CTR_MODE;
            ctx->keyLen     = 16;
            ctx->block_size = AES_BLOCK_SIZE;
            if (enc == 0 || enc == 1)
                ctx->enc = enc ? 1 : 0;
            if (key) {
              ret = wc_AesSetKey(&ctx->cipher.aes, key, ctx->keyLen, iv,
                    AES_ENCRYPTION);
                if (ret != 0)
                    return ret;
            }
            if (iv && key == NULL) {
                ret = wc_AesSetIV(&ctx->cipher.aes, iv);
                if (ret != 0)
                    return ret;
            }
        }
        else if (ctx->cipherType == AES_192_CTR_TYPE ||
                 (type && XSTRNCMP(type, EVP_AES_192_CTR, EVP_AES_SIZE) == 0)) {
            WOLFSSL_MSG(EVP_AES_192_CTR);
            ctx->cipherType = AES_192_CTR_TYPE;
            ctx->flags      = WOLFSSL_EVP_CIPH_CTR_MODE;
            ctx->keyLen     = 24;
            ctx->block_size = AES_BLOCK_SIZE;
            if (enc == 0 || enc == 1)
                ctx->enc = enc ? 1 : 0;
            if (key) {
                ret = wc_AesSetKey(&ctx->cipher.aes, key, ctx->keyLen, iv,
                      AES_ENCRYPTION);
                if (ret != 0)
                    return ret;
            }
            if (iv && key == NULL) {
                ret = wc_AesSetIV(&ctx->cipher.aes, iv);
                if (ret != 0)
                    return ret;
            }
        }
        else if (ctx->cipherType == AES_256_CTR_TYPE ||
                 (type && XSTRNCMP(type, EVP_AES_256_CTR, EVP_AES_SIZE) == 0)) {
            WOLFSSL_MSG(EVP_AES_256_CTR);
            ctx->cipherType = AES_256_CTR_TYPE;
            ctx->flags      = WOLFSSL_EVP_CIPH_CTR_MODE;
            ctx->keyLen     = 32;
            ctx->block_size = AES_BLOCK_SIZE;
            if (enc == 0 || enc == 1)
                ctx->enc = enc ? 1 : 0;
            if (key) {
                ret = wc_AesSetKey(&ctx->cipher.aes, key, ctx->keyLen, iv,
                      AES_ENCRYPTION);
                if (ret != 0)
                    return ret;
            }
            if (iv && key == NULL) {
                ret = wc_AesSetIV(&ctx->cipher.aes, iv);
                if (ret != 0)
                    return ret;
            }
        }
#endif /* WOLFSSL_AES_CTR */
        else if (ctx->cipherType == AES_128_ECB_TYPE ||
            (type && XSTRNCMP(type, EVP_AES_128_ECB, EVP_AES_SIZE) == 0)) {
            WOLFSSL_MSG(EVP_AES_128_ECB);
            ctx->cipherType = AES_128_ECB_TYPE;
            ctx->flags      = WOLFSSL_EVP_CIPH_ECB_MODE;
            ctx->keyLen     = 16;
            ctx->block_size = AES_BLOCK_SIZE;
            if (enc == 0 || enc == 1)
                ctx->enc = enc ? 1 : 0;
            if (key) {
                ret = wc_AesSetKey(&ctx->cipher.aes, key, ctx->keyLen, NULL,
                      ctx->enc ? AES_ENCRYPTION : AES_DECRYPTION);
            }
            if (ret != 0)
                return ret;
        }
        else if (ctx->cipherType == AES_192_ECB_TYPE ||
                 (type && XSTRNCMP(type, EVP_AES_192_ECB, EVP_AES_SIZE) == 0)) {
            WOLFSSL_MSG(EVP_AES_192_ECB);
            ctx->cipherType = AES_192_ECB_TYPE;
            ctx->flags      = WOLFSSL_EVP_CIPH_ECB_MODE;
            ctx->keyLen     = 24;
            ctx->block_size = AES_BLOCK_SIZE;
            if (enc == 0 || enc == 1)
                ctx->enc = enc ? 1 : 0;
            if (key) {
                if(ctx->enc)
                ret = wc_AesSetKey(&ctx->cipher.aes, key, ctx->keyLen, NULL,
                      ctx->enc ? AES_ENCRYPTION : AES_DECRYPTION);
            }
            if (ret != 0)
                return ret;
        }
        else if (ctx->cipherType == AES_256_ECB_TYPE ||
                 (type && XSTRNCMP(type, EVP_AES_256_ECB, EVP_AES_SIZE) == 0)) {
            WOLFSSL_MSG(EVP_AES_256_ECB);
            ctx->cipherType = AES_256_ECB_TYPE;
            ctx->flags      = WOLFSSL_EVP_CIPH_ECB_MODE;
            ctx->keyLen     = 32;
            ctx->block_size = AES_BLOCK_SIZE;
            if (enc == 0 || enc == 1)
                ctx->enc = enc ? 1 : 0;
            if (key) {
              ret = wc_AesSetKey(&ctx->cipher.aes, key, ctx->keyLen, NULL,
                    ctx->enc ? AES_ENCRYPTION : AES_DECRYPTION);
            }
            if (ret != 0)
                return ret;
        }
#endif /* NO_AES */

#ifndef NO_DES3
        if (ctx->cipherType == DES_CBC_TYPE ||
                 (type && XSTRNCMP(type, EVP_DES_CBC, EVP_DES_SIZE) == 0)) {
            WOLFSSL_MSG(EVP_DES_CBC);
            ctx->cipherType = DES_CBC_TYPE;
            ctx->flags      = WOLFSSL_EVP_CIPH_CBC_MODE;
            ctx->keyLen     = 8;
            ctx->block_size = DES_BLOCK_SIZE;
            if (enc == 0 || enc == 1)
                ctx->enc = enc ? 1 : 0;
            if (key) {
                ret = wc_Des_SetKey(&ctx->cipher.des, key, iv,
                          ctx->enc ? DES_ENCRYPTION : DES_DECRYPTION);
                if (ret != 0)
                    return ret;
            }

            if (iv && key == NULL)
                wc_Des_SetIV(&ctx->cipher.des, iv);
        }
#ifdef WOLFSSL_DES_ECB
        else if (ctx->cipherType == DES_ECB_TYPE ||
                 (type && XSTRNCMP(type, EVP_DES_ECB, EVP_DES_SIZE) == 0)) {
            WOLFSSL_MSG(EVP_DES_ECB);
            ctx->cipherType = DES_ECB_TYPE;
            ctx->flags      = WOLFSSL_EVP_CIPH_ECB_MODE;
            ctx->keyLen     = 8;
            ctx->block_size = DES_BLOCK_SIZE;
            if (enc == 0 || enc == 1)
                ctx->enc = enc ? 1 : 0;
            if (key) {
                ret = wc_Des_SetKey(&ctx->cipher.des, key, NULL,
                          ctx->enc ? DES_ENCRYPTION : DES_DECRYPTION);
                if (ret != 0)
                    return ret;
            }
        }
#endif
        else if (ctx->cipherType == DES_EDE3_CBC_TYPE ||
                 (type &&
                  XSTRNCMP(type, EVP_DES_EDE3_CBC, EVP_DES_EDE3_SIZE) == 0)) {
            WOLFSSL_MSG(EVP_DES_EDE3_CBC);
            ctx->cipherType = DES_EDE3_CBC_TYPE;
            ctx->flags      = WOLFSSL_EVP_CIPH_CBC_MODE;
            ctx->keyLen     = 24;
            ctx->block_size = DES_BLOCK_SIZE;
            if (enc == 0 || enc == 1)
                ctx->enc = enc ? 1 : 0;
            if (key) {
                ret = wc_Des3_SetKey(&ctx->cipher.des3, key, iv,
                          ctx->enc ? DES_ENCRYPTION : DES_DECRYPTION);
                if (ret != 0)
                    return ret;
            }

            if (iv && key == NULL) {
                ret = wc_Des3_SetIV(&ctx->cipher.des3, iv);
                if (ret != 0)
                    return ret;
            }
        }
        else if (ctx->cipherType == DES_EDE3_ECB_TYPE ||
                 (type &&
                  XSTRNCMP(type, EVP_DES_EDE3_ECB, EVP_DES_EDE3_SIZE) == 0)) {
            WOLFSSL_MSG(EVP_DES_EDE3_ECB);
            ctx->cipherType = DES_EDE3_ECB_TYPE;
            ctx->flags      = WOLFSSL_EVP_CIPH_ECB_MODE;
            ctx->keyLen     = 24;
            ctx->block_size = DES_BLOCK_SIZE;
            if (enc == 0 || enc == 1)
                ctx->enc = enc ? 1 : 0;
            if (key) {
                ret = wc_Des3_SetKey(&ctx->cipher.des3, key, NULL,
                          ctx->enc ? DES_ENCRYPTION : DES_DECRYPTION);
                if (ret != 0)
                    return ret;
            }
        }
#endif /* NO_DES3 */
#ifndef NO_RC4
        if (ctx->cipherType == ARC4_TYPE || (type &&
                                     XSTRNCMP(type, "ARC4", 4) == 0)) {
            WOLFSSL_MSG("ARC4");
            ctx->cipherType = ARC4_TYPE;
            ctx->flags      = WOLFSSL_EVP_CIPH_STREAM_CIPHER;
            if (ctx->keyLen == 0)  /* user may have already set */
                ctx->keyLen = 16;  /* default to 128 */
            if (key)
                wc_Arc4SetKey(&ctx->cipher.arc4, key, ctx->keyLen);
            ret = 0;  /* success */
        }
#endif /* NO_RC4 */
#ifdef HAVE_IDEA
        if (ctx->cipherType == IDEA_CBC_TYPE ||
                 (type && XSTRNCMP(type, EVP_IDEA_CBC, EVP_IDEA_SIZE) == 0)) {
            WOLFSSL_MSG(EVP_IDEA_CBC);
            ctx->cipherType = IDEA_CBC_TYPE;
            ctx->flags      = WOLFSSL_EVP_CIPH_CBC_MODE;
            ctx->keyLen     = IDEA_KEY_SIZE;
            if (enc == 0 || enc == 1)
                ctx->enc = enc ? 1 : 0;
            if (key) {
                ret = wc_IdeaSetKey(&ctx->cipher.idea, key, (word16)ctx->keyLen,
                                    iv, ctx->enc ? IDEA_ENCRYPTION :
                                                   IDEA_DECRYPTION);
                if (ret != 0)
                    return ret;
            }

            if (iv && key == NULL)
                wc_IdeaSetIV(&ctx->cipher.idea, iv);
        }
#endif /* HAVE_IDEA */
        if (ctx->cipherType == NULL_CIPHER_TYPE || (type &&
                                     XSTRNCMP(type, "NULL", 4) == 0)) {
            WOLFSSL_MSG("NULL cipher");
            ctx->cipherType = NULL_CIPHER_TYPE;
            ctx->keyLen = 0;
            ret = 0;  /* success */
        }

        if (ret == 0)
            return SSL_SUCCESS;
        else
            return 0;  /* overall failure */
    }


    /* SSL_SUCCESS on ok */
    int wolfSSL_EVP_CIPHER_CTX_key_length(WOLFSSL_EVP_CIPHER_CTX* ctx)
    {
        WOLFSSL_ENTER("wolfSSL_EVP_CIPHER_CTX_key_length");
        if (ctx)
            return ctx->keyLen;

        return 0;   /* failure */
    }


    /* SSL_SUCCESS on ok */
    int wolfSSL_EVP_CIPHER_CTX_set_key_length(WOLFSSL_EVP_CIPHER_CTX* ctx,
                                             int keylen)
    {
        WOLFSSL_ENTER("wolfSSL_EVP_CIPHER_CTX_set_key_length");
        if (ctx)
            ctx->keyLen = keylen;
        else
            return 0;  /* failure */

        return SSL_SUCCESS;
    }


    /* SSL_SUCCESS on ok */
    int wolfSSL_EVP_Cipher(WOLFSSL_EVP_CIPHER_CTX* ctx, byte* dst, byte* src,
                          word32 len)
    {
        int ret = 0;
        WOLFSSL_ENTER("wolfSSL_EVP_Cipher");

        if (ctx == NULL || dst == NULL || src == NULL) {
            WOLFSSL_MSG("Bad function argument");
            return 0;  /* failure */
        }

        if (ctx->cipherType == 0xff) {
            WOLFSSL_MSG("no init");
            return 0;  /* failure */
        }

        switch (ctx->cipherType) {

#ifndef NO_AES
#ifdef HAVE_AES_CBC
            case AES_128_CBC_TYPE :
            case AES_192_CBC_TYPE :
            case AES_256_CBC_TYPE :
                WOLFSSL_MSG("AES CBC");
                if (ctx->enc)
                    ret = wc_AesCbcEncrypt(&ctx->cipher.aes, dst, src, len);
                else
                    ret = wc_AesCbcDecrypt(&ctx->cipher.aes, dst, src, len);
                break;
#endif /* HAVE_AES_CBC */
#ifdef HAVE_AES_ECB
            case AES_128_ECB_TYPE :
            case AES_192_ECB_TYPE :
            case AES_256_ECB_TYPE :
                WOLFSSL_MSG("AES ECB");
                if (ctx->enc)
                    ret = wc_AesEcbEncrypt(&ctx->cipher.aes, dst, src, len);
                else
                    ret = wc_AesEcbDecrypt(&ctx->cipher.aes, dst, src, len);
                break;
#endif
#ifdef WOLFSSL_AES_COUNTER
            case AES_128_CTR_TYPE :
            case AES_192_CTR_TYPE :
            case AES_256_CTR_TYPE :
                    WOLFSSL_MSG("AES CTR");
                    wc_AesCtrEncrypt(&ctx->cipher.aes, dst, src, len);
                break;
#endif /* WOLFSSL_AES_COUNTER */
#endif /* NO_AES */

#ifndef NO_DES3
            case DES_CBC_TYPE :
                if (ctx->enc)
                    wc_Des_CbcEncrypt(&ctx->cipher.des, dst, src, len);
                else
                    wc_Des_CbcDecrypt(&ctx->cipher.des, dst, src, len);
                break;
#ifdef WOLFSSL_DES_ECB
            case DES_ECB_TYPE :
                if (ctx->enc)
                    ret = wc_Des_EcbEncrypt(&ctx->cipher.des, dst, src, len);
            else
                    ret = wc_Des_EcbDecrypt(&ctx->cipher.des, dst, src, len);
            break;
#endif
            case DES_EDE3_CBC_TYPE :
                if (ctx->enc)
                    ret = wc_Des3_CbcEncrypt(&ctx->cipher.des3, dst, src, len);
                else
                    ret = wc_Des3_CbcDecrypt(&ctx->cipher.des3, dst, src, len);
                break;
#ifdef WOLFSSL_DES_ECB
                case DES_EDE3_ECB_TYPE :
                if (ctx->enc)
                    ret = wc_Des3_EcbEncrypt(&ctx->cipher.des3, dst, src, len);
                else
                    ret = wc_Des3_EcbDecrypt(&ctx->cipher.des3, dst, src, len);
                break;
#endif
#endif

#ifndef NO_RC4
            case ARC4_TYPE :
                wc_Arc4Process(&ctx->cipher.arc4, dst, src, len);
                break;
#endif

#ifdef HAVE_IDEA
            case IDEA_CBC_TYPE :
                if (ctx->enc)
                    wc_IdeaCbcEncrypt(&ctx->cipher.idea, dst, src, len);
                else
                    wc_IdeaCbcDecrypt(&ctx->cipher.idea, dst, src, len);
                break;
#endif
            case NULL_CIPHER_TYPE :
                XMEMCPY(dst, src, len);
                break;

            default: {
                WOLFSSL_MSG("bad type");
                return 0;  /* failure */
            }
        }

        if (ret != 0) {
            WOLFSSL_MSG("wolfSSL_EVP_Cipher failure");
            return 0;  /* failuer */
        }

        WOLFSSL_MSG("wolfSSL_EVP_Cipher success");
        return SSL_SUCCESS;  /* success */
    }

#include "wolfcrypt/src/evp.c"


    /* store for external read of iv, SSL_SUCCESS on success */
    int  wolfSSL_StoreExternalIV(WOLFSSL_EVP_CIPHER_CTX* ctx)
    {
        WOLFSSL_ENTER("wolfSSL_StoreExternalIV");

        if (ctx == NULL) {
            WOLFSSL_MSG("Bad function argument");
            return SSL_FATAL_ERROR;
        }

        switch (ctx->cipherType) {

#ifndef NO_AES
            case AES_128_CBC_TYPE :
            case AES_192_CBC_TYPE :
            case AES_256_CBC_TYPE :
                WOLFSSL_MSG("AES CBC");
                XMEMCPY(ctx->iv, &ctx->cipher.aes.reg, AES_BLOCK_SIZE);
                break;

#ifdef WOLFSSL_AES_COUNTER
            case AES_128_CTR_TYPE :
            case AES_192_CTR_TYPE :
            case AES_256_CTR_TYPE :
                WOLFSSL_MSG("AES CTR");
                XMEMCPY(ctx->iv, &ctx->cipher.aes.reg, AES_BLOCK_SIZE);
                break;
#endif /* WOLFSSL_AES_COUNTER */

#endif /* NO_AES */

#ifndef NO_DES3
            case DES_CBC_TYPE :
                WOLFSSL_MSG("DES CBC");
                XMEMCPY(ctx->iv, &ctx->cipher.des.reg, DES_BLOCK_SIZE);
                break;

            case DES_EDE3_CBC_TYPE :
                WOLFSSL_MSG("DES EDE3 CBC");
                XMEMCPY(ctx->iv, &ctx->cipher.des3.reg, DES_BLOCK_SIZE);
                break;
#endif

#ifdef HAVE_IDEA
            case IDEA_CBC_TYPE :
                WOLFSSL_MSG("IDEA CBC");
                XMEMCPY(ctx->iv, &ctx->cipher.idea.reg, IDEA_BLOCK_SIZE);
                break;
#endif
            case ARC4_TYPE :
                WOLFSSL_MSG("ARC4");
                break;

            case NULL_CIPHER_TYPE :
                WOLFSSL_MSG("NULL");
                break;

            default: {
                WOLFSSL_MSG("bad type");
                return SSL_FATAL_ERROR;
            }
        }
        return SSL_SUCCESS;
    }


    /* set internal IV from external, SSL_SUCCESS on success */
    int  wolfSSL_SetInternalIV(WOLFSSL_EVP_CIPHER_CTX* ctx)
    {

        WOLFSSL_ENTER("wolfSSL_SetInternalIV");

        if (ctx == NULL) {
            WOLFSSL_MSG("Bad function argument");
            return SSL_FATAL_ERROR;
        }

        switch (ctx->cipherType) {

#ifndef NO_AES
            case AES_128_CBC_TYPE :
            case AES_192_CBC_TYPE :
            case AES_256_CBC_TYPE :
                WOLFSSL_MSG("AES CBC");
                XMEMCPY(&ctx->cipher.aes.reg, ctx->iv, AES_BLOCK_SIZE);
                break;

#ifdef WOLFSSL_AES_COUNTER
            case AES_128_CTR_TYPE :
            case AES_192_CTR_TYPE :
            case AES_256_CTR_TYPE :
                WOLFSSL_MSG("AES CTR");
                XMEMCPY(&ctx->cipher.aes.reg, ctx->iv, AES_BLOCK_SIZE);
                break;
#endif

#endif /* NO_AES */

#ifndef NO_DES3
            case DES_CBC_TYPE :
                WOLFSSL_MSG("DES CBC");
                XMEMCPY(&ctx->cipher.des.reg, ctx->iv, DES_BLOCK_SIZE);
                break;

            case DES_EDE3_CBC_TYPE :
                WOLFSSL_MSG("DES EDE3 CBC");
                XMEMCPY(&ctx->cipher.des3.reg, ctx->iv, DES_BLOCK_SIZE);
                break;
#endif

#ifdef HAVE_IDEA
            case IDEA_CBC_TYPE :
                WOLFSSL_MSG("IDEA CBC");
                XMEMCPY(&ctx->cipher.idea.reg, ctx->iv, IDEA_BLOCK_SIZE);
                break;
#endif
            case ARC4_TYPE :
                WOLFSSL_MSG("ARC4");
                break;

            case NULL_CIPHER_TYPE :
                WOLFSSL_MSG("NULL");
                break;

            default: {
                WOLFSSL_MSG("bad type");
                return SSL_FATAL_ERROR;
            }
        }
        return SSL_SUCCESS;
    }


    /* SSL_SUCCESS on ok */
    int wolfSSL_EVP_DigestInit(WOLFSSL_EVP_MD_CTX* ctx,
                               const WOLFSSL_EVP_MD* type)
    {
        WOLFSSL_ENTER("EVP_DigestInit");

        if (ctx == NULL || type == NULL) {
            return BAD_FUNC_ARG;
        }

        if (XSTRNCMP(type, "SHA256", 6) == 0) {
             ctx->macType = SHA256;
             wolfSSL_SHA256_Init(&(ctx->hash.sha256));
        }
    #ifdef WOLFSSL_SHA224
        else if (XSTRNCMP(type, "SHA224", 6) == 0) {
             ctx->macType = SHA224;
             wolfSSL_SHA224_Init(&(ctx->hash.sha224));
        }
    #endif
    #ifdef WOLFSSL_SHA384
        else if (XSTRNCMP(type, "SHA384", 6) == 0) {
             ctx->macType = SHA384;
             wolfSSL_SHA384_Init(&(ctx->hash.sha384));
        }
    #endif
    #ifdef WOLFSSL_SHA512
        else if (XSTRNCMP(type, "SHA512", 6) == 0) {
             ctx->macType = SHA512;
             wolfSSL_SHA512_Init(&(ctx->hash.sha512));
        }
    #endif
    #ifndef NO_MD5
        else if (XSTRNCMP(type, "MD5", 3) == 0) {
            ctx->macType = MD5;
            wolfSSL_MD5_Init(&(ctx->hash.md5));
        }
    #endif
    #ifndef NO_SHA
        /* has to be last since would pick or 224, 256, 384, or 512 too */
        else if (XSTRNCMP(type, "SHA", 3) == 0) {
             ctx->macType = SHA;
             wolfSSL_SHA_Init(&(ctx->hash.sha));
        }
    #endif /* NO_SHA */
        else
             return BAD_FUNC_ARG;

        return SSL_SUCCESS;
    }


    /* SSL_SUCCESS on ok */
    int wolfSSL_EVP_DigestUpdate(WOLFSSL_EVP_MD_CTX* ctx, const void* data,
                                size_t sz)
    {
        WOLFSSL_ENTER("EVP_DigestUpdate");

        switch (ctx->macType) {
#ifndef NO_MD5
            case MD5:
                wolfSSL_MD5_Update((MD5_CTX*)&ctx->hash, data,
                                  (unsigned long)sz);
                break;
#endif
#ifndef NO_SHA
            case SHA:
                wolfSSL_SHA_Update((SHA_CTX*)&ctx->hash, data,
                                  (unsigned long)sz);
                break;
#endif
#ifdef WOLFSSL_SHA224
            case SHA224:
                wolfSSL_SHA224_Update((SHA224_CTX*)&ctx->hash, data,
                                     (unsigned long)sz);
                break;
#endif
#ifndef NO_SHA256
            case SHA256:
                wolfSSL_SHA256_Update((SHA256_CTX*)&ctx->hash, data,
                                     (unsigned long)sz);
                break;
#endif
#ifdef WOLFSSL_SHA384
            case SHA384:
                wolfSSL_SHA384_Update((SHA384_CTX*)&ctx->hash, data,
                                     (unsigned long)sz);
                break;
#endif
#ifdef WOLFSSL_SHA512
            case SHA512:
                wolfSSL_SHA512_Update((SHA512_CTX*)&ctx->hash, data,
                                     (unsigned long)sz);
                break;
#endif
            default:
                return BAD_FUNC_ARG;
        }

        return SSL_SUCCESS;
    }


    /* SSL_SUCCESS on ok */
    int wolfSSL_EVP_DigestFinal(WOLFSSL_EVP_MD_CTX* ctx, unsigned char* md,
                               unsigned int* s)
    {
        WOLFSSL_ENTER("EVP_DigestFinal");
        switch (ctx->macType) {
#ifndef NO_MD5
            case MD5:
                wolfSSL_MD5_Final(md, (MD5_CTX*)&ctx->hash);
                if (s) *s = MD5_DIGEST_SIZE;
                break;
#endif
#ifndef NO_SHA
            case SHA:
                wolfSSL_SHA_Final(md, (SHA_CTX*)&ctx->hash);
                if (s) *s = SHA_DIGEST_SIZE;
                break;
#endif
#ifdef WOLFSSL_SHA224
            case SHA224:
                wolfSSL_SHA224_Final(md, (SHA224_CTX*)&ctx->hash);
                if (s) *s = SHA224_DIGEST_SIZE;
                break;
#endif
#ifndef NO_SHA256
            case SHA256:
                wolfSSL_SHA256_Final(md, (SHA256_CTX*)&ctx->hash);
                if (s) *s = SHA256_DIGEST_SIZE;
                break;
#endif
#ifdef WOLFSSL_SHA384
            case SHA384:
                wolfSSL_SHA384_Final(md, (SHA384_CTX*)&ctx->hash);
                if (s) *s = SHA384_DIGEST_SIZE;
                break;
#endif
#ifdef WOLFSSL_SHA512
            case SHA512:
                wolfSSL_SHA512_Final(md, (SHA512_CTX*)&ctx->hash);
                if (s) *s = SHA512_DIGEST_SIZE;
                break;
#endif
            default:
                return BAD_FUNC_ARG;
        }

        return SSL_SUCCESS;
    }


    /* SSL_SUCCESS on ok */
    int wolfSSL_EVP_DigestFinal_ex(WOLFSSL_EVP_MD_CTX* ctx, unsigned char* md,
                                   unsigned int* s)
    {
        WOLFSSL_ENTER("EVP_DigestFinal_ex");
        return EVP_DigestFinal(ctx, md, s);
    }


    unsigned char* wolfSSL_HMAC(const WOLFSSL_EVP_MD* evp_md, const void* key,
                                int key_len, const unsigned char* d, int n,
                                unsigned char* md, unsigned int* md_len)
    {
        int type;
        unsigned char* ret = NULL;
#ifdef WOLFSSL_SMALL_STACK
        Hmac* hmac = NULL;
#else
        Hmac  hmac[1];
#endif

        WOLFSSL_ENTER("wolfSSL_HMAC");
        if (!md) {
            WOLFSSL_MSG("Static buffer not supported, pass in md buffer");
            return NULL;  /* no static buffer support */
        }

        if (XSTRNCMP(evp_md, "MD5", 3) == 0)
            type = MD5;
        else if (XSTRNCMP(evp_md, "SHA", 3) == 0)
            type = SHA;
        else
            return NULL;

    #ifdef WOLFSSL_SMALL_STACK
        hmac = (Hmac*)XMALLOC(sizeof(Hmac), NULL, DYNAMIC_TYPE_TMP_BUFFER);
        if (hmac == NULL)
            return NULL;
    #endif

        if (wc_HmacSetKey(hmac, type, (const byte*)key, key_len) == 0)
            if (wc_HmacUpdate(hmac, d, n) == 0)
                if (wc_HmacFinal(hmac, md) == 0) {
                    if (md_len)
                        *md_len = (type == MD5) ? (int)MD5_DIGEST_SIZE
                                                : (int)SHA_DIGEST_SIZE;
                    ret = md;
                }

    #ifdef WOLFSSL_SMALL_STACK
        XFREE(hmac, NULL, DYNAMIC_TYPE_TMP_BUFFER);
    #endif

        return ret;
    }

    void wolfSSL_ERR_clear_error(void)
    {
        /* TODO: */
    }


    int wolfSSL_RAND_status(void)
    {
        return SSL_SUCCESS;  /* wolfCrypt provides enough seed internally */
    }



    void wolfSSL_RAND_add(const void* add, int len, double entropy)
    {
        (void)add;
        (void)len;
        (void)entropy;

        /* wolfSSL seeds/adds internally, use explicit RNG if you want
           to take control */
    }


#ifndef NO_DES3
    /* 0 on ok */
    int wolfSSL_DES_key_sched(WOLFSSL_const_DES_cblock* key,
                              WOLFSSL_DES_key_schedule* schedule)
    {
        WOLFSSL_ENTER("DES_key_sched");

        if (key == NULL || schedule == NULL) {
            WOLFSSL_MSG("Null argument passed in");
        }
        else {
            XMEMCPY(schedule, key, sizeof(const_DES_cblock));
        }

        return 0;
    }


    /* intended to behave similar to Kerberos mit_des_cbc_cksum
     * return the last 4 bytes of cipher text */
    WOLFSSL_DES_LONG wolfSSL_DES_cbc_cksum(const unsigned char* in,
            WOLFSSL_DES_cblock* out, long length, WOLFSSL_DES_key_schedule* sc,
            WOLFSSL_const_DES_cblock* iv)
    {
        WOLFSSL_DES_LONG ret;
        unsigned char* tmp;
        unsigned char* data   = (unsigned char*)in;
        long           dataSz = length;
        byte dynamicFlag = 0; /* when padding the buffer created needs free'd */

        WOLFSSL_ENTER("wolfSSL_DES_cbc_cksum");

        if (in == NULL || out == NULL || sc == NULL || iv == NULL) {
            WOLFSSL_MSG("Bad argument passed in");
            return 0;
        }

        /* if input length is not a multiple of DES_BLOCK_SIZE pad with 0s */
        if (dataSz % DES_BLOCK_SIZE) {
            dataSz += DES_BLOCK_SIZE - (dataSz % DES_BLOCK_SIZE);
            data = (unsigned char*)XMALLOC(dataSz, NULL,
                                           DYNAMIC_TYPE_TMP_BUFFER);
            if (data == NULL) {
                WOLFSSL_MSG("Issue creating temporary buffer");
                return 0;
            }
            dynamicFlag = 1; /* set to free buffer at end */
            XMEMCPY(data, in, length);
            XMEMSET(data + length, 0, dataSz - length); /* padding */
        }

        tmp = (unsigned char*)XMALLOC(dataSz, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        if (tmp == NULL) {
            WOLFSSL_MSG("Issue creating temporary buffer");
            if (dynamicFlag == 1) {
                XFREE(data, NULL, DYNAMIC_TYPE_TMP_BUFFER);
            }
            return 0;
        }

        wolfSSL_DES_cbc_encrypt(data, tmp, dataSz, sc,
                (WOLFSSL_DES_cblock*)iv, 1);
        XMEMCPY((unsigned char*)out, tmp + (dataSz - DES_BLOCK_SIZE),
                DES_BLOCK_SIZE);

        ret = (((*((unsigned char*)out + 4) & 0xFF) << 24)|
               ((*((unsigned char*)out + 5) & 0xFF) << 16)|
               ((*((unsigned char*)out + 6) & 0xFF) << 8) |
               (*((unsigned char*)out + 7) & 0xFF));

        XFREE(tmp, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        if (dynamicFlag == 1) {
            XFREE(data, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        }

        return ret;
    }


    void wolfSSL_DES_cbc_encrypt(const unsigned char* input,
                                 unsigned char* output, long length,
                                 WOLFSSL_DES_key_schedule* schedule,
                                 WOLFSSL_DES_cblock* ivec, int enc)
    {
        Des myDes;

        WOLFSSL_ENTER("DES_cbc_encrypt");

        /* OpenSSL compat, no ret */
        wc_Des_SetKey(&myDes, (const byte*)schedule, (const byte*)ivec, !enc);

        if (enc)
            wc_Des_CbcEncrypt(&myDes, output, input, (word32)length);
        else
            wc_Des_CbcDecrypt(&myDes, output, input, (word32)length);
    }


    /* WOLFSSL_DES_key_schedule is a unsigned char array of size 8 */
    void wolfSSL_DES_ede3_cbc_encrypt(const unsigned char* input,
                                      unsigned char* output, long sz,
                                      WOLFSSL_DES_key_schedule* ks1,
                                      WOLFSSL_DES_key_schedule* ks2,
                                      WOLFSSL_DES_key_schedule* ks3,
                                      WOLFSSL_DES_cblock* ivec, int enc)
    {
        Des3 des;
        byte key[24];/* EDE uses 24 size key */

        WOLFSSL_ENTER("wolfSSL_DES_ede3_cbc_encrypt");

        XMEMSET(key, 0, sizeof(key));
        XMEMCPY(key, *ks1, DES_BLOCK_SIZE);
        XMEMCPY(&key[DES_BLOCK_SIZE], *ks2, DES_BLOCK_SIZE);
        XMEMCPY(&key[DES_BLOCK_SIZE * 2], *ks3, DES_BLOCK_SIZE);

        if (enc) {
            wc_Des3_SetKey(&des, key, (const byte*)ivec, DES_ENCRYPTION);
            wc_Des3_CbcEncrypt(&des, output, input, (word32)sz);
        }
        else {
            wc_Des3_SetKey(&des, key, (const byte*)ivec, DES_DECRYPTION);
            wc_Des3_CbcDecrypt(&des, output, input, (word32)sz);
        }
    }


    /* correctly sets ivec for next call */
    void wolfSSL_DES_ncbc_encrypt(const unsigned char* input,
                     unsigned char* output, long length,
                     WOLFSSL_DES_key_schedule* schedule, WOLFSSL_DES_cblock* ivec,
                     int enc)
    {
        Des myDes;

        WOLFSSL_ENTER("DES_ncbc_encrypt");

        /* OpenSSL compat, no ret */
        wc_Des_SetKey(&myDes, (const byte*)schedule, (const byte*)ivec, !enc);

        if (enc)
            wc_Des_CbcEncrypt(&myDes, output, input, (word32)length);
        else
            wc_Des_CbcDecrypt(&myDes, output, input, (word32)length);

        XMEMCPY(ivec, output + length - sizeof(DES_cblock), sizeof(DES_cblock));
    }

#endif /* NO_DES3 */


    void wolfSSL_ERR_free_strings(void)
    {
        /* handled internally */
    }


    void wolfSSL_ERR_remove_state(unsigned long state)
    {
        /* TODO: GetErrors().Remove(); */
        (void)state;
    }


    void wolfSSL_EVP_cleanup(void)
    {
        /* nothing to do here */
    }


    void wolfSSL_cleanup_all_ex_data(void)
    {
        /* nothing to do here */
    }


    int wolfSSL_clear(WOLFSSL* ssl)
    {
        (void)ssl;
        /* TODO: GetErrors().Remove(); */
        return SSL_SUCCESS;
    }


    long wolfSSL_SSL_SESSION_set_timeout(WOLFSSL_SESSION* ses, long t)
    {
        word32 tmptime;
        if (!ses || t < 0)
            return BAD_FUNC_ARG;

        tmptime = t & 0xFFFFFFFF;

        ses->timeout = tmptime;

        return SSL_SUCCESS;
    }


    long wolfSSL_CTX_set_mode(WOLFSSL_CTX* ctx, long mode)
    {
        /* SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER is wolfSSL default mode */

        WOLFSSL_ENTER("SSL_CTX_set_mode");
        if (mode == SSL_MODE_ENABLE_PARTIAL_WRITE)
            ctx->partialWrite = 1;

        return mode;
    }


    long wolfSSL_SSL_get_mode(WOLFSSL* ssl)
    {
        /* TODO: */
        (void)ssl;
        return 0;
    }


    long wolfSSL_CTX_get_mode(WOLFSSL_CTX* ctx)
    {
        /* TODO: */
        (void)ctx;
        return 0;
    }


    void wolfSSL_CTX_set_default_read_ahead(WOLFSSL_CTX* ctx, int m)
    {
        /* TODO: maybe? */
        (void)ctx;
        (void)m;
    }


    int wolfSSL_CTX_set_session_id_context(WOLFSSL_CTX* ctx,
                                           const unsigned char* sid_ctx,
                                           unsigned int sid_ctx_len)
    {
        /* No application specific context needed for wolfSSL */
        (void)ctx;
        (void)sid_ctx;
        (void)sid_ctx_len;
        return SSL_SUCCESS;
    }


    long wolfSSL_CTX_sess_get_cache_size(WOLFSSL_CTX* ctx)
    {
        (void)ctx;
        #ifndef NO_SESSION_CACHE
            return SESSIONS_PER_ROW * SESSION_ROWS;
        #else
            return 0;
        #endif
    }


    unsigned long wolfSSL_ERR_get_error_line(const char** file, int* line)
    {
    #ifdef DEBUG_WOLFSSL
        int ret = wc_PullErrorNode(file, NULL, line);
        if (ret < 0) {
            WOLFSSL_MSG("Issue getting error node");
            return 0;
        }
        return (unsigned long)ret;
    #else
        (void)file;
        (void)line;

        return 0;
    #endif
    }


#ifdef DEBUG_WOLFSSL
    static const char WOLFSSL_SYS_ACCEPT_T[]  = "accept";
    static const char WOLFSSL_SYS_BIND_T[]    = "bind";
    static const char WOLFSSL_SYS_CONNECT_T[] = "connect";
    static const char WOLFSSL_SYS_FOPEN_T[]   = "fopen";
    static const char WOLFSSL_SYS_FREAD_T[]   = "fread";
    static const char WOLFSSL_SYS_GETADDRINFO_T[] = "getaddrinfo";
    static const char WOLFSSL_SYS_GETSOCKOPT_T[]  = "getsockopt";
    static const char WOLFSSL_SYS_GETSOCKNAME_T[] = "getsockname";
    static const char WOLFSSL_SYS_GETHOSTBYNAME_T[] = "gethostbyname";
    static const char WOLFSSL_SYS_GETNAMEINFO_T[]   = "getnameinfo";
    static const char WOLFSSL_SYS_GETSERVBYNAME_T[] = "getservbyname";
    static const char WOLFSSL_SYS_IOCTLSOCKET_T[]   = "ioctlsocket";
    static const char WOLFSSL_SYS_LISTEN_T[]        = "listen";
    static const char WOLFSSL_SYS_OPENDIR_T[]       = "opendir";
    static const char WOLFSSL_SYS_SETSOCKOPT_T[]    = "setsockopt";
    static const char WOLFSSL_SYS_SOCKET_T[]        = "socket";

    /* switch with int mapped to function name for compatibility */
    static const char* wolfSSL_ERR_sys_func(int fun)
    {
        switch (fun) {
            case WOLFSSL_SYS_ACCEPT:      return WOLFSSL_SYS_ACCEPT_T;
            case WOLFSSL_SYS_BIND:        return WOLFSSL_SYS_BIND_T;
            case WOLFSSL_SYS_CONNECT:     return WOLFSSL_SYS_CONNECT_T;
            case WOLFSSL_SYS_FOPEN:       return WOLFSSL_SYS_FOPEN_T;
            case WOLFSSL_SYS_FREAD:       return WOLFSSL_SYS_FREAD_T;
            case WOLFSSL_SYS_GETADDRINFO: return WOLFSSL_SYS_GETADDRINFO_T;
            case WOLFSSL_SYS_GETSOCKOPT:  return WOLFSSL_SYS_GETSOCKOPT_T;
            case WOLFSSL_SYS_GETSOCKNAME: return WOLFSSL_SYS_GETSOCKNAME_T;
            case WOLFSSL_SYS_GETHOSTBYNAME: return WOLFSSL_SYS_GETHOSTBYNAME_T;
            case WOLFSSL_SYS_GETNAMEINFO: return WOLFSSL_SYS_GETNAMEINFO_T;
            case WOLFSSL_SYS_GETSERVBYNAME: return WOLFSSL_SYS_GETSERVBYNAME_T;
            case WOLFSSL_SYS_IOCTLSOCKET: return WOLFSSL_SYS_IOCTLSOCKET_T;
            case WOLFSSL_SYS_LISTEN:      return WOLFSSL_SYS_LISTEN_T;
            case WOLFSSL_SYS_OPENDIR:     return WOLFSSL_SYS_OPENDIR_T;
            case WOLFSSL_SYS_SETSOCKOPT:  return WOLFSSL_SYS_SETSOCKOPT_T;
            case WOLFSSL_SYS_SOCKET:      return WOLFSSL_SYS_SOCKET_T;
            default:
                return "NULL";
        }
    }
#endif /* DEBUG_WOLFSSL */


    /* @TODO when having an error queue this needs to push to the queue */
    void wolfSSL_ERR_put_error(int lib, int fun, int err, const char* file,
            int line)
    {
        WOLFSSL_ENTER("wolfSSL_ERR_put_error");

        #ifndef DEBUG_WOLFSSL
        (void)fun;
        (void)err;
        (void)file;
        (void)line;
        WOLFSSL_MSG("Not compiled in debug mode");
        #else
        WOLFSSL_ERROR_LINE(err, wolfSSL_ERR_sys_func(fun), (unsigned int)line,
            file, NULL);
        #endif
        (void)lib;
    }


    unsigned long wolfSSL_ERR_get_error_line_data(const char** file, int* line,
                                                  const char** data, int *flags)
    {
        /* Not implemented */
        (void)file;
        (void)line;
        (void)data;
        (void)flags;
        WOLFSSL_STUB("wolfSSL_ERR_get_error_line_data");

        return 0;
    }

    WOLFSSL_API pem_password_cb* wolfSSL_CTX_get_default_passwd_cb(
                                                               WOLFSSL_CTX *ctx)
    {
        if (ctx == NULL || ctx->passwd_cb == NULL) {
            return NULL;
        }

        return ctx->passwd_cb;
    }


    WOLFSSL_API void *wolfSSL_CTX_get_default_passwd_cb_userdata(
                                                               WOLFSSL_CTX *ctx)
    {
        if (ctx == NULL) {
            return NULL;
        }

        return ctx->userdata;
    }

#endif /* OPENSSL_EXTRA */


#if defined(KEEP_PEER_CERT)

    WOLFSSL_X509* wolfSSL_get_peer_certificate(WOLFSSL* ssl)
    {
        WOLFSSL_ENTER("SSL_get_peer_certificate");
        if (ssl->peerCert.issuer.sz)
            return &ssl->peerCert;
        else
            return 0;
    }

#endif /* KEEP_PEER_CERT */


#ifndef NO_CERTS
#if defined(KEEP_PEER_CERT) || defined(SESSION_CERTS) || defined(OPENSSL_EXTRA)

/* user externally called free X509, if dynamic go ahead with free, otherwise
 * don't */
static void ExternalFreeX509(WOLFSSL_X509* x509)
{
    WOLFSSL_ENTER("ExternalFreeX509");
    if (x509) {
        if (x509->dynamicMemory) {
            FreeX509(x509);
            XFREE(x509, x509->heap, DYNAMIC_TYPE_X509);
        } else {
            WOLFSSL_MSG("free called on non dynamic object, not freeing");
        }
    }
}

#endif /* KEEP_PEER_CERT || SESSION_CERTS || OPENSSSL_EXTRA */

#if defined(KEEP_PEER_CERT) || defined(SESSION_CERTS)

    void wolfSSL_FreeX509(WOLFSSL_X509* x509)
    {
        WOLFSSL_ENTER("wolfSSL_FreeX509");
        ExternalFreeX509(x509);
    }

    /* return the next, if any, altname from the peer cert */
    char* wolfSSL_X509_get_next_altname(WOLFSSL_X509* cert)
    {
        char* ret = NULL;
        WOLFSSL_ENTER("wolfSSL_X509_get_next_altname");

        /* don't have any to work with */
        if (cert == NULL || cert->altNames == NULL)
            return NULL;

        /* already went through them */
        if (cert->altNamesNext == NULL)
            return NULL;

        ret = cert->altNamesNext->name;
        cert->altNamesNext = cert->altNamesNext->next;

        return ret;
    }


    WOLFSSL_X509_NAME* wolfSSL_X509_get_issuer_name(WOLFSSL_X509* cert)
    {
        WOLFSSL_ENTER("X509_get_issuer_name");
        if(cert)
            return &cert->issuer;
        return NULL;
    }


    WOLFSSL_X509_NAME* wolfSSL_X509_get_subject_name(WOLFSSL_X509* cert)
    {
        WOLFSSL_ENTER("wolfSSL_X509_get_subject_name");
        if(cert)
            return &cert->subject;
        return NULL;
    }


    int wolfSSL_X509_get_isCA(WOLFSSL_X509* x509)
    {
        int isCA = 0;

        WOLFSSL_ENTER("wolfSSL_X509_get_isCA");

        if (x509 != NULL)
            isCA = x509->isCa;

        WOLFSSL_LEAVE("wolfSSL_X509_get_isCA", isCA);

        return isCA;
    }


#ifdef OPENSSL_EXTRA
    int wolfSSL_X509_ext_isSet_by_NID(WOLFSSL_X509* x509, int nid)
    {
        int isSet = 0;

        WOLFSSL_ENTER("wolfSSL_X509_ext_isSet_by_NID");

        if (x509 != NULL) {
            switch (nid) {
                case BASIC_CA_OID: isSet = x509->basicConstSet; break;
                case ALT_NAMES_OID: isSet = x509->subjAltNameSet; break;
                case AUTH_KEY_OID: isSet = x509->authKeyIdSet; break;
                case SUBJ_KEY_OID: isSet = x509->subjKeyIdSet; break;
                case KEY_USAGE_OID: isSet = x509->keyUsageSet; break;
                #ifdef WOLFSSL_SEP
                    case CERT_POLICY_OID: isSet = x509->certPolicySet; break;
                #endif /* WOLFSSL_SEP */
            }
        }

        WOLFSSL_LEAVE("wolfSSL_X509_ext_isSet_by_NID", isSet);

        return isSet;
    }


    int wolfSSL_X509_ext_get_critical_by_NID(WOLFSSL_X509* x509, int nid)
    {
        int crit = 0;

        WOLFSSL_ENTER("wolfSSL_X509_ext_get_critical_by_NID");

        if (x509 != NULL) {
            switch (nid) {
                case BASIC_CA_OID: crit = x509->basicConstCrit; break;
                case ALT_NAMES_OID: crit = x509->subjAltNameCrit; break;
                case AUTH_KEY_OID: crit = x509->authKeyIdCrit; break;
                case SUBJ_KEY_OID: crit = x509->subjKeyIdCrit; break;
                case KEY_USAGE_OID: crit = x509->keyUsageCrit; break;
                #ifdef WOLFSSL_SEP
                    case CERT_POLICY_OID: crit = x509->certPolicyCrit; break;
                #endif /* WOLFSSL_SEP */
            }
        }

        WOLFSSL_LEAVE("wolfSSL_X509_ext_get_critical_by_NID", crit);

        return crit;
    }


    int wolfSSL_X509_get_isSet_pathLength(WOLFSSL_X509* x509)
    {
        int isSet = 0;

        WOLFSSL_ENTER("wolfSSL_X509_get_isSet_pathLength");

        if (x509 != NULL)
            isSet = x509->basicConstPlSet;

        WOLFSSL_LEAVE("wolfSSL_X509_get_isSet_pathLength", isSet);

        return isSet;
    }


    word32 wolfSSL_X509_get_pathLength(WOLFSSL_X509* x509)
    {
        word32 pathLength = 0;

        WOLFSSL_ENTER("wolfSSL_X509_get_pathLength");

        if (x509 != NULL)
            pathLength = x509->pathLength;

        WOLFSSL_LEAVE("wolfSSL_X509_get_pathLength", pathLength);

        return pathLength;
    }


    unsigned int wolfSSL_X509_get_keyUsage(WOLFSSL_X509* x509)
    {
        word16 usage = 0;

        WOLFSSL_ENTER("wolfSSL_X509_get_keyUsage");

        if (x509 != NULL)
            usage = x509->keyUsage;

        WOLFSSL_LEAVE("wolfSSL_X509_get_keyUsage", usage);

        return usage;
    }


    byte* wolfSSL_X509_get_authorityKeyID(WOLFSSL_X509* x509,
                                          byte* dst, int* dstLen)
    {
        byte *id = NULL;
        int copySz = 0;

        WOLFSSL_ENTER("wolfSSL_X509_get_authorityKeyID");

        if (x509 != NULL) {
            if (x509->authKeyIdSet) {
                copySz = min(dstLen != NULL ? *dstLen : 0,
                             (int)x509->authKeyIdSz);
                id = x509->authKeyId;
            }

            if (dst != NULL && dstLen != NULL && id != NULL && copySz > 0) {
                XMEMCPY(dst, id, copySz);
                id = dst;
                *dstLen = copySz;
            }
        }

        WOLFSSL_LEAVE("wolfSSL_X509_get_authorityKeyID", copySz);

        return id;
    }


    byte* wolfSSL_X509_get_subjectKeyID(WOLFSSL_X509* x509,
                                        byte* dst, int* dstLen)
    {
        byte *id = NULL;
        int copySz = 0;

        WOLFSSL_ENTER("wolfSSL_X509_get_subjectKeyID");

        if (x509 != NULL) {
            if (x509->subjKeyIdSet) {
                copySz = min(dstLen != NULL ? *dstLen : 0,
                                                        (int)x509->subjKeyIdSz);
                id = x509->subjKeyId;
            }

            if (dst != NULL && dstLen != NULL && id != NULL && copySz > 0) {
                XMEMCPY(dst, id, copySz);
                id = dst;
                *dstLen = copySz;
            }
        }

        WOLFSSL_LEAVE("wolfSSL_X509_get_subjectKeyID", copySz);

        return id;
    }


    int wolfSSL_X509_NAME_entry_count(WOLFSSL_X509_NAME* name)
    {
        int count = 0;

        WOLFSSL_ENTER("wolfSSL_X509_NAME_entry_count");

        if (name != NULL)
            count = name->fullName.entryCount;

        WOLFSSL_LEAVE("wolfSSL_X509_NAME_entry_count", count);
        return count;
    }


    int wolfSSL_X509_NAME_get_text_by_NID(WOLFSSL_X509_NAME* name,
                                          int nid, char* buf, int len)
    {
        char *text = NULL;
        int textSz = 0;

        WOLFSSL_ENTER("wolfSSL_X509_NAME_get_text_by_NID");

        switch (nid) {
            case ASN_COMMON_NAME:
                text = name->fullName.fullName + name->fullName.cnIdx;
                textSz = name->fullName.cnLen;
                break;
            case ASN_SUR_NAME:
                text = name->fullName.fullName + name->fullName.snIdx;
                textSz = name->fullName.snLen;
                break;
            case ASN_SERIAL_NUMBER:
                text = name->fullName.fullName + name->fullName.serialIdx;
                textSz = name->fullName.serialLen;
                break;
            case ASN_COUNTRY_NAME:
                text = name->fullName.fullName + name->fullName.cIdx;
                textSz = name->fullName.cLen;
                break;
            case ASN_LOCALITY_NAME:
                text = name->fullName.fullName + name->fullName.lIdx;
                textSz = name->fullName.lLen;
                break;
            case ASN_STATE_NAME:
                text = name->fullName.fullName + name->fullName.stIdx;
                textSz = name->fullName.stLen;
                break;
            case ASN_ORG_NAME:
                text = name->fullName.fullName + name->fullName.oIdx;
                textSz = name->fullName.oLen;
                break;
            case ASN_ORGUNIT_NAME:
                text = name->fullName.fullName + name->fullName.ouIdx;
                textSz = name->fullName.ouLen;
                break;
            case ASN_DOMAIN_COMPONENT:
                text = name->fullName.fullName + name->fullName.dcIdx;
                textSz = name->fullName.dcLen;
            break;
            default:
                break;
        }

        if (buf != NULL && text != NULL) {
            textSz = min(textSz, len);
            XMEMCPY(buf, text, textSz);
            buf[textSz] = '\0';
        }

        WOLFSSL_LEAVE("wolfSSL_X509_NAME_get_text_by_NID", textSz);
        return textSz;
    }

    int wolfSSL_X509_NAME_get_index_by_NID(WOLFSSL_X509_NAME* name,
                                          int nid, int pos)
    {
        int ret    = -1;

        WOLFSSL_ENTER("wolfSSL_X509_NAME_get_index_by_NID");

        if (name == NULL) {
            return BAD_FUNC_ARG;
        }

        /* these index values are already stored in DecodedName
           use those when available */
        if (name->fullName.fullName && name->fullName.fullNameLen > 0) {
            switch (nid) {
                case ASN_COMMON_NAME:
                    ret = name->fullName.cnIdx;
                    break;
                default:
                    WOLFSSL_MSG("NID not yet implemented");
                    break;
            }
        }

        WOLFSSL_LEAVE("wolfSSL_X509_NAME_get_index_by_NID", ret);

        (void)pos;
        (void)nid;

        return ret;
    }


    WOLFSSL_ASN1_STRING*  wolfSSL_X509_NAME_ENTRY_get_data(
                                                    WOLFSSL_X509_NAME_ENTRY* in)
    {
        WOLFSSL_ENTER("wolfSSL_X509_NAME_ENTRY_get_data");
        return in->value;
    }


    WOLFSSL_ASN1_STRING* wolfSSL_ASN1_STRING_new()
    {
        WOLFSSL_ASN1_STRING* asn1;

        WOLFSSL_ENTER("wolfSSL_ASN1_STRING_new");

        asn1 = (WOLFSSL_ASN1_STRING*)XMALLOC(sizeof(WOLFSSL_ASN1_STRING), NULL,
                DYNAMIC_TYPE_OPENSSL);
        if (asn1 != NULL) {
            XMEMSET(asn1, 0, sizeof(WOLFSSL_ASN1_STRING));
        }

        return asn1; /* no check for null because error case is returning null*/
    }


    void wolfSSL_ASN1_STRING_free(WOLFSSL_ASN1_STRING* asn1)
    {
        WOLFSSL_ENTER("wolfSSL_ASN1_STRING_free");

        if (asn1 != NULL) {
            if (asn1->length > 0 && asn1->data != NULL) {
                XFREE(asn1->data, NULL, DYNAMIC_TYPE_OPENSSL);
            }
            XFREE(asn1, NULL, DYNAMIC_TYPE_OPENSSL);
        }
    }


    WOLFSSL_ASN1_STRING* wolfSSL_ASN1_STRING_type_new(int type)
    {
        WOLFSSL_ASN1_STRING* asn1;

        WOLFSSL_ENTER("wolfSSL_ASN1_STRING_type_new");

        asn1 = wolfSSL_ASN1_STRING_new();
        if (asn1 == NULL) {
            return NULL;
        }
        asn1->type = type;

        return asn1;
    }


    /* if dataSz is negative then use XSTRLEN to find length of data
     * return SSL_SUCCESS on success and SSL_FAILURE on failure */
    int wolfSSL_ASN1_STRING_set(WOLFSSL_ASN1_STRING* asn1, const void* data,
            int dataSz)
    {
        int sz;

        WOLFSSL_ENTER("wolfSSL_ASN1_STRING_set");

        if (data == NULL || asn1 == NULL) {
            return SSL_FAILURE;
        }

        if (dataSz < 0) {
            sz = (int)XSTRLEN((const char*)data);
        }
        else {
            sz = dataSz;
        }

        if (sz < 0) {
            return SSL_FAILURE;
        }

        /* free any existing data before copying */
        if (asn1->data != NULL) {
            XFREE(asn1->data, NULL, DYNAMIC_TYPE_OPENSSL);
        }

        /* create new data buffer and copy over */
        asn1->data = (char*)XMALLOC(sz, NULL, DYNAMIC_TYPE_OPENSSL);
        if (asn1->data == NULL) {
            return SSL_FAILURE;
        }
        XMEMCPY(asn1->data, data, sz);
        asn1->length = sz;

        return SSL_SUCCESS;
    }


    char* wolfSSL_ASN1_STRING_data(WOLFSSL_ASN1_STRING* asn)
    {
        WOLFSSL_ENTER("wolfSSL_ASN1_STRING_data");

        if (asn) {
            return asn->data;
        }
        else {
            return NULL;
        }
    }


    int wolfSSL_ASN1_STRING_length(WOLFSSL_ASN1_STRING* asn)
    {
        WOLFSSL_ENTER("wolfSSL_ASN1_STRING_length");

        if (asn) {
            return asn->length;
        }
        else {
            return 0;
        }
    }


#ifdef XSNPRINTF /* a snprintf function needs to be available */
    /* Writes the human readable form of x509 to bio.
     *
     * bio  WOLFSSL_BIO to write to.
     * x509 Certificate to write.
     */
    int wolfSSL_X509_print(WOLFSSL_BIO* bio, WOLFSSL_X509* x509)
    {
        WOLFSSL_ENTER("wolfSSL_X509_print");

        if (bio == NULL || x509 == NULL) {
            return SSL_FAILURE;
        }

        if (wolfSSL_BIO_write(bio, "Certificate:\n", sizeof("Certificate:\n"))
            <= 0) {
                return SSL_FAILURE;
        }

        if (wolfSSL_BIO_write(bio, "    Data:\n", sizeof("    Data:\n"))
            <= 0) {
                return SSL_FAILURE;
        }

        /* print version of cert */
        {
            int version;
            char tmp[17];

            if ((version = wolfSSL_X509_version(x509)) <= 0) {
                WOLFSSL_MSG("Error getting X509 version");
                return SSL_FAILURE;
            }
            if (wolfSSL_BIO_write(bio, "        Version: ",
                                sizeof("        Version: ")) <= 0) {
                return SSL_FAILURE;
            }
	        XSNPRINTF(tmp, sizeof(tmp), "%d\n", version);
            if (wolfSSL_BIO_write(bio, tmp, (int)XSTRLEN(tmp)) <= 0) {
                return SSL_FAILURE;
            }
        }

        /* print serial number out */
        {
            unsigned char serial[32];
            int  sz = sizeof(serial);

            XMEMSET(serial, 0, sz);
            if (wolfSSL_X509_get_serial_number(x509, serial, &sz)
                    != SSL_SUCCESS) {
                WOLFSSL_MSG("Error getting x509 serial number");
                return SSL_FAILURE;
            }
            if (wolfSSL_BIO_write(bio, "        Serial Number: ",
                                sizeof("        Serial Number: ")) <= 0) {
                return SSL_FAILURE;
            }

            /* if serial can fit into byte than print on the same line */
            if (sz <= (int)sizeof(byte)) {
                char tmp[17];
                XSNPRINTF(tmp, sizeof(tmp), "%d (0x%x)\n", serial[0],serial[0]);
                if (wolfSSL_BIO_write(bio, tmp, (int)XSTRLEN(tmp)) <= 0) {
                    return SSL_FAILURE;
                }
            }
            else {
                int i;
                char tmp[100];
                int  tmpSz = 100;
                char val[5];
                int  valSz = 5;

                /* serial is larger than int size so print off hex values */
                if (wolfSSL_BIO_write(bio, "\n            ",
                                sizeof("\n            ")) <= 0) {
                    return SSL_FAILURE;
                }
                tmp[0] = '\0';
                for (i = 0; i < sz - 1 && (3 * i) < tmpSz - valSz; i++) {
                    XSNPRINTF(val, sizeof(val) - 1, "%02x:", serial[i]);
                    val[3] = '\0'; /* make sure is null terminated */
                    XSTRNCAT(tmp, val, valSz);
                }
                XSNPRINTF(val, sizeof(val) - 1, "%02x\n", serial[i]);
                val[3] = '\0'; /* make sure is null terminated */
                XSTRNCAT(tmp, val, valSz);
                if (wolfSSL_BIO_write(bio, tmp, (int)XSTRLEN(tmp)) <= 0) {
                    return SSL_FAILURE;
                }
            }
        }

        /* print signature algo */
        {
            int   oid;
            char* sig;

            if ((oid = wolfSSL_X509_get_signature_type(x509)) <= 0) {
                WOLFSSL_MSG("Error getting x509 signature type");
                return SSL_FAILURE;
            }
            if (wolfSSL_BIO_write(bio, "    Signature Algorithm: ",
                                sizeof("    Signature Algorithm: ")) <= 0) {
                return SSL_FAILURE;
            }
            sig = GetSigName(oid);
            if (wolfSSL_BIO_write(bio, sig, (int)XSTRLEN(sig)) <= 0) {
                return SSL_FAILURE;
            }
            if (wolfSSL_BIO_write(bio, "\n", sizeof("\n")) <= 0) {
                return SSL_FAILURE;
            }
        }

        /* print issuer */
        {
            char* issuer;
        #ifndef WOLFSSL_SMALL_STACK
            char* buff  = NULL;
            int   issSz = 0;
        #else
            char buff[256];
            int  issSz = 256;
        #endif

            issuer  = wolfSSL_X509_NAME_oneline(
                             wolfSSL_X509_get_issuer_name(x509), buff, issSz);

            if (wolfSSL_BIO_write(bio, "        Issuer: ",
                                sizeof("        Issuer: ")) <= 0) {
                #ifdef WOLFSSL_SMALL_STACK
                XFREE(issuer, NULL, DYNAMIC_TYPE_OPENSSL);
                #endif
                return SSL_FAILURE;
            }
            if (issuer != NULL) {
                if (wolfSSL_BIO_write(bio, issuer, (int)XSTRLEN(issuer)) <= 0) {
                    #ifdef WOLFSSL_SMALL_STACK
                    XFREE(issuer, NULL, DYNAMIC_TYPE_OPENSSL);
                    #endif
                    return SSL_FAILURE;
                }
            }
            #ifdef WOLFSSL_SMALL_STACK
            XFREE(issuer, NULL, DYNAMIC_TYPE_OPENSSL);
            #endif
            if (wolfSSL_BIO_write(bio, "\n", sizeof("\n")) <= 0) {
                return SSL_FAILURE;
            }
        }

        /* print validity */
        {
            char tmp[80];

            if (wolfSSL_BIO_write(bio, "        Validity\n",
                                sizeof("        Validity\n")) <= 0) {
                return SSL_FAILURE;
            }
            if (wolfSSL_BIO_write(bio, "            Not Before: ",
                                sizeof("            Not Before: ")) <= 0) {
                return SSL_FAILURE;
            }
            if (GetTimeString(x509->notBefore + 2, ASN_UTC_TIME,
                tmp, sizeof(tmp)) != SSL_SUCCESS) {
                WOLFSSL_MSG("Error getting not before date");
                return SSL_FAILURE;
            }
            tmp[sizeof(tmp) - 1] = '\0'; /* make sure null terminated */
            if (wolfSSL_BIO_write(bio, tmp, (int)XSTRLEN(tmp)) <= 0) {
                return SSL_FAILURE;
            }
            if (wolfSSL_BIO_write(bio, "\n            Not After : ",
                                sizeof("\n            Not After : ")) <= 0) {
                return SSL_FAILURE;
            }
            if (GetTimeString(x509->notAfter + 2,ASN_UTC_TIME,
                tmp, sizeof(tmp)) != SSL_SUCCESS) {
                WOLFSSL_MSG("Error getting not before date");
                return SSL_FAILURE;
            }
            tmp[sizeof(tmp) - 1] = '\0'; /* make sure null terminated */
            if (wolfSSL_BIO_write(bio, tmp, (int)XSTRLEN(tmp)) <= 0) {
                return SSL_FAILURE;
            }
        }

        /* print subject */
        {
            char* subject;
        #ifdef WOLFSSL_SMALL_STACK
            char* buff  = NULL;
            int   subSz = 0;
        #else
            char buff[256];
            int  subSz = 256;
        #endif

            subject  = wolfSSL_X509_NAME_oneline(
                             wolfSSL_X509_get_subject_name(x509), buff, subSz);

            if (wolfSSL_BIO_write(bio, "\n        Subject: ",
                                sizeof("\n        Subject: ")) <= 0) {
                #ifdef WOLFSSL_SMALL_STACK
                XFREE(subject, NULL, DYNAMIC_TYPE_OPENSSL);
                #endif
                return SSL_FAILURE;
            }
            if (subject != NULL) {
                if (wolfSSL_BIO_write(bio, subject, (int)XSTRLEN(subject)) <= 0) {
                    #ifdef WOLFSSL_SMALL_STACK
                    XFREE(subject, NULL, DYNAMIC_TYPE_OPENSSL);
                    #endif
                    return SSL_FAILURE;
                }
            }
            #ifdef WOLFSSL_SMALL_STACK
            XFREE(subject, NULL, DYNAMIC_TYPE_OPENSSL);
            #endif
        }

        /* get and print public key */
        if (wolfSSL_BIO_write(bio, "\n        Subject Public Key Info:\n",
                          sizeof("\n        Subject Public Key Info:\n")) <= 0) {
            return SSL_FAILURE;
        }
        {
            char tmp[100];

            switch (x509->pubKeyOID) {
                #ifndef NO_RSA
                case RSAk:
                    if (wolfSSL_BIO_write(bio,
                                "            Public Key Algorithm: RSA\n",
                      sizeof("            Public Key Algorithm: RSA\n")) <= 0) {
                        return SSL_FAILURE;
                    }
                #ifdef HAVE_USER_RSA
                    if (wolfSSL_BIO_write(bio,
                        "                Build without user RSA to print key\n",
                sizeof("                Build without user RSA to print key\n"))
                        <= 0) {
                        return SSL_FAILURE;
                    }
                #else
                    {
                        RsaKey rsa;
                        word32 idx = 0;
                        int  sz;
                        byte lbit = 0;
                        int  rawLen;
                        unsigned char* rawKey;

                        if (wc_InitRsaKey(&rsa, NULL) != 0) {
                            WOLFSSL_MSG("wc_InitRsaKey failure");
                            return SSL_FAILURE;
                        }
                        if (wc_RsaPublicKeyDecode(x509->pubKey.buffer,
                                &idx, &rsa, x509->pubKey.length) != 0) {
                            WOLFSSL_MSG("Error decoding RSA key");
                            return SSL_FAILURE;
                        }
                        if ((sz = wc_RsaEncryptSize(&rsa)) < 0) {
                            WOLFSSL_MSG("Error getting RSA key size");
                            return SSL_FAILURE;
                        }
                        XSNPRINTF(tmp, sizeof(tmp) - 1, "%s%s: (%d bit)\n%s\n",
                                "                 ", "Public-Key", 8 * sz,
                                "                 Modulus:");
                        tmp[sizeof(tmp) - 1] = '\0';
                        if (wolfSSL_BIO_write(bio, tmp, (int)XSTRLEN(tmp)) <= 0) {
                            return SSL_FAILURE;
                        }

                        /* print out modulus */
                        XSNPRINTF(tmp, sizeof(tmp) - 1,"                     ");
                        tmp[sizeof(tmp) - 1] = '\0';
                        if (mp_leading_bit(&rsa.n)) {
                            lbit = 1;
                            XSTRNCAT(tmp, "00", sizeof("00"));
                        }

                        rawLen = mp_unsigned_bin_size(&rsa.n);
                        rawKey = (unsigned char*)XMALLOC(rawLen, NULL,
                                DYNAMIC_TYPE_TMP_BUFFER);
                        if (rawKey == NULL) {
                            WOLFSSL_MSG("Memory error");
                            return SSL_FAILURE;
                        }
                        mp_to_unsigned_bin(&rsa.n, rawKey);
                        for (idx = 0; idx < (word32)rawLen; idx++) {
                            char val[5];
                            int valSz = 5;

                            if ((idx == 0) && !lbit) {
                                XSNPRINTF(val, valSz - 1, "%02x", rawKey[idx]);
                            }
                            else if ((idx != 0) && (((idx + lbit) % 15) == 0)) {
                                tmp[sizeof(tmp) - 1] = '\0';
                                if (wolfSSL_BIO_write(bio, tmp, (int)XSTRLEN(tmp))
                                        <= 0) {
                                    XFREE(rawKey, NULL, DYNAMIC_TYPE_TMP_BUFFER);
                                    return SSL_FAILURE;
                                }
                                XSNPRINTF(tmp, sizeof(tmp) - 1,
                                        ":\n                     ");
                                XSNPRINTF(val, valSz - 1, "%02x", rawKey[idx]);
                            }
                            else {
                                XSNPRINTF(val, valSz - 1, ":%02x", rawKey[idx]);
                            }
                            XSTRNCAT(tmp, val, valSz);
                        }
                        XFREE(rawKey, NULL, DYNAMIC_TYPE_TMP_BUFFER);

                        /* print out remaning modulus values */
                        if ((idx > 0) && (((idx - 1 + lbit) % 15) != 0)) {
                                tmp[sizeof(tmp) - 1] = '\0';
                                if (wolfSSL_BIO_write(bio, tmp, (int)XSTRLEN(tmp))
                                        <= 0) {
                                    return SSL_FAILURE;
                                }
                        }

                        /* print out exponent values */
                        rawLen = mp_unsigned_bin_size(&rsa.e);
                        if (rawLen < 0) {
                            WOLFSSL_MSG("Error getting exponent size");
                            return SSL_FAILURE;
                        }

                        if ((word32)rawLen < sizeof(word32)) {
                            rawLen = sizeof(word32);
                        }
                        rawKey = (unsigned char*)XMALLOC(rawLen, NULL,
                                DYNAMIC_TYPE_TMP_BUFFER);
                        if (rawKey == NULL) {
                            WOLFSSL_MSG("Memory error");
                            return SSL_FAILURE;
                        }
                        XMEMSET(rawKey, 0, rawLen);
                        mp_to_unsigned_bin(&rsa.e, rawKey);
                        if ((word32)rawLen <= sizeof(word32)) {
                            idx = *(word32*)rawKey;
                        }
                        XSNPRINTF(tmp, sizeof(tmp) - 1,
                        "\n                 Exponent: %d\n", idx);
                        if (wolfSSL_BIO_write(bio, tmp, (int)XSTRLEN(tmp)) <= 0) {
                            XFREE(rawKey, NULL, DYNAMIC_TYPE_TMP_BUFFER);
                            return SSL_FAILURE;
                        }
                        XFREE(rawKey, NULL, DYNAMIC_TYPE_TMP_BUFFER);
                    }
                #endif /* HAVE_USER_RSA */
                    break;
                #endif /* NO_RSA */

                #ifdef HAVE_ECC
                case ECDSAk:
                    {
                        word32 i;
                        ecc_key ecc;

                        if (wolfSSL_BIO_write(bio,
                                "            Public Key Algorithm: EC\n",
                      sizeof("            Public Key Algorithm: EC\n")) <= 0) {
                        return SSL_FAILURE;
                        }
                        if (wc_ecc_init_ex(&ecc, x509->heap, INVALID_DEVID)
                                != 0) {
                            return SSL_FAILURE;
                        }
                        if (wc_ecc_import_x963(x509->pubKey.buffer,
                                    x509->pubKey.length, &ecc) != 0) {
                            wc_ecc_free(&ecc);
                            return SSL_FAILURE;
                        }
                        XSNPRINTF(tmp, sizeof(tmp) - 1, "%s%s: (%d bit)\n%s\n",
                                "                 ", "Public-Key",
                                8 * wc_ecc_size(&ecc),
                                "                 pub:");
                        tmp[sizeof(tmp) - 1] = '\0';
                        if (wolfSSL_BIO_write(bio, tmp, (int)XSTRLEN(tmp)) <= 0) {
                            wc_ecc_free(&ecc);
                            return SSL_FAILURE;
                        }
                        XSNPRINTF(tmp, sizeof(tmp) - 1,"                     ");
                        for (i = 0; i < x509->pubKey.length; i++) {
                            char val[5];
                            int valSz = 5;

                            if (i == 0) {
                                XSNPRINTF(val, valSz - 1, "%02x",
                                        x509->pubKey.buffer[i]);
                            }
                            else if ((i % 15) == 0) {
                                tmp[sizeof(tmp) - 1] = '\0';
                                if (wolfSSL_BIO_write(bio, tmp, (int)XSTRLEN(tmp))
                                        <= 0) {
                                    wc_ecc_free(&ecc);
                                    return SSL_FAILURE;
                                }
                                XSNPRINTF(tmp, sizeof(tmp) - 1,
                                        ":\n                     ");
                                XSNPRINTF(val, valSz - 1, "%02x",
                                        x509->pubKey.buffer[i]);
                            }
                            else {
                                XSNPRINTF(val, valSz - 1, ":%02x",
                                        x509->pubKey.buffer[i]);
                            }
                            XSTRNCAT(tmp, val, valSz);
                        }

                        /* print out remaning modulus values */
                        if ((i > 0) && (((i - 1) % 15) != 0)) {
                                tmp[sizeof(tmp) - 1] = '\0';
                                if (wolfSSL_BIO_write(bio, tmp, (int)XSTRLEN(tmp))
                                        <= 0) {
                                    wc_ecc_free(&ecc);
                                    return SSL_FAILURE;
                                }
                        }
                        XSNPRINTF(tmp, sizeof(tmp) - 1, "\n%s%s: %s\n",
                                "                ", "ASN1 OID",
                                ecc.dp->name);
                        if (wolfSSL_BIO_write(bio, tmp, (int)XSTRLEN(tmp)) <= 0) {
                            wc_ecc_free(&ecc);
                            return SSL_FAILURE;
                        }
                        wc_ecc_free(&ecc);
                    }
                    break;
                #endif /* HAVE_ECC */
                default:
                    WOLFSSL_MSG("Unknown key type");
                    return SSL_FAILURE;
            }
        }

        /* print out extensions */
        if (wolfSSL_BIO_write(bio, "        X509v3 extensions:\n",
                            sizeof("        X509v3 extensions:\n")) <= 0) {
            return SSL_FAILURE;
        }

        /* print subject key id */
        if (x509->subjKeyIdSet && x509->subjKeyId != NULL &&
                x509->subjKeyIdSz > 0) {
            char tmp[100];
            word32 i;
            char val[5];
            int valSz = 5;


            if (wolfSSL_BIO_write(bio,
                        "            X509v3 Subject Key Identifier:\n",
                 sizeof("            X509v3 Subject Key Identifier:\n"))
                 <= 0) {
                return SSL_FAILURE;
            }

            XSNPRINTF(tmp, sizeof(tmp) - 1, "                 ");
            for (i = 0; i < sizeof(tmp) && i < (x509->subjKeyIdSz - 1); i++) {
                XSNPRINTF(val, valSz - 1, "%02X:", x509->subjKeyId[i]);
                XSTRNCAT(tmp, val, valSz);
            }
            XSNPRINTF(val, valSz - 1, "%02X\n", x509->subjKeyId[i]);
            XSTRNCAT(tmp, val, valSz);
            if (wolfSSL_BIO_write(bio, tmp, (int)XSTRLEN(tmp)) <= 0) {
                return SSL_FAILURE;
            }
        }

        /* printf out authority key id */
        if (x509->authKeyIdSet && x509->authKeyId != NULL &&
                x509->authKeyIdSz > 0) {
            char tmp[100];
            word32 i;
            char val[5];
            int valSz = 5;

            if (wolfSSL_BIO_write(bio,
                        "            X509v3 Authority Key Identifier:\n",
                 sizeof("            X509v3 Authority Key Identifier:\n"))
                 <= 0) {
                return SSL_FAILURE;
            }

            XSNPRINTF(tmp, sizeof(tmp) - 1, "                 keyid");
            for (i = 0; i < x509->authKeyIdSz; i++) {
                /* check if buffer is almost full */
                if (XSTRLEN(tmp) >= sizeof(tmp) - valSz) {
                    if (wolfSSL_BIO_write(bio, tmp, (int)XSTRLEN(tmp)) <= 0) {
                        return SSL_FAILURE;
                    }
                    tmp[0] = '\0';
                }
                XSNPRINTF(val, valSz - 1, ":%02X", x509->authKeyId[i]);
                XSTRNCAT(tmp, val, valSz);
            }
            if (wolfSSL_BIO_write(bio, tmp, (int)XSTRLEN(tmp)) <= 0) {
                return SSL_FAILURE;
            }

            /* print issuer */
            {
                char* issuer;
            #ifdef WOLFSSL_SMALL_STACK
                char* buff  = NULL;
                int   issSz = 0;
            #else
                char buff[256];
                int  issSz = 256;
            #endif

                issuer  = wolfSSL_X509_NAME_oneline(
                               wolfSSL_X509_get_issuer_name(x509), buff, issSz);

                if (wolfSSL_BIO_write(bio, "\n                 DirName:",
                                  sizeof("\n                 DirName:")) <= 0) {
                    #ifdef WOLFSSL_SMALL_STACK
                    XFREE(issuer, NULL, DYNAMIC_TYPE_OPENSSL);
                    #endif
                    return SSL_FAILURE;
                }
                if (issuer != NULL) {
                    if (wolfSSL_BIO_write(bio, issuer, (int)XSTRLEN(issuer)) <= 0) {
                        #ifdef WOLFSSL_SMALL_STACK
                        XFREE(issuer, NULL, DYNAMIC_TYPE_OPENSSL);
                        #endif
                        return SSL_FAILURE;
                    }
                }
                #ifdef WOLFSSL_SMALL_STACK
                XFREE(issuer, NULL, DYNAMIC_TYPE_OPENSSL);
                #endif
                if (wolfSSL_BIO_write(bio, "\n", sizeof("\n")) <= 0) {
                    return SSL_FAILURE;
                }
            }
        }

        /* print basic constraint */
        if (x509->basicConstSet) {
            char tmp[100];

            if (wolfSSL_BIO_write(bio,
                        "\n            X509v3 Basic Constraints:\n",
                 sizeof("\n            X509v3 Basic Constraints:\n"))
                 <= 0) {
                return SSL_FAILURE;
            }
            XSNPRINTF(tmp, sizeof(tmp),
                    "                    CA:%s\n",
                    (x509->isCa)? "TRUE": "FALSE");
            if (wolfSSL_BIO_write(bio, tmp, (int)XSTRLEN(tmp)) <= 0) {
                return SSL_FAILURE;
            }
        }

        /* print out signature */
        {
            unsigned char* sig;
            int sigSz;
            int i;
            char tmp[100];
            int sigOid = wolfSSL_X509_get_signature_type(x509);

            if (wolfSSL_BIO_write(bio,
                                "    Signature Algorithm: ",
                      sizeof("    Signature Algorithm: ")) <= 0) {
                return SSL_FAILURE;
            }
            XSNPRINTF(tmp, sizeof(tmp) - 1,"%s\n", GetSigName(sigOid));
            tmp[sizeof(tmp) - 1] = '\0';
            if (wolfSSL_BIO_write(bio, tmp, (int)XSTRLEN(tmp)) <= 0) {
                return SSL_FAILURE;
            }

            sigSz = (int)x509->sig.length;
            sig = (unsigned char*)XMALLOC(sigSz, NULL, DYNAMIC_TYPE_TMP_BUFFER);
            if (sig == NULL || sigSz <= 0) {
                return SSL_FAILURE;
            }
            if (wolfSSL_X509_get_signature(x509, sig, &sigSz) <= 0) {
                XFREE(sig, NULL, DYNAMIC_TYPE_TMP_BUFFER);
                return SSL_FAILURE;
            }
            XSNPRINTF(tmp, sizeof(tmp) - 1,"         ");
            tmp[sizeof(tmp) - 1] = '\0';
            for (i = 0; i < sigSz; i++) {
                char val[5];
                int valSz = 5;

                if (i == 0) {
                    XSNPRINTF(val, valSz - 1, "%02x", sig[i]);
                }
                else if (((i % 18) == 0)) {
                    tmp[sizeof(tmp) - 1] = '\0';
                    if (wolfSSL_BIO_write(bio, tmp, (int)XSTRLEN(tmp))
                            <= 0) {
                        XFREE(sig, NULL, DYNAMIC_TYPE_TMP_BUFFER);
                        return SSL_FAILURE;
                    }
                    XSNPRINTF(tmp, sizeof(tmp) - 1,
                            ":\n         ");
                    XSNPRINTF(val, valSz - 1, "%02x", sig[i]);
                }
                else {
                    XSNPRINTF(val, valSz - 1, ":%02x", sig[i]);
                }
                XSTRNCAT(tmp, val, valSz);
            }
            XFREE(sig, NULL, DYNAMIC_TYPE_TMP_BUFFER);

            /* print out remaning sig values */
            if ((i > 0) && (((i - 1) % 18) != 0)) {
                    tmp[sizeof(tmp) - 1] = '\0';
                    if (wolfSSL_BIO_write(bio, tmp, (int)XSTRLEN(tmp))
                            <= 0) {
                        return SSL_FAILURE;
                    }
            }
        }

        /* done with print out */
        if (wolfSSL_BIO_write(bio, "\n", sizeof("\n")) <= 0) {
            return SSL_FAILURE;
        }

        return SSL_SUCCESS;
    }
#endif /* XSNPRINTF */
#endif /* OPENSSL_EXTRA */


    /* copy name into in buffer, at most sz bytes, if buffer is null will
       malloc buffer, call responsible for freeing                     */
    char* wolfSSL_X509_NAME_oneline(WOLFSSL_X509_NAME* name, char* in, int sz)
    {
        int copySz = min(sz, name->sz);

        WOLFSSL_ENTER("wolfSSL_X509_NAME_oneline");
        if (!name->sz) return in;

        if (!in) {
        #ifdef WOLFSSL_STATIC_MEMORY
            WOLFSSL_MSG("Using static memory -- please pass in a buffer");
            return NULL;
        #else
            in = (char*)XMALLOC(name->sz, NULL, DYNAMIC_TYPE_OPENSSL);
            if (!in ) return in;
            copySz = name->sz;
        #endif
        }

        if (copySz == 0)
            return in;

        XMEMCPY(in, name->name, copySz - 1);
        in[copySz - 1] = 0;

        return in;
    }


    int wolfSSL_X509_get_signature_type(WOLFSSL_X509* x509)
    {
        int type = 0;

        WOLFSSL_ENTER("wolfSSL_X509_get_signature_type");

        if (x509 != NULL)
            type = x509->sigOID;

        return type;
    }


    int wolfSSL_X509_get_signature(WOLFSSL_X509* x509,
                                                 unsigned char* buf, int* bufSz)
    {
        WOLFSSL_ENTER("wolfSSL_X509_get_signature");
        if (x509 == NULL || bufSz == NULL || *bufSz < (int)x509->sig.length)
            return SSL_FATAL_ERROR;

        if (buf != NULL)
            XMEMCPY(buf, x509->sig.buffer, x509->sig.length);
        *bufSz = x509->sig.length;

        return SSL_SUCCESS;
    }


    /* write X509 serial number in unsigned binary to buffer
       buffer needs to be at least EXTERNAL_SERIAL_SIZE (32) for all cases
       return SSL_SUCCESS on success */
    int wolfSSL_X509_get_serial_number(WOLFSSL_X509* x509,
                                       byte* in, int* inOutSz)
    {
        WOLFSSL_ENTER("wolfSSL_X509_get_serial_number");
        if (x509 == NULL || in == NULL ||
                                   inOutSz == NULL || *inOutSz < x509->serialSz)
            return BAD_FUNC_ARG;

        XMEMCPY(in, x509->serial, x509->serialSz);
        *inOutSz = x509->serialSz;

        return SSL_SUCCESS;
    }


    const byte* wolfSSL_X509_get_der(WOLFSSL_X509* x509, int* outSz)
    {
        WOLFSSL_ENTER("wolfSSL_X509_get_der");

        if (x509 == NULL || x509->derCert == NULL || outSz == NULL)
            return NULL;

        *outSz = (int)x509->derCert->length;
        return x509->derCert->buffer;
    }


    int wolfSSL_X509_version(WOLFSSL_X509* x509)
    {
        WOLFSSL_ENTER("wolfSSL_X509_version");

        if (x509 == NULL)
            return 0;

        return x509->version;
    }


    const byte* wolfSSL_X509_notBefore(WOLFSSL_X509* x509)
    {
        WOLFSSL_ENTER("wolfSSL_X509_notBefore");

        if (x509 == NULL)
            return NULL;

        return x509->notBefore;
    }


    const byte* wolfSSL_X509_notAfter(WOLFSSL_X509* x509)
    {
        WOLFSSL_ENTER("wolfSSL_X509_notAfter");

        if (x509 == NULL)
            return NULL;

        return x509->notAfter;
    }


#ifdef WOLFSSL_SEP

/* copy oid into in buffer, at most *inOutSz bytes, if buffer is null will
   malloc buffer, call responsible for freeing. Actual size returned in
   *inOutSz. Requires inOutSz be non-null */
byte* wolfSSL_X509_get_device_type(WOLFSSL_X509* x509, byte* in, int *inOutSz)
{
    int copySz;

    WOLFSSL_ENTER("wolfSSL_X509_get_dev_type");
    if (inOutSz == NULL) return NULL;
    if (!x509->deviceTypeSz) return in;

    copySz = min(*inOutSz, x509->deviceTypeSz);

    if (!in) {
    #ifdef WOLFSSL_STATIC_MEMORY
        WOLFSSL_MSG("Using static memory -- please pass in a buffer");
        return NULL;
    #else
        in = (byte*)XMALLOC(x509->deviceTypeSz, 0, DYNAMIC_TYPE_OPENSSL);
        if (!in) return in;
        copySz = x509->deviceTypeSz;
    #endif
    }

    XMEMCPY(in, x509->deviceType, copySz);
    *inOutSz = copySz;

    return in;
}


byte* wolfSSL_X509_get_hw_type(WOLFSSL_X509* x509, byte* in, int* inOutSz)
{
    int copySz;

    WOLFSSL_ENTER("wolfSSL_X509_get_hw_type");
    if (inOutSz == NULL) return NULL;
    if (!x509->hwTypeSz) return in;

    copySz = min(*inOutSz, x509->hwTypeSz);

    if (!in) {
    #ifdef WOLFSSL_STATIC_MEMORY
        WOLFSSL_MSG("Using static memory -- please pass in a buffer");
        return NULL;
    #else
        in = (byte*)XMALLOC(x509->hwTypeSz, 0, DYNAMIC_TYPE_OPENSSL);
        if (!in) return in;
        copySz = x509->hwTypeSz;
    #endif
    }

    XMEMCPY(in, x509->hwType, copySz);
    *inOutSz = copySz;

    return in;
}


byte* wolfSSL_X509_get_hw_serial_number(WOLFSSL_X509* x509,byte* in,
                                        int* inOutSz)
{
    int copySz;

    WOLFSSL_ENTER("wolfSSL_X509_get_hw_serial_number");
    if (inOutSz == NULL) return NULL;
    if (!x509->hwTypeSz) return in;

    copySz = min(*inOutSz, x509->hwSerialNumSz);

    if (!in) {
    #ifdef WOLFSSL_STATIC_MEMORY
        WOLFSSL_MSG("Using static memory -- please pass in a buffer");
        return NULL;
    #else
        in = (byte*)XMALLOC(x509->hwSerialNumSz, 0, DYNAMIC_TYPE_OPENSSL);
        if (!in) return in;
        copySz = x509->hwSerialNumSz;
    #endif
    }

    XMEMCPY(in, x509->hwSerialNum, copySz);
    *inOutSz = copySz;

    return in;
}

#endif /* WOLFSSL_SEP */

/* require OPENSSL_EXTRA since wolfSSL_X509_free is wrapped by OPENSSL_EXTRA */
#if !defined(NO_CERTS) && defined(OPENSSL_EXTRA)
/* return 1 on success 0 on fail */
int wolfSSL_sk_X509_push(STACK_OF(WOLFSSL_X509_NAME)* sk, WOLFSSL_X509* x509)
{
    WOLFSSL_STACK* node;

    if (sk == NULL || x509 == NULL) {
        return SSL_FAILURE;
    }

    /* no previous values in stack */
    if (sk->data.x509 == NULL) {
        sk->data.x509 = x509;
        sk->num += 1;
        return SSL_SUCCESS;
    }

    /* stack already has value(s) create a new node and add more */
    node = (WOLFSSL_STACK*)XMALLOC(sizeof(WOLFSSL_STACK), NULL,
                                                             DYNAMIC_TYPE_X509);
    if (node == NULL) {
        WOLFSSL_MSG("Memory error");
        return SSL_FAILURE;
    }
    XMEMSET(node, 0, sizeof(WOLFSSL_STACK));

    /* push new x509 onto head of stack */
    node->data.x509 = sk->data.x509;
    node->next      = sk->next;
    sk->next        = node;
    sk->data.x509   = x509;
    sk->num        += 1;

    return SSL_SUCCESS;
}


WOLFSSL_X509* wolfSSL_sk_X509_pop(STACK_OF(WOLFSSL_X509_NAME)* sk) {
    WOLFSSL_STACK* node;
    WOLFSSL_X509*  x509;

    if (sk == NULL) {
        return NULL;
    }

    node = sk->next;
    x509 = sk->data.x509;

    if (node != NULL) { /* update sk and remove node from stack */
        sk->data.x509 = node->data.x509;
        sk->next = node->next;
        XFREE(node, NULL, DYNAMIC_TYPE_X509);
    }
    else { /* last x509 in stack */
        sk->data.x509 = NULL;
    }

    if (sk->num > 0) {
        sk->num -= 1;
    }

    return x509;
}


/* free structure for x509 stack */
void wolfSSL_sk_X509_free(STACK_OF(WOLFSSL_X509_NAME)* sk) {
    WOLFSSL_STACK* node;

    if (sk == NULL) {
        return;
    }

    /* parse through stack freeing each node */
    node = sk->next;
    while (sk->num > 1) {
        WOLFSSL_STACK* tmp = node;
        node = node->next;

        wolfSSL_X509_free(tmp->data.x509);
        XFREE(tmp, NULL, DYNAMIC_TYPE_X509);
        sk->num -= 1;
    }

    /* free head of stack */
    if (sk->num == 1) {
	wolfSSL_X509_free(sk->data.x509);
    }
    XFREE(sk, NULL, DYNAMIC_TYPE_X509);
}
#endif /* NO_CERTS && OPENSSL_EXTRA */


WOLFSSL_X509* wolfSSL_d2i_X509(WOLFSSL_X509** x509, const unsigned char** in,
        int len)
{
    return wolfSSL_X509_d2i(x509, *in, len);
}


WOLFSSL_X509* wolfSSL_X509_d2i(WOLFSSL_X509** x509, const byte* in, int len)
{
    WOLFSSL_X509 *newX509 = NULL;

    WOLFSSL_ENTER("wolfSSL_X509_d2i");

    if (in != NULL && len != 0) {
    #ifdef WOLFSSL_SMALL_STACK
        DecodedCert* cert = NULL;
    #else
        DecodedCert  cert[1];
    #endif

    #ifdef WOLFSSL_SMALL_STACK
        cert = (DecodedCert*)XMALLOC(sizeof(DecodedCert), NULL,
                                     DYNAMIC_TYPE_TMP_BUFFER);
        if (cert == NULL)
            return NULL;
    #endif

        InitDecodedCert(cert, (byte*)in, len, NULL);
        if (ParseCertRelative(cert, CERT_TYPE, 0, NULL) == 0) {
            newX509 = (WOLFSSL_X509*)XMALLOC(sizeof(WOLFSSL_X509), NULL,
                                             DYNAMIC_TYPE_X509);
            if (newX509 != NULL) {
                InitX509(newX509, 1, NULL);
                if (CopyDecodedToX509(newX509, cert) != 0) {
                    XFREE(newX509, NULL, DYNAMIC_TYPE_X509);
                    newX509 = NULL;
                }
            }
        }
        FreeDecodedCert(cert);
    #ifdef WOLFSSL_SMALL_STACK
        XFREE(cert, NULL, DYNAMIC_TYPE_TMP_BUFFER);
    #endif
    }

    if (x509 != NULL)
        *x509 = newX509;

    return newX509;
}


#ifndef NO_FILESYSTEM

#ifndef NO_STDIO_FILESYSTEM

WOLFSSL_X509* wolfSSL_X509_d2i_fp(WOLFSSL_X509** x509, XFILE file)
{
    WOLFSSL_X509* newX509 = NULL;

    WOLFSSL_ENTER("wolfSSL_X509_d2i_fp");

    if (file != XBADFILE) {
        byte* fileBuffer = NULL;
        long sz = 0;

        XFSEEK(file, 0, XSEEK_END);
        sz = XFTELL(file);
        XREWIND(file);

        if (sz < 0) {
            WOLFSSL_MSG("Bad tell on FILE");
            return NULL;
        }

        fileBuffer = (byte*)XMALLOC(sz, NULL, DYNAMIC_TYPE_FILE);
        if (fileBuffer != NULL) {
            int ret = (int)XFREAD(fileBuffer, sz, 1, file);
            if (ret > 0) {
                newX509 = wolfSSL_X509_d2i(NULL, fileBuffer, (int)sz);
            }
            XFREE(fileBuffer, NULL, DYNAMIC_TYPE_FILE);
        }
    }

    if (x509 != NULL)
        *x509 = newX509;

    return newX509;
}

#endif /* NO_STDIO_FILESYSTEM */

WOLFSSL_X509* wolfSSL_X509_load_certificate_file(const char* fname, int format)
{
#ifdef WOLFSSL_SMALL_STACK
    byte  staticBuffer[1]; /* force heap usage */
#else
    byte  staticBuffer[FILE_BUFFER_SIZE];
#endif
    byte* fileBuffer = staticBuffer;
    int   dynamic = 0;
    int   ret;
    long  sz = 0;
    XFILE file;

    WOLFSSL_X509* x509 = NULL;

    /* Check the inputs */
    if ((fname == NULL) ||
        (format != SSL_FILETYPE_ASN1 && format != SSL_FILETYPE_PEM))
        return NULL;

    file = XFOPEN(fname, "rb");
    if (file == XBADFILE)
        return NULL;

    XFSEEK(file, 0, XSEEK_END);
    sz = XFTELL(file);
    XREWIND(file);

    if (sz > (long)sizeof(staticBuffer)) {
        fileBuffer = (byte*)XMALLOC(sz, NULL, DYNAMIC_TYPE_FILE);
        if (fileBuffer == NULL) {
            XFCLOSE(file);
            return NULL;
        }
        dynamic = 1;
    }
    else if (sz < 0) {
        XFCLOSE(file);
        return NULL;
    }

    ret = (int)XFREAD(fileBuffer, sz, 1, file);
    if (ret < 0) {
        XFCLOSE(file);
        if (dynamic)
            XFREE(fileBuffer, NULL, DYNAMIC_TYPE_FILE);
        return NULL;
    }

    XFCLOSE(file);

    x509 = wolfSSL_X509_load_certificate_buffer(fileBuffer, (int)sz, format);

    if (dynamic)
        XFREE(fileBuffer, NULL, DYNAMIC_TYPE_FILE);

    return x509;
}

#endif /* NO_FILESYSTEM */


WOLFSSL_X509* wolfSSL_X509_load_certificate_buffer(
    const unsigned char* buf, int sz, int format)
{
    int ret;
    WOLFSSL_X509* x509 = NULL;
    DerBuffer* der = NULL;

    WOLFSSL_ENTER("wolfSSL_X509_load_certificate_ex");

    if (format == SSL_FILETYPE_PEM) {
        int ecc = 0;
    #ifdef WOLFSSL_SMALL_STACK
        EncryptedInfo* info = NULL;
    #else
        EncryptedInfo  info[1];
    #endif

    #ifdef WOLFSSL_SMALL_STACK
        info = (EncryptedInfo*)XMALLOC(sizeof(EncryptedInfo), NULL,
                                                      DYNAMIC_TYPE_TMP_BUFFER);
        if (info == NULL) {
            return NULL;
        }
    #endif

        info->set = 0;
        info->ctx = NULL;
        info->consumed = 0;

        if (PemToDer(buf, sz, CERT_TYPE, &der, NULL, info, &ecc) != 0) {
            FreeDer(&der);
        }

    #ifdef WOLFSSL_SMALL_STACK
        XFREE(info, NULL, DYNAMIC_TYPE_TMP_BUFFER);
    #endif
    }
    else {
        ret = AllocDer(&der, (word32)sz, CERT_TYPE, NULL);
        if (ret == 0) {
            XMEMCPY(der->buffer, buf, sz);
        }
    }

    /* At this point we want `der` to have the certificate in DER format */
    /* ready to be decoded. */
    if (der != NULL && der->buffer != NULL) {
    #ifdef WOLFSSL_SMALL_STACK
        DecodedCert* cert = NULL;
    #else
        DecodedCert  cert[1];
    #endif

    #ifdef WOLFSSL_SMALL_STACK
        cert = (DecodedCert*)XMALLOC(sizeof(DecodedCert), NULL,
                                                       DYNAMIC_TYPE_TMP_BUFFER);
        if (cert != NULL)
    #endif
        {
            InitDecodedCert(cert, der->buffer, der->length, NULL);
            if (ParseCertRelative(cert, CERT_TYPE, 0, NULL) == 0) {
                x509 = (WOLFSSL_X509*)XMALLOC(sizeof(WOLFSSL_X509), NULL,
                                                             DYNAMIC_TYPE_X509);
                if (x509 != NULL) {
                    InitX509(x509, 1, NULL);
                    if (CopyDecodedToX509(x509, cert) != 0) {
                        XFREE(x509, NULL, DYNAMIC_TYPE_X509);
                        x509 = NULL;
                    }
                }
            }

            FreeDecodedCert(cert);
        #ifdef WOLFSSL_SMALL_STACK
            XFREE(cert, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        #endif
        }

        FreeDer(&der);
    }

    return x509;
}

#endif /* KEEP_PEER_CERT || SESSION_CERTS */

/* OPENSSL_EXTRA is needed for wolfSSL_X509_d21 function
   KEEP_OUR_CERT is to insure ability for returning ssl certificate */
#if defined(OPENSSL_EXTRA) && defined(KEEP_OUR_CERT)
WOLFSSL_X509* wolfSSL_get_certificate(WOLFSSL* ssl)
{
    if (ssl == NULL) {
        return NULL;
    }

    if (ssl->buffers.weOwnCert) {
        if (ssl->ourCert == NULL) {
            if (ssl->buffers.certificate == NULL) {
                WOLFSSL_MSG("Certificate buffer not set!");
                return NULL;
            }
            ssl->ourCert = wolfSSL_X509_d2i(NULL,
                                              ssl->buffers.certificate->buffer,
                                              ssl->buffers.certificate->length);
        }
        return ssl->ourCert;
    }
    else { /* if cert not owned get parent ctx cert or return null */
        if (ssl->ctx) {
            if (ssl->ctx->ourCert == NULL) {
                if (ssl->ctx->certificate == NULL) {
                    WOLFSSL_MSG("Ctx Certificate buffer not set!");
                    return NULL;
                }
                ssl->ctx->ourCert = wolfSSL_X509_d2i(NULL,
                                               ssl->ctx->certificate->buffer,
                                               ssl->ctx->certificate->length);
            }
            return ssl->ctx->ourCert;
        }
    }

    return NULL;
}
#endif /* OPENSSL_EXTRA && KEEP_OUR_CERT */
#endif /* NO_CERTS */


#ifdef OPENSSL_EXTRA
/* return 1 on success 0 on fail */
int wolfSSL_sk_ASN1_OBJECT_push(STACK_OF(WOLFSSL_ASN1_OBJEXT)* sk,
                                                      WOLFSSL_ASN1_OBJECT* obj)
{
    WOLFSSL_STACK* node;

    if (sk == NULL || obj == NULL) {
        return SSL_FAILURE;
    }

    /* no previous values in stack */
    if (sk->data.obj == NULL) {
        sk->data.obj = obj;
        sk->num += 1;
        return SSL_SUCCESS;
    }

    /* stack already has value(s) create a new node and add more */
    node = (WOLFSSL_STACK*)XMALLOC(sizeof(WOLFSSL_STACK), NULL,
                                                             DYNAMIC_TYPE_ASN1);
    if (node == NULL) {
        WOLFSSL_MSG("Memory error");
        return SSL_FAILURE;
    }
    XMEMSET(node, 0, sizeof(WOLFSSL_STACK));

    /* push new obj onto head of stack */
    node->data.obj = sk->data.obj;
    node->next      = sk->next;
    sk->next        = node;
    sk->data.obj   = obj;
    sk->num        += 1;

    return SSL_SUCCESS;
}


WOLFSSL_ASN1_OBJECT* wolfSSL_sk_ASN1_OBJCET_pop(
                                            STACK_OF(WOLFSSL_ASN1_OBJECT)* sk)
{
    WOLFSSL_STACK* node;
    WOLFSSL_ASN1_OBJECT*  obj;

    if (sk == NULL) {
        return NULL;
    }

    node = sk->next;
    obj = sk->data.obj;

    if (node != NULL) { /* update sk and remove node from stack */
        sk->data.obj = node->data.obj;
        sk->next = node->next;
        XFREE(node, NULL, DYNAMIC_TYPE_ASN1);
    }
    else { /* last obj in stack */
        sk->data.obj = NULL;
    }

    if (sk->num > 0) {
        sk->num -= 1;
    }

    return obj;
}


#ifndef NO_ASN
WOLFSSL_ASN1_OBJECT* wolfSSL_ASN1_OBJECT_new(void)
{
    WOLFSSL_ASN1_OBJECT* obj;

    obj = (WOLFSSL_ASN1_OBJECT*)XMALLOC(sizeof(WOLFSSL_ASN1_OBJECT), NULL,
                                        DYNAMIC_TYPE_ASN1);
    if (obj == NULL) {
        return NULL;
    }

    XMEMSET(obj, 0, sizeof(WOLFSSL_ASN1_OBJECT));
    return obj;
}


void wolfSSL_ASN1_OBJECT_free(WOLFSSL_ASN1_OBJECT* obj)
{
    if (obj == NULL) {
        return;
    }

    if (obj->dynamic == 1) {
        if (obj->obj != NULL) {
            WOLFSSL_MSG("Freeing ASN1 OBJECT data");
            XFREE(obj->obj, obj->heap, DYNAMIC_TYPE_ASN1);
        }
    }

    XFREE(obj, NULL, DYNAMIC_TYPE_ASN1);
}


/* free structure for x509 stack */
void wolfSSL_sk_ASN1_OBJECT_free(STACK_OF(WOLFSSL_ASN1_OBJECT)* sk)
{
    WOLFSSL_STACK* node;

    if (sk == NULL) {
        return;
    }

    /* parse through stack freeing each node */
    node = sk->next;
    while (sk->num > 1) {
        WOLFSSL_STACK* tmp = node;
        node = node->next;

        wolfSSL_ASN1_OBJECT_free(tmp->data.obj);
        XFREE(tmp, NULL, DYNAMIC_TYPE_ASN1);
        sk->num -= 1;
    }

    /* free head of stack */
    if (sk->num == 1) {
	    wolfSSL_ASN1_OBJECT_free(sk->data.obj);
    }
    XFREE(sk, NULL, DYNAMIC_TYPE_ASN1);
}
#endif /* NO_ASN */


int wolfSSL_set_session_id_context(WOLFSSL* ssl, const unsigned char* id,
                               unsigned int len)
{
    (void)ssl;
    (void)id;
    (void)len;
    return 0;
}


void wolfSSL_set_connect_state(WOLFSSL* ssl)
{
    (void)ssl;
    /* client by default */
}
#endif

int wolfSSL_get_shutdown(const WOLFSSL* ssl)
{
    WOLFSSL_ENTER("wolfSSL_get_shutdown");
    /* in OpenSSL, SSL_SENT_SHUTDOWN = 1, when closeNotifySent   *
     * SSL_RECEIVED_SHUTDOWN = 2, from close notify or fatal err */
    return ((ssl->options.closeNotify||ssl->options.connReset) << 1)
            | (ssl->options.sentNotify);
}


int wolfSSL_session_reused(WOLFSSL* ssl)
{
    return ssl->options.resuming;
}

#ifdef OPENSSL_EXTRA
void wolfSSL_SESSION_free(WOLFSSL_SESSION* session)
{
    /* No need to free since cache is static */
    (void)session;
}
#endif

const char* wolfSSL_get_version(WOLFSSL* ssl)
{
    WOLFSSL_ENTER("SSL_get_version");
    if (ssl->version.major == SSLv3_MAJOR) {
        switch (ssl->version.minor) {
            case SSLv3_MINOR :
                return "SSLv3";
            case TLSv1_MINOR :
                return "TLSv1";
            case TLSv1_1_MINOR :
                return "TLSv1.1";
            case TLSv1_2_MINOR :
                return "TLSv1.2";
            default:
                return "unknown";
        }
    }
    else if (ssl->version.major == DTLS_MAJOR) {
        switch (ssl->version.minor) {
            case DTLS_MINOR :
                return "DTLS";
            case DTLSv1_2_MINOR :
                return "DTLSv1.2";
            default:
                return "unknown";
        }
    }
    return "unknown";
}


/* current library version */
const char* wolfSSL_lib_version(void)
{
    return LIBWOLFSSL_VERSION_STRING;
}


/* current library version in hex */
word32 wolfSSL_lib_version_hex(void)
{
    return LIBWOLFSSL_VERSION_HEX;
}


int wolfSSL_get_current_cipher_suite(WOLFSSL* ssl)
{
    WOLFSSL_ENTER("SSL_get_current_cipher_suite");
    if (ssl)
        return (ssl->options.cipherSuite0 << 8) | ssl->options.cipherSuite;
    return 0;
}

WOLFSSL_CIPHER* wolfSSL_get_current_cipher(WOLFSSL* ssl)
{
    WOLFSSL_ENTER("SSL_get_current_cipher");
    if (ssl)
        return &ssl->cipher;
    else
        return NULL;
}


const char* wolfSSL_CIPHER_get_name(const WOLFSSL_CIPHER* cipher)
{
    WOLFSSL_ENTER("SSL_CIPHER_get_name");

    if (cipher == NULL || cipher->ssl == NULL) {
        return NULL;
    }

    return wolfSSL_get_cipher_name_from_suite(cipher->ssl->options.cipherSuite,
        cipher->ssl->options.cipherSuite0);
}

const char* wolfSSL_SESSION_CIPHER_get_name(WOLFSSL_SESSION* session)
{
    if (session == NULL) {
        return NULL;
    }

#ifdef SESSION_CERTS
    return wolfSSL_get_cipher_name_from_suite(session->cipherSuite,
        session->cipherSuite0);
#else
    return NULL;
#endif
}

const char* wolfSSL_get_cipher(WOLFSSL* ssl)
{
    WOLFSSL_ENTER("wolfSSL_get_cipher");
    return wolfSSL_CIPHER_get_name(wolfSSL_get_current_cipher(ssl));
}

/* gets cipher name in the format DHE-RSA-... rather then TLS_DHE... */
const char* wolfSSL_get_cipher_name(WOLFSSL* ssl)
{
    /* get access to cipher_name_idx in internal.c */
    return wolfSSL_get_cipher_name_internal(ssl);
}


#ifdef OPENSSL_EXTRA

char* wolfSSL_CIPHER_description(WOLFSSL_CIPHER* cipher, char* in, int len)
{
    (void)cipher;
    (void)in;
    (void)len;
    return 0;
}


#ifndef NO_SESSION_CACHE

WOLFSSL_SESSION* wolfSSL_get1_session(WOLFSSL* ssl)
{
    if (ssl == NULL) {
        return NULL;
    }

    /* sessions are stored statically, no need for reference count */
    return wolfSSL_get_session(ssl);
}

#endif /* NO_SESSION_CACHE */

#ifndef NO_CERTS
void wolfSSL_X509_free(WOLFSSL_X509* x509)
{
    WOLFSSL_ENTER("wolfSSL_X509_free");
    ExternalFreeX509(x509);
}


WOLFSSL_X509* wolfSSL_X509_new()
{
    WOLFSSL_X509* x509;

    x509 = (WOLFSSL_X509*)XMALLOC(sizeof(WOLFSSL_X509), NULL,
            DYNAMIC_TYPE_X509);
    if (x509 != NULL) {
        InitX509(x509, 1, NULL);
    }

    return x509;
}
#endif /* NO_CERTS */


/* was do nothing */
/*
void OPENSSL_free(void* buf)
{
    (void)buf;
}
*/


int wolfSSL_OCSP_parse_url(char* url, char** host, char** port, char** path,
                   int* ssl)
{
    (void)url;
    (void)host;
    (void)port;
    (void)path;
    (void)ssl;
    return 0;
}


WOLFSSL_METHOD* wolfSSLv2_client_method(void)
{
    return 0;
}


WOLFSSL_METHOD* wolfSSLv2_server_method(void)
{
    return 0;
}


#ifndef NO_MD4

void wolfSSL_MD4_Init(WOLFSSL_MD4_CTX* md4)
{
    /* make sure we have a big enough buffer */
    typedef char ok[sizeof(md4->buffer) >= sizeof(Md4) ? 1 : -1];
    (void) sizeof(ok);

    WOLFSSL_ENTER("MD4_Init");
    wc_InitMd4((Md4*)md4);
}


void wolfSSL_MD4_Update(WOLFSSL_MD4_CTX* md4, const void* data,
                       unsigned long len)
{
    WOLFSSL_ENTER("MD4_Update");
    wc_Md4Update((Md4*)md4, (const byte*)data, (word32)len);
}


void wolfSSL_MD4_Final(unsigned char* digest, WOLFSSL_MD4_CTX* md4)
{
    WOLFSSL_ENTER("MD4_Final");
    wc_Md4Final((Md4*)md4, digest);
}

#endif /* NO_MD4 */


WOLFSSL_BIO* wolfSSL_BIO_pop(WOLFSSL_BIO* top)
{
    (void)top;
    return 0;
}


int wolfSSL_BIO_pending(WOLFSSL_BIO* bio)
{
    return (int)wolfSSL_BIO_ctrl_pending(bio);
}



WOLFSSL_BIO_METHOD* wolfSSL_BIO_s_mem(void)
{
    static WOLFSSL_BIO_METHOD meth;

    WOLFSSL_ENTER("BIO_s_mem");
    meth.type = WOLFSSL_BIO_MEMORY;

    return &meth;
}


WOLFSSL_BIO_METHOD* wolfSSL_BIO_f_base64(void)
{
    return 0;
}


void wolfSSL_BIO_set_flags(WOLFSSL_BIO* bio, int flags)
{
    (void)bio;
    (void)flags;
}



void wolfSSL_RAND_screen(void)
{

}


const char* wolfSSL_RAND_file_name(char* fname, unsigned long len)
{
    (void)fname;
    (void)len;
    return 0;
}


int wolfSSL_RAND_write_file(const char* fname)
{
    (void)fname;
    return 0;
}


int wolfSSL_RAND_load_file(const char* fname, long len)
{
    (void)fname;
    /* wolfCrypt provides enough entropy internally or will report error */
    if (len == -1)
        return 1024;
    else
        return (int)len;
}


int wolfSSL_RAND_egd(const char* path)
{
    (void)path;
    return 0;
}



WOLFSSL_COMP_METHOD* wolfSSL_COMP_zlib(void)
{
    return 0;
}


WOLFSSL_COMP_METHOD* wolfSSL_COMP_rle(void)
{
    return 0;
}


int wolfSSL_COMP_add_compression_method(int method, void* data)
{
    (void)method;
    (void)data;
    return 0;
}


void wolfSSL_set_dynlock_create_callback(WOLFSSL_dynlock_value* (*f)(
                                                          const char*, int))
{
    (void)f;
}


void wolfSSL_set_dynlock_lock_callback(
             void (*f)(int, WOLFSSL_dynlock_value*, const char*, int))
{
    (void)f;
}


void wolfSSL_set_dynlock_destroy_callback(
                  void (*f)(WOLFSSL_dynlock_value*, const char*, int))
{
    (void)f;
}



const char* wolfSSL_X509_verify_cert_error_string(long err)
{
    return wolfSSL_ERR_reason_error_string(err);
}



int wolfSSL_X509_LOOKUP_add_dir(WOLFSSL_X509_LOOKUP* lookup, const char* dir,
                               long len)
{
    (void)lookup;
    (void)dir;
    (void)len;
    return 0;
}


int wolfSSL_X509_LOOKUP_load_file(WOLFSSL_X509_LOOKUP* lookup,
                                 const char* file, long len)
{
    (void)lookup;
    (void)file;
    (void)len;
    return 0;
}


WOLFSSL_X509_LOOKUP_METHOD* wolfSSL_X509_LOOKUP_hash_dir(void)
{
    return 0;
}


WOLFSSL_X509_LOOKUP_METHOD* wolfSSL_X509_LOOKUP_file(void)
{
    return 0;
}



WOLFSSL_X509_LOOKUP* wolfSSL_X509_STORE_add_lookup(WOLFSSL_X509_STORE* store,
                                               WOLFSSL_X509_LOOKUP_METHOD* m)
{
    (void)store;
    (void)m;
    return 0;
}


#ifndef NO_CERTS
int wolfSSL_i2d_X509_bio(WOLFSSL_BIO* bio, WOLFSSL_X509* x509)
{
    WOLFSSL_ENTER("wolfSSL_i2d_X509_bio");

    if (bio == NULL || x509 == NULL) {
        return SSL_FAILURE;
    }

    if (bio->mem != NULL) {
        XFREE(bio->mem, NULL, DYNAMIC_TYPE_OPENSSL);
    }

    if (x509->derCert != NULL) {
        word32 len = x509->derCert->length;
        byte*  der = x509->derCert->buffer;

        bio->mem = (byte*)XMALLOC(len, NULL, DYNAMIC_TYPE_OPENSSL);
        if (bio->mem == NULL) {
            WOLFSSL_MSG("Memory allocation error");
            return SSL_FAILURE;
        }
        bio->memLen = len;
        XMEMCPY(bio->mem, der, len);
        return SSL_SUCCESS;
    }

    return SSL_FAILURE;
}


WOLFSSL_X509* wolfSSL_d2i_X509_bio(WOLFSSL_BIO* bio, WOLFSSL_X509** x509)
{
    WOLFSSL_X509* localX509 = NULL;
    const unsigned char* mem  = NULL;
    int    ret;
    word32 size;

    WOLFSSL_ENTER("wolfSSL_d2i_X509_bio");

    if (bio == NULL) {
        WOLFSSL_MSG("Bad Function Argument bio is NULL");
        return NULL;
    }

    ret = wolfSSL_BIO_get_mem_data(bio, &mem);
    if (mem == NULL || ret <= 0) {
        WOLFSSL_MSG("Failed to get data from bio struct");
        return NULL;
    }
    size = ret;

    localX509 = wolfSSL_X509_d2i(NULL, mem, size);
    if (localX509 == NULL) {
        return NULL;
    }

    if (x509 != NULL) {
        *x509 = localX509;
    }

    return localX509;
}


#if !defined(NO_ASN) && !defined(NO_PWDBASED)
WC_PKCS12* wolfSSL_d2i_PKCS12_bio(WOLFSSL_BIO* bio, WC_PKCS12** pkcs12)
{
    WC_PKCS12* localPkcs12    = NULL;
    const unsigned char* mem  = NULL;
    int ret;
    word32 size;

    WOLFSSL_ENTER("wolfSSL_d2i_PKCS12_bio");

    if (bio == NULL) {
        WOLFSSL_MSG("Bad Function Argument bio is NULL");
        return NULL;
    }

    localPkcs12 = wc_PKCS12_new();
    if (localPkcs12 == NULL) {
        WOLFSSL_MSG("Memory error");
        return NULL;
    }

    if (pkcs12 != NULL) {
        *pkcs12 = localPkcs12;
    }

    ret = wolfSSL_BIO_get_mem_data(bio, &mem);
    if (mem == NULL || ret <= 0) {
        WOLFSSL_MSG("Failed to get data from bio struct");
        wc_PKCS12_free(localPkcs12);
        if (pkcs12 != NULL) {
            *pkcs12 = NULL;
        }
        return NULL;
    }
    size = ret;

    ret = wc_d2i_PKCS12(mem, size, localPkcs12);
    if (ret <= 0) {
        WOLFSSL_MSG("Failed to get PKCS12 sequence");
        wc_PKCS12_free(localPkcs12);
        if (pkcs12 != NULL) {
            *pkcs12 = NULL;
        }
        return NULL;
    }

    return localPkcs12;
}


/* helper function to get DER buffer from WOLFSSL_EVP_PKEY */
static int wolfSSL_i2d_PrivateKey(WOLFSSL_EVP_PKEY* key, unsigned char** der)
{
    *der = (unsigned char*)key->pkey.ptr;

    return key->pkey_sz;
}



WC_PKCS12* wolfSSL_PKCS12_create(char* pass, char* name,
        WOLFSSL_EVP_PKEY* pkey, WOLFSSL_X509* cert, STACK_OF(WOLFSSL_X509)* ca,
        int keyNID, int certNID, int itt, int macItt, int keyType)
{
    WC_PKCS12*      pkcs12;
    WC_DerCertList* list = NULL;
    word32 passSz = (word32)XSTRLEN(pass);
    byte* keyDer;
    word32 keyDerSz;
    byte* certDer;
    int certDerSz;

    int ret;

    WOLFSSL_ENTER("wolfSSL_PKCS12_create()");

    if (pass == NULL || pkey == NULL || cert == NULL) {
        WOLFSSL_LEAVE("wolfSSL_PKCS12_create()", BAD_FUNC_ARG);
        return NULL;
    }

    if ((ret = wolfSSL_i2d_PrivateKey(pkey, &keyDer)) < 0) {
        WOLFSSL_LEAVE("wolfSSL_PKCS12_create", ret);
        return NULL;
    }
    keyDerSz = ret;

    certDer = (byte*)wolfSSL_X509_get_der(cert, &certDerSz);
    if (certDer == NULL) {
        return NULL;
    }

    if (ca != NULL) {
        WC_DerCertList* cur;
        unsigned long numCerts = ca->num;
        byte* curDer;
        int   curDerSz = 0;
        WOLFSSL_STACK* sk = ca;

        while (numCerts > 0 && sk != NULL) {
            cur = (WC_DerCertList*)XMALLOC(sizeof(WC_DerCertList), NULL,
                    DYNAMIC_TYPE_PKCS);
            if (cur == NULL) {
                wc_FreeCertList(list, NULL);
                return NULL;
            }

            curDer = (byte*)wolfSSL_X509_get_der(sk->data.x509, &curDerSz);
            if (certDer == NULL || curDerSz < 0) {
                wc_FreeCertList(list, NULL);
                return NULL;
            }

            cur->buffer = (byte*)XMALLOC(curDerSz, NULL, DYNAMIC_TYPE_PKCS);
            if (cur->buffer == NULL) {
                wc_FreeCertList(list, NULL);
                return NULL;
            }
            XMEMCPY(cur->buffer, curDer, curDerSz);
            cur->bufferSz = curDerSz;
            cur->next = list;
            list = cur;

            sk = sk->next;
            numCerts--;
        }
    }

    pkcs12 = wc_PKCS12_create(pass, passSz, name, keyDer, keyDerSz,
            certDer, certDerSz, list, keyNID, certNID, itt, macItt,
            keyType, NULL);

    if (ca != NULL) {
        wc_FreeCertList(list, NULL);
    }

    return pkcs12;
}


/* return 1 on success, 0 on failure */
int wolfSSL_PKCS12_parse(WC_PKCS12* pkcs12, const char* psw,
      WOLFSSL_EVP_PKEY** pkey, WOLFSSL_X509** cert, STACK_OF(WOLFSSL_X509)** ca)
{
    DecodedCert DeCert;
    void* heap = NULL;
    int ret;
    byte* certData = NULL;
    word32 certDataSz;
    byte* pk = NULL;
    word32 pkSz;
    WC_DerCertList* certList = NULL;

    WOLFSSL_ENTER("wolfSSL_PKCS12_parse");

    if (pkcs12 == NULL || psw == NULL || pkey == NULL || cert == NULL) {
        WOLFSSL_MSG("Bad argument value");
        return 0;
    }

    heap  = wc_PKCS12_GetHeap(pkcs12);
    *pkey = NULL;
    *cert = NULL;

    if (ca == NULL) {
        ret = wc_PKCS12_parse(pkcs12, psw, &pk, &pkSz, &certData, &certDataSz,
            NULL);
    }
    else {
        *ca = NULL;
        ret = wc_PKCS12_parse(pkcs12, psw, &pk, &pkSz, &certData, &certDataSz,
            &certList);
    }
    if (ret < 0) {
        WOLFSSL_LEAVE("wolfSSL_PKCS12_parse", ret);
        return 0;
    }

    /* Decode cert and place in X509 stack struct */
    if (certList != NULL) {
        WC_DerCertList* current = certList;

        *ca = (STACK_OF(WOLFSSL_X509)*)XMALLOC(sizeof(STACK_OF(WOLFSSL_X509)),
                                               heap, DYNAMIC_TYPE_X509);
        if (*ca == NULL) {
            if (pk != NULL) {
                XFREE(pk, heap, DYNAMIC_TYPE_PKCS);
            }
            if (certData != NULL) {
                XFREE(*cert, heap, DYNAMIC_TYPE_PKCS); *cert = NULL;
            }
            /* Free up WC_DerCertList and move on */
            while (current != NULL) {
                WC_DerCertList* next = current->next;

                XFREE(current->buffer, heap, DYNAMIC_TYPE_PKCS);
                XFREE(current, heap, DYNAMIC_TYPE_PKCS);
                current = next;
            }
            return 0;
        }
        XMEMSET(*ca, 0, sizeof(STACK_OF(WOLFSSL_X509)));

        /* add list of DER certs as X509's to stack */
        while (current != NULL) {
            WC_DerCertList*  toFree = current;
            WOLFSSL_X509* x509;

            x509 = (WOLFSSL_X509*)XMALLOC(sizeof(WOLFSSL_X509), heap,
                                                             DYNAMIC_TYPE_X509);
            InitX509(x509, 1, heap);
            InitDecodedCert(&DeCert, current->buffer, current->bufferSz, heap);
            if (ParseCertRelative(&DeCert, CERT_TYPE, NO_VERIFY, NULL) != 0) {
                WOLFSSL_MSG("Issue with parsing certificate");
                FreeDecodedCert(&DeCert);
                wolfSSL_X509_free(x509);
            }
            else {
                if ((ret = CopyDecodedToX509(x509, &DeCert)) != 0) {
                    WOLFSSL_MSG("Failed to copy decoded cert");
                    FreeDecodedCert(&DeCert);
                    wolfSSL_X509_free(x509);
                    wolfSSL_sk_X509_free(*ca); *ca = NULL;
                    if (pk != NULL) {
                        XFREE(pk, heap, DYNAMIC_TYPE_PKCS);
                    }
                    if (certData != NULL) {
                        XFREE(certData, heap, DYNAMIC_TYPE_PKCS);
                    }
                    /* Free up WC_DerCertList */
                    while (current != NULL) {
                        WC_DerCertList* next = current->next;

                        XFREE(current->buffer, heap, DYNAMIC_TYPE_PKCS);
                        XFREE(current, heap, DYNAMIC_TYPE_PKCS);
                        current = next;
                    }
                    return 0;
                }
                FreeDecodedCert(&DeCert);

                if (wolfSSL_sk_X509_push(*ca, x509) != 1) {
                    WOLFSSL_MSG("Failed to push x509 onto stack");
                    wolfSSL_X509_free(x509);
                    wolfSSL_sk_X509_free(*ca); *ca = NULL;
                    if (pk != NULL) {
                        XFREE(pk, heap, DYNAMIC_TYPE_PKCS);
                    }
                    if (certData != NULL) {
                        XFREE(certData, heap, DYNAMIC_TYPE_PKCS);
                    }

                    /* Free up WC_DerCertList */
                    while (current != NULL) {
                        WC_DerCertList* next = current->next;

                        XFREE(current->buffer, heap, DYNAMIC_TYPE_PKCS);
                        XFREE(current, heap, DYNAMIC_TYPE_PKCS);
                        current = next;
                    }
                    return 0;
                }
            }
            current = current->next;
            XFREE(toFree->buffer, heap, DYNAMIC_TYPE_PKCS);
            XFREE(toFree, heap, DYNAMIC_TYPE_PKCS);
        }
    }


    /* Decode cert and place in X509 struct */
    if (certData != NULL) {
        *cert = (WOLFSSL_X509*)XMALLOC(sizeof(WOLFSSL_X509), heap,
                                                             DYNAMIC_TYPE_X509);
        if (*cert == NULL) {
            if (pk != NULL) {
                XFREE(pk, heap, DYNAMIC_TYPE_PKCS);
            }
            if (ca != NULL) {
                wolfSSL_sk_X509_free(*ca); *ca = NULL;
            }
            XFREE(certData, heap, DYNAMIC_TYPE_PKCS);
            return 0;
        }
        InitX509(*cert, 1, heap);
        InitDecodedCert(&DeCert, certData, certDataSz, heap);
        if (ParseCertRelative(&DeCert, CERT_TYPE, NO_VERIFY, NULL) != 0) {
            WOLFSSL_MSG("Issue with parsing certificate");
        }
        if ((ret = CopyDecodedToX509(*cert, &DeCert)) != 0) {
            WOLFSSL_MSG("Failed to copy decoded cert");
            FreeDecodedCert(&DeCert);
            if (pk != NULL) {
                XFREE(pk, heap, DYNAMIC_TYPE_PKCS);
            }
            if (ca != NULL) {
                wolfSSL_sk_X509_free(*ca); *ca = NULL;
            }
            wolfSSL_X509_free(*cert); *cert = NULL;
            return 0;
        }
        FreeDecodedCert(&DeCert);
        XFREE(certData, heap, DYNAMIC_TYPE_PKCS);
    }


    /* get key type */
    ret = BAD_STATE_E;
    if (pk != NULL) { /* decode key if present */
        *pkey = wolfSSL_PKEY_new_ex(heap);
        if (*pkey == NULL) {
            wolfSSL_X509_free(*cert); *cert = NULL;
            if (ca != NULL) {
                wolfSSL_sk_X509_free(*ca); *ca = NULL;
            }
            XFREE(pk, heap, DYNAMIC_TYPE_PKCS);
            return 0;
        }
        #ifndef NO_RSA
        {
            word32 keyIdx = 0;
            RsaKey key;

            if (wc_InitRsaKey(&key, heap) != 0) {
                ret = BAD_STATE_E;
            }
            else {
                if ((ret = wc_RsaPrivateKeyDecode(pk, &keyIdx, &key, pkSz))
                                                                         == 0) {
                    (*pkey)->type = EVP_PKEY_RSA;
                    (*pkey)->rsa  = wolfSSL_RSA_new();
                    (*pkey)->ownRsa = 1; /* we own RSA */
                    if ((*pkey)->rsa == NULL) {
                        WOLFSSL_MSG("issue creating EVP RSA key");
                        wolfSSL_X509_free(*cert); *cert = NULL;
                        if (ca != NULL) {
                            wolfSSL_sk_X509_free(*ca); *ca = NULL;
                        }
                        wolfSSL_EVP_PKEY_free(*pkey); *pkey = NULL;
                        XFREE(pk, heap, DYNAMIC_TYPE_PKCS);
                        return SSL_FAILURE;
                    }
                    if ((ret = wolfSSL_RSA_LoadDer_ex((*pkey)->rsa, pk, pkSz,
                                    WOLFSSL_RSA_LOAD_PRIVATE)) != SSL_SUCCESS) {
                        WOLFSSL_MSG("issue loading RSA key");
                        wolfSSL_X509_free(*cert); *cert = NULL;
                        if (ca != NULL) {
                            wolfSSL_sk_X509_free(*ca); *ca = NULL;
                        }
                        wolfSSL_EVP_PKEY_free(*pkey); *pkey = NULL;
                        XFREE(pk, heap, DYNAMIC_TYPE_PKCS);
                        return SSL_FAILURE;
                    }

                    WOLFSSL_MSG("Found PKCS12 RSA key");
                    ret = 0; /* set in success state for upcoming ECC check */
                }
                wc_FreeRsaKey(&key);
            }
        }
        #endif /* NO_RSA */

        #ifdef HAVE_ECC
        {
            word32  keyIdx = 0;
            ecc_key key;

            if (ret != 0) { /* if is in fail state check if ECC key */
                if (wc_ecc_init(&key) != 0) {
                    wolfSSL_X509_free(*cert); *cert = NULL;
                    if (ca != NULL) {
                        wolfSSL_sk_X509_free(*ca); *ca = NULL;
                    }
                    wolfSSL_EVP_PKEY_free(*pkey); *pkey = NULL;
                    XFREE(pk, heap, DYNAMIC_TYPE_PKCS);
                    return 0;
                }

                if ((ret = wc_EccPrivateKeyDecode(pk, &keyIdx, &key, pkSz))
                                                                         != 0) {
                    wolfSSL_X509_free(*cert); *cert = NULL;
                    if (ca != NULL) {
                        wolfSSL_sk_X509_free(*ca); *ca = NULL;
                    }
                    wolfSSL_EVP_PKEY_free(*pkey); *pkey = NULL;
                    XFREE(pk, heap, DYNAMIC_TYPE_PKCS);
                    WOLFSSL_MSG("Bad PKCS12 key format");
                    return 0;
                }
                (*pkey)->type = ECDSAk;
                (*pkey)->pkey_curve = key.dp->oidSum;
                wc_ecc_free(&key);
                WOLFSSL_MSG("Found PKCS12 ECC key");
            }
        }
        #else
        if (ret != 0) { /* if is in fail state and no ECC then fail */
            wolfSSL_X509_free(*cert); *cert = NULL;
            if (ca != NULL) {
                wolfSSL_sk_X509_free(*ca); *ca = NULL;
            }
            wolfSSL_EVP_PKEY_free(*pkey); *pkey = NULL;
            XFREE(pk, heap, DYNAMIC_TYPE_PKCS);
            WOLFSSL_MSG("Bad PKCS12 key format");
            return 0;
        }
        #endif /* HAVE_ECC */

        (*pkey)->save_type = 0;
        (*pkey)->pkey_sz   = pkSz;
        (*pkey)->pkey.ptr  = (char*)pk;
    }

    (void)ret;
    (void)ca;

    return 1;
}
#endif /* !defined(NO_ASN) && !defined(NO_PWDBASED) */


/* no-op function. Was initially used for adding encryption algorithms available
 * for PKCS12 */
void wolfSSL_PKCS12_PBE_add(void)
{
    WOLFSSL_ENTER("wolfSSL_PKCS12_PBE_add");
}



WOLFSSL_STACK* wolfSSL_X509_STORE_CTX_get_chain(WOLFSSL_X509_STORE_CTX* ctx)
{
    if (ctx == NULL) {
        return NULL;
    }

    return ctx->chain;
}


int wolfSSL_X509_STORE_add_cert(WOLFSSL_X509_STORE* store, WOLFSSL_X509* x509)
{
    int result = SSL_FATAL_ERROR;

    WOLFSSL_ENTER("wolfSSL_X509_STORE_add_cert");
    if (store != NULL && store->cm != NULL && x509 != NULL
                                                && x509->derCert != NULL) {
        DerBuffer* derCert = NULL;

        result = AllocDer(&derCert, x509->derCert->length,
            x509->derCert->type, NULL);
        if (result == 0) {
            /* AddCA() frees the buffer. */
            XMEMCPY(derCert->buffer,
                            x509->derCert->buffer, x509->derCert->length);
            result = AddCA(store->cm, &derCert, WOLFSSL_USER_CA, 1);
        }
    }

    WOLFSSL_LEAVE("wolfSSL_X509_STORE_add_cert", result);

    if (result != SSL_SUCCESS) {
        result = SSL_FATAL_ERROR;
    }

    return result;
}


WOLFSSL_X509_STORE* wolfSSL_X509_STORE_new(void)
{
    WOLFSSL_X509_STORE* store = NULL;

    store = (WOLFSSL_X509_STORE*)XMALLOC(sizeof(WOLFSSL_X509_STORE), NULL,
                                         DYNAMIC_TYPE_X509_STORE);
    if (store != NULL) {
        store->cm = wolfSSL_CertManagerNew();
        if (store->cm == NULL) {
            XFREE(store, NULL, DYNAMIC_TYPE_X509_STORE);
            store = NULL;
        }
    }

    return store;
}


void wolfSSL_X509_STORE_free(WOLFSSL_X509_STORE* store)
{
    if (store != NULL) {
        if (store->cm != NULL)
        wolfSSL_CertManagerFree(store->cm);
        XFREE(store, NULL, DYNAMIC_TYPE_X509_STORE);
    }
}


int wolfSSL_X509_STORE_set_flags(WOLFSSL_X509_STORE* store, unsigned long flag)
{
    int ret = SSL_SUCCESS;

    WOLFSSL_ENTER("wolfSSL_X509_STORE_set_flags");

    if ((flag & WOLFSSL_CRL_CHECKALL) || (flag & WOLFSSL_CRL_CHECK)) {
        ret = wolfSSL_CertManagerEnableCRL(store->cm, (int)flag);
    }

    (void)store;
    (void)flag;

    return ret;
}


int wolfSSL_X509_STORE_set_default_paths(WOLFSSL_X509_STORE* store)
{
    (void)store;
    return SSL_SUCCESS;
}


int wolfSSL_X509_STORE_get_by_subject(WOLFSSL_X509_STORE_CTX* ctx, int idx,
                            WOLFSSL_X509_NAME* name, WOLFSSL_X509_OBJECT* obj)
{
    (void)ctx;
    (void)idx;
    (void)name;
    (void)obj;
    return 0;
}


WOLFSSL_X509_STORE_CTX* wolfSSL_X509_STORE_CTX_new(void)
{
    WOLFSSL_X509_STORE_CTX* ctx = (WOLFSSL_X509_STORE_CTX*)XMALLOC(
                                    sizeof(WOLFSSL_X509_STORE_CTX), NULL,
                                    DYNAMIC_TYPE_X509_CTX);
    if (ctx != NULL)
        wolfSSL_X509_STORE_CTX_init(ctx, NULL, NULL, NULL);

    return ctx;
}


int wolfSSL_X509_STORE_CTX_init(WOLFSSL_X509_STORE_CTX* ctx,
     WOLFSSL_X509_STORE* store, WOLFSSL_X509* x509, STACK_OF(WOLFSSL_X509)* sk)
{
    (void)sk;
    WOLFSSL_ENTER("wolfSSL_X509_STORE_CTX_init");
    if (ctx != NULL) {
        ctx->store = store;
        ctx->current_cert = x509;
        ctx->chain  = sk;
        ctx->domain = NULL;
        ctx->ex_data = NULL;
        ctx->userCtx = NULL;
        ctx->error = 0;
        ctx->error_depth = 0;
        ctx->discardSessionCerts = 0;
        return SSL_SUCCESS;
    }
    return SSL_FATAL_ERROR;
}


void wolfSSL_X509_STORE_CTX_free(WOLFSSL_X509_STORE_CTX* ctx)
{
    if (ctx != NULL) {
        if (ctx->store != NULL)
            wolfSSL_X509_STORE_free(ctx->store);
        if (ctx->current_cert != NULL)
            wolfSSL_FreeX509(ctx->current_cert);
        if (ctx->chain != NULL)
            wolfSSL_sk_X509_free(ctx->chain);
        XFREE(ctx, NULL, DYNAMIC_TYPE_X509_CTX);
    }
}


void wolfSSL_X509_STORE_CTX_cleanup(WOLFSSL_X509_STORE_CTX* ctx)
{
    (void)ctx;
}


int wolfSSL_X509_verify_cert(WOLFSSL_X509_STORE_CTX* ctx)
{
    if (ctx != NULL && ctx->store != NULL && ctx->store->cm != NULL
         && ctx->current_cert != NULL && ctx->current_cert->derCert != NULL) {
        return wolfSSL_CertManagerVerifyBuffer(ctx->store->cm,
                    ctx->current_cert->derCert->buffer,
                    ctx->current_cert->derCert->length,
                    SSL_FILETYPE_ASN1);
    }
    return SSL_FATAL_ERROR;
}
#endif /* NO_CERTS */


WOLFSSL_ASN1_TIME* wolfSSL_X509_CRL_get_lastUpdate(WOLFSSL_X509_CRL* crl)
{
    (void)crl;
    return 0;
}


WOLFSSL_ASN1_TIME* wolfSSL_X509_CRL_get_nextUpdate(WOLFSSL_X509_CRL* crl)
{
    (void)crl;
    return 0;
}




int wolfSSL_X509_CRL_verify(WOLFSSL_X509_CRL* crl, WOLFSSL_EVP_PKEY* key)
{
    (void)crl;
    (void)key;
    return 0;
}


void wolfSSL_X509_STORE_CTX_set_error(WOLFSSL_X509_STORE_CTX* ctx, int err)
{
    (void)ctx;
    (void)err;
}


void wolfSSL_X509_OBJECT_free_contents(WOLFSSL_X509_OBJECT* obj)
{
    (void)obj;
}


WOLFSSL_EVP_PKEY* wolfSSL_PKEY_new(){
    return wolfSSL_PKEY_new_ex(NULL);
}


WOLFSSL_EVP_PKEY* wolfSSL_PKEY_new_ex(void* heap)
{
    WOLFSSL_EVP_PKEY* pkey;
    int ret;
    WOLFSSL_ENTER("wolfSSL_PKEY_new");
    pkey = (WOLFSSL_EVP_PKEY*)XMALLOC(sizeof(WOLFSSL_EVP_PKEY), heap,
            DYNAMIC_TYPE_PUBLIC_KEY);
    if (pkey != NULL) {
        XMEMSET(pkey, 0, sizeof(WOLFSSL_EVP_PKEY));
        pkey->heap = heap;
        pkey->type = WOLFSSL_EVP_PKEY_DEFAULT;
        ret = wc_InitRng_ex(&(pkey->rng), heap);
        if (ret != 0){
            wolfSSL_EVP_PKEY_free(pkey);
            WOLFSSL_MSG("memory falure");
            return NULL;
        }
    }
    else {
        WOLFSSL_MSG("memory failure");
    }

    return pkey;
}


void wolfSSL_EVP_PKEY_free(WOLFSSL_EVP_PKEY* key)
{
    WOLFSSL_ENTER("wolfSSL_PKEY_free");
    if (key != NULL) {
        wc_FreeRng(&(key->rng));
        if (key->pkey.ptr != NULL)
        {
            XFREE(key->pkey.ptr, key->heap, DYNAMIC_TYPE_PUBLIC_KEY);
        }
        switch(key->type)
        {
            #ifndef NO_RSA
            case EVP_PKEY_RSA:
                if (key->rsa != NULL && key->ownRsa == 1) {
                    wolfSSL_RSA_free(key->rsa);
                }
                break;
            #endif /* NO_RSA */

            #ifdef HAVE_ECC
            case EVP_PKEY_EC:
                if (key->ecc != NULL && key->ownEcc == 1) {
                    wolfSSL_EC_KEY_free(key->ecc);
                }
                break;
            #endif /* HAVE_ECC */

            default:
            break;
        }
        XFREE(key, key->heap, DYNAMIC_TYPE_PUBLIC_KEY);
    }
}


int wolfSSL_X509_cmp_current_time(const WOLFSSL_ASN1_TIME* asnTime)
{
    (void)asnTime;
    return 0;
}


int wolfSSL_sk_X509_REVOKED_num(WOLFSSL_X509_REVOKED* revoked)
{
    (void)revoked;
    return 0;
}



WOLFSSL_X509_REVOKED* wolfSSL_X509_CRL_get_REVOKED(WOLFSSL_X509_CRL* crl)
{
    (void)crl;
    return 0;
}


WOLFSSL_X509_REVOKED* wolfSSL_sk_X509_REVOKED_value(
                                    WOLFSSL_X509_REVOKED* revoked, int value)
{
    (void)revoked;
    (void)value;
    return 0;
}



WOLFSSL_ASN1_INTEGER* wolfSSL_X509_get_serialNumber(WOLFSSL_X509* x509)
{
    (void)x509;
    return 0;
}


int wolfSSL_ASN1_TIME_print(WOLFSSL_BIO* bio, const WOLFSSL_ASN1_TIME* asnTime)
{
    (void)bio;
    (void)asnTime;
    return 0;
}


#if defined(WOLFSSL_MYSQL_COMPATIBLE)
char* wolfSSL_ASN1_TIME_to_string(WOLFSSL_ASN1_TIME* tm, char* buf, int len)
{
    int format;
    int dateLen;
    byte* date = (byte*)tm;

    WOLFSSL_ENTER("wolfSSL_ASN1_TIME_to_string");

    if (tm == NULL || buf == NULL || len < 5) {
        WOLFSSL_MSG("Bad argument");
        return NULL;
    }

    format  = *date; date++;
    dateLen = *date; date++;
    if (dateLen > len) {
        WOLFSSL_MSG("Length of date is longer then buffer");
        return NULL;
    }

    if (!GetTimeString(date, format, buf, len)) {
        return NULL;
    }

    return buf;
}
#endif /* WOLFSSL_MYSQL_COMPATIBLE */


int wolfSSL_ASN1_INTEGER_cmp(const WOLFSSL_ASN1_INTEGER* a,
                            const WOLFSSL_ASN1_INTEGER* b)
{
    (void)a;
    (void)b;
    return 0;
}


long wolfSSL_ASN1_INTEGER_get(const WOLFSSL_ASN1_INTEGER* i)
{
    (void)i;
    return 0;
}


void* wolfSSL_X509_STORE_CTX_get_ex_data(WOLFSSL_X509_STORE_CTX* ctx, int idx)
{
    WOLFSSL_ENTER("wolfSSL_X509_STORE_CTX_get_ex_data");
#if defined(FORTRESS) || defined(HAVE_STUNNEL)
    if (ctx != NULL && idx == 0)
        return ctx->ex_data;
#else
    (void)ctx;
    (void)idx;
#endif
    return 0;
}


int wolfSSL_get_ex_data_X509_STORE_CTX_idx(void)
{
    WOLFSSL_ENTER("wolfSSL_get_ex_data_X509_STORE_CTX_idx");
    return 0;
}


void wolfSSL_CTX_set_info_callback(WOLFSSL_CTX* ctx,
       void (*f)(const WOLFSSL* ssl, int type, int val))
{
    (void)ctx;
    (void)f;
}


unsigned long wolfSSL_ERR_peek_error(void)
{
    return 0;
}


int wolfSSL_ERR_GET_REASON(unsigned long err)
{
    (void)err;
    return 0;
}


char* wolfSSL_alert_type_string_long(int alertID)
{
    (void)alertID;
    return 0;
}


char* wolfSSL_alert_desc_string_long(int alertID)
{
    (void)alertID;
    return 0;
}


char* wolfSSL_state_string_long(const WOLFSSL* ssl)
{
    (void)ssl;
    return 0;
}


int wolfSSL_PEM_def_callback(char* name, int num, int w, void* key)
{
    (void)name;
    (void)num;
    (void)w;
    (void)key;
    return 0;
}


unsigned long wolfSSL_set_options(WOLFSSL* ssl, unsigned long op)
{
    WOLFSSL_ENTER("wolfSSL_set_options");

    if (ssl == NULL) {
        return 0;
    }

    /* if SSL_OP_ALL then turn all bug workarounds one */
    if ((op & SSL_OP_ALL) == SSL_OP_ALL) {
        WOLFSSL_MSG("\tSSL_OP_ALL");

        op |= SSL_OP_MICROSOFT_SESS_ID_BUG;
        op |= SSL_OP_NETSCAPE_CHALLENGE_BUG;
        op |= SSL_OP_NETSCAPE_REUSE_CIPHER_CHANGE_BUG;
        op |= SSL_OP_SSLREF2_REUSE_CERT_TYPE_BUG;
        op |= SSL_OP_MICROSOFT_BIG_SSLV3_BUFFER;
        op |= SSL_OP_MSIE_SSLV2_RSA_PADDING;
        op |= SSL_OP_SSLEAY_080_CLIENT_DH_BUG;
        op |= SSL_OP_TLS_D5_BUG;
        op |= SSL_OP_TLS_BLOCK_PADDING_BUG;
        op |= SSL_OP_TLS_ROLLBACK_BUG;
        op |= SSL_OP_DONT_INSERT_EMPTY_FRAGMENTS;
    }


    /* by default cookie exchange is on with DTLS */
    if ((op & SSL_OP_COOKIE_EXCHANGE) == SSL_OP_COOKIE_EXCHANGE) {
        WOLFSSL_MSG("\tSSL_OP_COOKIE_EXCHANGE : on by default");
    }

    if ((op & SSL_OP_NO_SSLv2) == SSL_OP_NO_SSLv2) {
        WOLFSSL_MSG("\tSSL_OP_NO_SSLv2 : wolfSSL does not support SSLv2");
    }

    if ((op & SSL_OP_NO_SSLv3) == SSL_OP_NO_SSLv3) {
        WOLFSSL_MSG("\tSSL_OP_NO_SSLv3");
    }

    if ((op & SSL_OP_NO_TLSv1) == SSL_OP_NO_TLSv1) {
        WOLFSSL_MSG("\tSSL_OP_NO_TLSv1");
    }

    if ((op & SSL_OP_NO_TLSv1_1) == SSL_OP_NO_TLSv1_1) {
        WOLFSSL_MSG("\tSSL_OP_NO_TLSv1_1");
    }

    if ((op & SSL_OP_NO_TLSv1_2) == SSL_OP_NO_TLSv1_2) {
        WOLFSSL_MSG("\tSSL_OP_NO_TLSv1_2");
    }

    if ((op & SSL_OP_NO_COMPRESSION) == SSL_OP_NO_COMPRESSION) {
    #ifdef HAVE_LIBZ
        WOLFSSL_MSG("SSL_OP_NO_COMPRESSION");
        ssl->options.usingCompression = 0;
    #else
        WOLFSSL_MSG("SSL_OP_NO_COMPRESSION: compression not compiled in");
    #endif
    }

    ssl->options.mask |= op;

    return ssl->options.mask;
}


unsigned long wolfSSL_get_options(const WOLFSSL* ssl)
{
    WOLFSSL_ENTER("wolfSSL_get_options");

    return ssl->options.mask;
}

/*** TBD ***/
WOLFSSL_API long wolfSSL_clear_num_renegotiations(WOLFSSL *s)
{
    (void)s;
    return 0;
}

/*** TBD ***/
WOLFSSL_API long wolfSSL_total_renegotiations(WOLFSSL *s)
{
    (void)s;
    return 0;
}


#ifndef NO_DH
long wolfSSL_set_tmp_dh(WOLFSSL *ssl, WOLFSSL_DH *dh)
{
    int pSz, gSz;
    byte *p, *g;
    int ret = 0;

    WOLFSSL_ENTER("wolfSSL_set_tmp_dh");

    if (!ssl || !dh)
        return BAD_FUNC_ARG;

    /* Get needed size for p and g */
    pSz = wolfSSL_BN_bn2bin(dh->p, NULL);
    gSz = wolfSSL_BN_bn2bin(dh->g, NULL);

    if (pSz <= 0 || gSz <= 0)
        return SSL_FATAL_ERROR;

    p = (byte*)XMALLOC(pSz, ssl->heap, DYNAMIC_TYPE_DH);
    if (!p)
        return MEMORY_E;

    g = (byte*)XMALLOC(gSz, ssl->heap, DYNAMIC_TYPE_DH);
    if (!g) {
        XFREE(p, ssl->heap, DYNAMIC_TYPE_DH);
        return MEMORY_E;
    }

    pSz = wolfSSL_BN_bn2bin(dh->p, p);
    gSz = wolfSSL_BN_bn2bin(dh->g, g);

    if (pSz >= 0 && gSz >= 0) /* Conversion successful */
        ret = wolfSSL_SetTmpDH(ssl, p, pSz, g, gSz);

    XFREE(p, ssl->heap, DYNAMIC_TYPE_DH);
    XFREE(g, ssl->heap, DYNAMIC_TYPE_DH);

    return pSz > 0 && gSz > 0 ? ret : SSL_FATAL_ERROR;
}
#endif /* !NO_DH */


#ifdef HAVE_PK_CALLBACKS
long wolfSSL_set_tlsext_debug_arg(WOLFSSL* ssl, void *arg)
{
    if (ssl == NULL) {
        return SSL_FAILURE;
    }

    ssl->loggingCtx = arg;
    return SSL_SUCCESS;
}
#endif /* HAVE_PK_CALLBACKS */

/*** TBD ***/
WOLFSSL_API long wolfSSL_set_tlsext_status_type(WOLFSSL *s, int type)
{
    (void)s;
    (void)type;
    return 0;
}

/*** TBD ***/
WOLFSSL_API long wolfSSL_get_tlsext_status_exts(WOLFSSL *s, void *arg)
{
    (void)s;
    (void)arg;
    return 0;
}

/*** TBD ***/
WOLFSSL_API long wolfSSL_set_tlsext_status_exts(WOLFSSL *s, void *arg)
{
    (void)s;
    (void)arg;
    return 0;
}

/*** TBD ***/
WOLFSSL_API long wolfSSL_get_tlsext_status_ids(WOLFSSL *s, void *arg)
{
    (void)s;
    (void)arg;
    return 0;
}

/*** TBD ***/
WOLFSSL_API long wolfSSL_set_tlsext_status_ids(WOLFSSL *s, void *arg)
{
    (void)s;
    (void)arg;
    return 0;
}

/*** TBD ***/
WOLFSSL_API long wolfSSL_get_tlsext_status_ocsp_resp(WOLFSSL *s, unsigned char **resp)
{
    (void)s;
    (void)resp;
    return 0;
}

/*** TBD ***/
WOLFSSL_API long wolfSSL_set_tlsext_status_ocsp_resp(WOLFSSL *s, unsigned char *resp, int len)
{
    (void)s;
    (void)resp;
    (void)len;
    return 0;
}


long wolfSSL_get_verify_result(const WOLFSSL *ssl)
{
    if (ssl == NULL) {
        return SSL_FAILURE;
    }

    return ssl->peerVerifyRet;
}


long wolfSSL_CTX_sess_accept(WOLFSSL_CTX* ctx)
{
    (void)ctx;
    return 0;
}

long wolfSSL_CTX_sess_connect(WOLFSSL_CTX* ctx)
{
    (void)ctx;
    return 0;
}


long wolfSSL_CTX_sess_accept_good(WOLFSSL_CTX* ctx)
{
    (void)ctx;
    return 0;
}


long wolfSSL_CTX_sess_connect_good(WOLFSSL_CTX* ctx)
{
    (void)ctx;
    return 0;
}


long wolfSSL_CTX_sess_accept_renegotiate(WOLFSSL_CTX* ctx)
{
    (void)ctx;
    return 0;
}


long wolfSSL_CTX_sess_connect_renegotiate(WOLFSSL_CTX* ctx)
{
    (void)ctx;
    return 0;
}


long wolfSSL_CTX_sess_hits(WOLFSSL_CTX* ctx)
{
    (void)ctx;
    return 0;
}


long wolfSSL_CTX_sess_cb_hits(WOLFSSL_CTX* ctx)
{
    (void)ctx;
    return 0;
}


long wolfSSL_CTX_sess_cache_full(WOLFSSL_CTX* ctx)
{
    (void)ctx;
    return 0;
}


long wolfSSL_CTX_sess_misses(WOLFSSL_CTX* ctx)
{
    (void)ctx;
    return 0;
}


long wolfSSL_CTX_sess_timeouts(WOLFSSL_CTX* ctx)
{
    (void)ctx;
    return 0;
}


long wolfSSL_CTX_sess_number(WOLFSSL_CTX* ctx)
{
    (void)ctx;
    return 0;
}


#ifndef NO_CERTS
long wolfSSL_CTX_add_extra_chain_cert(WOLFSSL_CTX* ctx, WOLFSSL_X509* x509)
{
    byte* chain;
    long  chainSz = 0;
    int derSz;
    const byte* der;
    int ret;

    WOLFSSL_ENTER("wolfSSL_CTX_add_extra_chain_cert");

    if (ctx == NULL || x509 == NULL) {
        WOLFSSL_MSG("Bad Argument");
        return SSL_FAILURE;
    }

    der = wolfSSL_X509_get_der(x509, &derSz);
    if (der == NULL || derSz <= 0) {
        WOLFSSL_MSG("Error getting X509 DER");
        return SSL_FAILURE;
    }

    /* adding cert to existing chain */
    if (ctx->certChain != NULL && ctx->certChain->length > 0) {
        chainSz += ctx->certChain->length;
    }
    chainSz += derSz;

    chain = (byte*)XMALLOC(chainSz, ctx->heap, DYNAMIC_TYPE_TMP_BUFFER);
    if (chain == NULL) {
        WOLFSSL_MSG("Memory Error");
        return SSL_FAILURE;
    }

    if (ctx->certChain != NULL && ctx->certChain->length > 0) {
        XMEMCPY(chain, ctx->certChain->buffer, ctx->certChain->length);
        XMEMCPY(chain + ctx->certChain->length, der, derSz);
    }
    else {
        XMEMCPY(chain, der, derSz);
    }

    ret = ProcessBuffer(ctx, chain, chainSz, SSL_FILETYPE_ASN1, CERT_TYPE,
                        NULL, NULL, 1);
    if (ret != SSL_SUCCESS) {
        WOLFSSL_LEAVE("wolfSSL_CTX_add_extra_chain_cert", ret);
        XFREE(chain, ctx->heap, DYNAMIC_TYPE_TMP_BUFFER);
        return SSL_FAILURE;
    }

    /* on success WOLFSSL_X509 memory is responsibility of ctx */
    wolfSSL_X509_free(x509);
    XFREE(chain, ctx->heap, DYNAMIC_TYPE_TMP_BUFFER);

    return SSL_SUCCESS;
}


long wolfSSL_CTX_set_tlsext_status_arg(WOLFSSL_CTX* ctx, void* arg)
{
    if (ctx == NULL || ctx->cm == NULL) {
        return SSL_FAILURE;
    }

    ctx->cm->ocspIOCtx = arg;
    return SSL_SUCCESS;
}

#endif /* NO_CERTS */


/*** TBC ***/
WOLFSSL_API long wolfSSL_CTX_get_session_cache_mode(WOLFSSL_CTX* ctx)
{
    (void)ctx;
    return 0;
}


int wolfSSL_CTX_get_read_ahead(WOLFSSL_CTX* ctx)
{
    if (ctx == NULL) {
        return SSL_FAILURE;
    }

    return ctx->readAhead;
}


int wolfSSL_CTX_set_read_ahead(WOLFSSL_CTX* ctx, int v)
{
    if (ctx == NULL) {
        return SSL_FAILURE;
    }

    ctx->readAhead = (byte)v;

    return SSL_SUCCESS;
}


long wolfSSL_CTX_set_tlsext_opaque_prf_input_callback_arg(WOLFSSL_CTX* ctx,
        void* arg)
{
    if (ctx == NULL) {
        return SSL_FAILURE;
    }

    ctx->userPRFArg = arg;
    return SSL_SUCCESS;
}


#ifndef NO_DES3
/* 0 on success */
int wolfSSL_DES_set_key(WOLFSSL_const_DES_cblock* myDes,
                                               WOLFSSL_DES_key_schedule* key)
{
#ifdef WOLFSSL_CHECK_DESKEY
    return wolfSSL_DES_set_key_checked(myDes, key);
#else
    wolfSSL_DES_set_key_unchecked(myDes, key);
    return 0;
#endif
}



/* return true in fail case (1) */
static int DES_check(word32 mask, word32 mask2, unsigned char* key)
{
    word32 value[2];

    /* sanity check on length made in wolfSSL_DES_set_key_checked */
    value[0] = mask;
    value[1] = mask2;
    return (XMEMCMP(value, key, sizeof(value)) == 0)? 1: 0;
}


/* check that the key is odd parity and is not a weak key
 * returns -1 if parity is wrong, -2 if weak/null key and 0 on success */
int wolfSSL_DES_set_key_checked(WOLFSSL_const_DES_cblock* myDes,
                                               WOLFSSL_DES_key_schedule* key)
{
    if (myDes == NULL || key == NULL) {
        WOLFSSL_MSG("Bad argument passed to wolfSSL_DES_set_key_checked");
        return -2;
    }
    else {
        word32 i;
        word32 sz = sizeof(WOLFSSL_DES_key_schedule);

        /* sanity check before call to DES_check */
        if (sz != (sizeof(word32) * 2)) {
            WOLFSSL_MSG("Unexpected WOLFSSL_DES_key_schedule size");
            return -2;
        }

        /* check odd parity */
        for (i = 0; i < sz; i++) {
            unsigned char c = *((unsigned char*)myDes + i);
            if (((c & 0x01) ^
                ((c >> 1) & 0x01) ^
                ((c >> 2) & 0x01) ^
                ((c >> 3) & 0x01) ^
                ((c >> 4) & 0x01) ^
                ((c >> 5) & 0x01) ^
                ((c >> 6) & 0x01) ^
                ((c >> 7) & 0x01)) != 1) {
                WOLFSSL_MSG("Odd parity test fail");
                return -1;
            }
        }

        if (wolfSSL_DES_is_weak_key(myDes) == 1) {
            WOLFSSL_MSG("Weak key found");
            return -2;
        }

        /* passed tests, now copy over key */
        XMEMCPY(key, myDes, sizeof(WOLFSSL_const_DES_cblock));

        return 0;
    }
}


/* check is not weak. Weak key list from Nist "Recommendation for the Triple
 * Data Encryption Algorithm (TDEA) Block Cipher"
 *
 * returns 1 if is weak 0 if not
 */
int wolfSSL_DES_is_weak_key(WOLFSSL_const_DES_cblock* key)
{
    word32 mask, mask2;

    WOLFSSL_ENTER("wolfSSL_DES_is_weak_key");

    if (key == NULL) {
        WOLFSSL_MSG("NULL key passed in");
        return 1;
    }

    mask = 0x01010101; mask2 = 0x01010101;
    if (DES_check(mask, mask2, *key)) {
        WOLFSSL_MSG("Weak key found");
        return 1;
    }

    mask = 0xFEFEFEFE; mask2 = 0xFEFEFEFE;
    if (DES_check(mask, mask2, *key)) {
        WOLFSSL_MSG("Weak key found");
        return 1;
    }

    mask = 0xE0E0E0E0; mask2 = 0xF1F1F1F1;
    if (DES_check(mask, mask2, *key)) {
        WOLFSSL_MSG("Weak key found");
        return 1;
    }

    mask = 0x1F1F1F1F; mask2 = 0x0E0E0E0E;
    if (DES_check(mask, mask2, *key)) {
        WOLFSSL_MSG("Weak key found");
        return 1;
    }

    /* semi-weak *key check (list from same Nist paper) */
    mask  = 0x011F011F; mask2 = 0x010E010E;
    if (DES_check(mask, mask2, *key) ||
       DES_check(ByteReverseWord32(mask), ByteReverseWord32(mask2), *key)) {
        WOLFSSL_MSG("Weak key found");
        return 1;
    }

    mask  = 0x01E001E0; mask2 = 0x01F101F1;
    if (DES_check(mask, mask2, *key) ||
       DES_check(ByteReverseWord32(mask), ByteReverseWord32(mask2), *key)) {
        WOLFSSL_MSG("Weak key found");
        return 1;
    }

    mask  = 0x01FE01FE; mask2 = 0x01FE01FE;
    if (DES_check(mask, mask2, *key) ||
       DES_check(ByteReverseWord32(mask), ByteReverseWord32(mask2), *key)) {
        WOLFSSL_MSG("Weak key found");
        return 1;
    }

    mask  = 0x1FE01FE0; mask2 = 0x0EF10EF1;
    if (DES_check(mask, mask2, *key) ||
       DES_check(ByteReverseWord32(mask), ByteReverseWord32(mask2), *key)) {
        WOLFSSL_MSG("Weak key found");
        return 1;
    }

    mask  = 0x1FFE1FFE; mask2 = 0x0EFE0EFE;
    if (DES_check(mask, mask2, *key) ||
       DES_check(ByteReverseWord32(mask), ByteReverseWord32(mask2), *key)) {
        WOLFSSL_MSG("Weak key found");
        return 1;
    }

    return 0;
}


void wolfSSL_DES_set_key_unchecked(WOLFSSL_const_DES_cblock* myDes,
                                               WOLFSSL_DES_key_schedule* key)
{
    if (myDes != NULL && key != NULL) {
        XMEMCPY(key, myDes, sizeof(WOLFSSL_const_DES_cblock));
    }
}


void wolfSSL_DES_set_odd_parity(WOLFSSL_DES_cblock* myDes)
{
    word32 i;
    word32 sz = sizeof(WOLFSSL_DES_cblock);

    WOLFSSL_ENTER("wolfSSL_DES_set_odd_parity");

    for (i = 0; i < sz; i++) {
        unsigned char c = *((unsigned char*)myDes + i);
        if (((c & 0x01) ^
            ((c >> 1) & 0x01) ^
            ((c >> 2) & 0x01) ^
            ((c >> 3) & 0x01) ^
            ((c >> 4) & 0x01) ^
            ((c >> 5) & 0x01) ^
            ((c >> 6) & 0x01) ^
            ((c >> 7) & 0x01)) != 1) {
            WOLFSSL_MSG("Setting odd parity bit");
            *((unsigned char*)myDes + i) = *((unsigned char*)myDes + i) | 0x80;
        }
    }
}


void wolfSSL_DES_ecb_encrypt(WOLFSSL_DES_cblock* desa,
             WOLFSSL_DES_cblock* desb, WOLFSSL_DES_key_schedule* key, int len)
{
    (void)desa;
    (void)desb;
    (void)key;
    (void)len;
    WOLFSSL_STUB("wolfSSL_DES_ecb_encrypt");
}

#endif /* NO_DES3 */

#ifndef NO_RC4
/* Set the key state for Arc4 structure.
 *
 * key  Arc4 structure to use
 * len  length of data buffer
 * data initial state to set Arc4 structure
 */
void wolfSSL_RC4_set_key(WOLFSSL_RC4_KEY* key, int len,
        const unsigned char* data)
{
    typedef char rc4_test[sizeof(WOLFSSL_RC4_KEY) >= sizeof(Arc4) ? 1 : -1];
    (void)sizeof(rc4_test);

    WOLFSSL_ENTER("wolfSSL_RC4_set_key");

    if (key == NULL || len < 0) {
        WOLFSSL_MSG("bad argument passed in");
        return;
    }

    XMEMSET(key, 0, sizeof(WOLFSSL_RC4_KEY));
    wc_Arc4SetKey((Arc4*)key, data, (word32)len);
}


/* Encrypt/decrypt with Arc4 structure.
 *
 * len length of buffer to encrypt/decrypt (in/out)
 * in  buffer to encrypt/decrypt
 * out results of encryption/decryption
 */
void wolfSSL_RC4(WOLFSSL_RC4_KEY* key, size_t len,
        const unsigned char* in, unsigned char* out)
{
    WOLFSSL_ENTER("wolfSSL_RC4");

    if (key == NULL || in == NULL || out == NULL) {
        WOLFSSL_MSG("Bad argument passed in");
        return;
    }

    wc_Arc4Process((Arc4*)key, out, in, (word32)len);
}
#endif /* NO_RC4 */

#ifndef NO_AES

#ifdef WOLFSSL_AES_DIRECT
/* AES encrypt direct, it is expected to be blocks of AES_BLOCK_SIZE for input.
 *
 * input  Data to encrypt
 * output Encrypted data after done
 * key    AES key to use for encryption
 */
void wolfSSL_AES_encrypt(const unsigned char* input, unsigned char* output,
        AES_KEY *key)
{
    WOLFSSL_ENTER("wolfSSL_AES_encrypt");

    if (input == NULL || output == NULL || key == NULL) {
        WOLFSSL_MSG("Null argument passed in");
        return;
    }

    wc_AesEncryptDirect((Aes*)key, output, input);
}


/* AES decrypt direct, it is expected to be blocks of AES_BLOCK_SIZE for input.
 *
 * input  Data to decrypt
 * output Decrypted data after done
 * key    AES key to use for encryption
 */
void wolfSSL_AES_decrypt(const unsigned char* input, unsigned char* output,
        AES_KEY *key)
{
    WOLFSSL_ENTER("wolfSSL_AES_decrypt");

    if (input == NULL || output == NULL || key == NULL) {
        WOLFSSL_MSG("Null argument passed in");
        return;
    }

    wc_AesDecryptDirect((Aes*)key, output, input);
}
#endif /* WOLFSSL_AES_DIRECT */

/* Setup of an AES key to use for encryption.
 *
 * key  key in bytes to use for encryption
 * bits size of key in bits
 * aes  AES structure to initialize
 */
void wolfSSL_AES_set_encrypt_key(const unsigned char *key, const int bits,
        AES_KEY *aes)
{
    typedef char aes_test[sizeof(AES_KEY) >= sizeof(Aes) ? 1 : -1];
    (void)sizeof(aes_test);

    WOLFSSL_ENTER("wolfSSL_AES_set_encrypt_key");

    if (key == NULL || aes == NULL) {
        WOLFSSL_MSG("Null argument passed in");
        return;
    }

    XMEMSET(aes, 0, sizeof(AES_KEY));
    if (wc_AesSetKey((Aes*)aes, key, ((bits)/8), NULL, AES_ENCRYPTION) != 0) {
        WOLFSSL_MSG("Error in setting AES key");
    }
}


/* Setup of an AES key to use for decryption.
 *
 * key  key in bytes to use for decryption
 * bits size of key in bits
 * aes  AES structure to initialize
 */
void wolfSSL_AES_set_decrypt_key(const unsigned char *key, const int bits,
        AES_KEY *aes)
{
    typedef char aes_test[sizeof(AES_KEY) >= sizeof(Aes) ? 1 : -1];
    (void)sizeof(aes_test);

    WOLFSSL_ENTER("wolfSSL_AES_set_decrypt_key");

    if (key == NULL || aes == NULL) {
        WOLFSSL_MSG("Null argument passed in");
        return;
    }

    XMEMSET(aes, 0, sizeof(AES_KEY));
    if (wc_AesSetKey((Aes*)aes, key, ((bits)/8), NULL, AES_DECRYPTION) != 0) {
        WOLFSSL_MSG("Error in setting AES key");
    }
}


/* Encrypt data using key and iv passed in. iv gets updated to most recent iv
 * state after encryptiond/decryption.
 *
 * in  buffer to encrypt/decyrpt
 * out buffer to hold result of encryption/decryption
 * len length of input buffer
 * key AES structure to use with encryption/decryption
 * iv  iv to use with operation
 * enc AES_ENCRPT for encryption and AES_DECRYPT for decryption
 */
void wolfSSL_AES_cbc_encrypt(const unsigned char *in, unsigned char* out,
        size_t len, AES_KEY *key, unsigned char* iv, const int enc)
{
    Aes* aes;

    WOLFSSL_ENTER("wolfSSL_AES_cbc_encrypt");

    if (key == NULL || in == NULL || out == NULL || iv == NULL) {
        WOLFSSL_MSG("Error, Null argument passed in");
        return;
    }

    aes = (Aes*)key;
    if (wc_AesSetIV(aes, (const byte*)iv) != 0) {
        WOLFSSL_MSG("Error with setting iv");
        return;
    }

    if (enc == AES_ENCRYPT) {
        if (wc_AesCbcEncrypt(aes, out, in, (word32)len) != 0) {
            WOLFSSL_MSG("Error with AES CBC encrypt");
        }
    }
    else {
        if (wc_AesCbcDecrypt(aes, out, in, (word32)len) != 0) {
            WOLFSSL_MSG("Error with AES CBC decrypt");
        }
    }

    /* to be compatible copy iv to iv buffer after completing operation */
    XMEMCPY(iv, (byte*)(aes->reg), AES_BLOCK_SIZE);
}


/* @TODO
 * STUB function
 */
void wolfSSL_AES_cfb128_encrypt(const unsigned char *in, unsigned char* out,
        size_t len, AES_KEY *key, unsigned char* iv, int* num,
        const int enc)
{
    (void)in;
    (void)out;
    (void)len;
    (void)key;
    (void)iv;
    (void)num;
    (void)enc;
    WOLFSSL_STUB("wolfSSL_AES_cfb128_encrypt");
}
#endif /* NO_AES */

int wolfSSL_BIO_printf(WOLFSSL_BIO* bio, const char* format, ...)
{
    (void)bio;
    (void)format;
    return 0;
}


int wolfSSL_ASN1_UTCTIME_print(WOLFSSL_BIO* bio, const WOLFSSL_ASN1_UTCTIME* a)
{
    (void)bio;
    (void)a;
    return 0;
}


int  wolfSSL_sk_num(WOLFSSL_X509_REVOKED* rev)
{
    (void)rev;
    return 0;
}


void* wolfSSL_sk_value(WOLFSSL_X509_REVOKED* rev, int i)
{
    (void)rev;
    (void)i;
    return 0;
}


/* stunnel 4.28 needs */
void wolfSSL_CTX_sess_set_get_cb(WOLFSSL_CTX* ctx,
                    WOLFSSL_SESSION*(*f)(WOLFSSL*, unsigned char*, int, int*))
{
    (void)ctx;
    (void)f;
}


void wolfSSL_CTX_sess_set_new_cb(WOLFSSL_CTX* ctx,
                             int (*f)(WOLFSSL*, WOLFSSL_SESSION*))
{
    (void)ctx;
    (void)f;
}


void wolfSSL_CTX_sess_set_remove_cb(WOLFSSL_CTX* ctx, void (*f)(WOLFSSL_CTX*,
                                                        WOLFSSL_SESSION*))
{
    (void)ctx;
    (void)f;
}


int wolfSSL_i2d_SSL_SESSION(WOLFSSL_SESSION* sess, unsigned char** p)
{
    (void)sess;
    (void)p;
    return sizeof(WOLFSSL_SESSION);
}


WOLFSSL_SESSION* wolfSSL_d2i_SSL_SESSION(WOLFSSL_SESSION** sess,
                                const unsigned char** p, long i)
{
    (void)p;
    (void)i;
    if (sess)
        return *sess;
    return NULL;
}


long wolfSSL_SESSION_get_timeout(const WOLFSSL_SESSION* sess)
{
    WOLFSSL_ENTER("wolfSSL_SESSION_get_timeout");
    return sess->timeout;
}


long wolfSSL_SESSION_get_time(const WOLFSSL_SESSION* sess)
{
    WOLFSSL_ENTER("wolfSSL_SESSION_get_time");
    return sess->bornOn;
}


#endif /* OPENSSL_EXTRA */


#ifdef KEEP_PEER_CERT
char*  wolfSSL_X509_get_subjectCN(WOLFSSL_X509* x509)
{
    if (x509 == NULL)
        return NULL;

    return x509->subjectCN;
}
#endif /* KEEP_PEER_CERT */

#ifdef OPENSSL_EXTRA

#ifdef FORTRESS
int wolfSSL_cmp_peer_cert_to_file(WOLFSSL* ssl, const char *fname)
{
    int ret = SSL_FATAL_ERROR;

    WOLFSSL_ENTER("wolfSSL_cmp_peer_cert_to_file");
    if (ssl != NULL && fname != NULL)
    {
    #ifdef WOLFSSL_SMALL_STACK
        EncryptedInfo* info = NULL;
        byte           staticBuffer[1]; /* force heap usage */
    #else
        EncryptedInfo  info[1];
        byte           staticBuffer[FILE_BUFFER_SIZE];
    #endif
        byte*          myBuffer  = staticBuffer;
        int            dynamic   = 0;
        XFILE          file      = XBADFILE;
        long           sz        = 0;
        int            eccKey    = 0;
        WOLFSSL_CTX*   ctx       = ssl->ctx;
        WOLFSSL_X509*  peer_cert = &ssl->peerCert;
        DerBuffer*     fileDer = NULL;

        file = XFOPEN(fname, "rb");
        if (file == XBADFILE)
            return SSL_BAD_FILE;

        XFSEEK(file, 0, XSEEK_END);
        sz = XFTELL(file);
        XREWIND(file);

        if (sz > (long)sizeof(staticBuffer)) {
            WOLFSSL_MSG("Getting dynamic buffer");
            myBuffer = (byte*)XMALLOC(sz, ctx->heap, DYNAMIC_TYPE_FILE);
            dynamic = 1;
        }

    #ifdef WOLFSSL_SMALL_STACK
        info = (EncryptedInfo*)XMALLOC(sizeof(EncryptedInfo), NULL,
                                                   DYNAMIC_TYPE_TMP_BUFFER);
        if (info == NULL)
            ret = MEMORY_E;
        else
    #endif
        {
            info->set = 0;
            info->ctx = ctx;
            info->consumed = 0;

            if ((myBuffer != NULL) &&
                (sz > 0) &&
                (XFREAD(myBuffer, sz, 1, file) > 0) &&
                (PemToDer(myBuffer, sz, CERT_TYPE,
                          &fileDer, ctx->heap, info, &eccKey) == 0) &&
                (fileDer->length != 0) &&
                (fileDer->length == peer_cert->derCert->length) &&
                (XMEMCMP(peer_cert->derCert->buffer, fileDer->buffer,
                                                    fileDer->length) == 0))
            {
                ret = 0;
            }

        #ifdef WOLFSSL_SMALL_STACK
            XFREE(info, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        #endif
        }

        FreeDer(&fileDer);

        if (dynamic)
            XFREE(myBuffer, ctx->heap, DYNAMIC_TYPE_FILE);

        XFCLOSE(file);
    }

    return ret;
}
#endif


static WC_RNG globalRNG;
static int initGlobalRNG = 0;

/* SSL_SUCCESS on ok */
int wolfSSL_RAND_seed(const void* seed, int len)
{

    WOLFSSL_MSG("wolfSSL_RAND_seed");

    (void)seed;
    (void)len;

    if (initGlobalRNG == 0) {
        if (wc_InitRng(&globalRNG) < 0) {
            WOLFSSL_MSG("wolfSSL Init Global RNG failed");
            return 0;
        }
        initGlobalRNG = 1;
    }

    return SSL_SUCCESS;
}


void wolfSSL_RAND_Cleanup(void)
{
    WOLFSSL_ENTER("wolfSSL_RAND_Cleanup()");

    if (initGlobalRNG != 0) {
        wc_FreeRng(&globalRNG);
        initGlobalRNG = 0;
    }
}


int wolfSSL_RAND_pseudo_bytes(unsigned char* buf, int num)
{
    return wolfSSL_RAND_bytes(buf, num);
}


/* SSL_SUCCESS on ok */
int wolfSSL_RAND_bytes(unsigned char* buf, int num)
{
    int     ret = 0;
    int     initTmpRng = 0;
    WC_RNG* rng = NULL;
#ifdef WOLFSSL_SMALL_STACK
    WC_RNG* tmpRNG = NULL;
#else
    WC_RNG  tmpRNG[1];
#endif

    WOLFSSL_ENTER("wolfSSL_RAND_bytes");

#ifdef WOLFSSL_SMALL_STACK
    tmpRNG = (WC_RNG*)XMALLOC(sizeof(WC_RNG), NULL, DYNAMIC_TYPE_TMP_BUFFER);
    if (tmpRNG == NULL)
        return ret;
#endif

    if (wc_InitRng(tmpRNG) == 0) {
        rng = tmpRNG;
        initTmpRng = 1;
    }
    else if (initGlobalRNG)
        rng = &globalRNG;

    if (rng) {
        if (wc_RNG_GenerateBlock(rng, buf, num) != 0)
            WOLFSSL_MSG("Bad wc_RNG_GenerateBlock");
        else
            ret = SSL_SUCCESS;
    }

    if (initTmpRng)
        wc_FreeRng(tmpRNG);

#ifdef WOLFSSL_SMALL_STACK
    XFREE(tmpRNG, NULL, DYNAMIC_TYPE_TMP_BUFFER);
#endif

    return ret;
}

WOLFSSL_BN_CTX* wolfSSL_BN_CTX_new(void)
{
    static int ctx;  /* wolfcrypt doesn't now need ctx */

    WOLFSSL_MSG("wolfSSL_BN_CTX_new");

    return (WOLFSSL_BN_CTX*)&ctx;
}

void wolfSSL_BN_CTX_init(WOLFSSL_BN_CTX* ctx)
{
    (void)ctx;
    WOLFSSL_MSG("wolfSSL_BN_CTX_init");
}


void wolfSSL_BN_CTX_free(WOLFSSL_BN_CTX* ctx)
{
    (void)ctx;
    WOLFSSL_MSG("wolfSSL_BN_CTX_free");

    /* do free since static ctx that does nothing */
}


static void InitwolfSSL_BigNum(WOLFSSL_BIGNUM* bn)
{
    WOLFSSL_MSG("InitwolfSSL_BigNum");
    if (bn) {
        XMEMSET(bn, 0, sizeof(WOLFSSL_BIGNUM));
        bn->neg      = 0;
        bn->internal = NULL;
    }
}


WOLFSSL_BIGNUM* wolfSSL_BN_new(void)
{
    WOLFSSL_BIGNUM* external;
    mp_int*        mpi;

    WOLFSSL_MSG("wolfSSL_BN_new");

    mpi = (mp_int*) XMALLOC(sizeof(mp_int), NULL, DYNAMIC_TYPE_BIGINT);
    if (mpi == NULL) {
        WOLFSSL_MSG("wolfSSL_BN_new malloc mpi failure");
        return NULL;
    }

    external = (WOLFSSL_BIGNUM*) XMALLOC(sizeof(WOLFSSL_BIGNUM), NULL,
                                        DYNAMIC_TYPE_BIGINT);
    if (external == NULL) {
        WOLFSSL_MSG("wolfSSL_BN_new malloc WOLFSSL_BIGNUM failure");
        XFREE(mpi, NULL, DYNAMIC_TYPE_BIGINT);
        return NULL;
    }

    InitwolfSSL_BigNum(external);
    external->internal = mpi;
    if (mp_init(mpi) != MP_OKAY) {
        wolfSSL_BN_free(external);
        return NULL;
    }

    return external;
}


void wolfSSL_BN_free(WOLFSSL_BIGNUM* bn)
{
    WOLFSSL_MSG("wolfSSL_BN_free");
    if (bn) {
        if (bn->internal) {
            mp_clear((mp_int*)bn->internal);
            XFREE(bn->internal, NULL, DYNAMIC_TYPE_BIGINT);
            bn->internal = NULL;
        }
        XFREE(bn, NULL, DYNAMIC_TYPE_BIGINT);
        bn = NULL;
    }
}


void wolfSSL_BN_clear_free(WOLFSSL_BIGNUM* bn)
{
    WOLFSSL_MSG("wolfSSL_BN_clear_free");

    wolfSSL_BN_free(bn);
}


/* SSL_SUCCESS on ok */
int wolfSSL_BN_sub(WOLFSSL_BIGNUM* r, const WOLFSSL_BIGNUM* a,
                  const WOLFSSL_BIGNUM* b)
{
    WOLFSSL_MSG("wolfSSL_BN_sub");

    if (r == NULL || a == NULL || b == NULL)
        return 0;

    if (mp_sub((mp_int*)a->internal,(mp_int*)b->internal,
               (mp_int*)r->internal) == MP_OKAY)
        return SSL_SUCCESS;

    WOLFSSL_MSG("wolfSSL_BN_sub mp_sub failed");
    return 0;
}


/* SSL_SUCCESS on ok */
int wolfSSL_BN_mod(WOLFSSL_BIGNUM* r, const WOLFSSL_BIGNUM* a,
                  const WOLFSSL_BIGNUM* b, const WOLFSSL_BN_CTX* c)
{
    (void)c;
    WOLFSSL_MSG("wolfSSL_BN_mod");

    if (r == NULL || a == NULL || b == NULL)
        return 0;

    if (mp_mod((mp_int*)a->internal,(mp_int*)b->internal,
               (mp_int*)r->internal) == MP_OKAY)
        return SSL_SUCCESS;

    WOLFSSL_MSG("wolfSSL_BN_mod mp_mod failed");
    return 0;
}


/* r = (a^p) % m */
int wolfSSL_BN_mod_exp(WOLFSSL_BIGNUM *r, const WOLFSSL_BIGNUM *a,
      const WOLFSSL_BIGNUM *p, const WOLFSSL_BIGNUM *m, WOLFSSL_BN_CTX *ctx)
{
    int ret;

    WOLFSSL_ENTER("wolfSSL_BN_mod_exp");

    (void) ctx;
    if (r == NULL || a == NULL || p == NULL || m == NULL) {
        WOLFSSL_MSG("Bad Argument");
        return SSL_FAILURE;
    }

    if ((ret = mp_exptmod((mp_int*)a->internal,(mp_int*)p->internal,
               (mp_int*)m->internal, (mp_int*)r->internal)) == MP_OKAY) {
        return SSL_SUCCESS;
    }

    WOLFSSL_LEAVE("wolfSSL_BN_mod_exp", ret);
    return SSL_FAILURE;
}

const WOLFSSL_BIGNUM* wolfSSL_BN_value_one(void)
{
    static WOLFSSL_BIGNUM* bn_one = NULL;

    WOLFSSL_MSG("wolfSSL_BN_value_one");

    if (bn_one == NULL) {
        bn_one = wolfSSL_BN_new();
        if (bn_one)
            mp_set_int((mp_int*)bn_one->internal, 1);
    }

    return bn_one;
}

/* return compliant with OpenSSL
 *   size of BIGNUM in bytes, 0 if error */
int wolfSSL_BN_num_bytes(const WOLFSSL_BIGNUM* bn)
{
    WOLFSSL_ENTER("wolfSSL_BN_num_bytes");

    if (bn == NULL || bn->internal == NULL)
        return SSL_FAILURE;

    return mp_unsigned_bin_size((mp_int*)bn->internal);
}

/* return compliant with OpenSSL
 *   size of BIGNUM in bits, 0 if error */
int wolfSSL_BN_num_bits(const WOLFSSL_BIGNUM* bn)
{
    WOLFSSL_ENTER("wolfSSL_BN_num_bits");

    if (bn == NULL || bn->internal == NULL)
        return SSL_FAILURE;

    return mp_count_bits((mp_int*)bn->internal);
}

/* return compliant with OpenSSL
 *   1 if BIGNUM is zero, 0 else */
int wolfSSL_BN_is_zero(const WOLFSSL_BIGNUM* bn)
{
    WOLFSSL_MSG("wolfSSL_BN_is_zero");

    if (bn == NULL || bn->internal == NULL)
        return SSL_FAILURE;

    if (mp_iszero((mp_int*)bn->internal) == MP_YES)
        return SSL_SUCCESS;

    return SSL_FAILURE;
}

/* return compliant with OpenSSL
 *   1 if BIGNUM is one, 0 else */
int wolfSSL_BN_is_one(const WOLFSSL_BIGNUM* bn)
{
    WOLFSSL_MSG("wolfSSL_BN_is_one");

    if (bn == NULL || bn->internal == NULL)
        return SSL_FAILURE;

    if (mp_cmp_d((mp_int*)bn->internal, 1) == MP_EQ)
        return SSL_SUCCESS;

    return SSL_FAILURE;
}

/* return compliant with OpenSSL
 *   1 if BIGNUM is odd, 0 else */
int wolfSSL_BN_is_odd(const WOLFSSL_BIGNUM* bn)
{
    WOLFSSL_MSG("wolfSSL_BN_is_odd");

    if (bn == NULL || bn->internal == NULL)
        return SSL_FAILURE;

    if (mp_isodd((mp_int*)bn->internal) == MP_YES)
        return SSL_SUCCESS;

    return SSL_FAILURE;
}

/* return compliant with OpenSSL
 *   -1 if a < b, 0 if a == b and 1 if a > b
 */
int wolfSSL_BN_cmp(const WOLFSSL_BIGNUM* a, const WOLFSSL_BIGNUM* b)
{
    int ret;

    WOLFSSL_MSG("wolfSSL_BN_cmp");

    if (a == NULL || a->internal == NULL || b == NULL || b->internal == NULL)
        return SSL_FATAL_ERROR;

    ret = mp_cmp((mp_int*)a->internal, (mp_int*)b->internal);

    return (ret == MP_EQ ? 0 : (ret == MP_GT ? 1 : -1));
}

/* return compliant with OpenSSL
 *   length of BIGNUM in bytes, -1 if error */
int wolfSSL_BN_bn2bin(const WOLFSSL_BIGNUM* bn, unsigned char* r)
{
    WOLFSSL_MSG("wolfSSL_BN_bn2bin");

    if (bn == NULL || bn->internal == NULL) {
        WOLFSSL_MSG("NULL bn error");
        return SSL_FATAL_ERROR;
    }

    if (r == NULL)
        return mp_unsigned_bin_size((mp_int*)bn->internal);

    if (mp_to_unsigned_bin((mp_int*)bn->internal, r) != MP_OKAY) {
        WOLFSSL_MSG("mp_to_unsigned_bin error");
        return SSL_FATAL_ERROR;
    }

    return mp_unsigned_bin_size((mp_int*)bn->internal);
}


WOLFSSL_BIGNUM* wolfSSL_BN_bin2bn(const unsigned char* str, int len,
                            WOLFSSL_BIGNUM* ret)
{
    int weOwn = 0;

    WOLFSSL_MSG("wolfSSL_BN_bin2bn");

    /* if ret is null create a BN */
    if (ret == NULL) {
        ret = wolfSSL_BN_new();
        weOwn = 1;
        if (ret == NULL)
            return NULL;
    }

    /* check ret and ret->internal then read in value */
    if (ret && ret->internal) {
        if (mp_read_unsigned_bin((mp_int*)ret->internal, str, len) != 0) {
            WOLFSSL_MSG("mp_read_unsigned_bin failure");
            if (weOwn)
                wolfSSL_BN_free(ret);
            return NULL;
        }
    }

    return ret;
}

/* return compliant with OpenSSL
 *   1 if success, 0 if error */
int wolfSSL_mask_bits(WOLFSSL_BIGNUM* bn, int n)
{
    (void)bn;
    (void)n;
    WOLFSSL_MSG("wolfSSL_BN_mask_bits");

    return SSL_FAILURE;
}


/* SSL_SUCCESS on ok */
int wolfSSL_BN_rand(WOLFSSL_BIGNUM* bn, int bits, int top, int bottom)
{
    int           ret    = 0;
    int           len    = bits / 8;
    int           initTmpRng = 0;
    WC_RNG*       rng    = NULL;
#ifdef WOLFSSL_SMALL_STACK
    WC_RNG*       tmpRNG = NULL;
    byte*         buff   = NULL;
#else
    WC_RNG        tmpRNG[1];
    byte          buff[1024];
#endif

    (void)top;
    (void)bottom;
    WOLFSSL_MSG("wolfSSL_BN_rand");

    if (bits % 8)
        len++;

#ifdef WOLFSSL_SMALL_STACK
    buff   = (byte*)XMALLOC(1024,        NULL, DYNAMIC_TYPE_TMP_BUFFER);
    tmpRNG = (WC_RNG*) XMALLOC(sizeof(WC_RNG), NULL, DYNAMIC_TYPE_TMP_BUFFER);
    if (buff == NULL || tmpRNG == NULL) {
        XFREE(buff,   NULL, DYNAMIC_TYPE_TMP_BUFFER);
        XFREE(tmpRNG, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        return ret;
    }
#endif

    if (bn == NULL || bn->internal == NULL)
        WOLFSSL_MSG("Bad function arguments");
    else if (wc_InitRng(tmpRNG) == 0) {
        rng = tmpRNG;
        initTmpRng = 1;
    }
    else if (initGlobalRNG)
        rng = &globalRNG;

    if (rng) {
        if (wc_RNG_GenerateBlock(rng, buff, len) != 0)
            WOLFSSL_MSG("Bad wc_RNG_GenerateBlock");
        else {
            buff[0]     |= 0x80 | 0x40;
            buff[len-1] |= 0x01;

            if (mp_read_unsigned_bin((mp_int*)bn->internal,buff,len) != MP_OKAY)
                WOLFSSL_MSG("mp read bin failed");
            else
                ret = SSL_SUCCESS;
        }
    }

    if (initTmpRng)
        wc_FreeRng(tmpRNG);

#ifdef WOLFSSL_SMALL_STACK
    XFREE(buff,   NULL, DYNAMIC_TYPE_TMP_BUFFER);
    XFREE(tmpRNG, NULL, DYNAMIC_TYPE_TMP_BUFFER);
#endif

    return ret;
}


/* SSL_SUCCESS on ok
 * code is same as wolfSSL_BN_rand except for how top and bottom is handled.
 * top -1 then leave most sig bit alone
 * top 0 then most sig is set to 1
 * top is 1 then first two most sig bits are 1
 *
 * bottom is hot then odd number */
int wolfSSL_BN_pseudo_rand(WOLFSSL_BIGNUM* bn, int bits, int top, int bottom)
{
    int           ret    = 0;
    int           len    = bits / 8;
    int           initTmpRng = 0;
    WC_RNG*       rng    = NULL;
#ifdef WOLFSSL_SMALL_STACK
    WC_RNG*       tmpRNG = NULL;
    byte*         buff   = NULL;
#else
    WC_RNG        tmpRNG[1];
    byte          buff[1024];
#endif

    WOLFSSL_MSG("wolfSSL_BN_rand");

    if (bits % 8)
        len++;

#ifdef WOLFSSL_SMALL_STACK
    buff   = (byte*)XMALLOC(1024,        NULL, DYNAMIC_TYPE_TMP_BUFFER);
    tmpRNG = (WC_RNG*) XMALLOC(sizeof(WC_RNG), NULL, DYNAMIC_TYPE_TMP_BUFFER);
    if (buff == NULL || tmpRNG == NULL) {
        XFREE(buff,   NULL, DYNAMIC_TYPE_TMP_BUFFER);
        XFREE(tmpRNG, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        return ret;
    }
#endif

    if (bn == NULL || bn->internal == NULL)
        WOLFSSL_MSG("Bad function arguments");
    else if (wc_InitRng(tmpRNG) == 0) {
        rng = tmpRNG;
        initTmpRng = 1;
    }
    else if (initGlobalRNG)
        rng = &globalRNG;

    if (rng) {
        if (wc_RNG_GenerateBlock(rng, buff, len) != 0)
            WOLFSSL_MSG("Bad wc_RNG_GenerateBlock");
        else {
            switch (top) {
                case -1:
                    break;

                case 0:
                    buff[0] |= 0x80;
                    break;

                case 1:
                    buff[0] |= 0x80 | 0x40;
                    break;
            }

            if (bottom == 1) {
                buff[len-1] |= 0x01;
            }

            if (mp_read_unsigned_bin((mp_int*)bn->internal,buff,len) != MP_OKAY)
                WOLFSSL_MSG("mp read bin failed");
            else
                ret = SSL_SUCCESS;
        }
    }

    if (initTmpRng)
        wc_FreeRng(tmpRNG);

#ifdef WOLFSSL_SMALL_STACK
    XFREE(buff,   NULL, DYNAMIC_TYPE_TMP_BUFFER);
    XFREE(tmpRNG, NULL, DYNAMIC_TYPE_TMP_BUFFER);
#endif

    return ret;
}

/* return code compliant with OpenSSL :
 *   1 if bit set, 0 else
 */
int wolfSSL_BN_is_bit_set(const WOLFSSL_BIGNUM* bn, int n)
{
    if (bn == NULL || bn->internal == NULL) {
        WOLFSSL_MSG("bn NULL error");
        return SSL_FAILURE;
    }

    if (n > DIGIT_BIT) {
        WOLFSSL_MSG("input bit count too large");
        return SSL_FAILURE;
    }

    return mp_is_bit_set((mp_int*)bn->internal, (mp_digit)n);
}

/* return code compliant with OpenSSL :
 *   1 if success, 0 else
 */
int wolfSSL_BN_set_bit(WOLFSSL_BIGNUM* bn, int n)
{
    if (bn == NULL || bn->internal == NULL) {
        WOLFSSL_MSG("bn NULL error");
        return SSL_FAILURE;
    }

    if (mp_set_bit((mp_int*)bn->internal, n) != MP_OKAY) {
        WOLFSSL_MSG("mp_set_int error");
        return SSL_FAILURE;
    }

    return SSL_SUCCESS;
}


/* SSL_SUCCESS on ok */
int wolfSSL_BN_hex2bn(WOLFSSL_BIGNUM** bn, const char* str)
{
    int     ret     = 0;
    word32  decSz   = 1024;
#ifdef WOLFSSL_SMALL_STACK
    byte*   decoded = NULL;
#else
    byte    decoded[1024];
#endif

    WOLFSSL_MSG("wolfSSL_BN_hex2bn");

#ifdef WOLFSSL_SMALL_STACK
    decoded = (byte*)XMALLOC(decSz, NULL, DYNAMIC_TYPE_TMP_BUFFER);
    if (decoded == NULL)
        return ret;
#endif

    if (str == NULL)
        WOLFSSL_MSG("Bad function argument");
    else if (Base16_Decode((byte*)str, (int)XSTRLEN(str), decoded, &decSz) < 0)
        WOLFSSL_MSG("Bad Base16_Decode error");
    else if (bn == NULL)
        ret = decSz;
    else {
        if (*bn == NULL)
            *bn = wolfSSL_BN_new();

        if (*bn == NULL)
            WOLFSSL_MSG("BN new failed");
        else if (wolfSSL_BN_bin2bn(decoded, decSz, *bn) == NULL)
            WOLFSSL_MSG("Bad bin2bn error");
        else
            ret = SSL_SUCCESS;
    }

#ifdef WOLFSSL_SMALL_STACK
    XFREE(decoded, NULL, DYNAMIC_TYPE_TMP_BUFFER);
#endif

    return ret;
}


WOLFSSL_BIGNUM* wolfSSL_BN_dup(const WOLFSSL_BIGNUM* bn)
{
    WOLFSSL_BIGNUM* ret;

    WOLFSSL_MSG("wolfSSL_BN_dup");

    if (bn == NULL || bn->internal == NULL) {
        WOLFSSL_MSG("bn NULL error");
        return NULL;
    }

    ret = wolfSSL_BN_new();
    if (ret == NULL) {
        WOLFSSL_MSG("bn new error");
        return NULL;
    }

    if (mp_copy((mp_int*)bn->internal, (mp_int*)ret->internal) != MP_OKAY) {
        WOLFSSL_MSG("mp_copy error");
        wolfSSL_BN_free(ret);
        return NULL;
    }

    ret->neg = bn->neg;

    return ret;
}


WOLFSSL_BIGNUM* wolfSSL_BN_copy(WOLFSSL_BIGNUM* r, const WOLFSSL_BIGNUM* bn)
{
    WOLFSSL_MSG("wolfSSL_BN_copy");

    if (mp_copy((mp_int*)bn->internal, (mp_int*)r->internal) != MP_OKAY) {
        WOLFSSL_MSG("mp_copy error");
        return NULL;
    }

    r->neg = bn->neg;

    return r;
}

/* return code compliant with OpenSSL :
 *   1 if success, 0 else
 */
int wolfSSL_BN_set_word(WOLFSSL_BIGNUM* bn, WOLFSSL_BN_ULONG w)
{
    WOLFSSL_MSG("wolfSSL_BN_set_word");

    if (mp_set_int((mp_int*)bn->internal, w) != MP_OKAY) {
        WOLFSSL_MSG("mp_init_set_int error");
        return SSL_FAILURE;
    }

    return SSL_SUCCESS;
}

/* return code compliant with OpenSSL :
 *   number length in decimal if success, 0 if error
 */
int wolfSSL_BN_dec2bn(WOLFSSL_BIGNUM** bn, const char* str)
{
    (void)bn;
    (void)str;

    WOLFSSL_MSG("wolfSSL_BN_dec2bn");

    return SSL_FAILURE;
}


#if defined(WOLFSSL_KEY_GEN) || defined(HAVE_COMP_KEY)
char *wolfSSL_BN_bn2dec(const WOLFSSL_BIGNUM *bn)
{
    int len = 0;
    char *buf;

    WOLFSSL_MSG("wolfSSL_BN_bn2dec");

    if (bn == NULL || bn->internal == NULL) {
        WOLFSSL_MSG("bn NULL error");
        return NULL;
    }

    if (mp_radix_size((mp_int*)bn->internal, 10, &len) != MP_OKAY) {
        WOLFSSL_MSG("mp_radix_size failure");
        return NULL;
    }

    buf = (char*) XMALLOC(len, NULL, DYNAMIC_TYPE_ECC);
    if (buf == NULL) {
        WOLFSSL_MSG("wolfSSL_BN_bn2hex malloc buffer failure");
        return NULL;
    }

    if (mp_toradix((mp_int*)bn->internal, buf, 10) != MP_OKAY) {
        XFREE(buf, NULL, DYNAMIC_TYPE_ECC);
        return NULL;
    }

    return buf;
}
#else
char* wolfSSL_BN_bn2dec(const WOLFSSL_BIGNUM* bn)
{
    (void)bn;

    WOLFSSL_MSG("wolfSSL_BN_bn2dec");

    return NULL;
}
#endif /* defined(WOLFSSL_KEY_GEN) || defined(HAVE_COMP_KEY) */

/* return code compliant with OpenSSL :
 *   1 if success, 0 else
 */
int wolfSSL_BN_lshift(WOLFSSL_BIGNUM *r, const WOLFSSL_BIGNUM *bn, int n)
{
    WOLFSSL_MSG("wolfSSL_BN_lshift");

    if (r == NULL || r->internal == NULL || bn == NULL || bn->internal == NULL){
        WOLFSSL_MSG("bn NULL error");
        return SSL_FAILURE;
    }

    if (mp_mul_2d((mp_int*)bn->internal, n, (mp_int*)r->internal) != MP_OKAY) {
        WOLFSSL_MSG("mp_mul_2d error");
        return SSL_FAILURE;
    }

    return SSL_SUCCESS;
}

/* return code compliant with OpenSSL :
 *   1 if success, 0 else
 */
int wolfSSL_BN_rshift(WOLFSSL_BIGNUM *r, const WOLFSSL_BIGNUM *bn, int n)
{
    WOLFSSL_MSG("wolfSSL_BN_rshift");

    if (r == NULL || r->internal == NULL || bn == NULL || bn->internal == NULL){
        WOLFSSL_MSG("bn NULL error");
        return SSL_FAILURE;
    }

    if (mp_div_2d((mp_int*)bn->internal, n,
                  (mp_int*)r->internal, NULL) != MP_OKAY) {
        WOLFSSL_MSG("mp_mul_2d error");
        return SSL_FAILURE;
    }

    return SSL_SUCCESS;
}

/* return code compliant with OpenSSL :
 *   1 if success, 0 else
 */
int wolfSSL_BN_add_word(WOLFSSL_BIGNUM *bn, WOLFSSL_BN_ULONG w)
{
    WOLFSSL_MSG("wolfSSL_BN_add_word");

    if (bn == NULL || bn->internal == NULL) {
        WOLFSSL_MSG("bn NULL error");
        return SSL_FAILURE;
    }

    if (mp_add_d((mp_int*)bn->internal, w, (mp_int*)bn->internal) != MP_OKAY) {
        WOLFSSL_MSG("mp_add_d error");
        return SSL_FAILURE;
    }

    return SSL_SUCCESS;
}

/* return code compliant with OpenSSL :
 *   1 if success, 0 else
 */
int wolfSSL_BN_add(WOLFSSL_BIGNUM *r, WOLFSSL_BIGNUM *a, WOLFSSL_BIGNUM *b)
{
    WOLFSSL_MSG("wolfSSL_BN_add");

    if (r == NULL || r->internal == NULL || a == NULL || a->internal == NULL ||
        b == NULL || b->internal == NULL) {
        WOLFSSL_MSG("bn NULL error");
        return SSL_FAILURE;
    }

    if (mp_add((mp_int*)a->internal, (mp_int*)b->internal,
               (mp_int*)r->internal) != MP_OKAY) {
        WOLFSSL_MSG("mp_add_d error");
        return SSL_FAILURE;
    }

    return SSL_SUCCESS;
}

#ifdef WOLFSSL_KEY_GEN

/* return code compliant with OpenSSL :
 *   1 if prime, 0 if not, -1 if error
 */
int wolfSSL_BN_is_prime_ex(const WOLFSSL_BIGNUM *bn, int nbchecks,
                           WOLFSSL_BN_CTX *ctx, WOLFSSL_BN_GENCB *cb)
{
    int res;

    (void)ctx;
    (void)cb;

    WOLFSSL_MSG("wolfSSL_BN_is_prime_ex");

    if (bn == NULL || bn->internal == NULL) {
        WOLFSSL_MSG("bn NULL error");
        return SSL_FATAL_ERROR;
    }

    if (mp_prime_is_prime((mp_int*)bn->internal, nbchecks, &res) != MP_OKAY) {
        WOLFSSL_MSG("mp_prime_is_prime error");
        return SSL_FATAL_ERROR;
    }

    if (res != MP_YES) {
        WOLFSSL_MSG("mp_prime_is_prime not prime");
        return SSL_FAILURE;
    }

    return SSL_SUCCESS;
}

/* return code compliant with OpenSSL :
 *   (bn mod w) if success, -1 if error
 */
WOLFSSL_BN_ULONG wolfSSL_BN_mod_word(const WOLFSSL_BIGNUM *bn,
                                     WOLFSSL_BN_ULONG w)
{
    WOLFSSL_BN_ULONG ret = 0;

    WOLFSSL_MSG("wolfSSL_BN_mod_word");

    if (bn == NULL || bn->internal == NULL) {
        WOLFSSL_MSG("bn NULL error");
        return (WOLFSSL_BN_ULONG)SSL_FATAL_ERROR;
    }

    if (mp_mod_d((mp_int*)bn->internal, w, &ret) != MP_OKAY) {
        WOLFSSL_MSG("mp_add_d error");
        return (WOLFSSL_BN_ULONG)SSL_FATAL_ERROR;
    }

    return ret;
}
#endif /* #ifdef WOLFSSL_KEY_GEN */

#if defined(WOLFSSL_KEY_GEN) || defined(HAVE_COMP_KEY)
char *wolfSSL_BN_bn2hex(const WOLFSSL_BIGNUM *bn)
{
    int len = 0;
    char *buf;

    WOLFSSL_MSG("wolfSSL_BN_bn2hex");

    if (bn == NULL || bn->internal == NULL) {
        WOLFSSL_MSG("bn NULL error");
        return NULL;
    }

    if (mp_radix_size((mp_int*)bn->internal, 16, &len) != MP_OKAY) {
        WOLFSSL_MSG("mp_radix_size failure");
        return NULL;
    }

    buf = (char*) XMALLOC(len, NULL, DYNAMIC_TYPE_ECC);
    if (buf == NULL) {
        WOLFSSL_MSG("wolfSSL_BN_bn2hex malloc buffer failure");
        return NULL;
    }

    if (mp_toradix((mp_int*)bn->internal, buf, 16) != MP_OKAY) {
        XFREE(buf, NULL, DYNAMIC_TYPE_ECC);
        return NULL;
    }

    return buf;
}

#ifndef NO_FILESYSTEM
/* return code compliant with OpenSSL :
 *   1 if success, 0 if error
 */
int wolfSSL_BN_print_fp(FILE *fp, const WOLFSSL_BIGNUM *bn)
{
    char *buf;

    WOLFSSL_MSG("wolfSSL_BN_print_fp");

    if (fp == NULL || bn == NULL || bn->internal == NULL) {
        WOLFSSL_MSG("bn NULL error");
        return SSL_FAILURE;
    }

    buf = wolfSSL_BN_bn2hex(bn);
    if (buf == NULL) {
        WOLFSSL_MSG("wolfSSL_BN_bn2hex failure");
        return SSL_FAILURE;
    }

    XFREE(buf, NULL, DYNAMIC_TYPE_ECC);

    return SSL_SUCCESS;
}
#endif /* !defined(NO_FILESYSTEM) */

#else /* defined(WOLFSSL_KEY_GEN) || defined(HAVE_COMP_KEY) */

char *wolfSSL_BN_bn2hex(const WOLFSSL_BIGNUM *bn)
{
    (void)bn;

    WOLFSSL_MSG("wolfSSL_BN_bn2hex need WOLFSSL_KEY_GEN or HAVE_COMP_KEY");

    return (char*)"";
}

#ifndef NO_FILESYSTEM
/* return code compliant with OpenSSL :
 *   1 if success, 0 if error
 */
int wolfSSL_BN_print_fp(FILE *fp, const WOLFSSL_BIGNUM *bn)
{
    (void)fp;
    (void)bn;

    WOLFSSL_MSG("wolfSSL_BN_print_fp not implemented");

    return SSL_SUCCESS;
}
#endif /* !defined(NO_FILESYSTEM) */

#endif /* defined(WOLFSSL_KEY_GEN) || defined(HAVE_COMP_KEY) */

WOLFSSL_BIGNUM *wolfSSL_BN_CTX_get(WOLFSSL_BN_CTX *ctx)
{
    /* ctx is not used, return new Bignum */
    (void)ctx;

    WOLFSSL_ENTER("wolfSSL_BN_CTX_get");

    return wolfSSL_BN_new();
}

void wolfSSL_BN_CTX_start(WOLFSSL_BN_CTX *ctx)
{
    (void)ctx;

    WOLFSSL_ENTER("wolfSSL_BN_CTX_start");
    WOLFSSL_MSG("wolfSSL_BN_CTX_start TBD");
}

#ifndef NO_DH

static void InitwolfSSL_DH(WOLFSSL_DH* dh)
{
    if (dh) {
        dh->p        = NULL;
        dh->g        = NULL;
        dh->q        = NULL;
        dh->pub_key  = NULL;
        dh->priv_key = NULL;
        dh->internal = NULL;
        dh->inSet    = 0;
        dh->exSet    = 0;
    }
}


WOLFSSL_DH* wolfSSL_DH_new(void)
{
    WOLFSSL_DH* external;
    DhKey*     key;

    WOLFSSL_MSG("wolfSSL_DH_new");

    key = (DhKey*) XMALLOC(sizeof(DhKey), NULL, DYNAMIC_TYPE_DH);
    if (key == NULL) {
        WOLFSSL_MSG("wolfSSL_DH_new malloc DhKey failure");
        return NULL;
    }

    external = (WOLFSSL_DH*) XMALLOC(sizeof(WOLFSSL_DH), NULL,
                                    DYNAMIC_TYPE_DH);
    if (external == NULL) {
        WOLFSSL_MSG("wolfSSL_DH_new malloc WOLFSSL_DH failure");
        XFREE(key, NULL, DYNAMIC_TYPE_DH);
        return NULL;
    }

    InitwolfSSL_DH(external);
    if (wc_InitDhKey(key) != 0) {
        WOLFSSL_MSG("wolfSSL_DH_new InitDhKey failure");
        XFREE(key, NULL, DYNAMIC_TYPE_DH);
        return NULL;
    }
    external->internal = key;

    return external;
}


void wolfSSL_DH_free(WOLFSSL_DH* dh)
{
    WOLFSSL_MSG("wolfSSL_DH_free");

    if (dh) {
        if (dh->internal) {
            wc_FreeDhKey((DhKey*)dh->internal);
            XFREE(dh->internal, NULL, DYNAMIC_TYPE_DH);
            dh->internal = NULL;
        }
        wolfSSL_BN_free(dh->priv_key);
        wolfSSL_BN_free(dh->pub_key);
        wolfSSL_BN_free(dh->g);
        wolfSSL_BN_free(dh->p);
        wolfSSL_BN_free(dh->q);
        InitwolfSSL_DH(dh);  /* set back to NULLs for safety */

        XFREE(dh, NULL, DYNAMIC_TYPE_DH);
    }
}


static int SetDhInternal(WOLFSSL_DH* dh)
{
    int            ret = SSL_FATAL_ERROR;
    int            pSz = 1024;
    int            gSz = 1024;
#ifdef WOLFSSL_SMALL_STACK
    unsigned char* p   = NULL;
    unsigned char* g   = NULL;
#else
    unsigned char  p[1024];
    unsigned char  g[1024];
#endif

    WOLFSSL_ENTER("SetDhInternal");

    if (dh == NULL || dh->p == NULL || dh->g == NULL)
        WOLFSSL_MSG("Bad function arguments");
    else if (wolfSSL_BN_bn2bin(dh->p, NULL) > pSz)
        WOLFSSL_MSG("Bad p internal size");
    else if (wolfSSL_BN_bn2bin(dh->g, NULL) > gSz)
        WOLFSSL_MSG("Bad g internal size");
    else {
    #ifdef WOLFSSL_SMALL_STACK
        p = (unsigned char*)XMALLOC(pSz, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        g = (unsigned char*)XMALLOC(gSz, NULL, DYNAMIC_TYPE_TMP_BUFFER);

        if (p == NULL || g == NULL) {
            XFREE(p, NULL, DYNAMIC_TYPE_TMP_BUFFER);
            XFREE(g, NULL, DYNAMIC_TYPE_TMP_BUFFER);
            return ret;
        }
    #endif

        pSz = wolfSSL_BN_bn2bin(dh->p, p);
        gSz = wolfSSL_BN_bn2bin(dh->g, g);

        if (pSz <= 0 || gSz <= 0)
            WOLFSSL_MSG("Bad BN2bin set");
        else if (wc_DhSetKey((DhKey*)dh->internal, p, pSz, g, gSz) < 0)
            WOLFSSL_MSG("Bad DH SetKey");
        else {
            dh->inSet = 1;
            ret = SSL_SUCCESS;
        }

    #ifdef WOLFSSL_SMALL_STACK
        XFREE(p, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        XFREE(g, NULL, DYNAMIC_TYPE_TMP_BUFFER);
    #endif
    }


    return ret;
}

/* return code compliant with OpenSSL :
 *   DH prime size in bytes if success, 0 if error
 */
int wolfSSL_DH_size(WOLFSSL_DH* dh)
{
    WOLFSSL_MSG("wolfSSL_DH_size");

    if (dh == NULL)
        return SSL_FATAL_ERROR;

    return wolfSSL_BN_num_bytes(dh->p);
}


/* return code compliant with OpenSSL :
 *   1 if success, 0 if error
 */
int wolfSSL_DH_generate_key(WOLFSSL_DH* dh)
{
    int            ret    = SSL_FAILURE;
    word32         pubSz  = 768;
    word32         privSz = 768;
    int            initTmpRng = 0;
    WC_RNG*        rng    = NULL;
#ifdef WOLFSSL_SMALL_STACK
    unsigned char* pub    = NULL;
    unsigned char* priv   = NULL;
    WC_RNG*        tmpRNG = NULL;
#else
    unsigned char  pub [768];
    unsigned char  priv[768];
    WC_RNG         tmpRNG[1];
#endif

    WOLFSSL_MSG("wolfSSL_DH_generate_key");

#ifdef WOLFSSL_SMALL_STACK
    tmpRNG = (WC_RNG*)XMALLOC(sizeof(WC_RNG), NULL, DYNAMIC_TYPE_TMP_BUFFER);
    pub    = (unsigned char*)XMALLOC(pubSz,   NULL, DYNAMIC_TYPE_TMP_BUFFER);
    priv   = (unsigned char*)XMALLOC(privSz,  NULL, DYNAMIC_TYPE_TMP_BUFFER);

    if (tmpRNG == NULL || pub == NULL || priv == NULL) {
        XFREE(tmpRNG, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        XFREE(pub,    NULL, DYNAMIC_TYPE_TMP_BUFFER);
        XFREE(priv,   NULL, DYNAMIC_TYPE_TMP_BUFFER);
        return ret;
    }
#endif

    if (dh == NULL || dh->p == NULL || dh->g == NULL)
        WOLFSSL_MSG("Bad function arguments");
    else if (dh->inSet == 0 && SetDhInternal(dh) != SSL_SUCCESS)
            WOLFSSL_MSG("Bad DH set internal");
    else if (wc_InitRng(tmpRNG) == 0) {
        rng = tmpRNG;
        initTmpRng = 1;
    }
    else {
        WOLFSSL_MSG("Bad RNG Init, trying global");
        if (initGlobalRNG == 0)
            WOLFSSL_MSG("Global RNG no Init");
        else
            rng = &globalRNG;
    }

    if (rng) {
       if (wc_DhGenerateKeyPair((DhKey*)dh->internal, rng, priv, &privSz,
                                                               pub, &pubSz) < 0)
            WOLFSSL_MSG("Bad wc_DhGenerateKeyPair");
       else {
            if (dh->pub_key)
                wolfSSL_BN_free(dh->pub_key);

            dh->pub_key = wolfSSL_BN_new();
            if (dh->pub_key == NULL) {
                WOLFSSL_MSG("Bad DH new pub");
            }
            if (dh->priv_key)
                wolfSSL_BN_free(dh->priv_key);

            dh->priv_key = wolfSSL_BN_new();

            if (dh->priv_key == NULL) {
                WOLFSSL_MSG("Bad DH new priv");
            }

            if (dh->pub_key && dh->priv_key) {
               if (wolfSSL_BN_bin2bn(pub, pubSz, dh->pub_key) == NULL)
                   WOLFSSL_MSG("Bad DH bn2bin error pub");
               else if (wolfSSL_BN_bin2bn(priv, privSz, dh->priv_key) == NULL)
                   WOLFSSL_MSG("Bad DH bn2bin error priv");
               else
                   ret = SSL_SUCCESS;
            }
        }
    }

    if (initTmpRng)
        wc_FreeRng(tmpRNG);

#ifdef WOLFSSL_SMALL_STACK
    XFREE(tmpRNG, NULL, DYNAMIC_TYPE_TMP_BUFFER);
    XFREE(pub,    NULL, DYNAMIC_TYPE_TMP_BUFFER);
    XFREE(priv,   NULL, DYNAMIC_TYPE_TMP_BUFFER);
#endif

    return ret;
}


/* return code compliant with OpenSSL :
 *   size of shared secret if success, -1 if error
 */
int wolfSSL_DH_compute_key(unsigned char* key, WOLFSSL_BIGNUM* otherPub,
                          WOLFSSL_DH* dh)
{
    int            ret    = SSL_FATAL_ERROR;
    word32         keySz  = 0;
    word32         pubSz  = 1024;
    word32         privSz = 1024;
#ifdef WOLFSSL_SMALL_STACK
    unsigned char* pub    = NULL;
    unsigned char* priv   = NULL;
#else
    unsigned char  pub [1024];
    unsigned char  priv[1024];
#endif

    WOLFSSL_MSG("wolfSSL_DH_compute_key");

#ifdef WOLFSSL_SMALL_STACK
    pub = (unsigned char*)XMALLOC(pubSz, NULL, DYNAMIC_TYPE_TMP_BUFFER);
    if (pub == NULL)
        return ret;

    priv = (unsigned char*)XMALLOC(privSz, NULL, DYNAMIC_TYPE_TMP_BUFFER);
    if (priv == NULL) {
        XFREE(pub, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        return ret;
    }
#endif

    if (dh == NULL || dh->priv_key == NULL || otherPub == NULL)
        WOLFSSL_MSG("Bad function arguments");
    else if ((keySz = (word32)DH_size(dh)) == 0)
        WOLFSSL_MSG("Bad DH_size");
    else if (wolfSSL_BN_bn2bin(dh->priv_key, NULL) > (int)privSz)
        WOLFSSL_MSG("Bad priv internal size");
    else if (wolfSSL_BN_bn2bin(otherPub, NULL) > (int)pubSz)
        WOLFSSL_MSG("Bad otherPub size");
    else {
        privSz = wolfSSL_BN_bn2bin(dh->priv_key, priv);
        pubSz  = wolfSSL_BN_bn2bin(otherPub, pub);

        if (privSz <= 0 || pubSz <= 0)
            WOLFSSL_MSG("Bad BN2bin set");
        else if (wc_DhAgree((DhKey*)dh->internal, key, &keySz,
                            priv, privSz, pub, pubSz) < 0)
            WOLFSSL_MSG("wc_DhAgree failed");
        else
            ret = (int)keySz;
    }

#ifdef WOLFSSL_SMALL_STACK
    XFREE(pub,  NULL, DYNAMIC_TYPE_TMP_BUFFER);
    XFREE(priv, NULL, DYNAMIC_TYPE_TMP_BUFFER);
#endif

    return ret;
}
#endif /* NO_DH */


#ifndef NO_DSA
static void InitwolfSSL_DSA(WOLFSSL_DSA* dsa)
{
    if (dsa) {
        dsa->p        = NULL;
        dsa->q        = NULL;
        dsa->g        = NULL;
        dsa->pub_key  = NULL;
        dsa->priv_key = NULL;
        dsa->internal = NULL;
        dsa->inSet    = 0;
        dsa->exSet    = 0;
    }
}


WOLFSSL_DSA* wolfSSL_DSA_new(void)
{
    WOLFSSL_DSA* external;
    DsaKey*     key;

    WOLFSSL_MSG("wolfSSL_DSA_new");

    key = (DsaKey*) XMALLOC(sizeof(DsaKey), NULL, DYNAMIC_TYPE_DSA);
    if (key == NULL) {
        WOLFSSL_MSG("wolfSSL_DSA_new malloc DsaKey failure");
        return NULL;
    }

    external = (WOLFSSL_DSA*) XMALLOC(sizeof(WOLFSSL_DSA), NULL,
                                    DYNAMIC_TYPE_DSA);
    if (external == NULL) {
        WOLFSSL_MSG("wolfSSL_DSA_new malloc WOLFSSL_DSA failure");
        XFREE(key, NULL, DYNAMIC_TYPE_DSA);
        return NULL;
    }

    InitwolfSSL_DSA(external);
    if (wc_InitDsaKey(key) != 0) {
        WOLFSSL_MSG("wolfSSL_DSA_new InitDsaKey failure");
        XFREE(key, NULL, DYNAMIC_TYPE_DSA);
        return NULL;
    }
    external->internal = key;

    return external;
}


void wolfSSL_DSA_free(WOLFSSL_DSA* dsa)
{
    WOLFSSL_MSG("wolfSSL_DSA_free");

    if (dsa) {
        if (dsa->internal) {
            FreeDsaKey((DsaKey*)dsa->internal);
            XFREE(dsa->internal, NULL, DYNAMIC_TYPE_DSA);
            dsa->internal = NULL;
        }
        wolfSSL_BN_free(dsa->priv_key);
        wolfSSL_BN_free(dsa->pub_key);
        wolfSSL_BN_free(dsa->g);
        wolfSSL_BN_free(dsa->q);
        wolfSSL_BN_free(dsa->p);
        InitwolfSSL_DSA(dsa);  /* set back to NULLs for safety */

        XFREE(dsa, NULL, DYNAMIC_TYPE_DSA);
        dsa = NULL;
    }
}

#endif /* NO_DSA */

#ifndef NO_RSA
static void InitwolfSSL_Rsa(WOLFSSL_RSA* rsa)
{
    if (rsa) {
        XMEMSET(rsa, 0, sizeof(WOLFSSL_RSA));
    #ifdef WC_RSA_BLINDING
        rsa->rng      = NULL;
    #endif
        rsa->n        = NULL;
        rsa->e        = NULL;
        rsa->d        = NULL;
        rsa->p        = NULL;
        rsa->q        = NULL;
        rsa->dmp1     = NULL;
        rsa->dmq1     = NULL;
        rsa->iqmp     = NULL;
        rsa->internal = NULL;
        rsa->inSet    = 0;
        rsa->exSet    = 0;
    }
}


WOLFSSL_RSA* wolfSSL_RSA_new(void)
{
    WOLFSSL_RSA* external;
    RsaKey*     key;

    WOLFSSL_ENTER("wolfSSL_RSA_new");

    key = (RsaKey*) XMALLOC(sizeof(RsaKey), NULL, DYNAMIC_TYPE_RSA);
    if (key == NULL) {
        WOLFSSL_MSG("wolfSSL_RSA_new malloc RsaKey failure");
        return NULL;
    }

    external = (WOLFSSL_RSA*) XMALLOC(sizeof(WOLFSSL_RSA), NULL,
                                     DYNAMIC_TYPE_RSA);
    if (external == NULL) {
        WOLFSSL_MSG("wolfSSL_RSA_new malloc WOLFSSL_RSA failure");
        XFREE(key, NULL, DYNAMIC_TYPE_RSA);
        return NULL;
    }

    InitwolfSSL_Rsa(external);
    if (wc_InitRsaKey(key, NULL) != 0) {
        WOLFSSL_MSG("InitRsaKey WOLFSSL_RSA failure");
        XFREE(external, NULL, DYNAMIC_TYPE_RSA);
        XFREE(key, NULL, DYNAMIC_TYPE_RSA);
        return NULL;
    }
    external->internal = key;

#ifdef WC_RSA_BLINDING
    {
        WC_RNG* rng = (WC_RNG*)XMALLOC(sizeof(WC_RNG), NULL, DYNAMIC_TYPE_RNG);
        if (rng == NULL) {
            WOLFSSL_MSG("RSA RNG create failure");
            XFREE(external, NULL, DYNAMIC_TYPE_RSA);
            XFREE(key, NULL, DYNAMIC_TYPE_RSA);
            return NULL;
        }

        if (wc_InitRng(rng) != 0) {
            WOLFSSL_MSG("RSA RNG init failure");
            wolfSSL_RSA_free(external);
            return NULL;
        }

        external->rng = rng;
        if (wc_RsaSetRNG(key, rng) != 0) {
            WOLFSSL_MSG("RSA RNG set failure");
            wolfSSL_RSA_free(external);
            return NULL;
        }
    }
#endif

    return external;
}


void wolfSSL_RSA_free(WOLFSSL_RSA* rsa)
{
    WOLFSSL_ENTER("wolfSSL_RSA_free");

    if (rsa) {
        if (rsa->internal) {
            wc_FreeRsaKey((RsaKey*)rsa->internal);
            XFREE(rsa->internal, NULL, DYNAMIC_TYPE_RSA);
            rsa->internal = NULL;
        }
        wolfSSL_BN_free(rsa->iqmp);
        wolfSSL_BN_free(rsa->dmq1);
        wolfSSL_BN_free(rsa->dmp1);
        wolfSSL_BN_free(rsa->q);
        wolfSSL_BN_free(rsa->p);
        wolfSSL_BN_free(rsa->d);
        wolfSSL_BN_free(rsa->e);
        wolfSSL_BN_free(rsa->n);

    #ifdef WC_RSA_BLINDING
        if (wc_FreeRng(rsa->rng) != 0) {
            WOLFSSL_MSG("Issue freeing rng");
        }
        XFREE(rsa->rng, NULL, DYNAMIC_TYPE_RNG);
    #endif

        InitwolfSSL_Rsa(rsa);  /* set back to NULLs for safety */

        XFREE(rsa, NULL, DYNAMIC_TYPE_RSA);
        rsa = NULL;
    }
}
#endif /* NO_RSA */


/* these defines are to make sure the functions SetIndividualExternal is not
 * declared and then not used. */
#if !defined(NO_ASN) || !defined(NO_DSA) || defined(HAVE_ECC) || \
    (!defined(NO_RSA) && !defined(HAVE_USER_RSA) && !defined(HAVE_FAST_RSA))
/* when calling SetIndividualExternal, mpi should be cleared by caller if no
 * longer used. ie mp_clear(mpi). This is to free data when fastmath is
 * disabled since a copy of mpi is made by this function and placed into bn.
 */
static int SetIndividualExternal(WOLFSSL_BIGNUM** bn, mp_int* mpi)
{
    byte dynamic = 0;

    WOLFSSL_MSG("Entering SetIndividualExternal");

    if (mpi == NULL || bn == NULL) {
        WOLFSSL_MSG("mpi NULL error");
        return SSL_FATAL_ERROR;
    }

    if (*bn == NULL) {
        *bn = wolfSSL_BN_new();
        if (*bn == NULL) {
            WOLFSSL_MSG("SetIndividualExternal alloc failed");
            return SSL_FATAL_ERROR;
        }
        dynamic = 1;
    }

    if (mp_copy(mpi, (mp_int*)((*bn)->internal)) != MP_OKAY) {
        WOLFSSL_MSG("mp_copy error");
        if (dynamic == 1) {
            wolfSSL_BN_free(*bn);
        }
        return SSL_FATAL_ERROR;
    }

    return SSL_SUCCESS;
}

static int SetIndividualInternal(WOLFSSL_BIGNUM* bn, mp_int* mpi)
{
    WOLFSSL_MSG("Entering SetIndividualInternal");

    if (bn == NULL || bn->internal == NULL) {
        WOLFSSL_MSG("bn NULL error");
        return SSL_FATAL_ERROR;
    }

    if (mpi == NULL || (mp_init(mpi) != MP_OKAY)) {
        WOLFSSL_MSG("mpi NULL error");
        return SSL_FATAL_ERROR;
    }

    if (mp_copy((mp_int*)bn->internal, mpi) != MP_OKAY) {
        WOLFSSL_MSG("mp_copy error");
        return SSL_FATAL_ERROR;
    }

    return SSL_SUCCESS;
}


#ifndef NO_ASN
WOLFSSL_BIGNUM *wolfSSL_ASN1_INTEGER_to_BN(const WOLFSSL_ASN1_INTEGER *ai,
                                       WOLFSSL_BIGNUM *bn)
{
    mp_int mpi;
    word32 idx = 0;
    int ret;

    WOLFSSL_ENTER("wolfSSL_ASN1_INTEGER_to_BN");

    if (ai == NULL) {
        return NULL;
    }

    if ((ret = GetInt(&mpi, ai->data, &idx, sizeof(ai->data))) != 0) {
        /* expecting ASN1 format for INTEGER */
        WOLFSSL_LEAVE("wolfSSL_ASN1_INTEGER_to_BN", ret);
        return NULL;
    }

    /* mp_clear needs called because mpi is copied and causes memory leak with
     * --disable-fastmath */
    ret = SetIndividualExternal(&bn, &mpi);
    mp_clear(&mpi);

    if (ret != SSL_SUCCESS) {
        return NULL;
    }
    return bn;
}
#endif /* !NO_ASN */

#if !defined(NO_DSA) && !defined(NO_DH)
WOLFSSL_DH *wolfSSL_DSA_dup_DH(const WOLFSSL_DSA *dsa)
{
    WOLFSSL_DH* dh;
    DhKey*      key;

    WOLFSSL_ENTER("wolfSSL_DSA_dup_DH");

    if (dsa == NULL) {
        return NULL;
    }

    dh = wolfSSL_DH_new();
    if (dh == NULL) {
        return NULL;
    }
    key = (DhKey*)dh->internal;

    if (dsa->p != NULL &&
        SetIndividualInternal(((WOLFSSL_DSA*)dsa)->p, &key->p) != SSL_SUCCESS) {
        WOLFSSL_MSG("rsa p key error");
        wolfSSL_DH_free(dh);
        return NULL;
    }
    if (dsa->g != NULL &&
        SetIndividualInternal(((WOLFSSL_DSA*)dsa)->g, &key->g) != SSL_SUCCESS) {
        WOLFSSL_MSG("rsa g key error");
        wolfSSL_DH_free(dh);
        return NULL;
    }

    if (SetIndividualExternal(&dh->p, &key->p) != SSL_SUCCESS) {
        WOLFSSL_MSG("dsa p key error");
        wolfSSL_DH_free(dh);
        return NULL;
    }
    if (SetIndividualExternal(&dh->g, &key->g) != SSL_SUCCESS) {
        WOLFSSL_MSG("dsa g key error");
        wolfSSL_DH_free(dh);
        return NULL;
    }

    return dh;
}
#endif /* !defined(NO_DSA) && !defined(NO_DH) */

#endif /* !NO_RSA && !NO_DSA */


#ifndef NO_DSA
/* wolfSSL -> OpenSSL */
static int SetDsaExternal(WOLFSSL_DSA* dsa)
{
    DsaKey* key;
    WOLFSSL_MSG("Entering SetDsaExternal");

    if (dsa == NULL || dsa->internal == NULL) {
        WOLFSSL_MSG("dsa key NULL error");
        return SSL_FATAL_ERROR;
    }

    key = (DsaKey*)dsa->internal;

    if (SetIndividualExternal(&dsa->p, &key->p) != SSL_SUCCESS) {
        WOLFSSL_MSG("dsa p key error");
        return SSL_FATAL_ERROR;
    }

    if (SetIndividualExternal(&dsa->q, &key->q) != SSL_SUCCESS) {
        WOLFSSL_MSG("dsa q key error");
        return SSL_FATAL_ERROR;
    }

    if (SetIndividualExternal(&dsa->g, &key->g) != SSL_SUCCESS) {
        WOLFSSL_MSG("dsa g key error");
        return SSL_FATAL_ERROR;
    }

    if (SetIndividualExternal(&dsa->pub_key, &key->y) != SSL_SUCCESS) {
        WOLFSSL_MSG("dsa y key error");
        return SSL_FATAL_ERROR;
    }

    if (SetIndividualExternal(&dsa->priv_key, &key->x) != SSL_SUCCESS) {
        WOLFSSL_MSG("dsa x key error");
        return SSL_FATAL_ERROR;
    }

    dsa->exSet = 1;

    return SSL_SUCCESS;
}

/* Openssl -> WolfSSL */
static int SetDsaInternal(WOLFSSL_DSA* dsa)
{
    DsaKey* key;
    WOLFSSL_MSG("Entering SetDsaInternal");

    if (dsa == NULL || dsa->internal == NULL) {
        WOLFSSL_MSG("dsa key NULL error");
        return SSL_FATAL_ERROR;
    }

    key = (DsaKey*)dsa->internal;

    if (dsa->p != NULL &&
        SetIndividualInternal(dsa->p, &key->p) != SSL_SUCCESS) {
        WOLFSSL_MSG("rsa p key error");
        return SSL_FATAL_ERROR;
    }

    if (dsa->q != NULL &&
        SetIndividualInternal(dsa->q, &key->q) != SSL_SUCCESS) {
        WOLFSSL_MSG("rsa q key error");
        return SSL_FATAL_ERROR;
    }

    if (dsa->g != NULL &&
        SetIndividualInternal(dsa->g, &key->g) != SSL_SUCCESS) {
        WOLFSSL_MSG("rsa g key error");
        return SSL_FATAL_ERROR;
    }

    if (dsa->pub_key != NULL) {
        if (SetIndividualInternal(dsa->pub_key, &key->y) != SSL_SUCCESS) {
            WOLFSSL_MSG("rsa pub_key error");
            return SSL_FATAL_ERROR;
        }

        /* public key */
        key->type = DSA_PUBLIC;
    }

    if (dsa->priv_key != NULL) {
        if (SetIndividualInternal(dsa->priv_key, &key->x) != SSL_SUCCESS) {
            WOLFSSL_MSG("rsa priv_key error");
            return SSL_FATAL_ERROR;
        }

        /* private key */
        key->type = DSA_PRIVATE;
    }

    dsa->inSet = 1;

    return SSL_SUCCESS;
}
#endif /* NO_DSA */


#if !defined(NO_RSA)
#if !defined(HAVE_USER_RSA) && !defined(HAVE_FAST_RSA)
/* WolfSSL -> OpenSSL */
static int SetRsaExternal(WOLFSSL_RSA* rsa)
{
    RsaKey* key;
    WOLFSSL_MSG("Entering SetRsaExternal");

    if (rsa == NULL || rsa->internal == NULL) {
        WOLFSSL_MSG("rsa key NULL error");
        return SSL_FATAL_ERROR;
    }

    key = (RsaKey*)rsa->internal;

    if (SetIndividualExternal(&rsa->n, &key->n) != SSL_SUCCESS) {
        WOLFSSL_MSG("rsa n key error");
        return SSL_FATAL_ERROR;
    }

    if (SetIndividualExternal(&rsa->e, &key->e) != SSL_SUCCESS) {
        WOLFSSL_MSG("rsa e key error");
        return SSL_FATAL_ERROR;
    }

    if (SetIndividualExternal(&rsa->d, &key->d) != SSL_SUCCESS) {
        WOLFSSL_MSG("rsa d key error");
        return SSL_FATAL_ERROR;
    }

    if (SetIndividualExternal(&rsa->p, &key->p) != SSL_SUCCESS) {
        WOLFSSL_MSG("rsa p key error");
        return SSL_FATAL_ERROR;
    }

    if (SetIndividualExternal(&rsa->q, &key->q) != SSL_SUCCESS) {
        WOLFSSL_MSG("rsa q key error");
        return SSL_FATAL_ERROR;
    }

    if (SetIndividualExternal(&rsa->dmp1, &key->dP) != SSL_SUCCESS) {
        WOLFSSL_MSG("rsa dP key error");
        return SSL_FATAL_ERROR;
    }

    if (SetIndividualExternal(&rsa->dmq1, &key->dQ) != SSL_SUCCESS) {
        WOLFSSL_MSG("rsa dQ key error");
        return SSL_FATAL_ERROR;
    }

    if (SetIndividualExternal(&rsa->iqmp, &key->u) != SSL_SUCCESS) {
        WOLFSSL_MSG("rsa u key error");
        return SSL_FATAL_ERROR;
    }

    rsa->exSet = 1;

    return SSL_SUCCESS;
}

/* Openssl -> WolfSSL */
static int SetRsaInternal(WOLFSSL_RSA* rsa)
{
    RsaKey* key;
    WOLFSSL_MSG("Entering SetRsaInternal");

    if (rsa == NULL || rsa->internal == NULL) {
        WOLFSSL_MSG("rsa key NULL error");
        return SSL_FATAL_ERROR;
    }

    key = (RsaKey*)rsa->internal;

    if (SetIndividualInternal(rsa->n, &key->n) != SSL_SUCCESS) {
        WOLFSSL_MSG("rsa n key error");
        return SSL_FATAL_ERROR;
    }

    if (SetIndividualInternal(rsa->e, &key->e) != SSL_SUCCESS) {
        WOLFSSL_MSG("rsa e key error");
        return SSL_FATAL_ERROR;
    }

    /* public key */
    key->type = RSA_PUBLIC;

    if (rsa->d != NULL) {
        if (SetIndividualInternal(rsa->d, &key->d) != SSL_SUCCESS) {
            WOLFSSL_MSG("rsa d key error");
            return SSL_FATAL_ERROR;
        }

        /* private key */
        key->type = RSA_PRIVATE;
    }

    if (rsa->p != NULL &&
        SetIndividualInternal(rsa->p, &key->p) != SSL_SUCCESS) {
        WOLFSSL_MSG("rsa p key error");
        return SSL_FATAL_ERROR;
    }

    if (rsa->q != NULL &&
        SetIndividualInternal(rsa->q, &key->q) != SSL_SUCCESS) {
        WOLFSSL_MSG("rsa q key error");
        return SSL_FATAL_ERROR;
    }

    if (rsa->dmp1 != NULL &&
        SetIndividualInternal(rsa->dmp1, &key->dP) != SSL_SUCCESS) {
        WOLFSSL_MSG("rsa dP key error");
        return SSL_FATAL_ERROR;
    }

    if (rsa->dmq1 != NULL &&
        SetIndividualInternal(rsa->dmq1, &key->dQ) != SSL_SUCCESS) {
        WOLFSSL_MSG("rsa dQ key error");
        return SSL_FATAL_ERROR;
    }

    if (rsa->iqmp != NULL &&
        SetIndividualInternal(rsa->iqmp, &key->u) != SSL_SUCCESS) {
        WOLFSSL_MSG("rsa u key error");
        return SSL_FATAL_ERROR;
    }

    rsa->inSet = 1;

    return SSL_SUCCESS;
}

/* return compliant with OpenSSL
 *   1 if success, 0 if error
 */
int wolfSSL_RSA_generate_key_ex(WOLFSSL_RSA* rsa, int bits, WOLFSSL_BIGNUM* bn,
                                void* cb)
{
    int ret = SSL_FAILURE;

    (void)cb;
    (void)bn;
    (void)bits;

    WOLFSSL_ENTER("wolfSSL_RSA_generate_key_ex");

    if (rsa == NULL || rsa->internal == NULL) {
        /* bit size checked during make key call */
        WOLFSSL_MSG("bad arguments");
        return SSL_FAILURE;
    }

#ifdef WOLFSSL_KEY_GEN
    {
    #ifdef WOLFSSL_SMALL_STACK
        WC_RNG* rng = NULL;
    #else
        WC_RNG  rng[1];
    #endif

    #ifdef WOLFSSL_SMALL_STACK
        rng = (WC_RNG*)XMALLOC(sizeof(WC_RNG), NULL, DYNAMIC_TYPE_TMP_BUFFER);
        if (rng == NULL)
            return SSL_FAILURE;
    #endif

        if (wc_InitRng(rng) < 0)
            WOLFSSL_MSG("RNG init failed");
        else if (wc_MakeRsaKey((RsaKey*)rsa->internal,
                               bits, 65537, rng) != MP_OKAY)
            WOLFSSL_MSG("wc_MakeRsaKey failed");
        else if (SetRsaExternal(rsa) != SSL_SUCCESS)
            WOLFSSL_MSG("SetRsaExternal failed");
        else {
            rsa->inSet = 1;
            ret = SSL_SUCCESS;
        }

        wc_FreeRng(rng);
    #ifdef WOLFSSL_SMALL_STACK
        XFREE(rng, NULL, DYNAMIC_TYPE_TMP_BUFFER);
    #endif
    }
#else
    WOLFSSL_MSG("No Key Gen built in");
#endif
    return ret;
}


/* SSL_SUCCESS on ok */
int wolfSSL_RSA_blinding_on(WOLFSSL_RSA* rsa, WOLFSSL_BN_CTX* bn)
{
    (void)rsa;
    (void)bn;

    WOLFSSL_MSG("wolfSSL_RSA_blinding_on");

    return SSL_SUCCESS;  /* on by default */
}

/* return compliant with OpenSSL
 *   size of encrypted data if success , -1 if error
 */
int wolfSSL_RSA_public_encrypt(int len, unsigned char* fr,
                            unsigned char* to, WOLFSSL_RSA* rsa, int padding)
{
    int initTmpRng = 0;
    WC_RNG *rng = NULL;
    int outLen;
    int ret = 0;
#ifdef WOLFSSL_SMALL_STACK
    WC_RNG* tmpRNG = NULL;
#else
    WC_RNG  tmpRNG[1];
#endif

    WOLFSSL_MSG("wolfSSL_RSA_public_encrypt");
    if (rsa->inSet == 0)
    {
        if (SetRsaInternal(rsa) != SSL_SUCCESS) {
            WOLFSSL_MSG("SetRsaInternal failed");
            return 0;
        }
    }

    outLen = wolfSSL_RSA_size(rsa);

#ifdef WOLFSSL_SMALL_STACK
    tmpRNG = (WC_RNG*)XMALLOC(sizeof(WC_RNG), NULL, DYNAMIC_TYPE_TMP_BUFFER);
    if (tmpRNG == NULL)
        return 0;

#endif

    if (outLen == 0)
        WOLFSSL_MSG("Bad RSA size");
    else if (wc_InitRng(tmpRNG) == 0) {
        rng = tmpRNG;
        initTmpRng = 1;
    }
    else {
        WOLFSSL_MSG("Bad RNG Init, trying global");

        if (initGlobalRNG == 0)
            WOLFSSL_MSG("Global RNG no Init");
        else
            rng = &globalRNG;
    }

    if (rng) {
        if(padding == 0) {
            ret = wc_RsaPublicEncrypt(fr, len, to, outLen, (RsaKey*)rsa->internal, rng);
        } else {
            ret = wc_RsaPublicEncrypt_ex(fr, len, to, outLen, (RsaKey*)rsa->internal, rng,
                       padding, WC_HASH_TYPE_SHA, WC_MGF1SHA1, NULL, 0);
        }
        if (len <= 0) {
            WOLFSSL_MSG("Bad Rsa Encrypt");
        }
    }

    if (initTmpRng)
        wc_FreeRng(tmpRNG);

#ifdef WOLFSSL_SMALL_STACK
    XFREE(tmpRNG,     NULL, DYNAMIC_TYPE_TMP_BUFFER);
#endif

    if (ret >= 0)
        WOLFSSL_MSG("wolfSSL_RSA_public_encrypt success");
    else {
        WOLFSSL_MSG("wolfSSL_RSA_public_encrypt failed");
    }
    return ret;
}

/* return compliant with OpenSSL
 *   size of plain recovered data if success , -1 if error
 */
int wolfSSL_RSA_private_decrypt(int len, unsigned char* fr,
                            unsigned char* to, WOLFSSL_RSA* rsa, int padding)
{
  int outLen;
  int ret = 0;

      WOLFSSL_ENTER("wolfSSL_RSA_private_decrypt");
      if (rsa->inSet == 0)
      {
          if (SetRsaInternal(rsa) != SSL_SUCCESS) {
              WOLFSSL_MSG("SetRsaInternal failed");
              return 0;
          }
      }

      outLen = wolfSSL_RSA_size(rsa);
      if (outLen == 0) {
          WOLFSSL_MSG("Bad RSA size");
      }

      if(padding == 0) {
          /* ((RsaKey *)rsa->internal)->rng = ((RsaKey *)rsa)->rng; */
          ret = wc_RsaPrivateDecrypt(fr, len, to, outLen, (RsaKey*)rsa->internal);
      } else {
          ret = wc_RsaPrivateDecrypt_ex(fr, len, to, outLen, (RsaKey*)rsa->internal,
                     padding, WC_HASH_TYPE_SHA, WC_MGF1SHA1, NULL, 0);
      }
      if (ret > 0){
          WOLFSSL_MSG("wolfSSL_RSA_private_decrypt success");
      } else {
          WOLFSSL_MSG("wolfSSL_RSA_private_decrypt failed");
      }
      return ret;
}


/* RSA private encrypt calls wc_RsaSSL_Sign. Similar function set up as RSA
 * public decrypt.
 *
 * len  Length of input buffer
 * in   Input buffer to sign
 * out  Output buffer (expected to be greater than or equal to RSA key size)
 * rsa     Key to use for encryption
 * padding Type of RSA padding to use.
 */
int wolfSSL_RSA_private_encrypt(int len, unsigned char* in,
                            unsigned char* out, WOLFSSL_RSA* rsa, int padding)
{
    int sz = 0;
    WC_RNG* rng = NULL;
    RsaKey* key;

    WOLFSSL_MSG("wolfSSL_RSA_private_encrypt");

    if (len < 0 || rsa == NULL || rsa->internal == NULL || in == NULL) {
        WOLFSSL_MSG("Bad function arguments");
        return 0;
    }

    if (padding != RSA_PKCS1_PADDING) {
        WOLFSSL_MSG("wolfSSL_RSA_private_encrypt unsupported padding");
        return 0;
    }

    if (rsa->inSet == 0)
    {
        WOLFSSL_MSG("Setting internal RSA structure");

        if (SetRsaInternal(rsa) != SSL_SUCCESS) {
            WOLFSSL_MSG("SetRsaInternal failed");
            return 0;
        }
    }

    key = (RsaKey*)rsa->internal;
    #if defined(WC_RSA_BLINDING) && !defined(HAVE_USER_RSA)
    rng = key->rng;
    #else
    if (wc_InitRng_ex(rng, key->heap) != 0) {
        WOLFSSL_MSG("Error with random number");
        return SSL_FATAL_ERROR;
    }
    #endif

    /* size of output buffer must be size of RSA key */
    sz = wc_RsaSSL_Sign(in, (word32)len, out, wolfSSL_RSA_size(rsa), key, rng);
    #if !defined(WC_RSA_BLINDING) || defined(HAVE_USER_RSA)
    if (wc_FreeRng(rng) != 0) {
        WOLFSSL_MSG("Error freeing random number generator");
        return SSL_FATAL_ERROR;
    }
    #endif
    if (sz <= 0) {
        WOLFSSL_LEAVE("wolfSSL_RSA_private_encrypt", sz);
        return 0;
    }

    return sz;
}

/* return compliant with OpenSSL
 *   RSA modulus size in bytes, -1 if error
 */
int wolfSSL_RSA_size(const WOLFSSL_RSA* rsa)
{
    WOLFSSL_ENTER("wolfSSL_RSA_size");

    if (rsa == NULL)
        return SSL_FATAL_ERROR;
    if (rsa->inSet == 0)
    {
        if (SetRsaInternal((WOLFSSL_RSA*)rsa) != SSL_SUCCESS) {
            WOLFSSL_MSG("SetRsaInternal failed");
            return 0;
        }
    }
    return wolfSSL_BN_num_bytes(rsa->n);
}
#endif /* HAVE_USER_RSA */
#endif /* NO_RSA */

#ifndef NO_DSA
/* return code compliant with OpenSSL :
 *   1 if success, 0 if error
 */
int wolfSSL_DSA_generate_key(WOLFSSL_DSA* dsa)
{
    int ret = SSL_FAILURE;

    WOLFSSL_ENTER("wolfSSL_DSA_generate_key");

    if (dsa == NULL || dsa->internal == NULL) {
        WOLFSSL_MSG("Bad arguments");
        return SSL_FAILURE;
    }

    if (dsa->inSet == 0) {
        WOLFSSL_MSG("No DSA internal set, do it");

        if (SetDsaInternal(dsa) != SSL_SUCCESS) {
            WOLFSSL_MSG("SetDsaInternal failed");
            return ret;
        }
    }

#ifdef WOLFSSL_KEY_GEN
    {
        int initTmpRng = 0;
        WC_RNG *rng = NULL;
#ifdef WOLFSSL_SMALL_STACK
        WC_RNG *tmpRNG = NULL;
#else
        WC_RNG tmpRNG[1];
#endif

#ifdef WOLFSSL_SMALL_STACK
        tmpRNG = (WC_RNG*)XMALLOC(sizeof(WC_RNG), NULL, DYNAMIC_TYPE_TMP_BUFFER);
        if (tmpRNG == NULL)
            return SSL_FATAL_ERROR;
#endif
        if (wc_InitRng(tmpRNG) == 0) {
            rng = tmpRNG;
            initTmpRng = 1;
        }
        else {
            WOLFSSL_MSG("Bad RNG Init, trying global");
            if (initGlobalRNG == 0)
                WOLFSSL_MSG("Global RNG no Init");
            else
                rng = &globalRNG;
        }

        if (rng) {
            if (wc_MakeDsaKey(rng, (DsaKey*)dsa->internal) != MP_OKAY)
                WOLFSSL_MSG("wc_MakeDsaKey failed");
            else if (SetDsaExternal(dsa) != SSL_SUCCESS)
                WOLFSSL_MSG("SetDsaExternal failed");
            else
                ret = SSL_SUCCESS;
        }

        if (initTmpRng)
            wc_FreeRng(tmpRNG);

#ifdef WOLFSSL_SMALL_STACK
        XFREE(tmpRNG, NULL, DYNAMIC_TYPE_TMP_BUFFER);
#endif
    }
#else /* WOLFSSL_KEY_GEN */
    WOLFSSL_MSG("No Key Gen built in");
#endif
    return ret;
}


WOLFSSL_DSA* wolfSSL_DSA_generate_parameters(int bits, unsigned char* seed,
        int seedLen, int* counterRet, unsigned long* hRet,
        WOLFSSL_BN_CB cb, void* CBArg)
{
    WOLFSSL_DSA* dsa;

    WOLFSSL_ENTER("wolfSSL_DSA_generate_parameters()");

    (void)cb;
    (void)CBArg;
    dsa = wolfSSL_DSA_new();
    if (dsa == NULL) {
        return NULL;
    }

    if (wolfSSL_DSA_generate_parameters_ex(dsa, bits, seed, seedLen,
                                  counterRet, hRet, NULL) != SSL_SUCCESS) {
        wolfSSL_DSA_free(dsa);
        return NULL;
    }

    return dsa;
}


/* return code compliant with OpenSSL :
 *   1 if success, 0 if error
 */
int wolfSSL_DSA_generate_parameters_ex(WOLFSSL_DSA* dsa, int bits,
                                       unsigned char* seed, int seedLen,
                                       int* counterRet,
                                       unsigned long* hRet, void* cb)
{
    int ret = SSL_FAILURE;

    (void)bits;
    (void)seed;
    (void)seedLen;
    (void)counterRet;
    (void)hRet;
    (void)cb;

    WOLFSSL_ENTER("wolfSSL_DSA_generate_parameters_ex");

    if (dsa == NULL || dsa->internal == NULL) {
        WOLFSSL_MSG("Bad arguments");
        return SSL_FAILURE;
    }

#ifdef WOLFSSL_KEY_GEN
    {
        int initTmpRng = 0;
        WC_RNG *rng = NULL;
#ifdef WOLFSSL_SMALL_STACK
        WC_RNG *tmpRNG = NULL;
#else
        WC_RNG tmpRNG[1];
#endif

#ifdef WOLFSSL_SMALL_STACK
        tmpRNG = (WC_RNG*)XMALLOC(sizeof(WC_RNG), NULL, DYNAMIC_TYPE_TMP_BUFFER);
        if (tmpRNG == NULL)
            return SSL_FATAL_ERROR;
#endif
        if (wc_InitRng(tmpRNG) == 0) {
            rng = tmpRNG;
            initTmpRng = 1;
        }
        else {
            WOLFSSL_MSG("Bad RNG Init, trying global");
            if (initGlobalRNG == 0)
                WOLFSSL_MSG("Global RNG no Init");
            else
                rng = &globalRNG;
        }

        if (rng) {
            if (wc_MakeDsaParameters(rng, bits,
                                     (DsaKey*)dsa->internal) != MP_OKAY)
                WOLFSSL_MSG("wc_MakeDsaParameters failed");
            else if (SetDsaExternal(dsa) != SSL_SUCCESS)
                WOLFSSL_MSG("SetDsaExternal failed");
            else
                ret = SSL_SUCCESS;
        }

        if (initTmpRng)
            wc_FreeRng(tmpRNG);

#ifdef WOLFSSL_SMALL_STACK
        XFREE(tmpRNG, NULL, DYNAMIC_TYPE_TMP_BUFFER);
#endif
    }
#else /* WOLFSSL_KEY_GEN */
    WOLFSSL_MSG("No Key Gen built in");
#endif

    return ret;
}

/* return SSL_SUCCESS on success, < 0 otherwise */
int wolfSSL_DSA_do_sign(const unsigned char* d, unsigned char* sigRet,
                       WOLFSSL_DSA* dsa)
{
    int     ret = SSL_FATAL_ERROR;
    int     initTmpRng = 0;
    WC_RNG* rng = NULL;
#ifdef WOLFSSL_SMALL_STACK
    WC_RNG* tmpRNG = NULL;
#else
    WC_RNG  tmpRNG[1];
#endif

    WOLFSSL_ENTER("wolfSSL_DSA_do_sign");

    if (d == NULL || sigRet == NULL || dsa == NULL) {
        WOLFSSL_MSG("Bad function arguments");
        return ret;
    }

    if (dsa->inSet == 0)
    {
        WOLFSSL_MSG("No DSA internal set, do it");

        if (SetDsaInternal(dsa) != SSL_SUCCESS) {
            WOLFSSL_MSG("SetDsaInternal failed");
            return ret;
        }
    }

#ifdef WOLFSSL_SMALL_STACK
    tmpRNG = (WC_RNG*)XMALLOC(sizeof(WC_RNG), NULL, DYNAMIC_TYPE_TMP_BUFFER);
    if (tmpRNG == NULL)
        return SSL_FATAL_ERROR;
#endif

    if (wc_InitRng(tmpRNG) == 0) {
        rng = tmpRNG;
        initTmpRng = 1;
    }
    else {
        WOLFSSL_MSG("Bad RNG Init, trying global");
        if (initGlobalRNG == 0)
            WOLFSSL_MSG("Global RNG no Init");
        else
            rng = &globalRNG;
    }

    if (rng) {
        if (DsaSign(d, sigRet, (DsaKey*)dsa->internal, rng) < 0)
            WOLFSSL_MSG("DsaSign failed");
        else
            ret = SSL_SUCCESS;
    }

    if (initTmpRng)
        wc_FreeRng(tmpRNG);
#ifdef WOLFSSL_SMALL_STACK
    XFREE(tmpRNG, NULL, DYNAMIC_TYPE_TMP_BUFFER);
#endif

    return ret;
}


int wolfSSL_DSA_do_verify(const unsigned char* d, unsigned char* sig,
                        WOLFSSL_DSA* dsa, int *dsacheck)
{
    int    ret = SSL_FATAL_ERROR;

    WOLFSSL_ENTER("wolfSSL_DSA_do_verify");

    if (d == NULL || sig == NULL || dsa == NULL) {
        WOLFSSL_MSG("Bad function arguments");
        return SSL_FATAL_ERROR;
    }
    if (dsa->inSet == 0)
    {
        WOLFSSL_MSG("No DSA internal set, do it");

        if (SetDsaInternal(dsa) != SSL_SUCCESS) {
            WOLFSSL_MSG("SetDsaInternal failed");
            return SSL_FATAL_ERROR;
        }
    }

    ret = DsaVerify(d, sig, (DsaKey*)dsa->internal, dsacheck);
    if (ret != 0 || *dsacheck != 1) {
        WOLFSSL_MSG("DsaVerify failed");
        return ret;
    }

    return SSL_SUCCESS;
}
#endif /* NO_DSA */


#if !defined(NO_RSA) && !defined(HAVE_USER_RSA)

#ifdef DEBUG_SIGN
static void show(const char *title, const unsigned char *out, unsigned int outlen)
{
    const unsigned char *pt;
    printf("%s[%d] = \n", title, (int)outlen);
    outlen = outlen>100?100:outlen;
    for (pt = out; pt < out + outlen;
            printf("%c", ((*pt)&0x6f)>='A'?((*pt)&0x6f):'.'), pt++);
    printf("\n");
}
#else
#define show(a,b,c)
#endif

/* return SSL_SUCCES on ok, 0 otherwise */
int wolfSSL_RSA_sign(int type, const unsigned char* m,
                           unsigned int mLen, unsigned char* sigRet,
                           unsigned int* sigLen, WOLFSSL_RSA* rsa)
{
    return wolfSSL_RSA_sign_ex(type, m, mLen, sigRet, sigLen, rsa, 1);
}

int wolfSSL_RSA_sign_ex(int type, const unsigned char* m,
                           unsigned int mLen, unsigned char* sigRet,
                           unsigned int* sigLen, WOLFSSL_RSA* rsa, int flag)
{
    word32  outLen;
    word32  signSz;
    int     initTmpRng = 0;
    WC_RNG* rng        = NULL;
    int     ret        = 0;
#ifdef WOLFSSL_SMALL_STACK
    WC_RNG* tmpRNG     = NULL;
    byte*   encodedSig = NULL;
#else
    WC_RNG  tmpRNG[1];
    byte    encodedSig[MAX_ENCODED_SIG_SZ];
#endif

    WOLFSSL_ENTER("wolfSSL_RSA_sign");

    if (m == NULL || sigRet == NULL || sigLen == NULL || rsa == NULL) {
        WOLFSSL_MSG("Bad function arguments");
        return 0;
    }
    show("Message to Sign", m, mLen);

    switch (type) {
    #ifdef WOLFSSL_MD2
        case NID_md2:       type = MD2h;    break;
    #endif
    #ifndef NO_MD5
        case NID_md5:       type = MD5h;    break;
    #endif
    #ifndef NO_SHA
        case NID_sha1:      type = SHAh;    break;
    #endif
    #ifndef NO_SHA256
        case NID_sha256:    type = SHA256h; break;
    #endif
    #ifdef WOLFSSL_SHA384
        case NID_sha384:    type = SHA384h; break;
    #endif
    #ifdef WOLFSSL_SHA512
        case NID_sha512:    type = SHA512h; break;
    #endif
        default:
            WOLFSSL_MSG("This NID (md type) not configured or not implemented");
            return 0;
    }

    if (rsa->inSet == 0)
    {
        WOLFSSL_MSG("No RSA internal set, do it");

        if (SetRsaInternal(rsa) != SSL_SUCCESS) {
            WOLFSSL_MSG("SetRsaInternal failed");
            return 0;
        }
    }

    outLen = (word32)wolfSSL_BN_num_bytes(rsa->n);

#ifdef WOLFSSL_SMALL_STACK
    tmpRNG = (WC_RNG*)XMALLOC(sizeof(WC_RNG), NULL, DYNAMIC_TYPE_TMP_BUFFER);
    if (tmpRNG == NULL)
        return 0;

    encodedSig = (byte*)XMALLOC(MAX_ENCODED_SIG_SZ, NULL,
                                                   DYNAMIC_TYPE_TMP_BUFFER);
    if (encodedSig == NULL) {
        XFREE(tmpRNG, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        return 0;
    }
#endif

    if (outLen == 0)
        WOLFSSL_MSG("Bad RSA size");
    else if (wc_InitRng(tmpRNG) == 0) {
        rng = tmpRNG;
        initTmpRng = 1;
    }
    else {
        WOLFSSL_MSG("Bad RNG Init, trying global");

        if (initGlobalRNG == 0)
            WOLFSSL_MSG("Global RNG no Init");
        else
            rng = &globalRNG;
    }

    if (rng) {

        signSz = wc_EncodeSignature(encodedSig, m, mLen, type);
        if (signSz == 0) {
            WOLFSSL_MSG("Bad Encode Signature");
        }
        else {
            show("Encoded Message", encodedSig, signSz);
            if(flag != 0){
                *sigLen = wc_RsaSSL_Sign(encodedSig, signSz, sigRet, outLen,
                                (RsaKey*)rsa->internal, rng);
                if (*sigLen <= 0)
                    WOLFSSL_MSG("Bad Rsa Sign");
                else{
                    ret = SSL_SUCCESS;
                    show("Signature", sigRet, *sigLen);
                }
            } else {
                ret = SSL_SUCCESS;
                XMEMCPY(sigRet, encodedSig, signSz);
                *sigLen = signSz;
            }
        }

    }

    if (initTmpRng)
        wc_FreeRng(tmpRNG);

#ifdef WOLFSSL_SMALL_STACK
    XFREE(tmpRNG,     NULL, DYNAMIC_TYPE_TMP_BUFFER);
    XFREE(encodedSig, NULL, DYNAMIC_TYPE_TMP_BUFFER);
#endif

    if (ret == SSL_SUCCESS)
        WOLFSSL_MSG("wolfSSL_RSA_sign success");
    else {
        WOLFSSL_MSG("wolfSSL_RSA_sign failed");
    }
    return ret;
}

WOLFSSL_API int wolfSSL_RSA_verify(int type, const unsigned char* m,
                               unsigned int mLen, const unsigned char* sig,
                               unsigned int sigLen, WOLFSSL_RSA* rsa)
{
    int     ret;
    unsigned char *sigRet ;
    unsigned char *sigDec ;
    unsigned int   len;

    WOLFSSL_ENTER("wolfSSL_RSA_verify");
    if((m == NULL) || (sig == NULL)) {
        WOLFSSL_MSG("Bad function arguments");
        return 0;
    }

    sigRet = (unsigned char *)XMALLOC(sigLen, NULL, DYNAMIC_TYPE_TMP_BUFFER);
    if(sigRet == NULL){
        WOLFSSL_MSG("Memory failure");
        return 0;
    }
    sigDec = (unsigned char *)XMALLOC(sigLen, NULL, DYNAMIC_TYPE_TMP_BUFFER);
    if(sigRet == NULL){
        WOLFSSL_MSG("Memory failure");
        XFREE(sigRet, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        return 0;
    }
    /* get non-encrypted signature to be compared with decrypted sugnature*/
    ret = wolfSSL_RSA_sign_ex(type, m, mLen, sigRet, &len, rsa, 0);
    if(ret <= 0){
        WOLFSSL_MSG("Message Digest Error");
        XFREE(sigRet, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        XFREE(sigDec, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        return 0;
    }
    show("Encoded Message", sigRet, len);
    /* decrypt signature */
    ret = wc_RsaSSL_Verify(sig, sigLen, (unsigned char *)sigDec, sigLen, (RsaKey*)rsa->internal);
    if(ret <= 0){
        WOLFSSL_MSG("RSA Decrypt error");
        XFREE(sigRet, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        XFREE(sigDec, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        return 0;
    }
    show("Decrypted Signature", sigDec, ret);

    if(XMEMCMP(sigRet, sigDec, ret) == 0){
        WOLFSSL_MSG("wolfSSL_RSA_verify success");
        XFREE(sigRet, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        XFREE(sigDec, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        return 1;
    } else {
        WOLFSSL_MSG("wolfSSL_RSA_verify failed");
        XFREE(sigRet, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        XFREE(sigDec, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        return 0;
    }
}

int wolfSSL_RSA_public_decrypt(int flen, unsigned char* from,
                          unsigned char* to, WOLFSSL_RSA* rsa, int padding)
{
    int tlen = 0;

    WOLFSSL_ENTER("wolfSSL_RSA_public_decrypt");

    if (rsa == NULL || rsa->internal == NULL || from == NULL) {
        WOLFSSL_MSG("Bad function arguments");
        return 0;
    }

    if (padding != RSA_PKCS1_PADDING) {
        WOLFSSL_MSG("wolfSSL_RSA_public_decrypt unsupported padding");
        return 0;
    }

    if (rsa->inSet == 0)
    {
        WOLFSSL_MSG("No RSA internal set, do it");

        if (SetRsaInternal(rsa) != SSL_SUCCESS) {
            WOLFSSL_MSG("SetRsaInternal failed");
            return 0;
        }
    }

    /* size of 'to' buffer must be size of RSA key */
    tlen = wc_RsaSSL_Verify(from, flen, to, wolfSSL_RSA_size(rsa),
                            (RsaKey*)rsa->internal);
    if (tlen <= 0)
        WOLFSSL_MSG("wolfSSL_RSA_public_decrypt failed");
    else {
        WOLFSSL_MSG("wolfSSL_RSA_public_decrypt success");
    }
    return tlen;
}


/* generate p-1 and q-1, SSL_SUCCESS on ok */
int wolfSSL_RSA_GenAdd(WOLFSSL_RSA* rsa)
{
    int    err;
    mp_int tmp;

    WOLFSSL_MSG("wolfSSL_RsaGenAdd");

    if (rsa == NULL || rsa->p == NULL || rsa->q == NULL || rsa->d == NULL ||
                       rsa->dmp1 == NULL || rsa->dmq1 == NULL) {
        WOLFSSL_MSG("rsa no init error");
        return SSL_FATAL_ERROR;
    }

    if (mp_init(&tmp) != MP_OKAY) {
        WOLFSSL_MSG("mp_init error");
        return SSL_FATAL_ERROR;
    }

    err = mp_sub_d((mp_int*)rsa->p->internal, 1, &tmp);
    if (err != MP_OKAY) {
        WOLFSSL_MSG("mp_sub_d error");
    }
    else
        err = mp_mod((mp_int*)rsa->d->internal, &tmp,
                     (mp_int*)rsa->dmp1->internal);

    if (err != MP_OKAY) {
        WOLFSSL_MSG("mp_mod error");
    }
    else
        err = mp_sub_d((mp_int*)rsa->q->internal, 1, &tmp);
    if (err != MP_OKAY) {
        WOLFSSL_MSG("mp_sub_d error");
    }
    else
        err = mp_mod((mp_int*)rsa->d->internal, &tmp,
                     (mp_int*)rsa->dmq1->internal);

    mp_clear(&tmp);

    if (err == MP_OKAY)
        return SSL_SUCCESS;
    else
        return SSL_FATAL_ERROR;
}
#endif /* NO_RSA */


void wolfSSL_HMAC_CTX_Init(WOLFSSL_HMAC_CTX* ctx)
{
    if (ctx != NULL) {
        /* wc_HmacSetKey sets up ctx->hmac */
        XMEMSET(ctx, 0, sizeof(WOLFSSL_HMAC_CTX));
    }
}


int wolfSSL_HMAC_Init_ex(WOLFSSL_HMAC_CTX* ctx, const void* key,
                             int keylen, const EVP_MD* type, WOLFSSL_ENGINE* e)
{
    WOLFSSL_ENTER("wolfSSL_HMAC_Init_ex()");

    /* WOLFSSL_ENGINE not used, call wolfSSL_HMAC_Init */
    (void)e;
    return wolfSSL_HMAC_Init(ctx, key, keylen, type);
}


int wolfSSL_HMAC_Init(WOLFSSL_HMAC_CTX* ctx, const void* key, int keylen,
                  const EVP_MD* type)
{
    WOLFSSL_MSG("wolfSSL_HMAC_Init");

    if (ctx == NULL) {
        WOLFSSL_MSG("no ctx on init");
        return SSL_FAILURE;
    }

    if (type) {
        WOLFSSL_MSG("init has type");

        if (XSTRNCMP(type, "MD5", 3) == 0) {
            WOLFSSL_MSG("md5 hmac");
            ctx->type = MD5;
        }
        else if (XSTRNCMP(type, "SHA256", 6) == 0) {
            WOLFSSL_MSG("sha256 hmac");
            ctx->type = SHA256;
        }

        /* has to be last since would pick or 256, 384, or 512 too */
        else if (XSTRNCMP(type, "SHA", 3) == 0) {
            WOLFSSL_MSG("sha hmac");
            ctx->type = SHA;
        }
        else {
            WOLFSSL_MSG("bad init type");
            return SSL_FAILURE;
        }
    }

    if (key && keylen) {
        WOLFSSL_MSG("keying hmac");
        if (wc_HmacSetKey(&ctx->hmac, ctx->type, (const byte*)key,
                    (word32)keylen) != 0) {
            return SSL_FAILURE;
        }
    }

    return SSL_SUCCESS;
}


int wolfSSL_HMAC_Update(WOLFSSL_HMAC_CTX* ctx, const unsigned char* data,
                    int len)
{
    WOLFSSL_MSG("wolfSSL_HMAC_Update");

    if (ctx == NULL) {
        return SSL_FAILURE;
    }

    if (data) {
        WOLFSSL_MSG("updating hmac");
        if (wc_HmacUpdate(&ctx->hmac, data, (word32)len) != 0) {
            return SSL_FAILURE;
        }
    }
    return SSL_SUCCESS;
}


int wolfSSL_HMAC_Final(WOLFSSL_HMAC_CTX* ctx, unsigned char* hash,
                   unsigned int* len)
{
    WOLFSSL_MSG("wolfSSL_HMAC_Final");

    if (ctx && hash) {
        WOLFSSL_MSG("final hmac");
        if (wc_HmacFinal(&ctx->hmac, hash) != 0) {
            return SSL_FAILURE;
        }

        if (len) {
            WOLFSSL_MSG("setting output len");
            switch (ctx->type) {
                case MD5:
                    *len = MD5_DIGEST_SIZE;
                    break;

                case SHA:
                    *len = SHA_DIGEST_SIZE;
                    break;

                case SHA256:
                    *len = SHA256_DIGEST_SIZE;
                    break;

                default:
                    WOLFSSL_MSG("bad hmac type");
                    return SSL_FAILURE;
            }
        }
        return SSL_SUCCESS;
    }
    return SSL_FAILURE;
}


void wolfSSL_HMAC_cleanup(WOLFSSL_HMAC_CTX* ctx)
{
    (void)ctx;

    WOLFSSL_MSG("wolfSSL_HMAC_cleanup");
}


const WOLFSSL_EVP_MD* wolfSSL_EVP_get_digestbynid(int id)
{
    WOLFSSL_MSG("wolfSSL_get_digestbynid");

    switch(id) {
#ifndef NO_MD5
        case NID_md5:
            return wolfSSL_EVP_md5();
#endif
#ifndef NO_SHA
        case NID_sha1:
            return wolfSSL_EVP_sha1();
#endif
        default:
            WOLFSSL_MSG("Bad digest id value");
    }

    return NULL;
}


WOLFSSL_RSA* wolfSSL_EVP_PKEY_get1_RSA(WOLFSSL_EVP_PKEY* key)
{
    (void)key;
    WOLFSSL_MSG("wolfSSL_EVP_PKEY_get1_RSA not implemented");

    return NULL;
}


#ifndef NO_RSA
/* with set1 functions the pkey struct does not own the RSA structure */
WOLFSSL_API int wolfSSL_EVP_PKEY_set1_RSA(WOLFSSL_EVP_PKEY *pkey, WOLFSSL_RSA *key)
{
    if((pkey == NULL) || (key ==NULL))return 0;
    WOLFSSL_ENTER("wolfSSL_EVP_PKEY_set1_RSA");
    if (pkey->rsa != NULL && pkey->ownRsa == 1) {
        wolfSSL_RSA_free(pkey->rsa);
    }
    pkey->rsa    = key;
    pkey->ownRsa = 0; /* pkey does not own RSA */
    pkey->type = EVP_PKEY_RSA;
#ifdef WC_RSA_BLINDING
    if (wc_RsaSetRNG((RsaKey*)(pkey->rsa->internal), &(pkey->rng)) != 0) {
        WOLFSSL_MSG("Error setting RSA rng");
        return SSL_FAILURE;
    }
#endif
    return 1;
}
#endif /* NO_RSA */

WOLFSSL_DSA* wolfSSL_EVP_PKEY_get1_DSA(WOLFSSL_EVP_PKEY* key)
{
    (void)key;
    WOLFSSL_MSG("wolfSSL_EVP_PKEY_get1_DSA not implemented");

    return NULL;
}


WOLFSSL_EC_KEY* wolfSSL_EVP_PKEY_get1_EC_KEY(WOLFSSL_EVP_PKEY* key)
{
    (void)key;
    WOLFSSL_MSG("wolfSSL_EVP_PKEY_get1_EC_KEY not implemented");

    return NULL;
}


void* wolfSSL_EVP_X_STATE(const WOLFSSL_EVP_CIPHER_CTX* ctx)
{
    WOLFSSL_MSG("wolfSSL_EVP_X_STATE");

    if (ctx) {
        switch (ctx->cipherType) {
            case ARC4_TYPE:
                WOLFSSL_MSG("returning arc4 state");
                return (void*)&ctx->cipher.arc4.x;

            default:
                WOLFSSL_MSG("bad x state type");
                return 0;
        }
    }

    return NULL;
}


int wolfSSL_EVP_X_STATE_LEN(const WOLFSSL_EVP_CIPHER_CTX* ctx)
{
    WOLFSSL_MSG("wolfSSL_EVP_X_STATE_LEN");

    if (ctx) {
        switch (ctx->cipherType) {
            case ARC4_TYPE:
                WOLFSSL_MSG("returning arc4 state size");
                return sizeof(Arc4);

            default:
                WOLFSSL_MSG("bad x state type");
                return 0;
        }
    }

    return 0;
}


#ifndef NO_DES3

void wolfSSL_3des_iv(WOLFSSL_EVP_CIPHER_CTX* ctx, int doset,
                            unsigned char* iv, int len)
{
    (void)len;

    WOLFSSL_MSG("wolfSSL_3des_iv");

    if (ctx == NULL || iv == NULL) {
        WOLFSSL_MSG("Bad function argument");
        return;
    }

    if (doset)
        wc_Des3_SetIV(&ctx->cipher.des3, iv);  /* OpenSSL compat, no ret */
    else
        XMEMCPY(iv, &ctx->cipher.des3.reg, DES_BLOCK_SIZE);
}

#endif /* NO_DES3 */


#ifndef NO_AES

void wolfSSL_aes_ctr_iv(WOLFSSL_EVP_CIPHER_CTX* ctx, int doset,
                      unsigned char* iv, int len)
{
    (void)len;

    WOLFSSL_MSG("wolfSSL_aes_ctr_iv");

    if (ctx == NULL || iv == NULL) {
        WOLFSSL_MSG("Bad function argument");
        return;
    }

    if (doset)
        wc_AesSetIV(&ctx->cipher.aes, iv);  /* OpenSSL compat, no ret */
    else
        XMEMCPY(iv, &ctx->cipher.aes.reg, AES_BLOCK_SIZE);
}

#endif /* NO_AES */


const WOLFSSL_EVP_MD* wolfSSL_EVP_ripemd160(void)
{
    WOLFSSL_MSG("wolfSSL_ripemd160");

    return NULL;
}


int wolfSSL_EVP_MD_size(const WOLFSSL_EVP_MD* type)
{
    WOLFSSL_MSG("wolfSSL_EVP_MD_size");

    if (type == NULL) {
        WOLFSSL_MSG("No md type arg");
        return BAD_FUNC_ARG;
    }

    if (XSTRNCMP(type, "SHA256", 6) == 0) {
        return SHA256_DIGEST_SIZE;
    }
#ifndef NO_MD5
    else if (XSTRNCMP(type, "MD5", 3) == 0) {
        return MD5_DIGEST_SIZE;
    }
#endif
#ifdef WOLFSSL_SHA224
    else if (XSTRNCMP(type, "SHA224", 6) == 0) {
        return SHA224_DIGEST_SIZE;
    }
#endif
#ifdef WOLFSSL_SHA384
    else if (XSTRNCMP(type, "SHA384", 6) == 0) {
        return SHA384_DIGEST_SIZE;
    }
#endif
#ifdef WOLFSSL_SHA512
    else if (XSTRNCMP(type, "SHA512", 6) == 0) {
        return SHA512_DIGEST_SIZE;
    }
#endif
#ifndef NO_SHA
    /* has to be last since would pick or 256, 384, or 512 too */
    else if (XSTRNCMP(type, "SHA", 3) == 0) {
        return SHA_DIGEST_SIZE;
    }
#endif

    return BAD_FUNC_ARG;
}


int wolfSSL_EVP_CIPHER_CTX_iv_length(const WOLFSSL_EVP_CIPHER_CTX* ctx)
{
    WOLFSSL_MSG("wolfSSL_EVP_CIPHER_CTX_iv_length");

    switch (ctx->cipherType) {

        case AES_128_CBC_TYPE :
        case AES_192_CBC_TYPE :
        case AES_256_CBC_TYPE :
            WOLFSSL_MSG("AES CBC");
            return AES_BLOCK_SIZE;

#ifdef WOLFSSL_AES_COUNTER
        case AES_128_CTR_TYPE :
        case AES_192_CTR_TYPE :
        case AES_256_CTR_TYPE :
            WOLFSSL_MSG("AES CTR");
            return AES_BLOCK_SIZE;
#endif

        case DES_CBC_TYPE :
            WOLFSSL_MSG("DES CBC");
            return DES_BLOCK_SIZE;

        case DES_EDE3_CBC_TYPE :
            WOLFSSL_MSG("DES EDE3 CBC");
            return DES_BLOCK_SIZE;
#ifdef HAVE_IDEA
        case IDEA_CBC_TYPE :
            WOLFSSL_MSG("IDEA CBC");
            return IDEA_BLOCK_SIZE;
#endif
        case ARC4_TYPE :
            WOLFSSL_MSG("ARC4");
            return 0;

        case NULL_CIPHER_TYPE :
            WOLFSSL_MSG("NULL");
            return 0;

        default: {
            WOLFSSL_MSG("bad type");
        }
    }
    return 0;
}


void wolfSSL_OPENSSL_free(void* p)
{
    WOLFSSL_MSG("wolfSSL_OPENSSL_free");

    XFREE(p, NULL, DYNAMIC_TYPE_OPENSSL);
}

#if defined(WOLFSSL_KEY_GEN)

static int EncryptDerKey(byte *der, int *derSz, const EVP_CIPHER* cipher,
                         unsigned char* passwd, int passwdSz, byte **cipherInfo)
{
    int ret, paddingSz;
    word32 idx, cipherInfoSz;
#ifdef WOLFSSL_SMALL_STACK
    EncryptedInfo* info = NULL;
#else
    EncryptedInfo  info[1];
#endif

    WOLFSSL_ENTER("EncryptDerKey");

    if (der == NULL || derSz == NULL || cipher == NULL ||
        passwd == NULL || cipherInfo == NULL)
        return BAD_FUNC_ARG;

#ifdef WOLFSSL_SMALL_STACK
    info = (EncryptedInfo*)XMALLOC(sizeof(EncryptedInfo), NULL,
                                   DYNAMIC_TYPE_TMP_BUFFER);
    if (info == NULL) {
        WOLFSSL_MSG("malloc failed");
        return SSL_FAILURE;
    }
#endif
    info->set      = 0;
    info->ctx      = NULL;
    info->consumed = 0;

    /* set iv size */
    if (XSTRNCMP(cipher, "DES", 3) == 0)
        info->ivSz = DES_IV_SIZE;
    else if (XSTRNCMP(cipher, "AES", 3) == 0)
        info->ivSz = AES_IV_SIZE;
    else {
        WOLFSSL_MSG("unsupported cipher");
#ifdef WOLFSSL_SMALL_STACK
        XFREE(info, NULL, DYNAMIC_TYPE_TMP_BUFFER);
#endif
        return SSL_FAILURE;
    }

    /* set the cipher name on info */
    XSTRNCPY(info->name, cipher, NAME_SZ);

    /* Generate a random salt */
    if (wolfSSL_RAND_bytes(info->iv, info->ivSz) != SSL_SUCCESS) {
        WOLFSSL_MSG("generate iv failed");
#ifdef WOLFSSL_SMALL_STACK
        XFREE(info, NULL, DYNAMIC_TYPE_TMP_BUFFER);
#endif
        return SSL_FAILURE;
    }

    /* add the padding before encryption */
    paddingSz = ((*derSz)/info->ivSz + 1) * info->ivSz - (*derSz);
    if (paddingSz == 0)
        paddingSz = info->ivSz;
    XMEMSET(der+(*derSz), (byte)paddingSz, paddingSz);
    (*derSz) += paddingSz;

    /* encrypt buffer */
    if (wolfssl_encrypt_buffer_key(der, *derSz,
                                   passwd, passwdSz, info) != SSL_SUCCESS) {
        WOLFSSL_MSG("encrypt key failed");
#ifdef WOLFSSL_SMALL_STACK
        XFREE(info, NULL, DYNAMIC_TYPE_TMP_BUFFER);
#endif
        return SSL_FAILURE;
    }

    /* create cipher info : 'cipher_name,Salt(hex)' */
    cipherInfoSz = (word32)(2*info->ivSz + XSTRLEN(info->name) + 2);
    *cipherInfo = (byte*)XMALLOC(cipherInfoSz, NULL,
                                DYNAMIC_TYPE_TMP_BUFFER);
    if (*cipherInfo == NULL) {
        WOLFSSL_MSG("malloc failed");
#ifdef WOLFSSL_SMALL_STACK
        XFREE(info, NULL, DYNAMIC_TYPE_TMP_BUFFER);
#endif
        return SSL_FAILURE;
    }
    XSTRNCPY((char*)*cipherInfo, info->name, cipherInfoSz);
    XSTRNCAT((char*)*cipherInfo, ",", 1);

    idx = (word32)XSTRLEN((char*)*cipherInfo);
    cipherInfoSz -= idx;
    ret = Base16_Encode(info->iv, info->ivSz, *cipherInfo+idx, &cipherInfoSz);

#ifdef WOLFSSL_SMALL_STACK
    XFREE(info, NULL, DYNAMIC_TYPE_TMP_BUFFER);
#endif
    if (ret != 0) {
        WOLFSSL_MSG("Base16_Encode failed");
        XFREE(*cipherInfo, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        return SSL_FAILURE;
    }

    return SSL_SUCCESS;
}
#endif /* defined(WOLFSSL_KEY_GEN) */

#if defined(WOLFSSL_KEY_GEN) || defined(WOLFSSL_CERT_GEN)

int wolfSSL_PEM_write_bio_PrivateKey(WOLFSSL_BIO* bio, WOLFSSL_EVP_PKEY* key,
                                        const WOLFSSL_EVP_CIPHER* cipher,
                                        unsigned char* passwd, int len,
                                        pem_password_cb* cb, void* arg)
{
    byte* keyDer;
    int pemSz;
    int type;
    int ret;
    byte* tmp;

    (void)cipher;
    (void)passwd;
    (void)len;
    (void)cb;
    (void)arg;

    WOLFSSL_ENTER("wolfSSL_PEM_write_bio_PrivateKey");

    if (bio == NULL || key == NULL) {
        return SSL_FAILURE;
    }

    keyDer = (byte*)key->pkey.ptr;

    switch (key->type) {
        case EVP_PKEY_RSA:
            type = PRIVATEKEY_TYPE;
            break;

#ifndef NO_DSA
        case EVP_PKEY_DSA:
            type = DSA_PRIVATEKEY_TYPE;
            break;
#endif

        case EVP_PKEY_EC:
            type = ECC_PRIVATEKEY_TYPE;
            break;

        default:
            WOLFSSL_MSG("Unknown Key type!");
            type = PRIVATEKEY_TYPE;
    }

    pemSz = wc_DerToPem(keyDer, key->pkey_sz, NULL, 0, type);
    if (pemSz < 0) {
        WOLFSSL_LEAVE("wolfSSL_PEM_write_bio_PrivateKey", pemSz);
        return SSL_FAILURE;
    }
    tmp = (byte*)XMALLOC(pemSz, bio->heap, DYNAMIC_TYPE_OPENSSL);
    if (tmp == NULL) {
        return MEMORY_E;
    }

    ret = wc_DerToPemEx(keyDer, key->pkey_sz, tmp, pemSz,
                                NULL, type);
    if (ret < 0) {
        WOLFSSL_LEAVE("wolfSSL_PEM_write_bio_PrivateKey", ret);
        XFREE(tmp, bio->heap, DYNAMIC_TYPE_OPENSSL);
        return SSL_FAILURE;
    }

    ret = wolfSSL_BIO_write(bio, tmp, pemSz);
    XFREE(tmp, bio->heap, DYNAMIC_TYPE_OPENSSL);
    if (ret != pemSz) {
        WOLFSSL_MSG("Unable to write full PEM to BIO");
        return SSL_FAILURE;
    }

    return SSL_SUCCESS;
}
#endif /* defined(WOLFSSL_KEY_GEN) || defined(WOLFSSL_CERT_GEN) */

#if defined(WOLFSSL_KEY_GEN) && !defined(NO_RSA) && !defined(HAVE_USER_RSA)

/* return code compliant with OpenSSL :
 *   1 if success, 0 if error
 */
int wolfSSL_PEM_write_mem_RSAPrivateKey(RSA* rsa, const EVP_CIPHER* cipher,
                                        unsigned char* passwd, int passwdSz,
                                        unsigned char **pem, int *plen)
{
    byte *derBuf, *tmp, *cipherInfo = NULL;
    int  der_max_len = 0, derSz = 0;

    WOLFSSL_ENTER("wolfSSL_PEM_write_mem_RSAPrivateKey");

    if (pem == NULL || plen == NULL || rsa == NULL || rsa->internal == NULL) {
        WOLFSSL_MSG("Bad function arguments");
        return SSL_FAILURE;
    }

    if (rsa->inSet == 0) {
        WOLFSSL_MSG("No RSA internal set, do it");

        if (SetRsaInternal(rsa) != SSL_SUCCESS) {
            WOLFSSL_MSG("SetRsaInternal failed");
            return SSL_FAILURE;
        }
    }

    /* 5 > size of n, d, p, q, d%(p-1), d(q-1), 1/q%p, e + ASN.1 additional
     *  informations
     */
    der_max_len = 5 * wolfSSL_RSA_size(rsa) + AES_BLOCK_SIZE;

    derBuf = (byte*)XMALLOC(der_max_len, NULL, DYNAMIC_TYPE_TMP_BUFFER);
    if (derBuf == NULL) {
        WOLFSSL_MSG("malloc failed");
        return SSL_FAILURE;
    }

    /* Key to DER */
    derSz = wc_RsaKeyToDer((RsaKey*)rsa->internal, derBuf, der_max_len);
    if (derSz < 0) {
        WOLFSSL_MSG("wc_RsaKeyToDer failed");
        XFREE(derBuf, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        return SSL_FAILURE;
    }

    /* encrypt DER buffer if required */
    if (passwd != NULL && passwdSz > 0 && cipher != NULL) {
        int ret;

        ret = EncryptDerKey(derBuf, &derSz, cipher,
                            passwd, passwdSz, &cipherInfo);
        if (ret != SSL_SUCCESS) {
            WOLFSSL_MSG("EncryptDerKey failed");
            XFREE(derBuf, NULL, DYNAMIC_TYPE_TMP_BUFFER);
            return ret;
        }

        /* tmp buffer with a max size */
        *plen = (derSz * 2) + sizeof(BEGIN_RSA_PRIV) +
                sizeof(END_RSA_PRIV) + HEADER_ENCRYPTED_KEY_SIZE;
    }
    else /* tmp buffer with a max size */
        *plen = (derSz * 2) + sizeof(BEGIN_RSA_PRIV) + sizeof(END_RSA_PRIV);

    tmp = (byte*)XMALLOC(*plen, NULL, DYNAMIC_TYPE_TMP_BUFFER);
    if (tmp == NULL) {
        WOLFSSL_MSG("malloc failed");
        XFREE(derBuf, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        if (cipherInfo != NULL)
            XFREE(cipherInfo, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        return SSL_FAILURE;
    }

    /* DER to PEM */
    *plen = wc_DerToPemEx(derBuf, derSz, tmp, *plen, cipherInfo, PRIVATEKEY_TYPE);
    if (*plen <= 0) {
        WOLFSSL_MSG("wc_DerToPemEx failed");
        XFREE(derBuf, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        XFREE(tmp, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        if (cipherInfo != NULL)
            XFREE(cipherInfo, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        return SSL_FAILURE;
    }
    XFREE(derBuf, NULL, DYNAMIC_TYPE_TMP_BUFFER);
    if (cipherInfo != NULL)
        XFREE(cipherInfo, NULL, DYNAMIC_TYPE_TMP_BUFFER);

    *pem = (byte*)XMALLOC((*plen)+1, NULL, DYNAMIC_TYPE_KEY);
    if (*pem == NULL) {
        WOLFSSL_MSG("malloc failed");
        XFREE(tmp, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        return SSL_FAILURE;
    }
    XMEMSET(*pem, 0, (*plen)+1);

    if (XMEMCPY(*pem, tmp, *plen) == NULL) {
        WOLFSSL_MSG("XMEMCPY failed");
        XFREE(pem, NULL, DYNAMIC_TYPE_KEY);
        XFREE(tmp, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        return SSL_FAILURE;
    }
    XFREE(tmp, NULL, DYNAMIC_TYPE_TMP_BUFFER);

    return SSL_SUCCESS;
}


#ifndef NO_FILESYSTEM
/* return code compliant with OpenSSL :
 *   1 if success, 0 if error
 */
int wolfSSL_PEM_write_RSAPrivateKey(FILE *fp, WOLFSSL_RSA *rsa,
                                    const EVP_CIPHER *enc,
                                    unsigned char *kstr, int klen,
                                    pem_password_cb *cb, void *u)
{
    byte *pem;
    int  plen, ret;

    (void)cb;
    (void)u;

    WOLFSSL_MSG("wolfSSL_PEM_write_RSAPrivateKey");

    if (fp == NULL || rsa == NULL || rsa->internal == NULL) {
        WOLFSSL_MSG("Bad function arguments");
        return SSL_FAILURE;
    }

    ret = wolfSSL_PEM_write_mem_RSAPrivateKey(rsa, enc, kstr, klen, &pem, &plen);
    if (ret != SSL_SUCCESS) {
        WOLFSSL_MSG("wolfSSL_PEM_write_mem_RSAPrivateKey failed");
        return SSL_FAILURE;
    }

    ret = (int)XFWRITE(pem, plen, 1, fp);
    if (ret != 1) {
        WOLFSSL_MSG("RSA private key file write failed");
        return SSL_FAILURE;
    }

    XFREE(pem, NULL, DYNAMIC_TYPE_KEY);
    return SSL_SUCCESS;
}
#endif /* NO_FILESYSTEM */

int wolfSSL_PEM_write_bio_RSAPrivateKey(WOLFSSL_BIO* bio, RSA* rsa,
                                        const EVP_CIPHER* cipher,
                                        unsigned char* passwd, int len,
                                        pem_password_cb* cb, void* arg)
{
    (void)bio;
    (void)rsa;
    (void)cipher;
    (void)passwd;
    (void)len;
    (void)cb;
    (void)arg;

    WOLFSSL_MSG("wolfSSL_PEM_write_bio_RSAPrivateKey not implemented");

    return SSL_FAILURE;
}
#endif /* defined(WOLFSSL_KEY_GEN) && !defined(NO_RSA) */

#ifdef HAVE_ECC

/* EC_POINT Openssl -> WolfSSL */
static int SetECPointInternal(WOLFSSL_EC_POINT *p)
{
    ecc_point* point;
    WOLFSSL_ENTER("SetECPointInternal");

    if (p == NULL || p->internal == NULL) {
        WOLFSSL_MSG("ECPoint NULL error");
        return SSL_FATAL_ERROR;
    }

    point = (ecc_point*)p->internal;

    if (p->X != NULL && SetIndividualInternal(p->X, point->x) != SSL_SUCCESS) {
        WOLFSSL_MSG("ecc point X error");
        return SSL_FATAL_ERROR;
    }

    if (p->Y != NULL && SetIndividualInternal(p->Y, point->y) != SSL_SUCCESS) {
        WOLFSSL_MSG("ecc point Y error");
        return SSL_FATAL_ERROR;
    }

    if (p->Z != NULL && SetIndividualInternal(p->Z, point->z) != SSL_SUCCESS) {
        WOLFSSL_MSG("ecc point Z error");
        return SSL_FATAL_ERROR;
    }

    p->inSet = 1;

    return SSL_SUCCESS;
}

/* EC_POINT WolfSSL -> OpenSSL */
static int SetECPointExternal(WOLFSSL_EC_POINT *p)
{
    ecc_point* point;

    WOLFSSL_ENTER("SetECPointExternal");

    if (p == NULL || p->internal == NULL) {
        WOLFSSL_MSG("ECPoint NULL error");
        return SSL_FATAL_ERROR;
    }

    point = (ecc_point*)p->internal;

    if (SetIndividualExternal(&p->X, point->x) != SSL_SUCCESS) {
        WOLFSSL_MSG("ecc point X error");
        return SSL_FATAL_ERROR;
    }

    if (SetIndividualExternal(&p->Y, point->y) != SSL_SUCCESS) {
        WOLFSSL_MSG("ecc point Y error");
        return SSL_FATAL_ERROR;
    }

    if (SetIndividualExternal(&p->Z, point->z) != SSL_SUCCESS) {
        WOLFSSL_MSG("ecc point Z error");
        return SSL_FATAL_ERROR;
    }

    p->exSet = 1;

    return SSL_SUCCESS;
}

/* EC_KEY wolfSSL -> OpenSSL */
static int SetECKeyExternal(WOLFSSL_EC_KEY* eckey)
{
    ecc_key* key;

    WOLFSSL_ENTER("SetECKeyExternal");

    if (eckey == NULL || eckey->internal == NULL) {
        WOLFSSL_MSG("ec key NULL error");
        return SSL_FATAL_ERROR;
    }

    key = (ecc_key*)eckey->internal;

    /* set group (nid and idx) */
    eckey->group->curve_nid = ecc_sets[key->idx].id;
    eckey->group->curve_idx = key->idx;

    if (eckey->pub_key->internal != NULL) {
        /* set the internal public key */
        if (wc_ecc_copy_point(&key->pubkey,
                             (ecc_point*)eckey->pub_key->internal) != MP_OKAY) {
            WOLFSSL_MSG("SetECKeyExternal ecc_copy_point failed");
            return SSL_FATAL_ERROR;
        }

        /* set the external pubkey (point) */
        if (SetECPointExternal(eckey->pub_key) != SSL_SUCCESS) {
            WOLFSSL_MSG("SetECKeyExternal SetECPointExternal failed");
            return SSL_FATAL_ERROR;
        }
    }

    /* set the external privkey */
    if (key->type == ECC_PRIVATEKEY) {
        if (SetIndividualExternal(&eckey->priv_key, &key->k) != SSL_SUCCESS) {
            WOLFSSL_MSG("ec priv key error");
            return SSL_FATAL_ERROR;
        }
    }

    eckey->exSet = 1;

    return SSL_SUCCESS;
}

/* EC_KEY Openssl -> WolfSSL */
static int SetECKeyInternal(WOLFSSL_EC_KEY* eckey)
{
    ecc_key* key;

    WOLFSSL_ENTER("SetECKeyInternal");

    if (eckey == NULL || eckey->internal == NULL) {
        WOLFSSL_MSG("ec key NULL error");
        return SSL_FATAL_ERROR;
    }

    key = (ecc_key*)eckey->internal;

    /* validate group */
    if ((eckey->group->curve_idx < 0) ||
        (wc_ecc_is_valid_idx(eckey->group->curve_idx) == 0)) {
        WOLFSSL_MSG("invalid curve idx");
        return SSL_FATAL_ERROR;
    }

    /* set group (idx of curve and corresponding domain parameters) */
    key->idx = eckey->group->curve_idx;
    key->dp = &ecc_sets[key->idx];

    /* set pubkey (point) */
    if (eckey->pub_key != NULL) {
        if (SetECPointInternal(eckey->pub_key) != SSL_SUCCESS) {
            WOLFSSL_MSG("ec key pub error");
            return SSL_FATAL_ERROR;
        }

        /* public key */
        key->type = ECC_PUBLICKEY;
    }

    /* set privkey */
    if (eckey->priv_key != NULL) {
        if (SetIndividualInternal(eckey->priv_key, &key->k) != SSL_SUCCESS) {
            WOLFSSL_MSG("ec key priv error");
            return SSL_FATAL_ERROR;
        }

        /* private key */
        key->type = ECC_PRIVATEKEY;
    }

    eckey->inSet = 1;

    return SSL_SUCCESS;
}

WOLFSSL_EC_POINT *wolfSSL_EC_KEY_get0_public_key(const WOLFSSL_EC_KEY *key)
{
    WOLFSSL_ENTER("wolfSSL_EC_KEY_get0_public_key");

    if (key == NULL) {
        WOLFSSL_MSG("wolfSSL_EC_KEY_get0_group Bad arguments");
        return NULL;
    }

    return key->pub_key;
}

const WOLFSSL_EC_GROUP *wolfSSL_EC_KEY_get0_group(const WOLFSSL_EC_KEY *key)
{
    WOLFSSL_ENTER("wolfSSL_EC_KEY_get0_group");

    if (key == NULL) {
        WOLFSSL_MSG("wolfSSL_EC_KEY_get0_group Bad arguments");
        return NULL;
    }

    return key->group;
}


/* return code compliant with OpenSSL :
 *   1 if success, 0 if error
 */
int wolfSSL_EC_KEY_set_private_key(WOLFSSL_EC_KEY *key,
                                   const WOLFSSL_BIGNUM *priv_key)
{
    WOLFSSL_ENTER("wolfSSL_EC_KEY_set_private_key");

    if (key == NULL || priv_key == NULL) {
        WOLFSSL_MSG("Bad arguments");
        return SSL_FAILURE;
    }

    /* free key if previously set */
    if (key->priv_key != NULL)
        wolfSSL_BN_free(key->priv_key);

    key->priv_key = wolfSSL_BN_dup(priv_key);
    if (key->priv_key == NULL) {
        WOLFSSL_MSG("key ecc priv key NULL");
        return SSL_FAILURE;
    }

    if (SetECKeyInternal(key) != SSL_SUCCESS) {
        WOLFSSL_MSG("SetECKeyInternal failed");
        wolfSSL_BN_free(key->priv_key);
        return SSL_FAILURE;
    }

    return SSL_SUCCESS;
}


WOLFSSL_BIGNUM *wolfSSL_EC_KEY_get0_private_key(const WOLFSSL_EC_KEY *key)
{
    WOLFSSL_ENTER("wolfSSL_EC_KEY_get0_private_key");

    if (key == NULL) {
        WOLFSSL_MSG("wolfSSL_EC_KEY_get0_private_key Bad arguments");
        return NULL;
    }

    return key->priv_key;
}

WOLFSSL_EC_KEY *wolfSSL_EC_KEY_new_by_curve_name(int nid)
{
    WOLFSSL_EC_KEY *key;
    int x;

    WOLFSSL_ENTER("wolfSSL_EC_KEY_new_by_curve_name");

    key = wolfSSL_EC_KEY_new();
    if (key == NULL) {
        WOLFSSL_MSG("wolfSSL_EC_KEY_new failure");
        return NULL;
    }

    /* set the nid of the curve */
    key->group->curve_nid = nid;

    /* search and set the corresponding internal curve idx */
    for (x = 0; ecc_sets[x].size != 0; x++)
        if (ecc_sets[x].id == key->group->curve_nid) {
            key->group->curve_idx = x;
            break;
        }

    return key;
}

static void InitwolfSSL_ECKey(WOLFSSL_EC_KEY* key)
{
    if (key) {
        key->group    = NULL;
        key->pub_key  = NULL;
        key->priv_key = NULL;
        key->internal = NULL;
        key->inSet    = 0;
        key->exSet    = 0;
    }
}

WOLFSSL_EC_KEY *wolfSSL_EC_KEY_new(void)
{
    WOLFSSL_EC_KEY *external;
    ecc_key* key;

    WOLFSSL_ENTER("wolfSSL_EC_KEY_new");

    external = (WOLFSSL_EC_KEY*)XMALLOC(sizeof(WOLFSSL_EC_KEY), NULL,
                                        DYNAMIC_TYPE_ECC);
    if (external == NULL) {
        WOLFSSL_MSG("wolfSSL_EC_KEY_new malloc WOLFSSL_EC_KEY failure");
        return NULL;
    }
    XMEMSET(external, 0, sizeof(WOLFSSL_EC_KEY));

    InitwolfSSL_ECKey(external);

    external->internal = (ecc_key*)XMALLOC(sizeof(ecc_key), NULL,
                                           DYNAMIC_TYPE_ECC);
    if (external->internal == NULL) {
        WOLFSSL_MSG("wolfSSL_EC_KEY_new malloc ecc key failure");
        wolfSSL_EC_KEY_free(external);
        return NULL;
    }
    XMEMSET(external->internal, 0, sizeof(ecc_key));

    wc_ecc_init((ecc_key*)external->internal);

    /* public key */
    external->pub_key = (WOLFSSL_EC_POINT*)XMALLOC(sizeof(WOLFSSL_EC_POINT),
                                                   NULL, DYNAMIC_TYPE_ECC);
    if (external->pub_key == NULL) {
        WOLFSSL_MSG("wolfSSL_EC_KEY_new malloc WOLFSSL_EC_POINT failure");
        wolfSSL_EC_KEY_free(external);
        return NULL;
    }
    XMEMSET(external->pub_key, 0, sizeof(WOLFSSL_EC_POINT));

    key = (ecc_key*)external->internal;
    external->pub_key->internal = (ecc_point*)&key->pubkey;

    /* curve group */
    external->group = (WOLFSSL_EC_GROUP*)XMALLOC(sizeof(WOLFSSL_EC_GROUP), NULL,
                                                 DYNAMIC_TYPE_ECC);
    if (external->group == NULL) {
        WOLFSSL_MSG("wolfSSL_EC_KEY_new malloc WOLFSSL_EC_GROUP failure");
        wolfSSL_EC_KEY_free(external);
        return NULL;
    }
    XMEMSET(external->group, 0, sizeof(WOLFSSL_EC_GROUP));

    /* private key */
    external->priv_key = wolfSSL_BN_new();
    if (external->priv_key == NULL) {
        WOLFSSL_MSG("wolfSSL_BN_new failure");
        wolfSSL_EC_KEY_free(external);
        return NULL;
    }

    return external;
}

void wolfSSL_EC_KEY_free(WOLFSSL_EC_KEY *key)
{
    WOLFSSL_ENTER("wolfSSL_EC_KEY_free");

    if (key != NULL) {
        if (key->internal != NULL) {
            wc_ecc_free((ecc_key*)key->internal);
            XFREE(key->internal, NULL, DYNAMIC_TYPE_ECC);
        }
        wolfSSL_BN_free(key->priv_key);
        wolfSSL_EC_POINT_free(key->pub_key);
        wolfSSL_EC_GROUP_free(key->group);
        InitwolfSSL_ECKey(key); /* set back to NULLs for safety */

        XFREE(key, NULL, DYNAMIC_TYPE_ECC);
        key = NULL;
    }
}

int wolfSSL_EC_KEY_set_group(WOLFSSL_EC_KEY *key, WOLFSSL_EC_GROUP *group)
{
    (void)key;
    (void)group;

    WOLFSSL_ENTER("wolfSSL_EC_KEY_set_group");
    WOLFSSL_MSG("wolfSSL_EC_KEY_set_group TBD");

    return -1;
}

int wolfSSL_EC_KEY_generate_key(WOLFSSL_EC_KEY *key)
{
    int     initTmpRng = 0;
    WC_RNG* rng = NULL;
#ifdef WOLFSSL_SMALL_STACK
    WC_RNG* tmpRNG = NULL;
#else
    WC_RNG  tmpRNG[1];
#endif

    WOLFSSL_ENTER("wolfSSL_EC_KEY_generate_key");

    if (key == NULL || key->internal == NULL ||
        key->group == NULL || key->group->curve_idx < 0) {
        WOLFSSL_MSG("wolfSSL_EC_KEY_generate_key Bad arguments");
        return 0;
    }

#ifdef WOLFSSL_SMALL_STACK
    tmpRNG = (WC_RNG*)XMALLOC(sizeof(WC_RNG), NULL, DYNAMIC_TYPE_TMP_BUFFER);
    if (tmpRNG == NULL)
        return 0;
#endif

    if (wc_InitRng(tmpRNG) == 0) {
        rng = tmpRNG;
        initTmpRng = 1;
    }
    else {
        WOLFSSL_MSG("Bad RNG Init, trying global");
        if (initGlobalRNG == 0)
            WOLFSSL_MSG("Global RNG no Init");
        else
            rng = &globalRNG;
    }

    if (rng == NULL) {
        WOLFSSL_MSG("wolfSSL_EC_KEY_generate_key failed to set RNG");
#ifdef WOLFSSL_SMALL_STACK
        XFREE(tmpRNG, NULL, DYNAMIC_TYPE_TMP_BUFFER);
#endif
        return 0;
    }

    if (wc_ecc_make_key_ex(rng, 0, (ecc_key*)key->internal,
                                        key->group->curve_nid) != MP_OKAY) {
        WOLFSSL_MSG("wolfSSL_EC_KEY_generate_key wc_ecc_make_key failed");
#ifdef WOLFSSL_SMALL_STACK
        XFREE(tmpRNG, NULL, DYNAMIC_TYPE_TMP_BUFFER);
#endif
        return 0;
    }

    if (initTmpRng)
        wc_FreeRng(tmpRNG);
#ifdef WOLFSSL_SMALL_STACK
    XFREE(tmpRNG, NULL, DYNAMIC_TYPE_TMP_BUFFER);
#endif

    if (SetECKeyExternal(key) != SSL_SUCCESS) {
        WOLFSSL_MSG("wolfSSL_EC_KEY_generate_key SetECKeyExternal failed");
        return 0;
    }

    return 1;
}

void wolfSSL_EC_KEY_set_asn1_flag(WOLFSSL_EC_KEY *key, int asn1_flag)
{
    (void)key;
    (void)asn1_flag;

    WOLFSSL_ENTER("wolfSSL_EC_KEY_set_asn1_flag");
    WOLFSSL_MSG("wolfSSL_EC_KEY_set_asn1_flag TBD");
}

/* return code compliant with OpenSSL :
 *   1 if success, 0 if error
 */
int wolfSSL_EC_KEY_set_public_key(WOLFSSL_EC_KEY *key,
                                  const WOLFSSL_EC_POINT *pub)
{
    ecc_point *pub_p, *key_p;

    WOLFSSL_ENTER("wolfSSL_EC_KEY_set_public_key");

    if (key == NULL || key->internal == NULL ||
        pub == NULL || pub->internal == NULL) {
        WOLFSSL_MSG("wolfSSL_EC_GROUP_get_order Bad arguments");
        return SSL_FAILURE;
    }

    if (key->inSet == 0) {
        if (SetECKeyInternal(key) != SSL_SUCCESS) {
            WOLFSSL_MSG("SetECKeyInternal failed");
            return SSL_FAILURE;
        }
    }

    if (pub->inSet == 0) {
        if (SetECPointInternal((WOLFSSL_EC_POINT *)pub) != SSL_SUCCESS) {
            WOLFSSL_MSG("SetECPointInternal failed");
            return SSL_FAILURE;
        }
    }

    pub_p = (ecc_point*)pub->internal;
    key_p = (ecc_point*)key->pub_key->internal;

    /* create new point if required */
    if (key_p == NULL)
        key_p = wc_ecc_new_point();

    if (key_p == NULL) {
        WOLFSSL_MSG("key ecc point NULL");
        return SSL_FAILURE;
    }

    if (wc_ecc_copy_point(pub_p, key_p) != MP_OKAY) {
        WOLFSSL_MSG("ecc_copy_point failure");
        return SSL_FAILURE;
    }

    if (SetECKeyExternal(key) != SSL_SUCCESS) {
        WOLFSSL_MSG("SetECKeyInternal failed");
        return SSL_FAILURE;
    }

#if defined(DEBUG_WOLFSSL) && !defined(NO_FILESYSTEM)
    wolfssl_EC_POINT_dump("pub", pub);
    wolfssl_EC_POINT_dump("key->pub_key", key->pub_key);
#endif
    return SSL_SUCCESS;
}
/* End EC_KEY */

#if defined(DEBUG_WOLFSSL) && !defined(NO_FILESYSTEM)
void wolfssl_EC_POINT_dump(const char *msg, const WOLFSSL_EC_POINT *p)
{
    char *num;

    WOLFSSL_ENTER("wolfssl_EC_POINT_dump");

    if (p == NULL) {
        fprintf(stderr, "%s = NULL", msg);
        return ;
    }

    fprintf(stderr, "%s:\n\tinSet=%d, exSet=%d\n", msg, p->inSet, p->exSet);
    num = wolfSSL_BN_bn2hex(p->X);
    fprintf(stderr, "\tX = %s\n", num);
    XFREE(num, NULL, DYNAMIC_TYPE_ECC);
    num = wolfSSL_BN_bn2hex(p->Y);
    fprintf(stderr, "\tY = %s\n", num);
    XFREE(num, NULL, DYNAMIC_TYPE_ECC);
}
#endif

/* Start EC_GROUP */

/* return code compliant with OpenSSL :
 *   0 if equal, 1 if not and -1 in case of error
 */
int wolfSSL_EC_GROUP_cmp(const WOLFSSL_EC_GROUP *a, const WOLFSSL_EC_GROUP *b,
                         WOLFSSL_BN_CTX *ctx)
{
    (void)ctx;

    WOLFSSL_ENTER("wolfSSL_EC_GROUP_cmp");

    if (a == NULL || b == NULL) {
        WOLFSSL_MSG("wolfSSL_EC_GROUP_cmp Bad arguments");
        return SSL_FATAL_ERROR;
    }

    /* ok */
    if ((a->curve_idx == b->curve_idx) && (a->curve_nid == b->curve_nid))
        return 0;

    /* ko */
    return 1;
}

void wolfSSL_EC_GROUP_free(WOLFSSL_EC_GROUP *group)
{
    WOLFSSL_ENTER("wolfSSL_EC_GROUP_free");

    XFREE(group, NULL, DYNAMIC_TYPE_ECC);
    group = NULL;
}

void wolfSSL_EC_GROUP_set_asn1_flag(WOLFSSL_EC_GROUP *group, int flag)
{
    (void)group;
    (void)flag;

    WOLFSSL_ENTER("wolfSSL_EC_GROUP_set_asn1_flag");
    WOLFSSL_MSG("wolfSSL_EC_GROUP_set_asn1_flag TBD");
}

WOLFSSL_EC_GROUP *wolfSSL_EC_GROUP_new_by_curve_name(int nid)
{
    WOLFSSL_EC_GROUP *g;
    int x;

    WOLFSSL_ENTER("wolfSSL_EC_GROUP_new_by_curve_name");

    /* curve group */
    g = (WOLFSSL_EC_GROUP*) XMALLOC(sizeof(WOLFSSL_EC_GROUP), NULL,
                                    DYNAMIC_TYPE_ECC);
    if (g == NULL) {
        WOLFSSL_MSG("wolfSSL_EC_GROUP_new_by_curve_name malloc failure");
        return NULL;
    }
    XMEMSET(g, 0, sizeof(WOLFSSL_EC_GROUP));

    /* set the nid of the curve */
    g->curve_nid = nid;

    /* search and set the corresponding internal curve idx */
    for (x = 0; ecc_sets[x].size != 0; x++)
        if (ecc_sets[x].id == g->curve_nid) {
            g->curve_idx = x;
            break;
        }

    return g;
}

/* return code compliant with OpenSSL :
 *   the curve nid if success, 0 if error
 */
int wolfSSL_EC_GROUP_get_curve_name(const WOLFSSL_EC_GROUP *group)
{
    WOLFSSL_ENTER("wolfSSL_EC_GROUP_get_curve_name");

    if (group == NULL) {
        WOLFSSL_MSG("wolfSSL_EC_GROUP_get_curve_name Bad arguments");
        return SSL_FAILURE;
    }

    return group->curve_nid;
}

/* return code compliant with OpenSSL :
 *   the degree of the curve if success, 0 if error
 */
int wolfSSL_EC_GROUP_get_degree(const WOLFSSL_EC_GROUP *group)
{
    WOLFSSL_ENTER("wolfSSL_EC_GROUP_get_degree");

    if (group == NULL || group->curve_idx < 0) {
        WOLFSSL_MSG("wolfSSL_EC_GROUP_get_degree Bad arguments");
        return SSL_FAILURE;
    }

    switch(group->curve_nid) {
        case NID_secp112r1:
        case NID_secp112r2:
            return 112;
        case NID_secp128r1:
        case NID_secp128r2:
            return 128;
        case NID_secp160k1:
        case NID_secp160r1:
        case NID_secp160r2:
        case NID_brainpoolP160r1:
            return 160;
        case NID_secp192k1:
        case NID_brainpoolP192r1:
        case NID_X9_62_prime192v1:
            return 192;
        case NID_secp224k1:
        case NID_secp224r1:
        case NID_brainpoolP224r1:
            return 224;
        case NID_secp256k1:
        case NID_brainpoolP256r1:
        case NID_X9_62_prime256v1:
            return 256;
        case NID_brainpoolP320r1:
            return 320;
        case NID_secp384r1:
        case NID_brainpoolP384r1:
            return 384;
        case NID_secp521r1:
        case NID_brainpoolP512r1:
            return 521;
        default:
            return SSL_FAILURE;
    }
}

/* return code compliant with OpenSSL :
 *   1 if success, 0 if error
 */
int wolfSSL_EC_GROUP_get_order(const WOLFSSL_EC_GROUP *group,
                               WOLFSSL_BIGNUM *order, WOLFSSL_BN_CTX *ctx)
{
    (void)ctx;

    if (group == NULL || order == NULL || order->internal == NULL) {
        WOLFSSL_MSG("wolfSSL_EC_GROUP_get_order NULL error");
        return SSL_FAILURE;
    }

    if (mp_init((mp_int*)order->internal) != MP_OKAY) {
        WOLFSSL_MSG("wolfSSL_EC_GROUP_get_order mp_init failure");
        return SSL_FAILURE;
    }

    if (mp_read_radix((mp_int*)order->internal,
                      ecc_sets[group->curve_idx].order, 16) != MP_OKAY) {
        WOLFSSL_MSG("wolfSSL_EC_GROUP_get_order mp_read order failure");
        mp_clear((mp_int*)order->internal);
        return SSL_FAILURE;
    }

    return SSL_SUCCESS;
}
/* End EC_GROUP */

/* Start EC_POINT */

/* return code compliant with OpenSSL :
 *   1 if success, 0 if error
 */
int wolfSSL_ECPoint_i2d(const WOLFSSL_EC_GROUP *group,
                        const WOLFSSL_EC_POINT *p,
                        unsigned char *out, unsigned int *len)
{
    int err;

    WOLFSSL_ENTER("wolfSSL_ECPoint_i2d");

    if (group == NULL || p == NULL || len == NULL) {
        WOLFSSL_MSG("wolfSSL_ECPoint_i2d NULL error");
        return SSL_FAILURE;
    }

    if (p->inSet == 0) {
        WOLFSSL_MSG("No ECPoint internal set, do it");

        if (SetECPointInternal((WOLFSSL_EC_POINT *)p) != SSL_SUCCESS) {
            WOLFSSL_MSG("SetECPointInternal SetECPointInternal failed");
            return SSL_FAILURE;
        }
    }

#if defined(DEBUG_WOLFSSL) && !defined(NO_FILESYSTEM)
    if (out != NULL) {
        wolfssl_EC_POINT_dump("i2d p", p);
    }
#endif
    err = wc_ecc_export_point_der(group->curve_idx, (ecc_point*)p->internal,
                                  out, len);
    if (err != MP_OKAY && !(out == NULL && err == LENGTH_ONLY_E)) {
        WOLFSSL_MSG("wolfSSL_ECPoint_i2d wc_ecc_export_point_der failed");
        return SSL_FAILURE;
    }

    return SSL_SUCCESS;
}

/* return code compliant with OpenSSL :
 *   1 if success, 0 if error
 */
int wolfSSL_ECPoint_d2i(unsigned char *in, unsigned int len,
                        const WOLFSSL_EC_GROUP *group, WOLFSSL_EC_POINT *p)
{
    WOLFSSL_ENTER("wolfSSL_ECPoint_d2i");

    if (group == NULL || p == NULL || p->internal == NULL || in == NULL) {
        WOLFSSL_MSG("wolfSSL_ECPoint_d2i NULL error");
        return SSL_FAILURE;
    }

    if (wc_ecc_import_point_der(in, len, group->curve_idx,
                                (ecc_point*)p->internal) != MP_OKAY) {
        WOLFSSL_MSG("wc_ecc_import_point_der failed");
        return SSL_FAILURE;
    }

    if (p->exSet == 0) {
        WOLFSSL_MSG("No ECPoint external set, do it");

        if (SetECPointExternal(p) != SSL_SUCCESS) {
            WOLFSSL_MSG("SetECPointExternal failed");
            return SSL_FAILURE;
        }
    }

#if defined(DEBUG_WOLFSSL) && !defined(NO_FILESYSTEM)
    wolfssl_EC_POINT_dump("d2i p", p);
#endif
    return SSL_SUCCESS;
}

WOLFSSL_EC_POINT *wolfSSL_EC_POINT_new(const WOLFSSL_EC_GROUP *group)
{
    WOLFSSL_EC_POINT *p;

    WOLFSSL_ENTER("wolfSSL_EC_POINT_new");

    if (group == NULL) {
        WOLFSSL_MSG("wolfSSL_EC_POINT_new NULL error");
        return NULL;
    }

    p = (WOLFSSL_EC_POINT *)XMALLOC(sizeof(WOLFSSL_EC_POINT), NULL,
                                    DYNAMIC_TYPE_ECC);
    if (p == NULL) {
        WOLFSSL_MSG("wolfSSL_EC_POINT_new malloc ecc point failure");
        return NULL;
    }
    XMEMSET(p, 0, sizeof(WOLFSSL_EC_POINT));

    p->internal = wc_ecc_new_point();
    if (p->internal == NULL) {
        WOLFSSL_MSG("ecc_new_point failure");
        XFREE(p, NULL, DYNAMIC_TYPE_ECC);
        return NULL;
    }

    return p;
}

/* return code compliant with OpenSSL :
 *   1 if success, 0 if error
 */
int wolfSSL_EC_POINT_get_affine_coordinates_GFp(const WOLFSSL_EC_GROUP *group,
                                                const WOLFSSL_EC_POINT *point,
                                                WOLFSSL_BIGNUM *x,
                                                WOLFSSL_BIGNUM *y,
                                                WOLFSSL_BN_CTX *ctx)
{
    (void)ctx;

    WOLFSSL_ENTER("wolfSSL_EC_POINT_get_affine_coordinates_GFp");

    if (group == NULL || point == NULL || point->internal == NULL ||
        x == NULL || y == NULL) {
        WOLFSSL_MSG("wolfSSL_EC_POINT_get_affine_coordinates_GFp NULL error");
        return SSL_FAILURE;
    }

    if (point->inSet == 0) {
        WOLFSSL_MSG("No ECPoint internal set, do it");

        if (SetECPointInternal((WOLFSSL_EC_POINT *)point) != SSL_SUCCESS) {
            WOLFSSL_MSG("SetECPointInternal failed");
            return SSL_FAILURE;
        }
    }

    BN_copy(x, point->X);
    BN_copy(y, point->Y);

    return SSL_SUCCESS;
}

/* return code compliant with OpenSSL :
 *   1 if success, 0 if error
 */
int wolfSSL_EC_POINT_mul(const WOLFSSL_EC_GROUP *group, WOLFSSL_EC_POINT *r,
                         const WOLFSSL_BIGNUM *n, const WOLFSSL_EC_POINT *q,
                         const WOLFSSL_BIGNUM *m, WOLFSSL_BN_CTX *ctx)
{
    mp_int a, prime;

    (void)ctx;
    (void)n;

    WOLFSSL_ENTER("wolfSSL_EC_POINT_mul");

    if (group == NULL || r == NULL || r->internal == NULL ||
        q == NULL || q->internal == NULL || m == NULL) {
        WOLFSSL_MSG("wolfSSL_EC_POINT_mul NULL error");
        return SSL_FAILURE;
    }

    if (q->inSet == 0) {
        WOLFSSL_MSG("No ECPoint internal set, do it");

        if (SetECPointInternal((WOLFSSL_EC_POINT *)q) != SSL_SUCCESS) {
            WOLFSSL_MSG("SetECPointInternal failed");
            return SSL_FAILURE;
        }
    }

    /* read the curve prime and a */
    if (mp_init_multi(&prime, &a, NULL, NULL, NULL, NULL) != MP_OKAY) {
        WOLFSSL_MSG("wolfSSL_EC_POINT_mul init 'prime/A' failed");
        return SSL_FAILURE;
    }
    if (mp_read_radix(&prime, ecc_sets[group->curve_idx].prime, 16) != MP_OKAY){
        WOLFSSL_MSG("wolfSSL_EC_POINT_mul read 'prime' curve value failed");
        return SSL_FAILURE;
    }
    if (mp_read_radix(&a, ecc_sets[group->curve_idx].Af, 16) != MP_OKAY){
        WOLFSSL_MSG("wolfSSL_EC_POINT_mul read 'A' curve value failed");
        return SSL_FAILURE;
    }

    /* r = q * m % prime */
    if (wc_ecc_mulmod((mp_int*)m->internal, (ecc_point*)q->internal,
                      (ecc_point*)r->internal, &a, &prime, 1) != MP_OKAY) {
        WOLFSSL_MSG("ecc_mulmod failure");
        mp_clear(&prime);
        return SSL_FAILURE;
    }

    mp_clear(&a);
    mp_clear(&prime);

    /* set the external value for the computed point */
    if (SetECPointInternal(r) != SSL_SUCCESS) {
        WOLFSSL_MSG("SetECPointInternal failed");
        return SSL_FAILURE;
    }

    return SSL_SUCCESS;
}

void wolfSSL_EC_POINT_clear_free(WOLFSSL_EC_POINT *p)
{
    WOLFSSL_ENTER("wolfSSL_EC_POINT_clear_free");

    wolfSSL_EC_POINT_free(p);
}

/* return code compliant with OpenSSL :
 *   0 if equal, 1 if not and -1 in case of error
 */
int wolfSSL_EC_POINT_cmp(const WOLFSSL_EC_GROUP *group,
                         const WOLFSSL_EC_POINT *a, const WOLFSSL_EC_POINT *b,
                         WOLFSSL_BN_CTX *ctx)
{
    int ret;

	(void)ctx;

    WOLFSSL_ENTER("wolfSSL_EC_POINT_cmp");

    if (group == NULL || a == NULL || a->internal == NULL || b == NULL ||
        b->internal == NULL) {
        WOLFSSL_MSG("wolfSSL_EC_POINT_cmp Bad arguments");
        return SSL_FATAL_ERROR;
    }

    ret = wc_ecc_cmp_point((ecc_point*)a->internal, (ecc_point*)b->internal);
    if (ret == MP_EQ)
        return 0;
    else if (ret == MP_LT || ret == MP_GT)
        return 1;

    return SSL_FATAL_ERROR;
}

void wolfSSL_EC_POINT_free(WOLFSSL_EC_POINT *p)
{
    WOLFSSL_ENTER("wolfSSL_EC_POINT_free");

    if (p != NULL) {
        if (p->internal == NULL) {
            wc_ecc_del_point((ecc_point*)p->internal);
            XFREE(p->internal, NULL, DYNAMIC_TYPE_ECC);
            p->internal = NULL;
        }

        wolfSSL_BN_free(p->X);
        wolfSSL_BN_free(p->Y);
        wolfSSL_BN_free(p->Z);
        p->X = NULL;
        p->Y = NULL;
        p->Z = NULL;
        p->inSet = p->exSet = 0;

        XFREE(p, NULL, DYNAMIC_TYPE_ECC);
        p = NULL;
    }
}

/* return code compliant with OpenSSL :
 *   1 if point at infinity, 0 else
 */
int wolfSSL_EC_POINT_is_at_infinity(const WOLFSSL_EC_GROUP *group,
                                    const WOLFSSL_EC_POINT *point)
{
    int ret;

    WOLFSSL_ENTER("wolfSSL_EC_POINT_is_at_infinity");

    if (group == NULL || point == NULL || point->internal == NULL) {
        WOLFSSL_MSG("wolfSSL_EC_POINT_is_at_infinity NULL error");
        return SSL_FAILURE;
    }
    if (point->inSet == 0) {
        WOLFSSL_MSG("No ECPoint internal set, do it");

        if (SetECPointInternal((WOLFSSL_EC_POINT *)point) != SSL_SUCCESS) {
            WOLFSSL_MSG("SetECPointInternal failed");
            return SSL_FAILURE;
        }
    }

    ret = wc_ecc_point_is_at_infinity((ecc_point*)point->internal);
    if (ret <= 0) {
        WOLFSSL_MSG("ecc_point_is_at_infinity failure");
        return SSL_FAILURE;
    }

    return SSL_SUCCESS;
}

/* End EC_POINT */

/* Start ECDSA_SIG */
void wolfSSL_ECDSA_SIG_free(WOLFSSL_ECDSA_SIG *sig)
{
    WOLFSSL_ENTER("wolfSSL_ECDSA_SIG_free");

    if (sig) {
        wolfSSL_BN_free(sig->r);
        wolfSSL_BN_free(sig->s);

        XFREE(sig, NULL, DYNAMIC_TYPE_ECC);
    }
}

WOLFSSL_ECDSA_SIG *wolfSSL_ECDSA_SIG_new(void)
{
    WOLFSSL_ECDSA_SIG *sig;

    WOLFSSL_ENTER("wolfSSL_ECDSA_SIG_new");

    sig = (WOLFSSL_ECDSA_SIG*) XMALLOC(sizeof(WOLFSSL_ECDSA_SIG), NULL,
                                       DYNAMIC_TYPE_ECC);
    if (sig == NULL) {
        WOLFSSL_MSG("wolfSSL_ECDSA_SIG_new malloc ECDSA signature failure");
        return NULL;
    }

    sig->s = NULL;
    sig->r = wolfSSL_BN_new();
    if (sig->r == NULL) {
        WOLFSSL_MSG("wolfSSL_ECDSA_SIG_new malloc ECDSA r failure");
        wolfSSL_ECDSA_SIG_free(sig);
        return NULL;
    }

    sig->s = wolfSSL_BN_new();
    if (sig->s == NULL) {
        WOLFSSL_MSG("wolfSSL_ECDSA_SIG_new malloc ECDSA s failure");
        wolfSSL_ECDSA_SIG_free(sig);
        return NULL;
    }

    return sig;
}

/* return signature structure on success, NULL otherwise */
WOLFSSL_ECDSA_SIG *wolfSSL_ECDSA_do_sign(const unsigned char *d, int dlen,
                                         WOLFSSL_EC_KEY *key)
{
    WOLFSSL_ECDSA_SIG *sig = NULL;
    int     initTmpRng = 0;
    WC_RNG* rng = NULL;
#ifdef WOLFSSL_SMALL_STACK
    WC_RNG* tmpRNG = NULL;
#else
    WC_RNG  tmpRNG[1];
#endif

    WOLFSSL_ENTER("wolfSSL_ECDSA_do_sign");

    if (d == NULL || key == NULL || key->internal == NULL) {
        WOLFSSL_MSG("wolfSSL_ECDSA_do_sign Bad arguments");
        return NULL;
    }

    /* set internal key if not done */
    if (key->inSet == 0)
    {
        WOLFSSL_MSG("wolfSSL_ECDSA_do_sign No EC key internal set, do it");

        if (SetECKeyInternal(key) != SSL_SUCCESS) {
            WOLFSSL_MSG("wolfSSL_ECDSA_do_sign SetECKeyInternal failed");
            return NULL;
        }
    }

#ifdef WOLFSSL_SMALL_STACK
    tmpRNG = (WC_RNG*)XMALLOC(sizeof(WC_RNG), NULL, DYNAMIC_TYPE_TMP_BUFFER);
    if (tmpRNG == NULL)
        return NULL;
#endif

    if (wc_InitRng(tmpRNG) == 0) {
        rng = tmpRNG;
        initTmpRng = 1;
    }
    else {
        WOLFSSL_MSG("wolfSSL_ECDSA_do_sign Bad RNG Init, trying global");
        if (initGlobalRNG == 0)
            WOLFSSL_MSG("wolfSSL_ECDSA_do_sign Global RNG no Init");
        else
            rng = &globalRNG;
    }

    if (rng) {
        mp_int sig_r, sig_s;

        if (mp_init_multi(&sig_r, &sig_s, NULL, NULL, NULL, NULL) == MP_OKAY) {
            if (wc_ecc_sign_hash_ex(d, dlen, rng, (ecc_key*)key->internal,
                                    &sig_r, &sig_s) != MP_OKAY) {
                WOLFSSL_MSG("wc_ecc_sign_hash_ex failed");
            }
            else {
                /* put signature blob in ECDSA structure */
                sig = wolfSSL_ECDSA_SIG_new();
                if (sig == NULL)
                    WOLFSSL_MSG("wolfSSL_ECDSA_SIG_new failed");
                else if (SetIndividualExternal(&(sig->r), &sig_r)!=SSL_SUCCESS){
                    WOLFSSL_MSG("ecdsa r key error");
                    wolfSSL_ECDSA_SIG_free(sig);
                    sig = NULL;
                }
                else if (SetIndividualExternal(&(sig->s), &sig_s)!=SSL_SUCCESS){
                    WOLFSSL_MSG("ecdsa s key error");
                    wolfSSL_ECDSA_SIG_free(sig);
                    sig = NULL;
                }

            }
            mp_clear(&sig_r);
            mp_clear(&sig_s);
        }
    }

    if (initTmpRng)
        wc_FreeRng(tmpRNG);
#ifdef WOLFSSL_SMALL_STACK
    XFREE(tmpRNG, NULL, DYNAMIC_TYPE_TMP_BUFFER);
#endif

    return sig;
}

/* return code compliant with OpenSSL :
 *   1 for a valid signature, 0 for an invalid signature and -1 on error
 */
int wolfSSL_ECDSA_do_verify(const unsigned char *d, int dlen,
                            const WOLFSSL_ECDSA_SIG *sig, WOLFSSL_EC_KEY *key)
{
    int check_sign = 0;

    WOLFSSL_ENTER("wolfSSL_ECDSA_do_verify");

    if (d == NULL || sig == NULL || key == NULL || key->internal == NULL) {
        WOLFSSL_MSG("wolfSSL_ECDSA_do_verify Bad arguments");
        return SSL_FATAL_ERROR;
    }

    /* set internal key if not done */
    if (key->inSet == 0)
    {
        WOLFSSL_MSG("No EC key internal set, do it");

        if (SetECKeyInternal(key) != SSL_SUCCESS) {
            WOLFSSL_MSG("SetECKeyInternal failed");
            return SSL_FATAL_ERROR;
        }
    }

    if (wc_ecc_verify_hash_ex((mp_int*)sig->r->internal,
                              (mp_int*)sig->s->internal, d, dlen, &check_sign,
                              (ecc_key *)key->internal) != MP_OKAY) {
        WOLFSSL_MSG("wc_ecc_verify_hash failed");
        return SSL_FATAL_ERROR;
    }
    else if (check_sign == 0) {
        WOLFSSL_MSG("wc_ecc_verify_hash incorrect signature detected");
        return SSL_FAILURE;
    }

    return SSL_SUCCESS;
}
/* End ECDSA_SIG */

/* Start ECDH */
/* return code compliant with OpenSSL :
 *   length of computed key if success, -1 if error
 */
int wolfSSL_ECDH_compute_key(void *out, size_t outlen,
                             const WOLFSSL_EC_POINT *pub_key,
                             WOLFSSL_EC_KEY *ecdh,
                             void *(*KDF) (const void *in, size_t inlen,
                                           void *out, size_t *outlen))
{
    word32 len;
    (void)KDF;

    (void)KDF;

	WOLFSSL_ENTER("wolfSSL_ECDH_compute_key");

    if (out == NULL || pub_key == NULL || pub_key->internal == NULL ||
        ecdh == NULL || ecdh->internal == NULL) {
        WOLFSSL_MSG("Bad function arguments");
        return SSL_FATAL_ERROR;
    }

    /* set internal key if not done */
    if (ecdh->inSet == 0)
    {
        WOLFSSL_MSG("No EC key internal set, do it");

        if (SetECKeyInternal(ecdh) != SSL_SUCCESS) {
            WOLFSSL_MSG("SetECKeyInternal failed");
            return SSL_FATAL_ERROR;
        }
    }

    len = (word32)outlen;

    if (wc_ecc_shared_secret_ssh((ecc_key*)ecdh->internal,
                                 (ecc_point*)pub_key->internal,
                                 (byte *)out, &len) != MP_OKAY) {
        WOLFSSL_MSG("wc_ecc_shared_secret failed");
        return SSL_FATAL_ERROR;
    }

    return len;
}
/* End ECDH */

#if !defined(NO_FILESYSTEM)
/* return code compliant with OpenSSL :
 *   1 if success, 0 if error
 */
int wolfSSL_PEM_write_EC_PUBKEY(FILE *fp, WOLFSSL_EC_KEY *x)
{
    (void)fp;
    (void)x;

    WOLFSSL_MSG("wolfSSL_PEM_write_EC_PUBKEY not implemented");

    return SSL_FAILURE;
}
#endif /* NO_FILESYSTEM */

#if defined(WOLFSSL_KEY_GEN)

/* return code compliant with OpenSSL :
 *   1 if success, 0 if error
 */
int wolfSSL_PEM_write_bio_ECPrivateKey(WOLFSSL_BIO* bio, WOLFSSL_EC_KEY* ecc,
                                       const EVP_CIPHER* cipher,
                                       unsigned char* passwd, int len,
                                       pem_password_cb* cb, void* arg)
{
    (void)bio;
    (void)ecc;
    (void)cipher;
    (void)passwd;
    (void)len;
    (void)cb;
    (void)arg;

    WOLFSSL_MSG("wolfSSL_PEM_write_bio_ECPrivateKey not implemented");

    return SSL_FAILURE;
}

/* return code compliant with OpenSSL :
 *   1 if success, 0 if error
 */
int wolfSSL_PEM_write_mem_ECPrivateKey(WOLFSSL_EC_KEY* ecc,
                                       const EVP_CIPHER* cipher,
                                       unsigned char* passwd, int passwdSz,
                                       unsigned char **pem, int *plen)
{
    byte *derBuf, *tmp, *cipherInfo = NULL;
    int  der_max_len = 0, derSz = 0;

    WOLFSSL_MSG("wolfSSL_PEM_write_mem_ECPrivateKey");

    if (pem == NULL || plen == NULL || ecc == NULL || ecc->internal == NULL) {
        WOLFSSL_MSG("Bad function arguments");
        return SSL_FAILURE;
    }

    if (ecc->inSet == 0) {
        WOLFSSL_MSG("No ECC internal set, do it");

        if (SetECKeyInternal(ecc) != SSL_SUCCESS) {
            WOLFSSL_MSG("SetDsaInternal failed");
            return SSL_FAILURE;
        }
    }

    /* 4 > size of pub, priv + ASN.1 additional informations
     */
    der_max_len = 4 * wc_ecc_size((ecc_key*)ecc->internal) + AES_BLOCK_SIZE;

    derBuf = (byte*)XMALLOC(der_max_len, NULL, DYNAMIC_TYPE_TMP_BUFFER);
    if (derBuf == NULL) {
        WOLFSSL_MSG("malloc failed");
        return SSL_FAILURE;
    }

    /* Key to DER */
    derSz = wc_EccKeyToDer((ecc_key*)ecc->internal, derBuf, der_max_len);
    if (derSz < 0) {
        WOLFSSL_MSG("wc_DsaKeyToDer failed");
        XFREE(derBuf, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        return SSL_FAILURE;
    }

    /* encrypt DER buffer if required */
    if (passwd != NULL && passwdSz > 0 && cipher != NULL) {
        int ret;

        ret = EncryptDerKey(derBuf, &derSz, cipher,
                            passwd, passwdSz, &cipherInfo);
        if (ret != SSL_SUCCESS) {
            WOLFSSL_MSG("EncryptDerKey failed");
            XFREE(derBuf, NULL, DYNAMIC_TYPE_TMP_BUFFER);
            return ret;
        }

        /* tmp buffer with a max size */
        *plen = (derSz * 2) + sizeof(BEGIN_EC_PRIV) +
        sizeof(END_EC_PRIV) + HEADER_ENCRYPTED_KEY_SIZE;
    }
    else /* tmp buffer with a max size */
        *plen = (derSz * 2) + sizeof(BEGIN_EC_PRIV) + sizeof(END_EC_PRIV);

    tmp = (byte*)XMALLOC(*plen, NULL, DYNAMIC_TYPE_TMP_BUFFER);
    if (tmp == NULL) {
        WOLFSSL_MSG("malloc failed");
        XFREE(derBuf, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        if (cipherInfo != NULL)
            XFREE(cipherInfo, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        return SSL_FAILURE;
    }

    /* DER to PEM */
    *plen = wc_DerToPemEx(derBuf, derSz, tmp, *plen, cipherInfo, ECC_PRIVATEKEY_TYPE);
    if (*plen <= 0) {
        WOLFSSL_MSG("wc_DerToPemEx failed");
        XFREE(derBuf, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        XFREE(tmp, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        if (cipherInfo != NULL)
            XFREE(cipherInfo, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        return SSL_FAILURE;
    }
    XFREE(derBuf, NULL, DYNAMIC_TYPE_TMP_BUFFER);
    if (cipherInfo != NULL)
        XFREE(cipherInfo, NULL, DYNAMIC_TYPE_TMP_BUFFER);

    *pem = (byte*)XMALLOC((*plen)+1, NULL, DYNAMIC_TYPE_KEY);
    if (*pem == NULL) {
        WOLFSSL_MSG("malloc failed");
        XFREE(tmp, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        return SSL_FAILURE;
    }
    XMEMSET(*pem, 0, (*plen)+1);

    if (XMEMCPY(*pem, tmp, *plen) == NULL) {
        WOLFSSL_MSG("XMEMCPY failed");
        XFREE(pem, NULL, DYNAMIC_TYPE_KEY);
        XFREE(tmp, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        return SSL_FAILURE;
    }
    XFREE(tmp, NULL, DYNAMIC_TYPE_TMP_BUFFER);

    return SSL_SUCCESS;
}

#ifndef NO_FILESYSTEM
/* return code compliant with OpenSSL :
 *   1 if success, 0 if error
 */
int wolfSSL_PEM_write_ECPrivateKey(FILE *fp, WOLFSSL_EC_KEY *ecc,
                                   const EVP_CIPHER *enc,
                                   unsigned char *kstr, int klen,
                                   pem_password_cb *cb, void *u)
{
    byte *pem;
    int  plen, ret;

    (void)cb;
    (void)u;

    WOLFSSL_MSG("wolfSSL_PEM_write_ECPrivateKey");

    if (fp == NULL || ecc == NULL || ecc->internal == NULL) {
        WOLFSSL_MSG("Bad function arguments");
        return SSL_FAILURE;
    }

    ret = wolfSSL_PEM_write_mem_ECPrivateKey(ecc, enc, kstr, klen, &pem, &plen);
    if (ret != SSL_SUCCESS) {
        WOLFSSL_MSG("wolfSSL_PEM_write_mem_ECPrivateKey failed");
        return SSL_FAILURE;
    }

    ret = (int)XFWRITE(pem, plen, 1, fp);
    if (ret != 1) {
        WOLFSSL_MSG("ECC private key file write failed");
        return SSL_FAILURE;
    }

    XFREE(pem, NULL, DYNAMIC_TYPE_KEY);
    return SSL_SUCCESS;
}

#endif /* NO_FILESYSTEM */
#endif /* defined(WOLFSSL_KEY_GEN) */

#endif /* HAVE_ECC */


#ifndef NO_DSA

#if defined(WOLFSSL_KEY_GEN)

/* return code compliant with OpenSSL :
 *   1 if success, 0 if error
 */
int wolfSSL_PEM_write_bio_DSAPrivateKey(WOLFSSL_BIO* bio, WOLFSSL_DSA* dsa,
                                       const EVP_CIPHER* cipher,
                                       unsigned char* passwd, int len,
                                       pem_password_cb* cb, void* arg)
{
    (void)bio;
    (void)dsa;
    (void)cipher;
    (void)passwd;
    (void)len;
    (void)cb;
    (void)arg;

    WOLFSSL_MSG("wolfSSL_PEM_write_bio_DSAPrivateKey not implemented");

    return SSL_FAILURE;
}

/* return code compliant with OpenSSL :
 *   1 if success, 0 if error
 */
int wolfSSL_PEM_write_mem_DSAPrivateKey(WOLFSSL_DSA* dsa,
                                        const EVP_CIPHER* cipher,
                                        unsigned char* passwd, int passwdSz,
                                        unsigned char **pem, int *plen)
{
    byte *derBuf, *tmp, *cipherInfo = NULL;
    int  der_max_len = 0, derSz = 0;

    WOLFSSL_MSG("wolfSSL_PEM_write_mem_DSAPrivateKey");

    if (pem == NULL || plen == NULL || dsa == NULL || dsa->internal == NULL) {
        WOLFSSL_MSG("Bad function arguments");
        return SSL_FAILURE;
    }

    if (dsa->inSet == 0) {
        WOLFSSL_MSG("No DSA internal set, do it");

        if (SetDsaInternal(dsa) != SSL_SUCCESS) {
            WOLFSSL_MSG("SetDsaInternal failed");
            return SSL_FAILURE;
        }
    }

    /* 4 > size of pub, priv, p, q, g + ASN.1 additional informations
     */
    der_max_len = 4 * wolfSSL_BN_num_bytes(dsa->g) + AES_BLOCK_SIZE;

    derBuf = (byte*)XMALLOC(der_max_len, NULL, DYNAMIC_TYPE_TMP_BUFFER);
    if (derBuf == NULL) {
        WOLFSSL_MSG("malloc failed");
        return SSL_FAILURE;
    }

    /* Key to DER */
    derSz = wc_DsaKeyToDer((DsaKey*)dsa->internal, derBuf, der_max_len);
    if (derSz < 0) {
        WOLFSSL_MSG("wc_DsaKeyToDer failed");
        XFREE(derBuf, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        return SSL_FAILURE;
    }

    /* encrypt DER buffer if required */
    if (passwd != NULL && passwdSz > 0 && cipher != NULL) {
        int ret;

        ret = EncryptDerKey(derBuf, &derSz, cipher,
                            passwd, passwdSz, &cipherInfo);
        if (ret != SSL_SUCCESS) {
            WOLFSSL_MSG("EncryptDerKey failed");
            XFREE(derBuf, NULL, DYNAMIC_TYPE_TMP_BUFFER);
            return ret;
        }

        /* tmp buffer with a max size */
        *plen = (derSz * 2) + sizeof(BEGIN_DSA_PRIV) +
        sizeof(END_DSA_PRIV) + HEADER_ENCRYPTED_KEY_SIZE;
    }
    else /* tmp buffer with a max size */
        *plen = (derSz * 2) + sizeof(BEGIN_DSA_PRIV) + sizeof(END_DSA_PRIV);

    tmp = (byte*)XMALLOC(*plen, NULL, DYNAMIC_TYPE_TMP_BUFFER);
    if (tmp == NULL) {
        WOLFSSL_MSG("malloc failed");
        XFREE(derBuf, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        if (cipherInfo != NULL)
            XFREE(cipherInfo, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        return SSL_FAILURE;
    }

    /* DER to PEM */
    *plen = wc_DerToPemEx(derBuf, derSz, tmp, *plen, cipherInfo, DSA_PRIVATEKEY_TYPE);
    if (*plen <= 0) {
        WOLFSSL_MSG("wc_DerToPemEx failed");
        XFREE(derBuf, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        XFREE(tmp, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        if (cipherInfo != NULL)
            XFREE(cipherInfo, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        return SSL_FAILURE;
    }
    XFREE(derBuf, NULL, DYNAMIC_TYPE_TMP_BUFFER);
    if (cipherInfo != NULL)
        XFREE(cipherInfo, NULL, DYNAMIC_TYPE_TMP_BUFFER);

    *pem = (byte*)XMALLOC((*plen)+1, NULL, DYNAMIC_TYPE_KEY);
    if (*pem == NULL) {
        WOLFSSL_MSG("malloc failed");
        XFREE(tmp, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        return SSL_FAILURE;
    }
    XMEMSET(*pem, 0, (*plen)+1);

    if (XMEMCPY(*pem, tmp, *plen) == NULL) {
        WOLFSSL_MSG("XMEMCPY failed");
        XFREE(pem, NULL, DYNAMIC_TYPE_KEY);
        XFREE(tmp, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        return SSL_FAILURE;
    }
    XFREE(tmp, NULL, DYNAMIC_TYPE_TMP_BUFFER);

    return SSL_SUCCESS;
}

#ifndef NO_FILESYSTEM
/* return code compliant with OpenSSL :
 *   1 if success, 0 if error
 */
int wolfSSL_PEM_write_DSAPrivateKey(FILE *fp, WOLFSSL_DSA *dsa,
                                    const EVP_CIPHER *enc,
                                    unsigned char *kstr, int klen,
                                    pem_password_cb *cb, void *u)
{
    byte *pem;
    int  plen, ret;

    (void)cb;
    (void)u;

    WOLFSSL_MSG("wolfSSL_PEM_write_DSAPrivateKey");

    if (fp == NULL || dsa == NULL || dsa->internal == NULL) {
        WOLFSSL_MSG("Bad function arguments");
        return SSL_FAILURE;
    }

    ret = wolfSSL_PEM_write_mem_DSAPrivateKey(dsa, enc, kstr, klen, &pem, &plen);
    if (ret != SSL_SUCCESS) {
        WOLFSSL_MSG("wolfSSL_PEM_write_mem_DSAPrivateKey failed");
        return SSL_FAILURE;
    }

    ret = (int)XFWRITE(pem, plen, 1, fp);
    if (ret != 1) {
        WOLFSSL_MSG("DSA private key file write failed");
        return SSL_FAILURE;
    }

    XFREE(pem, NULL, DYNAMIC_TYPE_KEY);
    return SSL_SUCCESS;
}

#endif /* NO_FILESYSTEM */
#endif /* defined(WOLFSSL_KEY_GEN) */

#ifndef NO_FILESYSTEM
/* return code compliant with OpenSSL :
 *   1 if success, 0 if error
 */
int wolfSSL_PEM_write_DSA_PUBKEY(FILE *fp, WOLFSSL_DSA *x)
{
    (void)fp;
    (void)x;

    WOLFSSL_MSG("wolfSSL_PEM_write_DSA_PUBKEY not implemented");

    return SSL_FAILURE;
}
#endif /* NO_FILESYSTEM */

#endif /* #ifndef NO_DSA */


WOLFSSL_EVP_PKEY* wolfSSL_PEM_read_bio_PrivateKey(WOLFSSL_BIO* bio,
                    WOLFSSL_EVP_PKEY** key, pem_password_cb* cb, void* pass)
{
    WOLFSSL_EVP_PKEY* pkey = NULL;
#ifdef WOLFSSL_SMALL_STACK
    EncryptedInfo* info;
#else
    EncryptedInfo info[1];
#endif /* WOLFSSL_SMALL_STACK */
    pem_password_cb* localCb = cb;
    DerBuffer* der = NULL;

    char* mem = NULL;
    int memSz;
    int ret;
    int eccFlag = 0;

    WOLFSSL_ENTER("wolfSSL_PEM_read_bio_PrivateKey");

    if ((ret = wolfSSL_BIO_pending(bio)) > 0) {
        memSz = ret;
        mem = (char*)XMALLOC(memSz, bio->heap, DYNAMIC_TYPE_OPENSSL);
        if (mem == NULL) {
            WOLFSSL_MSG("Memory error");
            return NULL;
        }

        if ((ret = wolfSSL_BIO_read(bio, mem, memSz)) <= 0) {
            WOLFSSL_LEAVE("wolfSSL_PEM_read_bio_PrivateKey", ret);
            XFREE(mem, bio->heap, DYNAMIC_TYPE_OPENSSL);
            return NULL;
        }
    }
    else {
        WOLFSSL_MSG("No data to read from bio");
        return NULL;
    }

#ifdef WOLFSSL_SMALL_STACK
    info = (EncryptedInfo*)XMALLOC(sizeof(EncryptedInfo), NULL,
                                   DYNAMIC_TYPE_TMP_BUFFER);
    if (info == NULL) {
        WOLFSSL_MSG("Error getting memory for EncryptedInfo structure");
        XFREE(mem, bio->heap, DYNAMIC_TYPE_OPENSSL);
        return NULL;
    }
#endif

    XMEMSET(info, 0, sizeof(EncryptedInfo));

    if (pass != NULL) {
        info->ctx = wolfSSL_CTX_new(wolfSSLv23_client_method());
        if (info->ctx == NULL) {
        #ifdef WOLFSSL_SMALL_STACK
            XFREE(info, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        #endif
            WOLFSSL_MSG("Error creating ctx for password");
            XFREE(mem, bio->heap, DYNAMIC_TYPE_OPENSSL);
            return NULL;
        }

        if (cb == NULL) {
            localCb = OurPasswordCb;
        }
        wolfSSL_CTX_set_default_passwd_cb(info->ctx, localCb);
        wolfSSL_CTX_set_default_passwd_cb_userdata(info->ctx, pass);
    }

    ret = PemToDer((const unsigned char*)mem, memSz, PRIVATEKEY_TYPE, &der,
            NULL, info, &eccFlag);

    if (info->ctx) {
        wolfSSL_CTX_free(info->ctx);
    }

    if (ret < 0) {
        WOLFSSL_MSG("Bad Pem To Der");
    }
    else {
        int type;

        /* write left over data back to bio */
        if ((memSz - (int)info->consumed) > 0) {
            if (wolfSSL_BIO_write(bio, mem + (int)info->consumed,
                                   memSz - (int)info->consumed) <= 0) {
                WOLFSSL_MSG("Unable to advance bio read pointer");
            }
        }

        if (eccFlag) {
            type = ECDSAk;
        }
        else {
            type = RSAk;
        }

        /* handle case where reuse is attempted */
        if (key != NULL && *key != NULL) {
            pkey = *key;
        }

        wolfSSL_d2i_PrivateKey(type, &pkey,
                (const unsigned char**)&der->buffer, der->length);
        if (pkey == NULL) {
            WOLFSSL_MSG("Error loading DER buffer into WOLFSSL_EVP_PKEY");
        }
    }

#ifdef WOLFSSL_SMALL_STACK
    XFREE(info, NULL, DYNAMIC_TYPE_TMP_BUFFER);
#endif

    XFREE(mem, bio->heap, DYNAMIC_TYPE_OPENSSL);
    FreeDer(&der);

    if (key != NULL) {
        *key = pkey;
    }

    return pkey;
}


int wolfSSL_EVP_PKEY_type(int type)
{
    (void)type;

    WOLFSSL_MSG("wolfSSL_EVP_PKEY_type not implemented");

    return SSL_FATAL_ERROR;
}


#if !defined(NO_FILESYSTEM)
WOLFSSL_EVP_PKEY *wolfSSL_PEM_read_PUBKEY(FILE *fp, EVP_PKEY **x,
                                          pem_password_cb *cb, void *u)
{
    (void)fp;
    (void)x;
    (void)cb;
    (void)u;

    WOLFSSL_MSG("wolfSSL_PEM_read_PUBKEY not implemented");

    return NULL;
}
#endif /* NO_FILESYSTEM */

#ifndef NO_RSA

#if !defined(NO_FILESYSTEM)
WOLFSSL_RSA *wolfSSL_PEM_read_RSAPublicKey(FILE *fp, WOLFSSL_RSA **x,
                                           pem_password_cb *cb, void *u)
{
    (void)fp;
    (void)x;
    (void)cb;
    (void)u;

    WOLFSSL_MSG("wolfSSL_PEM_read_RSAPublicKey not implemented");

    return NULL;
}

/* return code compliant with OpenSSL :
 *   1 if success, 0 if error
 */
int wolfSSL_PEM_write_RSAPublicKey(FILE *fp, WOLFSSL_RSA *x)
{
    (void)fp;
    (void)x;

    WOLFSSL_MSG("wolfSSL_PEM_write_RSAPublicKey not implemented");

    return SSL_FAILURE;
}

/* return code compliant with OpenSSL :
 *   1 if success, 0 if error
 */
int wolfSSL_PEM_write_RSA_PUBKEY(FILE *fp, WOLFSSL_RSA *x)
{
    (void)fp;
    (void)x;

    WOLFSSL_MSG("wolfSSL_PEM_write_RSA_PUBKEY not implemented");

    return SSL_FAILURE;
}
#endif /* NO_FILESYSTEM */

/* return SSL_SUCCESS if success, SSL_FATAL_ERROR if error */
int wolfSSL_RSA_LoadDer(WOLFSSL_RSA* rsa, const unsigned char* derBuf, int derSz)
{
  return wolfSSL_RSA_LoadDer_ex(rsa, derBuf, derSz, WOLFSSL_RSA_LOAD_PRIVATE);
}

int wolfSSL_RSA_LoadDer_ex(WOLFSSL_RSA* rsa, const unsigned char* derBuf,
                                                     int derSz, int opt)
{

    word32 idx = 0;
    int    ret;

    WOLFSSL_ENTER("wolfSSL_RSA_LoadDer");

    if (rsa == NULL || rsa->internal == NULL || derBuf == NULL || derSz <= 0) {
        WOLFSSL_MSG("Bad function arguments");
        return SSL_FATAL_ERROR;
    }

    if(opt == WOLFSSL_RSA_LOAD_PRIVATE)
        ret = wc_RsaPrivateKeyDecode(derBuf, &idx, (RsaKey*)rsa->internal, derSz);
    else
        ret = wc_RsaPublicKeyDecode(derBuf, &idx, (RsaKey*)rsa->internal, derSz);

    if (ret < 0) {
        if(opt == WOLFSSL_RSA_LOAD_PRIVATE) {
             WOLFSSL_MSG("RsaPrivateKeyDecode failed");
        }
        else {
             WOLFSSL_MSG("RsaPublicKeyDecode failed");
        }
        return SSL_FATAL_ERROR;
    }

    if (SetRsaExternal(rsa) != SSL_SUCCESS) {
        WOLFSSL_MSG("SetRsaExternal failed");
        return SSL_FATAL_ERROR;
    }

    rsa->inSet = 1;

    return SSL_SUCCESS;
}
#endif /* NO_RSA */


#ifndef NO_DSA
/* return SSL_SUCCESS if success, SSL_FATAL_ERROR if error */
int wolfSSL_DSA_LoadDer(WOLFSSL_DSA* dsa, const unsigned char* derBuf, int derSz)
{
    word32 idx = 0;
    int    ret;

    WOLFSSL_ENTER("wolfSSL_DSA_LoadDer");

    if (dsa == NULL || dsa->internal == NULL || derBuf == NULL || derSz <= 0) {
        WOLFSSL_MSG("Bad function arguments");
        return SSL_FATAL_ERROR;
    }

    ret = DsaPrivateKeyDecode(derBuf, &idx, (DsaKey*)dsa->internal, derSz);
    if (ret < 0) {
        WOLFSSL_MSG("DsaPrivateKeyDecode failed");
        return SSL_FATAL_ERROR;
    }

    if (SetDsaExternal(dsa) != SSL_SUCCESS) {
        WOLFSSL_MSG("SetDsaExternal failed");
        return SSL_FATAL_ERROR;
    }

    dsa->inSet = 1;

    return SSL_SUCCESS;
}
#endif /* NO_DSA */

#ifdef HAVE_ECC
/* return SSL_SUCCESS if success, SSL_FATAL_ERROR if error */
int wolfSSL_EC_KEY_LoadDer(WOLFSSL_EC_KEY* key,
                           const unsigned char* derBuf,  int derSz)
{
    word32 idx = 0;
    int    ret;

    WOLFSSL_ENTER("wolfSSL_EC_KEY_LoadDer");

    if (key == NULL || key->internal == NULL || derBuf == NULL || derSz <= 0) {
        WOLFSSL_MSG("Bad function arguments");
        return SSL_FATAL_ERROR;
    }

    ret = wc_EccPrivateKeyDecode(derBuf, &idx, (ecc_key*)key->internal, derSz);
    if (ret < 0) {
        WOLFSSL_MSG("wc_EccPrivateKeyDecode failed");
        return SSL_FATAL_ERROR;
    }

    if (SetECKeyExternal(key) != SSL_SUCCESS) {
        WOLFSSL_MSG("SetECKeyExternal failed");
        return SSL_FATAL_ERROR;
    }

    key->inSet = 1;

    return SSL_SUCCESS;
}
#endif /* HAVE_ECC */


WOLFSSL_EVP_PKEY* wolfSSL_X509_get_pubkey(WOLFSSL_X509* x509)
{
    WOLFSSL_EVP_PKEY* key = NULL;
    WOLFSSL_ENTER("X509_get_pubkey");
    if (x509 != NULL) {
        key = (WOLFSSL_EVP_PKEY*)XMALLOC(
                    sizeof(WOLFSSL_EVP_PKEY), x509->heap,
                                                       DYNAMIC_TYPE_PUBLIC_KEY);
        if (key != NULL) {
            XMEMSET(key, 0, sizeof(WOLFSSL_EVP_PKEY));
            if (x509->pubKeyOID == RSAk) {
                key->type = EVP_PKEY_RSA;
            }
            else {
                key->type = EVP_PKEY_EC;
            }
            key->save_type = 0;
            key->pkey.ptr = (char*)XMALLOC(
                        x509->pubKey.length, x509->heap,
                                                       DYNAMIC_TYPE_PUBLIC_KEY);
            if (key->pkey.ptr == NULL) {
                XFREE(key, x509->heap, DYNAMIC_TYPE_PUBLIC_KEY);
                return NULL;
            }
            XMEMCPY(key->pkey.ptr, x509->pubKey.buffer, x509->pubKey.length);
            key->pkey_sz = x509->pubKey.length;

            #ifdef HAVE_ECC
                key->pkey_curve = (int)x509->pkCurveOID;
            #endif /* HAVE_ECC */

            /* decode RSA key */
            #ifndef NO_RSA
            if (key->type == EVP_PKEY_RSA) {
                key->ownRsa = 1;
                key->rsa = wolfSSL_RSA_new();
                if (key->rsa == NULL) {
                    XFREE(key, x509->heap, DYNAMIC_TYPE_PUBLIC_KEY);
                    return NULL;
                }

                if (wolfSSL_RSA_LoadDer_ex(key->rsa,
                            (const unsigned char*)key->pkey.ptr, key->pkey_sz,
                            WOLFSSL_RSA_LOAD_PUBLIC) != SSL_SUCCESS) {
                    XFREE(key, x509->heap, DYNAMIC_TYPE_PUBLIC_KEY);
                    wolfSSL_RSA_free(key->rsa);
                    return NULL;
                }
            }
            #endif /* NO_RSA */

            /* decode ECC key */
            #ifdef HAVE_ECC
            if (key->type == EVP_PKEY_EC) {
                key->ownEcc = 1;
                key->ecc = wolfSSL_EC_KEY_new();
                if (key->ecc == NULL || key->ecc->internal == NULL) {
                    XFREE(key, x509->heap, DYNAMIC_TYPE_PUBLIC_KEY);
                    return NULL;
                }

                /* not using wolfSSL_EC_KEY_LoadDer because public key in x509
                 * is in the format of x963 (no sequence at start of buffer) */
                if (wc_ecc_import_x963((const unsigned char*)key->pkey.ptr,
                            key->pkey_sz, (ecc_key*)key->ecc->internal) < 0) {
                    WOLFSSL_MSG("wc_ecc_import_x963 failed");
                    XFREE(key, x509->heap, DYNAMIC_TYPE_PUBLIC_KEY);
                    wolfSSL_EC_KEY_free(key->ecc);
                    return NULL;
                }

                if (SetECKeyExternal(key->ecc) != SSL_SUCCESS) {
                    WOLFSSL_MSG("SetECKeyExternal failed");
                    XFREE(key, x509->heap, DYNAMIC_TYPE_PUBLIC_KEY);
                    wolfSSL_EC_KEY_free(key->ecc);
                    return NULL;
                }

                key->ecc->inSet = 1;
            }
            #endif /* HAVE_ECC */
        }
    }
    return key;
}
#endif /* OPENSSL_EXTRA */


#ifdef SESSION_CERTS


/* Get peer's certificate chain */
WOLFSSL_X509_CHAIN* wolfSSL_get_peer_chain(WOLFSSL* ssl)
{
    WOLFSSL_ENTER("wolfSSL_get_peer_chain");
    if (ssl)
        return &ssl->session.chain;

    return 0;
}


/* Get peer's certificate chain total count */
int wolfSSL_get_chain_count(WOLFSSL_X509_CHAIN* chain)
{
    WOLFSSL_ENTER("wolfSSL_get_chain_count");
    if (chain)
        return chain->count;

    return 0;
}


/* Get peer's ASN.1 DER certificate at index (idx) length in bytes */
int wolfSSL_get_chain_length(WOLFSSL_X509_CHAIN* chain, int idx)
{
    WOLFSSL_ENTER("wolfSSL_get_chain_length");
    if (chain)
        return chain->certs[idx].length;

    return 0;
}


/* Get peer's ASN.1 DER certificate at index (idx) */
byte* wolfSSL_get_chain_cert(WOLFSSL_X509_CHAIN* chain, int idx)
{
    WOLFSSL_ENTER("wolfSSL_get_chain_cert");
    if (chain)
        return chain->certs[idx].buffer;

    return 0;
}


/* Get peer's wolfSSL X509 certificate at index (idx) */
WOLFSSL_X509* wolfSSL_get_chain_X509(WOLFSSL_X509_CHAIN* chain, int idx)
{
    int          ret;
    WOLFSSL_X509* x509 = NULL;
#ifdef WOLFSSL_SMALL_STACK
    DecodedCert* cert = NULL;
#else
    DecodedCert  cert[1];
#endif

    WOLFSSL_ENTER("wolfSSL_get_chain_X509");
    if (chain != NULL) {
    #ifdef WOLFSSL_SMALL_STACK
        cert = (DecodedCert*)XMALLOC(sizeof(DecodedCert), NULL,
                                                       DYNAMIC_TYPE_TMP_BUFFER);
        if (cert != NULL)
    #endif
        {
            InitDecodedCert(cert, chain->certs[idx].buffer,
                                  chain->certs[idx].length, NULL);

            if ((ret = ParseCertRelative(cert, CERT_TYPE, 0, NULL)) != 0) {
                WOLFSSL_MSG("Failed to parse cert");
            }
            else {
                x509 = (WOLFSSL_X509*)XMALLOC(sizeof(WOLFSSL_X509), NULL,
                                                             DYNAMIC_TYPE_X509);
                if (x509 == NULL) {
                    WOLFSSL_MSG("Failed alloc X509");
                }
                else {
                    InitX509(x509, 1, NULL);

                    if ((ret = CopyDecodedToX509(x509, cert)) != 0) {
                        WOLFSSL_MSG("Failed to copy decoded");
                        XFREE(x509, NULL, DYNAMIC_TYPE_X509);
                        x509 = NULL;
                    }
                }
            }

            FreeDecodedCert(cert);
        #ifdef WOLFSSL_SMALL_STACK
            XFREE(cert, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        #endif
        }
    }

    return x509;
}


/* Get peer's PEM certificate at index (idx), output to buffer if inLen big
   enough else return error (-1). If buffer is NULL only calculate
   outLen. Output length is in *outLen SSL_SUCCESS on ok */
int  wolfSSL_get_chain_cert_pem(WOLFSSL_X509_CHAIN* chain, int idx,
                               unsigned char* buf, int inLen, int* outLen)
{
    const char header[] = "-----BEGIN CERTIFICATE-----\n";
    const char footer[] = "-----END CERTIFICATE-----\n";

    int headerLen = sizeof(header) - 1;
    int footerLen = sizeof(footer) - 1;
    int i;
    int err;
    word32 szNeeded = 0;

    WOLFSSL_ENTER("wolfSSL_get_chain_cert_pem");
    if (!chain || !outLen || idx < 0 || idx >= wolfSSL_get_chain_count(chain))
        return BAD_FUNC_ARG;

    /* Null output buffer return size needed in outLen */
    if(!buf) {
        if(Base64_Encode(chain->certs[idx].buffer, chain->certs[idx].length,
                    NULL, &szNeeded) != LENGTH_ONLY_E)
            return SSL_FAILURE;
        *outLen = szNeeded + headerLen + footerLen;
        return LENGTH_ONLY_E;
    }

    /* don't even try if inLen too short */
    if (inLen < headerLen + footerLen + chain->certs[idx].length)
        return BAD_FUNC_ARG;

    /* header */
    if (XMEMCPY(buf, header, headerLen) == NULL)
        return SSL_FATAL_ERROR;

    i = headerLen;

    /* body */
    *outLen = inLen;  /* input to Base64_Encode */
    if ( (err = Base64_Encode(chain->certs[idx].buffer,
                       chain->certs[idx].length, buf + i, (word32*)outLen)) < 0)
        return err;
    i += *outLen;

    /* footer */
    if ( (i + footerLen) > inLen)
        return BAD_FUNC_ARG;
    if (XMEMCPY(buf + i, footer, footerLen) == NULL)
        return SSL_FATAL_ERROR;
    *outLen += headerLen + footerLen;

    return SSL_SUCCESS;
}


/* get session ID */
const byte* wolfSSL_get_sessionID(const WOLFSSL_SESSION* session)
{
    WOLFSSL_ENTER("wolfSSL_get_sessionID");
    if (session)
        return session->sessionID;

    return NULL;
}


#endif /* SESSION_CERTS */

#ifdef HAVE_FUZZER
void wolfSSL_SetFuzzerCb(WOLFSSL* ssl, CallbackFuzzer cbf, void* fCtx)
{
    if (ssl) {
        ssl->fuzzerCb  = cbf;
        ssl->fuzzerCtx = fCtx;
    }
}
#endif

#ifndef NO_CERTS
#ifdef  HAVE_PK_CALLBACKS

#ifdef HAVE_ECC

void  wolfSSL_CTX_SetEccSignCb(WOLFSSL_CTX* ctx, CallbackEccSign cb)
{
    if (ctx)
        ctx->EccSignCb = cb;
}


void  wolfSSL_SetEccSignCtx(WOLFSSL* ssl, void *ctx)
{
    if (ssl)
        ssl->EccSignCtx = ctx;
}


void* wolfSSL_GetEccSignCtx(WOLFSSL* ssl)
{
    if (ssl)
        return ssl->EccSignCtx;

    return NULL;
}


void  wolfSSL_CTX_SetEccVerifyCb(WOLFSSL_CTX* ctx, CallbackEccVerify cb)
{
    if (ctx)
        ctx->EccVerifyCb = cb;
}


void  wolfSSL_SetEccVerifyCtx(WOLFSSL* ssl, void *ctx)
{
    if (ssl)
        ssl->EccVerifyCtx = ctx;
}


void* wolfSSL_GetEccVerifyCtx(WOLFSSL* ssl)
{
    if (ssl)
        return ssl->EccVerifyCtx;

    return NULL;
}

void wolfSSL_CTX_SetEccSharedSecretCb(WOLFSSL_CTX* ctx, CallbackEccSharedSecret cb)
{
    if (ctx)
        ctx->EccSharedSecretCb = cb;
}

void  wolfSSL_SetEccSharedSecretCtx(WOLFSSL* ssl, void *ctx)
{
    if (ssl)
        ssl->EccSharedSecretCtx = ctx;
}


void* wolfSSL_GetEccSharedSecretCtx(WOLFSSL* ssl)
{
    if (ssl)
        return ssl->EccSharedSecretCtx;

    return NULL;
}
#endif /* HAVE_ECC */

#ifndef NO_RSA

void  wolfSSL_CTX_SetRsaSignCb(WOLFSSL_CTX* ctx, CallbackRsaSign cb)
{
    if (ctx)
        ctx->RsaSignCb = cb;
}


void  wolfSSL_SetRsaSignCtx(WOLFSSL* ssl, void *ctx)
{
    if (ssl)
        ssl->RsaSignCtx = ctx;
}


void* wolfSSL_GetRsaSignCtx(WOLFSSL* ssl)
{
    if (ssl)
        return ssl->RsaSignCtx;

    return NULL;
}


void  wolfSSL_CTX_SetRsaVerifyCb(WOLFSSL_CTX* ctx, CallbackRsaVerify cb)
{
    if (ctx)
        ctx->RsaVerifyCb = cb;
}


void  wolfSSL_SetRsaVerifyCtx(WOLFSSL* ssl, void *ctx)
{
    if (ssl)
        ssl->RsaVerifyCtx = ctx;
}


void* wolfSSL_GetRsaVerifyCtx(WOLFSSL* ssl)
{
    if (ssl)
        return ssl->RsaVerifyCtx;

    return NULL;
}

void  wolfSSL_CTX_SetRsaEncCb(WOLFSSL_CTX* ctx, CallbackRsaEnc cb)
{
    if (ctx)
        ctx->RsaEncCb = cb;
}


void  wolfSSL_SetRsaEncCtx(WOLFSSL* ssl, void *ctx)
{
    if (ssl)
        ssl->RsaEncCtx = ctx;
}


void* wolfSSL_GetRsaEncCtx(WOLFSSL* ssl)
{
    if (ssl)
        return ssl->RsaEncCtx;

    return NULL;
}

void  wolfSSL_CTX_SetRsaDecCb(WOLFSSL_CTX* ctx, CallbackRsaDec cb)
{
    if (ctx)
        ctx->RsaDecCb = cb;
}


void  wolfSSL_SetRsaDecCtx(WOLFSSL* ssl, void *ctx)
{
    if (ssl)
        ssl->RsaDecCtx = ctx;
}


void* wolfSSL_GetRsaDecCtx(WOLFSSL* ssl)
{
    if (ssl)
        return ssl->RsaDecCtx;

    return NULL;
}


#endif /* NO_RSA */

#endif /* HAVE_PK_CALLBACKS */
#endif /* NO_CERTS */


#ifdef WOLFSSL_HAVE_WOLFSCEP
    /* Used by autoconf to see if wolfSCEP is available */
    void wolfSSL_wolfSCEP(void) {}
#endif


#ifdef WOLFSSL_HAVE_CERT_SERVICE
    /* Used by autoconf to see if cert service is available */
    void wolfSSL_cert_service(void) {}
#endif


#ifdef OPENSSL_EXTRA /*Lighttp compatibility*/

    #ifndef NO_CERTS
    void wolfSSL_X509_NAME_free(WOLFSSL_X509_NAME *name){
        WOLFSSL_ENTER("wolfSSL_X509_NAME_free");
        FreeX509Name(name, NULL);
    }


    /* Malloc's a new WOLFSSL_X509_NAME structure
     *
     * returns NULL on failure, otherwise returns a new structure.
     */
    WOLFSSL_X509_NAME* wolfSSL_X509_NAME_new()
    {
        WOLFSSL_X509_NAME* name;

        WOLFSSL_ENTER("wolfSSL_X509_NAME_new");

        name = (WOLFSSL_X509_NAME*)XMALLOC(sizeof(WOLFSSL_X509_NAME), NULL,
                DYNAMIC_TYPE_X509);
        if (name != NULL) {
            InitX509Name(name, 1);
            name->dynamic = 1;
        }
        return name;
    }


#if defined(WOLFSSL_CERT_GEN) && !defined(NO_RSA)
/* needed SetName function from asn.c is wrapped by NO_RSA */
    /* helper function for CopyX509NameToCertName() */
    static int CopyX509NameEntry(char* out, int max, char* in, int inLen)
    {
        if (inLen > max) {
            WOLFSSL_MSG("Name too long");
            XMEMCPY(out, in, max);
        }
        else {
            XMEMCPY(out, in, inLen);
            out[inLen] = '\0';
        }

        /* make sure is null terminated */
        out[max-1] = '\0';

        return SSL_SUCCESS;
    }


    static int CopyX509NameToCertName(WOLFSSL_X509_NAME* n, CertName* cName)
    {
        DecodedName* dn = NULL;

        if (n == NULL || cName == NULL) {
            return BAD_FUNC_ARG;
        }

        dn = &(n->fullName);

        /* initialize cert name */
        cName->country[0] = '\0';
        cName->countryEnc = CTC_PRINTABLE;
        cName->state[0] = '\0';
        cName->stateEnc = CTC_UTF8;
        cName->locality[0] = '\0';
        cName->localityEnc = CTC_UTF8;
        cName->sur[0] = '\0';
        cName->surEnc = CTC_UTF8;
        cName->org[0] = '\0';
        cName->orgEnc = CTC_UTF8;
        cName->unit[0] = '\0';
        cName->unitEnc = CTC_UTF8;
        cName->commonName[0] = '\0';
        cName->commonNameEnc = CTC_UTF8;
        cName->email[0] = '\0';


        /* ASN_COUNTRY_NAME */
        WOLFSSL_MSG("Copy Country Name");
        if (CopyX509NameEntry(cName->country, CTC_NAME_SIZE, dn->fullName + dn->cIdx,
                    dn->cLen) != SSL_SUCCESS) {
            return BUFFER_E;
        }

        /* ASN_ORGUNIT_NAME */
        WOLFSSL_MSG("Copy Org Unit Name");
        if (CopyX509NameEntry(cName->unit, CTC_NAME_SIZE, dn->fullName + dn->ouIdx,
                    dn->ouLen) != SSL_SUCCESS) {
            return BUFFER_E;
        }

        /* ASN_ORG_NAME */
        WOLFSSL_MSG("Copy Org Name");
        if (CopyX509NameEntry(cName->org, CTC_NAME_SIZE, dn->fullName + dn->oIdx,
                    dn->oLen) != SSL_SUCCESS) {
            return BUFFER_E;
        }

        /* ASN_STATE_NAME */
        WOLFSSL_MSG("Copy State Name");
        if (CopyX509NameEntry(cName->state, CTC_NAME_SIZE, dn->fullName + dn->stIdx,
                    dn->stLen) != SSL_SUCCESS) {
            return BUFFER_E;
        }

        /* ASN_LOCALITY_NAME */
        WOLFSSL_MSG("Copy Locality Name");
        if (CopyX509NameEntry(cName->locality, CTC_NAME_SIZE,
                    dn->fullName + dn->lIdx, dn->lLen)
                    != SSL_SUCCESS) {
            return BUFFER_E;
        }

        /* ASN_SUR_NAME */
        WOLFSSL_MSG("Copy Sur Name");
        if (CopyX509NameEntry(cName->sur, CTC_NAME_SIZE, dn->fullName + dn->snIdx,
                    dn->snLen) != SSL_SUCCESS) {
            return BUFFER_E;
        }

        /* ASN_COMMON_NAME */
        WOLFSSL_MSG("Copy Common Name");
        if (CopyX509NameEntry(cName->commonName, CTC_NAME_SIZE,
                    dn->fullName + dn->cnIdx, dn->cnLen)
                    != SSL_SUCCESS) {
            return BUFFER_E;
        }

        WOLFSSL_MSG("Copy Email");
        if (CopyX509NameEntry(cName->email, CTC_NAME_SIZE,
                    dn->fullName + dn->emailIdx, dn->emailLen)
                    != SSL_SUCCESS) {
            return BUFFER_E;
        }

        return SSL_SUCCESS;
    }


    /* Converts the x509 name structure into DER format.
     *
     * out  pointer to either a pre setup buffer or a pointer to null for
     *      creating a dynamic buffer. In the case that a pre-existing buffer is
     *      used out will be incremented the size of the DER buffer on success.
     *
     * returns the size of the buffer on success, or negative value with failure
     */
    int wolfSSL_i2d_X509_NAME(WOLFSSL_X509_NAME* name, unsigned char** out)
    {
        CertName cName;
        unsigned char buf[256]; //ASN_MAX_NAME
        int sz;

        if (out == NULL || name == NULL) {
            return BAD_FUNC_ARG;
        }

        if (CopyX509NameToCertName(name, &cName) != SSL_SUCCESS) {
            WOLFSSL_MSG("Error converting x509 name to internal CertName");
            return SSL_FATAL_ERROR;
        }

        sz = SetName(buf, sizeof(buf), &cName);
        if (sz < 0) {
            return sz;
        }

        /* using buffer passed in */
        if (*out != NULL) {
            XMEMCPY(*out, buf, sz);
            *out += sz;
        }
        else {
            *out = XMALLOC(sz, NULL, DYNAMIC_TYPE_OPENSSL);
            if (*out == NULL) {
                return MEMORY_E;
            }
            XMEMCPY(*out, buf, sz);
        }

        return sz;
    }
#endif /* WOLFSSL_CERT_GEN */


    /* Compares the two X509 names. If the size of x is larger then y then a
     * positive value is returned if x is smaller a negative value is returned.
     * In the case that the sizes are equal a the value of memcmp between the
     * two names is returned.
     *
     * x First name for comparision
     * y Second name to compare with x
     */
    int wolfSSL_X509_NAME_cmp(const WOLFSSL_X509_NAME* x,
            const WOLFSSL_X509_NAME* y)
    {
        WOLFSSL_STUB("wolfSSL_X509_NAME_cmp");

        if (x == NULL || y == NULL) {
            WOLFSSL_MSG("Bad argument passed in");
            return -2;
        }

        if ((x->sz - y->sz) != 0) {
            return x->sz - y->sz;
        }
        else {
            return XMEMCMP(x->name, y->name, x->sz); /* y sz is the same */
        }
    }


    WOLFSSL_X509 *wolfSSL_PEM_read_bio_X509(WOLFSSL_BIO *bp, WOLFSSL_X509 **x,
                                                 pem_password_cb *cb, void *u) {
        WOLFSSL_X509* x509 = NULL;
        const unsigned char* pem = NULL;
        int pemSz;

        WOLFSSL_ENTER("wolfSSL_PEM_read_bio_X509");

        if (bp == NULL) {
            WOLFSSL_LEAVE("wolfSSL_PEM_read_bio_X509", BAD_FUNC_ARG);
            return NULL;
        }

        pemSz = wolfSSL_BIO_get_mem_data(bp, &pem);
        if (pemSz <= 0 || pem == NULL) {
            WOLFSSL_MSG("Issue getting WOLFSSL_BIO mem");
            WOLFSSL_LEAVE("wolfSSL_PEM_read_bio_X509", pemSz);
            return NULL;
        }

        x509 = wolfSSL_X509_load_certificate_buffer(pem, pemSz,
                                                              SSL_FILETYPE_PEM);

        if (x != NULL) {
            *x = x509;
        }

        (void)cb;
        (void)u;

        return x509;
    }


    /*
     * bp : bio to read X509 from
     * x  : x509 to write to
     * cb : password call back for reading PEM
     * u  : password
     * _AUX is for working with a trusted X509 certificate
     */
    WOLFSSL_X509 *wolfSSL_PEM_read_bio_X509_AUX(WOLFSSL_BIO *bp,
                               WOLFSSL_X509 **x, pem_password_cb *cb, void *u) {
        WOLFSSL_ENTER("wolfSSL_PEM_read_bio_X509");

        /* AUX info is; trusted/rejected uses, friendly name, private key id,
         * and potentially a stack of "other" info. wolfSSL does not store
         * friendly name or private key id yet in WOLFSSL_X509 for human
         * readibility and does not support extra trusted/rejected uses for
         * root CA. */
        return wolfSSL_PEM_read_bio_X509(bp, x, cb, u);
    }

    void wolfSSL_X509_NAME_ENTRY_free(WOLFSSL_X509_NAME_ENTRY* ne)
    {
        if (ne != NULL) {
            if (ne->value != NULL && ne->value != &(ne->data)) {
                wolfSSL_ASN1_STRING_free(ne->value);
            }
            XFREE(ne, NULL, DYNAMIC_TYPE_NAME_ENTRY);
        }
    }


    WOLFSSL_X509_NAME_ENTRY* wolfSSL_X509_NAME_ENTRY_new(void)
    {
        WOLFSSL_X509_NAME_ENTRY* ne = NULL;

        ne = (WOLFSSL_X509_NAME_ENTRY*)XMALLOC(sizeof(WOLFSSL_X509_NAME_ENTRY),
                NULL, DYNAMIC_TYPE_NAME_ENTRY);
        if (ne != NULL) {
            XMEMSET(ne, 0, sizeof(WOLFSSL_X509_NAME_ENTRY));
            ne->value = &(ne->data);
        }

        return ne;
    }


    WOLFSSL_X509_NAME_ENTRY* wolfSSL_X509_NAME_ENTRY_create_by_NID(
            WOLFSSL_X509_NAME_ENTRY** out, int nid, int type,
            unsigned char* data, int dataSz)
    {
        WOLFSSL_X509_NAME_ENTRY* ne = NULL;

        WOLFSSL_ENTER("wolfSSL_X509_NAME_ENTRY_create_by_NID()");

        ne = wolfSSL_X509_NAME_ENTRY_new();
        if (ne == NULL) {
            return NULL;
        }

        ne->nid = nid;
        ne->value = wolfSSL_ASN1_STRING_type_new(type);
        wolfSSL_ASN1_STRING_set(ne->value, (const void*)data, dataSz);
        ne->set = 1;

        if (out != NULL) {
            *out = ne;
        }

        return ne;
    }


    /* Copies entry into name. With it being copied freeing entry becomes the
     * callers responsibility.
     * returns 1 for success and 0 for error */
    int wolfSSL_X509_NAME_add_entry(WOLFSSL_X509_NAME* name,
            WOLFSSL_X509_NAME_ENTRY* entry, int idx, int set)
    {
        int i;

        WOLFSSL_ENTER("wolfSSL_X509_NAME_add_entry()");

        for (i = 0; i < MAX_NAME_ENTRIES; i++) {
            if (name->extra[i].set != 1) { /* not set so overwrited */
                WOLFSSL_X509_NAME_ENTRY* current = &(name->extra[i]);
                WOLFSSL_ASN1_STRING*     str;

                WOLFSSL_MSG("Found place for name entry");

                XMEMCPY(current, entry, sizeof(WOLFSSL_X509_NAME_ENTRY));
                str = entry->value;
                XMEMCPY(&(current->data), str, sizeof(WOLFSSL_ASN1_STRING));
                current->value = &(current->data);
                current->data.data = (char*)XMALLOC(str->length,
                       name->x509->heap, DYNAMIC_TYPE_OPENSSL);

                if (current->data.data == NULL) {
                    return SSL_FAILURE;
                }
                XMEMCPY(current->data.data, str->data, str->length);

                /* make sure is null terminated */
                current->data.data[str->length - 1] = '\0';

                current->set = 1; /* make sure now listed as set */
                break;
            }
        }

        if (i == MAX_NAME_ENTRIES) {
            WOLFSSL_MSG("No spot found for name entry");
            return SSL_FAILURE;
        }

        (void)idx;
        (void)set;
        return SSL_SUCCESS;
    }
    #endif /* ifndef NO_CERTS */


    /* NID variables are dependent on compatibility header files currently */
    WOLFSSL_ASN1_OBJECT* wolfSSL_OBJ_nid2obj(int id)
    {
        word32 oidSz = 0;
        const byte* oid;
        word32 type = 0;
        WOLFSSL_ASN1_OBJECT* obj;
        byte objBuf[MAX_OID_SZ + MAX_LENGTH_SZ + 1]; /* +1 for object tag */
        word32 objSz = 0;
        const char* sName;

        WOLFSSL_ENTER("wolfSSL_OBJ_nid2obj()");

        /* get OID type */
        switch (id) {
            /* oidHashType */
        #ifdef WOLFSSL_MD2
            case NID_md2:
                id = MD2h;
                type = oidHashType;
                sName = "md2";
                break;
        #endif
        #ifndef NO_MD5
            case NID_md5:
                id = MD5h;
                type = oidHashType;
                sName = "md5";
                break;
        #endif
        #ifndef NO_SHA
            case NID_sha1:
                id = SHAh;
                type = oidHashType;
                sName = "sha";
                break;
        #endif
            case NID_sha224:
                id = SHA224h;
                type = oidHashType;
                sName = "sha224";
                break;
        #ifndef NO_SHA256
            case NID_sha256:
                id = SHA256h;
                type = oidHashType;
                sName = "sha256";
                break;
        #endif
        #ifdef WOLFSSL_SHA384
            case NID_sha384:
                id = SHA384h;
                type = oidHashType;
                sName = "sha384";
                break;
        #endif
        #ifdef WOLFSSL_SHA512
            case NID_sha512:
                id = SHA512h;
                type = oidHashType;
                sName = "sha512";
                break;
        #endif

            /*  oidSigType */
        #ifndef NO_DSA
            case CTC_SHAwDSA:
                sName = "shaWithDSA";
                type = oidSigType;
                break;

        #endif /* NO_DSA */
        #ifndef NO_RSA
            case CTC_MD2wRSA:
                sName = "md2WithRSA";
                type = oidSigType;
                break;

            case CTC_MD5wRSA:
                sName = "md5WithRSA";
                type = oidSigType;
                break;

            case CTC_SHAwRSA:
                sName = "shaWithRSA";
                type = oidSigType;
                break;

            case CTC_SHA224wRSA:
                sName = "sha224WithRSA";
                type = oidSigType;
                break;

            case CTC_SHA256wRSA:
                sName = "sha256WithRSA";
                type = oidSigType;
                break;

            case CTC_SHA384wRSA:
                sName = "sha384WithRSA";
                type = oidSigType;
                break;

            case CTC_SHA512wRSA:
                sName = "sha512WithRSA";
                type = oidSigType;
                break;

        #endif /* NO_RSA */
        #ifdef HAVE_ECC
            case CTC_SHAwECDSA:
                sName = "shaWithECDSA";
                type = oidSigType;
                break;

            case CTC_SHA224wECDSA:
                sName = "sha224WithECDSA";
                type = oidSigType;
                break;

            case CTC_SHA256wECDSA:
                sName = "sha256WithECDSA";
                type = oidSigType;
                break;

            case CTC_SHA384wECDSA:
                sName = "sha384WithECDSA";
                type = oidSigType;
                break;

            case CTC_SHA512wECDSA:
                sName = "sha512WithECDSA";
                type = oidSigType;
                break;
        #endif /* HAVE_ECC */

            /* oidKeyType */
        #ifndef NO_DSA
            case DSAk:
                sName = "DSA key";
                type = oidKeyType;
                break;
        #endif /* NO_DSA */
        #ifndef NO_RSA
            case RSAk:
                sName = "RSA key";
                type = oidKeyType;
                break;
        #endif /* NO_RSA */
        #ifdef HAVE_NTRU
            case NTRUk:
                sName = "NTRU key";
                type = oidKeyType;
                break;
        #endif /* HAVE_NTRU */
        #ifdef HAVE_ECC
            case ECDSAk:
                sName = "ECDSA key";
                type = oidKeyType;
                break;
        #endif /* HAVE_ECC */

            /* oidBlkType */
            case AES128CBCb:
                sName = "AES-128-CBC";
                type = oidBlkType;
                break;

            case AES192CBCb:
                sName = "AES-192-CBC";
                type = oidBlkType;
                break;

            case AES256CBCb:
                sName = "AES-256-CBC";
                type = oidBlkType;
                break;

            case NID_des:
                id = DESb;
                sName = "DES-CBC";
                type = oidBlkType;
                break;

            case NID_des3:
                id = DES3b;
                sName = "DES3-CBC";
                type = oidBlkType;
                break;

        #ifdef HAVE_OCSP
            case NID_id_pkix_OCSP_basic:
                id = OCSP_BASIC_OID;
                sName = "OCSP_basic";
                type = oidOcspType;
                break;

            case OCSP_NONCE_OID:
                sName = "OCSP_nonce";
                type = oidOcspType;
                break;
        #endif /* HAVE_OCSP */

            /* oidCertExtType */
            case BASIC_CA_OID:
                sName = "X509 basic ca";
                type = oidCertExtType;
                break;

            case ALT_NAMES_OID:
                sName = "X509 alt names";
                type = oidCertExtType;
                break;

            case CRL_DIST_OID:
                sName = "X509 crl";
                type = oidCertExtType;
                break;

            case AUTH_INFO_OID:
                sName = "X509 auth info";
                type = oidCertExtType;
                break;

            case AUTH_KEY_OID:
                sName = "X509 auth key";
                type = oidCertExtType;
                break;

            case SUBJ_KEY_OID:
                sName = "X509 subject key";
                type = oidCertExtType;
                break;

            case KEY_USAGE_OID:
                sName = "X509 key usage";
                type = oidCertExtType;
                break;

            case INHIBIT_ANY_OID:
                id = INHIBIT_ANY_OID;
                sName = "X509 inhibit any";
                type = oidCertExtType;
                break;

            case NID_ext_key_usage:
                id = KEY_USAGE_OID;
                sName = "X509 ext key usage";
                type = oidCertExtType;
                break;

            case NID_name_constraints:
                id = NAME_CONS_OID;
                sName = "X509 name constraints";
                type = oidCertExtType;
                break;

            case NID_certificate_policies:
                id = CERT_POLICY_OID;
                sName = "X509 certificate policies";
                type = oidCertExtType;
                break;

            /* oidCertAuthInfoType */
            case AIA_OCSP_OID:
                sName = "Cert Auth OCSP";
                type = oidCertAuthInfoType;
                break;

            case AIA_CA_ISSUER_OID:
                sName = "Cert Auth CA Issuer";
                type = oidCertAuthInfoType;
                break;

            /* oidCertPolicyType */
            case NID_any_policy:
                id = CP_ANY_OID;
                sName = "Cert any policy";
                type = oidCertPolicyType;
                break;

                /* oidCertAltNameType */
            case NID_hw_name_oid:
                id = HW_NAME_OID;
                sName = "Hardware name";
                type = oidCertAltNameType;
                break;

            /* oidCertKeyUseType */
            case NID_anyExtendedKeyUsage:
                id = EKU_ANY_OID;
                sName = "Cert any extended key";
                type = oidCertKeyUseType;
                break;

            case EKU_SERVER_AUTH_OID:
                sName = "Cert server auth key";
                type = oidCertKeyUseType;
                break;

            case EKU_CLIENT_AUTH_OID:
                sName = "Cert client auth key";
                type = oidCertKeyUseType;
                break;

            case EKU_OCSP_SIGN_OID:
                sName = "Cert OCSP sign key";
                type = oidCertKeyUseType;
                break;

            /* oidKdfType */
            case PBKDF2_OID:
                sName = "PBKDFv2";
                type = oidKdfType;
                break;

                /* oidPBEType */
            case PBE_SHA1_RC4_128:
                sName = "PBE shaWithRC4-128";
                type = oidPBEType;
                break;

            case PBE_SHA1_DES:
                sName = "PBE shaWithDES";
                type = oidPBEType;
                break;

            case PBE_SHA1_DES3:
                sName = "PBE shaWithDES3";
                type = oidPBEType;
                break;

                /* oidKeyWrapType */
            case AES128_WRAP:
                sName = "AES-128 wrap";
                type = oidKeyWrapType;
                break;

            case AES192_WRAP:
                sName = "AES-192 wrap";
                type = oidKeyWrapType;
                break;

            case AES256_WRAP:
                sName = "AES-256 wrap";
                type = oidKeyWrapType;
                break;

                /* oidCmsKeyAgreeType */
            case dhSinglePass_stdDH_sha1kdf_scheme:
                sName = "DH-SHA kdf";
                type = oidCmsKeyAgreeType;
                break;

            case dhSinglePass_stdDH_sha224kdf_scheme:
                sName = "DH-SHA224 kdf";
                type = oidCmsKeyAgreeType;
                break;

            case dhSinglePass_stdDH_sha256kdf_scheme:
                sName = "DH-SHA256 kdf";
                type = oidCmsKeyAgreeType;
                break;

            case dhSinglePass_stdDH_sha384kdf_scheme:
                sName = "DH-SHA384 kdf";
                type = oidCmsKeyAgreeType;
                break;

            case dhSinglePass_stdDH_sha512kdf_scheme:
                sName = "DH-SHA512 kdf";
                type = oidCmsKeyAgreeType;
                break;

            default:
                WOLFSSL_MSG("NID not in table");
                return NULL;
        }

    #ifdef HAVE_ECC
         if (type == 0 && wc_ecc_get_oid(id, &oid, &oidSz) > 0) {
             type = oidCurveType;
         }
    #endif /* HAVE_ECC */

        if (XSTRLEN(sName) > WOLFSSL_MAX_SNAME - 1) {
            WOLFSSL_MSG("Attempted short name is too large");
            return NULL;
        }

        oid = OidFromId(id, type, &oidSz);

        /* set object ID to buffer */
        obj = wolfSSL_ASN1_OBJECT_new();
        if (obj == NULL) {
            WOLFSSL_MSG("Issue creating WOLFSSL_ASN1_OBJECT struct");
            return NULL;
        }
        obj->type    = id;
        obj->dynamic = 1;
        XMEMCPY(obj->sName, (char*)sName, XSTRLEN((char*)sName));

        objBuf[0] = ASN_OBJECT_ID; objSz++;
        objSz += SetLength(oidSz, objBuf + 1);
        XMEMCPY(objBuf + objSz, oid, oidSz);
        objSz     += oidSz;
        obj->objSz = objSz;

        obj->obj = (byte*)XMALLOC(obj->objSz, NULL, DYNAMIC_TYPE_ASN1);
        if (obj->obj == NULL) {
            wolfSSL_ASN1_OBJECT_free(obj);
            return NULL;
        }
        XMEMCPY(obj->obj, objBuf, obj->objSz);

        (void)type;

        return obj;
    }


    /* if no_name is one than use numerical form otherwise can be short name. */
    int wolfSSL_OBJ_obj2txt(char *buf, int bufLen, WOLFSSL_ASN1_OBJECT *a, int no_name)
    {
        int bufSz;

        WOLFSSL_ENTER("wolfSSL_OBJ_obj2txt()");

        if (buf == NULL || bufLen <= 1 || a == NULL) {
            WOLFSSL_MSG("Bad input argument");
            return SSL_FAILURE;
        }

        if (no_name == 1) {
            int    length;
            word32 idx = 0;

            if (a->obj[idx++] != ASN_OBJECT_ID) {
                WOLFSSL_MSG("Bad ASN1 Object");
                return SSL_FAILURE;
            }

            if (GetLength((const byte*)a->obj, &idx, &length,
                           a->objSz) < 0 || length < 0) {
                return ASN_PARSE_E;
            }

            if (bufLen < MAX_OID_STRING_SZ) {
                bufSz = bufLen - 1;
            }
            else {
                bufSz = MAX_OID_STRING_SZ;
            }

            if ((bufSz = DecodePolicyOID(buf, (word32)bufSz, a->obj + idx,
                        (word32)length)) <= 0) {
                WOLFSSL_MSG("Error decoding OID");
                return SSL_FAILURE;
            }

        }
        else { /* return short name */
            if (XSTRLEN(a->sName) + 1 < (word32)bufLen - 1) {
                bufSz = (int)XSTRLEN(a->sName);
            }
            else {
                bufSz = bufLen - 1;
            }
            XMEMCPY(buf, a->sName, bufSz);
        }

        buf[bufSz] = '\0';
        return bufSz;
    }

#if defined(HAVE_LIGHTY) || defined(WOLFSSL_MYSQL_COMPATIBLE) || defined(HAVE_STUNNEL)

    unsigned char *wolfSSL_SHA1(const unsigned char *d, size_t n, unsigned char *md)
    {
        (void) *d; (void) n; (void) *md;
        WOLFSSL_ENTER("wolfSSL_SHA1");
        WOLFSSL_STUB("wolfssl_SHA1");

        return NULL;
    }

    char wolfSSL_CTX_use_certificate(WOLFSSL_CTX *ctx, WOLFSSL_X509 *x) {
        (void)ctx;
        (void)x;
        WOLFSSL_ENTER("wolfSSL_CTX_use_certificate");
        WOLFSSL_STUB("wolfSSL_CTX_use_certificate");

        return 0;
    }

    int wolfSSL_BIO_read_filename(WOLFSSL_BIO *b, const char *name) {
        (void)b;
        (void)name;
        WOLFSSL_ENTER("wolfSSL_BIO_read_filename");
        WOLFSSL_STUB("wolfSSL_BIO_read_filename");

        return 0;
    }

#ifdef HAVE_ECC
    const char * wolfSSL_OBJ_nid2sn(int n) {
        int i;
        WOLFSSL_ENTER("wolfSSL_OBJ_nid2sn");

        /* find based on NID and return name */
        for (i = 0; i < ecc_sets[i].size; i++) {
            if (n == ecc_sets[i].id) {
                return ecc_sets[i].name;
            }
        }
        return NULL;
    }

    int wolfSSL_OBJ_sn2nid(const char *sn) {
        int i;
        WOLFSSL_ENTER("wolfSSL_OBJ_osn2nid");

        /* find based on name and return NID */
        for (i = 0; i < ecc_sets[i].size; i++) {
            if (XSTRNCMP(sn, ecc_sets[i].name, ECC_MAXNAME) == 0) {
                return ecc_sets[i].id;
            }
        }
        return -1;
    }
#endif /* HAVE_ECC */

    int wolfSSL_OBJ_obj2nid(const WOLFSSL_ASN1_OBJECT *o) {
        (void)o;
        WOLFSSL_ENTER("wolfSSL_OBJ_obj2nid");
        WOLFSSL_STUB("wolfSSL_OBJ_obj2nid");

        return 0;
    }

    char * wolfSSL_OBJ_nid2ln(int n)
    {
        (void)n;
        WOLFSSL_ENTER("wolfSSL_OBJ_nid2ln");
        WOLFSSL_STUB("wolfSSL_OBJ_nid2ln");

        return NULL;
    }

    int wolfSSL_OBJ_txt2nid(const char* s)
    {
        (void)s;
        WOLFSSL_STUB("wolfSSL_OBJ_txt2nid");

        return 0;
    }


    /* compatibility function. It's intended use is to remove OID's from an
     * internal table that have been added with OBJ_create. wolfSSL manages it's
     * own interenal OID values and does not currently support OBJ_create. */
    void wolfSSL_OBJ_cleanup(void)
    {
        WOLFSSL_ENTER("wolfSSL_OBJ_cleanup()");
    }


    void wolfSSL_CTX_set_verify_depth(WOLFSSL_CTX *ctx, int depth) {
        (void)ctx;
        (void)depth;
        WOLFSSL_ENTER("wolfSSL_CTX_set_verify_depth");
        WOLFSSL_STUB("wolfSSL_CTX_set_verify_depth");

    }

    void* wolfSSL_get_app_data( const WOLFSSL *ssl)
    {
        /* checkout exdata stuff... */
        (void)ssl;
        WOLFSSL_ENTER("wolfSSL_get_app_data");
        WOLFSSL_STUB("wolfSSL_get_app_data");

        return 0;
    }

    void wolfSSL_set_app_data(WOLFSSL *ssl, void *arg) {
        (void)ssl;
        (void)arg;
        WOLFSSL_ENTER("wolfSSL_set_app_data");
        WOLFSSL_STUB("wolfSSL_set_app_data");
    }

    WOLFSSL_ASN1_OBJECT * wolfSSL_X509_NAME_ENTRY_get_object(WOLFSSL_X509_NAME_ENTRY *ne) {
        (void)ne;
        WOLFSSL_ENTER("wolfSSL_X509_NAME_ENTRY_get_object");
        WOLFSSL_STUB("wolfSSL_X509_NAME_ENTRY_get_object");

        return NULL;
    }

    WOLFSSL_X509_NAME_ENTRY *wolfSSL_X509_NAME_get_entry(
                                             WOLFSSL_X509_NAME *name, int loc) {

        int maxLoc = name->fullName.fullNameLen;

        WOLFSSL_ENTER("wolfSSL_X509_NAME_get_entry");

        if (loc < 0 || loc > maxLoc) {
            WOLFSSL_MSG("Bad argument");
            return NULL;
        }

        /* common name index case */
        if (loc == name->fullName.cnIdx) {
            /* get CN shortcut from x509 since it has null terminator */
            name->cnEntry.data.data   = name->x509->subjectCN;
            name->cnEntry.data.length = name->fullName.cnLen;
            name->cnEntry.data.type   = CTC_UTF8;
            name->cnEntry.nid         = ASN_COMMON_NAME;
            name->cnEntry.set  = 1;
            return &(name->cnEntry);
        }

        /* additionall cases to check for go here */

        WOLFSSL_MSG("Entry not found or implemented");
        (void)name;
        (void)loc;

        return NULL;
    }


    void wolfSSL_sk_X509_NAME_pop_free(STACK_OF(WOLFSSL_X509_NAME)* sk, void f (WOLFSSL_X509_NAME*)){
        (void) sk;
        (void) f;
        WOLFSSL_ENTER("wolfSSL_sk_X509_NAME_pop_free");
        WOLFSSL_STUB("wolfSSL_sk_X509_NAME_pop_free");
    }

    int wolfSSL_X509_check_private_key(WOLFSSL_X509 *x509, WOLFSSL_EVP_PKEY *key){
        (void) x509;
        (void) key;
        WOLFSSL_ENTER("wolfSSL_X509_check_private_key");
        WOLFSSL_STUB("wolfSSL_X509_check_private_key");

        return SSL_SUCCESS;
    }

    STACK_OF(WOLFSSL_X509_NAME) *wolfSSL_dup_CA_list( STACK_OF(WOLFSSL_X509_NAME) *sk ){
        (void) sk;
        WOLFSSL_ENTER("wolfSSL_dup_CA_list");
        WOLFSSL_STUB("wolfSSL_dup_CA_list");

        return NULL;
    }

#endif /* HAVE_LIGHTY || WOLFSSL_MYSQL_COMPATIBLE || HAVE_STUNNEL */
#endif


#ifdef OPENSSL_EXTRA

/* wolfSSL uses negative values for error states. This function returns an
 * unsigned type so the value returned is the absolute value of the error.
 */
unsigned long wolfSSL_ERR_peek_last_error_line(const char **file, int *line)
{
    WOLFSSL_ENTER("wolfSSL_ERR_peek_last_error");

    (void)line;
    (void)file;
#if defined(DEBUG_WOLFSSL)
    {
        int ret;

        if ((ret = wc_PeekErrorNode(-1, file, NULL, line)) < 0) {
            WOLFSSL_MSG("Issue peeking at error node in queue");
            return 0;
        }
        return (unsigned long)ret;
    }
#else
    return (unsigned long)(0 - NOT_COMPILED_IN);
#endif
}


#ifndef NO_CERTS
int wolfSSL_CTX_use_PrivateKey(WOLFSSL_CTX *ctx, WOLFSSL_EVP_PKEY *pkey)
{
    WOLFSSL_ENTER("wolfSSL_CTX_use_PrivateKey");

    if (ctx == NULL || pkey == NULL) {
        return SSL_FAILURE;
    }

    return wolfSSL_CTX_use_PrivateKey_buffer(ctx,
                                       (const unsigned char*)pkey->pkey.ptr,
                                       pkey->pkey_sz, PRIVATEKEY_TYPE);
}
#endif /* !NO_CERTS */


void* wolfSSL_CTX_get_ex_data(const WOLFSSL_CTX* ctx, int idx)
{
    WOLFSSL_ENTER("wolfSSL_CTX_get_ex_data");
    #ifdef HAVE_STUNNEL
    if(ctx != NULL && idx < MAX_EX_DATA && idx >= 0) {
        return ctx->ex_data[idx];
    }
    #else
    (void)ctx;
    (void)idx;
    #endif
    return NULL;
}


int wolfSSL_CTX_get_ex_new_index(long idx, void* arg, void* a, void* b,
                                void* c)
{
    WOLFSSL_ENTER("wolfSSL_CTX_get_ex_new_index");
    (void)idx;
    (void)arg;
    (void)a;
    (void)b;
    (void)c;
    return 0;
}


int wolfSSL_CTX_set_ex_data(WOLFSSL_CTX* ctx, int idx, void* data)
{
    WOLFSSL_ENTER("wolfSSL_CTX_set_ex_data");
    #ifdef HAVE_STUNNEL
    if (ctx != NULL && idx < MAX_EX_DATA)
    {
        ctx->ex_data[idx] = data;
        return SSL_SUCCESS;
    }
    #else
    (void)ctx;
    (void)idx;
    (void)data;
    #endif
    return SSL_FAILURE;
}


int wolfSSL_set_ex_data(WOLFSSL* ssl, int idx, void* data)
{
    WOLFSSL_ENTER("wolfSSL_set_ex_data");
#if defined(FORTRESS) || defined(HAVE_STUNNEL)
    if (ssl != NULL && idx < MAX_EX_DATA)
    {
        ssl->ex_data[idx] = data;
        return SSL_SUCCESS;
    }
#else
    (void)ssl;
    (void)idx;
    (void)data;
#endif
    return SSL_FAILURE;
}


int wolfSSL_get_ex_new_index(long idx, void* data, void* cb1, void* cb2,
                         void* cb3)
{
    WOLFSSL_ENTER("wolfSSL_get_ex_new_index");
    (void)idx;
    (void)data;
    (void)cb1;
    (void)cb2;
    (void)cb3;
    return 0;
}


void* wolfSSL_get_ex_data(const WOLFSSL* ssl, int idx)
{
    WOLFSSL_ENTER("wolfSSL_get_ex_data");
#if defined(FORTRESS) || defined(HAVE_STUNNEL)
    if (ssl != NULL && idx < MAX_EX_DATA && idx >= 0)
        return ssl->ex_data[idx];
#else
    (void)ssl;
    (void)idx;
#endif
    return 0;
}

#ifndef NO_DSA
WOLFSSL_DSA *wolfSSL_PEM_read_bio_DSAparams(WOLFSSL_BIO *bp, WOLFSSL_DSA **x,
        pem_password_cb *cb, void *u)
{
    WOLFSSL_DSA* dsa;
    DsaKey* key;
    int    length;
    const unsigned char*  buf;
    word32 bufSz;
    int ret;
    word32 idx = 0;
    DerBuffer* pDer;

    WOLFSSL_ENTER("wolfSSL_PEM_read_bio_DSAparams");

    ret = wolfSSL_BIO_get_mem_data(bp, &buf);
    if (ret <= 0) {
        WOLFSSL_LEAVE("wolfSSL_PEM_read_bio_DSAparams", ret);
        return NULL;
    }

    bufSz = (word32)ret;

    if (cb != NULL || u != NULL) {
        /*
         * cb is for a call back when encountering encrypted PEM files
         * if cb == NULL and u != NULL then u = null terminated password string
         */
        WOLFSSL_MSG("Not yet supporting call back or password for encrypted PEM");
    }

    if ((ret = PemToDer(buf, (long)bufSz, DSA_PARAM_TYPE, &pDer, NULL, NULL,
                    NULL)) < 0 ) {
        WOLFSSL_MSG("Issue converting from PEM to DER");
        return NULL;
    }

    if ((ret = GetSequence(pDer->buffer, &idx, &length, pDer->length)) < 0) {
        WOLFSSL_LEAVE("wolfSSL_PEM_read_bio_DSAparams", ret);
        FreeDer(&pDer);
        return NULL;
    }

    dsa = wolfSSL_DSA_new();
    if (dsa == NULL) {
        FreeDer(&pDer);
        WOLFSSL_MSG("Error creating DSA struct");
        return NULL;
    }

    key = (DsaKey*)dsa->internal;
    if (key == NULL) {
        FreeDer(&pDer);
        wolfSSL_DSA_free(dsa);
        WOLFSSL_MSG("Error finding DSA key struct");
        return NULL;
    }

    if (GetInt(&key->p,  pDer->buffer, &idx, pDer->length) < 0 ||
        GetInt(&key->q,  pDer->buffer, &idx, pDer->length) < 0 ||
        GetInt(&key->g,  pDer->buffer, &idx, pDer->length) < 0 ) {
        WOLFSSL_MSG("dsa key error");
        FreeDer(&pDer);
        wolfSSL_DSA_free(dsa);
        return NULL;
    }

    if (SetIndividualExternal(&dsa->p, &key->p) != SSL_SUCCESS) {
        WOLFSSL_MSG("dsa p key error");
        FreeDer(&pDer);
        wolfSSL_DSA_free(dsa);
        return NULL;
    }

    if (SetIndividualExternal(&dsa->q, &key->q) != SSL_SUCCESS) {
        WOLFSSL_MSG("dsa q key error");
        FreeDer(&pDer);
        wolfSSL_DSA_free(dsa);
        return NULL;
    }

    if (SetIndividualExternal(&dsa->g, &key->g) != SSL_SUCCESS) {
        WOLFSSL_MSG("dsa g key error");
        FreeDer(&pDer);
        wolfSSL_DSA_free(dsa);
        return NULL;
    }

    if (x != NULL) {
        *x = dsa;
    }

    FreeDer(&pDer);
    return dsa;
}
#endif /* NO_DSA */

#include "src/bio.c"

/* Begin functions for openssl/buffer.h */
WOLFSSL_BUF_MEM* wolfSSL_BUF_MEM_new(void)
{
    WOLFSSL_BUF_MEM* buf;
    buf = (WOLFSSL_BUF_MEM*)XMALLOC(sizeof(WOLFSSL_BUF_MEM), NULL,
                                                        DYNAMIC_TYPE_OPENSSL);
    if (buf) {
        XMEMSET(buf, 0, sizeof(WOLFSSL_BUF_MEM));
    }
    return buf;
}

int wolfSSL_BUF_MEM_grow(WOLFSSL_BUF_MEM* buf, size_t len)
{
    int len_int = (int)len;
    int max;

    /* verify provided arguments */
    if (buf == NULL || len_int < 0) {
        return 0; /* BAD_FUNC_ARG; */
    }

    /* check to see if fits in existing length */
    if (buf->length > len) {
        buf->length = len;
        return len_int;
    }

    /* check to see if fits in max buffer */
    if (buf->max >= len) {
        if (buf->data != NULL) {
            XMEMSET(&buf->data[buf->length], 0, len - buf->length);
        }
        buf->length = len;
        return len_int;
    }

    /* expand size, to handle growth */
    max = (len_int + 3) / 3 * 4;

    /* use realloc */
    buf->data = (char*)XREALLOC(buf->data, max, NULL, DYNAMIC_TYPE_TMP_BUFFER);
    if (buf->data == NULL) {
        return 0; /* ERR_R_MALLOC_FAILURE; */
    }

    buf->max = max;
    XMEMSET(&buf->data[buf->length], 0, len - buf->length);
    buf->length = len;

    return len_int;
}

void wolfSSL_BUF_MEM_free(WOLFSSL_BUF_MEM* buf)
{
    if (buf) {
        if (buf->data) {
            XFREE(buf->data, NULL, DYNAMIC_TYPE_TMP_BUFFER);
            buf->data = NULL;
        }
        buf->max = 0;
        buf->length = 0;
        XFREE(buf, NULL, DYNAMIC_TYPE_OPENSSL);
    }
}
/* End Functions for openssl/buffer.h */

#endif /* OPENSSL_EXTRA */


#if defined(HAVE_LIGHTY) || defined(HAVE_STUNNEL) \
    || defined(WOLFSSL_MYSQL_COMPATIBLE) || defined(OPENSSL_EXTRA)

#ifndef NO_FILESYSTEM
WOLFSSL_BIO *wolfSSL_BIO_new_file(const char *name, const char *m) {
    BIO  *bio;
    FILE *f;

    WOLFSSL_ENTER("wolfSSL_BIO_new_file");
    if ((f = XFOPEN(name, m)) == NULL)return NULL;
    if ((bio = wolfSSL_BIO_new(wolfSSL_BIO_s_file())) == NULL) {
        fclose(f);
        return NULL;
    }
    wolfSSL_BIO_set_fp(bio, f, BIO_CLOSE);
    return bio;
}
#endif /* NO_FILESYSTEM */


WOLFSSL_DH *wolfSSL_PEM_read_bio_DHparams(WOLFSSL_BIO *bp, WOLFSSL_DH **x,
        pem_password_cb *cb, void *u)
{
    (void) bp;
    (void) x;
    (void) cb;
    (void) u;

    WOLFSSL_ENTER("wolfSSL_PEM_read_bio_DHparams");
    WOLFSSL_STUB("wolfSSL_PEM_read_bio_DHparams");

    return NULL;
}


#ifdef WOLFSSL_CERT_GEN

#ifdef WOLFSSL_CERT_REQ
int wolfSSL_PEM_write_bio_X509_REQ(WOLFSSL_BIO *bp, WOLFSSL_X509 *x)
{
    byte* pem;
    int   pemSz = 0;
    const unsigned char* der;
    int derSz;
    int ret;

    WOLFSSL_ENTER("wolfSSL_PEM_write_bio_X509_REQ()");

    if (x == NULL || bp == NULL) {
        return SSL_FAILURE;
    }

    der = wolfSSL_X509_get_der(x, &derSz);
    if (der == NULL) {
        return SSL_FAILURE;
    }

    /* get PEM size */
    pemSz = wc_DerToPemEx(der, derSz, NULL, 0, NULL, CERTREQ_TYPE);
    if (pemSz < 0) {
        #ifdef WOLFSSL_SMALL_STACK
            XFREE(der, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        #endif
        return SSL_FAILURE;
    }

    /* create PEM buffer and convert from DER */
    pem = (byte*)XMALLOC(pemSz, NULL, DYNAMIC_TYPE_TMP_BUFFER);
    if (pem == NULL) {
        return SSL_FAILURE;
    }
    if (wc_DerToPemEx(der, derSz, pem, pemSz, NULL, CERTREQ_TYPE) < 0) {
        XFREE(pem, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        return SSL_FAILURE;
    }

    /* write the PEM to BIO */
    ret = wolfSSL_BIO_write(bp, pem, pemSz);
    XFREE(pem, NULL, DYNAMIC_TYPE_TMP_BUFFER);

    if (ret <= 0) return SSL_FAILURE;
    return SSL_SUCCESS;
}
#endif /* WOLFSSL_CERT_REQ */


int wolfSSL_PEM_write_bio_X509_AUX(WOLFSSL_BIO *bp, WOLFSSL_X509 *x)
{
    byte* pem;
    int   pemSz = 0;
    const unsigned char* der;
    int derSz;
    int ret;

    WOLFSSL_ENTER("wolfSSL_PEM_write_bio_X509_AUX()");

    if (bp == NULL || x == NULL) {
        WOLFSSL_MSG("NULL argument passed in");
        return SSL_FAILURE;
    }

    der = wolfSSL_X509_get_der(x, &derSz);
    if (der == NULL) {
        return SSL_FAILURE;
    }

    /* get PEM size */
    pemSz = wc_DerToPemEx(der, derSz, NULL, 0, NULL, CERT_TYPE);
    if (pemSz < 0) {
        return SSL_FAILURE;
    }

    /* create PEM buffer and convert from DER */
    pem = (byte*)XMALLOC(pemSz, NULL, DYNAMIC_TYPE_TMP_BUFFER);
    if (pem == NULL) {
        return SSL_FAILURE;
    }
    if (wc_DerToPemEx(der, derSz, pem, pemSz, NULL, CERT_TYPE) < 0) {
        XFREE(pem, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        return SSL_FAILURE;
    }

    /* write the PEM to BIO */
    ret = wolfSSL_BIO_write(bp, pem, pemSz);
    XFREE(pem, NULL, DYNAMIC_TYPE_TMP_BUFFER);

    if (ret <= 0) return SSL_FAILURE;
    return SSL_SUCCESS;
}
#endif /* WOLFSSL_CERT_GEN */


int wolfSSL_PEM_write_bio_X509(WOLFSSL_BIO *bp, WOLFSSL_X509 *x) {
    (void)bp;
    (void)x;
    WOLFSSL_ENTER("wolfSSL_PEM_write_bio_X509");
    WOLFSSL_STUB("wolfSSL_PEM_write_bio_X509");

    return 0;
}


#if defined(OPENSSL_EXTRA) && !defined(NO_DH)
/* Intialize ctx->dh with dh's params. Return SSL_SUCCESS on ok */
long wolfSSL_CTX_set_tmp_dh(WOLFSSL_CTX* ctx, WOLFSSL_DH* dh)
{
    int pSz, gSz;
    byte *p, *g;
    int ret=0;

    WOLFSSL_ENTER("wolfSSL_CTX_set_tmp_dh");

    if(!ctx || !dh)
        return BAD_FUNC_ARG;

    /* Get needed size for p and g */
    pSz = wolfSSL_BN_bn2bin(dh->p, NULL);
    gSz = wolfSSL_BN_bn2bin(dh->g, NULL);

    if(pSz <= 0 || gSz <= 0)
        return SSL_FATAL_ERROR;

    p = (byte*)XMALLOC(pSz, ctx->heap, DYNAMIC_TYPE_DH);
    if(!p)
        return MEMORY_E;

    g = (byte*)XMALLOC(gSz, ctx->heap, DYNAMIC_TYPE_DH);
    if(!g) {
        XFREE(p, ctx->heap, DYNAMIC_TYPE_DH);
        return MEMORY_E;
    }

    pSz = wolfSSL_BN_bn2bin(dh->p, p);
    gSz = wolfSSL_BN_bn2bin(dh->g, g);

    if(pSz >= 0 && gSz >= 0) /* Conversion successful */
        ret = wolfSSL_CTX_SetTmpDH(ctx, p, pSz, g, gSz);

    XFREE(p, ctx->heap, DYNAMIC_TYPE_DH);
    XFREE(g, ctx->heap, DYNAMIC_TYPE_DH);

    return pSz > 0 && gSz > 0 ? ret : SSL_FATAL_ERROR;
}
#endif /* OPENSSL_EXTRA && !NO_DH */
#endif /* HAVE_LIGHTY || HAVE_STUNNEL || WOLFSSL_MYSQL_COMPATIBLE */


/* stunnel compatibility functions*/
#if defined(OPENSSL_EXTRA) && defined(HAVE_STUNNEL)
void WOLFSSL_ERR_remove_thread_state(void* pid)
{
    (void) pid;
    return;
}

/***TBD ***/
void wolfSSL_print_all_errors_fp(XFILE *fp)
{
    (void)fp;
}

int wolfSSL_SESSION_set_ex_data(WOLFSSL_SESSION* session, int idx, void* data)
{
    WOLFSSL_ENTER("wolfSSL_SESSION_set_ex_data");
    if(session != NULL && idx < MAX_EX_DATA) {
        session->ex_data[idx] = data;
        return SSL_SUCCESS;
    }
    return SSL_FAILURE;
}


int wolfSSL_SESSION_get_ex_new_index(long idx, void* data, void* cb1,
       void* cb2, CRYPTO_free_func* cb3)
{
    WOLFSSL_ENTER("wolfSSL_SESSION_get_ex_new_index");
    (void)idx;
    (void)cb1;
    (void)cb2;
    (void)cb3;
    if(XSTRNCMP((const char*)data, "redirect index", 14) == 0) {
        return 0;
    }
    else if(XSTRNCMP((const char*)data, "addr index", 10) == 0) {
        return 1;
    }
    return SSL_FAILURE;
}


void* wolfSSL_SESSION_get_ex_data(const WOLFSSL_SESSION* session, int idx)
{
    WOLFSSL_ENTER("wolfSSL_SESSION_get_ex_data");
    if (session != NULL && idx < MAX_EX_DATA && idx >= 0)
        return session->ex_data[idx];
    return NULL;
}


int wolfSSL_CRYPTO_set_mem_ex_functions(void *(*m) (size_t, const char *, int),
                                void *(*r) (void *, size_t, const char *,
                                            int), void (*f) (void *))
{
    (void) m;
    (void) r;
    (void) f;
    WOLFSSL_ENTER("wolfSSL_CRYPTO_set_mem_ex_functions");
    WOLFSSL_STUB("wolfSSL_CRYPTO_set_mem_ex_functions");

    return SSL_FAILURE;
}

void wolfSSL_CRYPTO_cleanup_all_ex_data(void){
    WOLFSSL_ENTER("CRYPTO_cleanup_all_ex_data");
}

WOLFSSL_DH *wolfSSL_DH_generate_parameters(int prime_len, int generator,
                           void (*callback) (int, int, void *), void *cb_arg)
{
    (void)prime_len;
    (void)generator;
    (void)callback;
    (void)cb_arg;
    WOLFSSL_ENTER("wolfSSL_DH_generate_parameters");
    WOLFSSL_STUB("wolfSSL_DH_generate_parameters");

    return NULL;
}

int wolfSSL_DH_generate_parameters_ex(WOLFSSL_DH* dh, int prime_len, int generator,
                           void (*callback) (int, int, void *))
{
    (void)prime_len;
    (void)generator;
    (void)callback;
    (void)dh;
    WOLFSSL_ENTER("wolfSSL_DH_generate_parameters_ex");
    WOLFSSL_STUB("wolfSSL_DH_generate_parameters_ex");

    return -1;
}


void wolfSSL_ERR_load_crypto_strings(void)
{
    WOLFSSL_ENTER("wolfSSL_ERR_load_crypto_strings");
    WOLFSSL_STUB("wolfSSL_ERR_load_crypto_strings");
    return;
}


unsigned long wolfSSL_ERR_peek_last_error(void)
{
    unsigned long l = 0UL;
    WOLFSSL_ENTER("wolfSSL_ERR_peek_last_error");
    WOLFSSL_STUB("wolfSSL_ERR_peek_last_error");

    return l;
}


int wolfSSL_FIPS_mode(void)
{
    WOLFSSL_ENTER("wolfSSL_FIPS_mode");
    WOLFSSL_STUB("wolfSSL_FIPS_mode");

    return SSL_FAILURE;
}

int wolfSSL_FIPS_mode_set(int r)
{
    (void)r;
    WOLFSSL_ENTER("wolfSSL_FIPS_mode_set");
    WOLFSSL_STUB("wolfSSL_FIPS_mode_set");

    return SSL_FAILURE;
}


int wolfSSL_RAND_set_rand_method(const void *meth)
{
    (void) meth;
    WOLFSSL_ENTER("wolfSSL_RAND_set_rand_method");
    WOLFSSL_STUB("wolfSSL_RAND_set_rand_method");

    /* if implemented RAND_bytes and RAND_pseudo_bytes need updated
     * those two functions will call the respective functions from meth */
    return SSL_FAILURE;
}


int wolfSSL_CIPHER_get_bits(const WOLFSSL_CIPHER *c, int *alg_bits)
{
    int ret = SSL_FAILURE;
    WOLFSSL_ENTER("wolfSSL_CIPHER_get_bits");
    if(c != NULL && c->ssl != NULL) {
        ret = 8 * c->ssl->specs.key_size;
        if(alg_bits != NULL) {
            *alg_bits = ret;
        }
    }
    return ret;
}


int wolfSSL_sk_X509_NAME_num(const STACK_OF(WOLFSSL_X509_NAME) *s)
{
    (void) s;
    WOLFSSL_ENTER("wolfSSL_sk_X509_NAME_num");
    WOLFSSL_STUB("wolfSSL_sk_X509_NAME_num");

    return SSL_FAILURE;
}


int wolfSSL_sk_X509_num(const STACK_OF(WOLFSSL_X509) *s)
{
    (void) s;
    WOLFSSL_ENTER("wolfSSL_sk_X509_num");
    WOLFSSL_STUB("wolfSSL_sk_X509_num");

    return SSL_FAILURE;
}


int wolfSSL_X509_NAME_print_ex(WOLFSSL_BIO* bio, WOLFSSL_X509_NAME* nm,
                int indent, unsigned long flags)
{
    (void)bio;
    (void)nm;
    (void)indent;
    (void)flags;
    WOLFSSL_ENTER("wolfSSL_X509_NAME_print_ex");
    WOLFSSL_STUB("wolfSSL_X509_NAME_print_ex");

    return SSL_FAILURE;
}


WOLFSSL_ASN1_BIT_STRING* wolfSSL_X509_get0_pubkey_bitstr(const WOLFSSL_X509* x)
{
    (void)x;
    WOLFSSL_ENTER("wolfSSL_X509_get0_pubkey_bitstr");
    WOLFSSL_STUB("wolfSSL_X509_get0_pubkey_bitstr");

    return NULL;
}


int wolfSSL_CTX_add_session(WOLFSSL_CTX* ctx, WOLFSSL_SESSION* session)
{
    (void)ctx;
    (void)session;
    WOLFSSL_ENTER("wolfSSL_CTX_add_session");
    WOLFSSL_STUB("wolfSSL_CTX_add_session");

    return SSL_SUCCESS;
}


int wolfSSL_get_state(const WOLFSSL* ssl)
{
    (void)ssl;
    WOLFSSL_ENTER("wolfSSL_get_state");
    WOLFSSL_STUB("wolfSSL_get_state");

    return SSL_FAILURE;
}


void* wolfSSL_sk_X509_NAME_value(const STACK_OF(WOLFSSL_X509_NAME)* sk, int i)
{
    (void)sk;
    (void)i;
    WOLFSSL_ENTER("wolfSSL_sk_X509_NAME_value");
    WOLFSSL_STUB("wolfSSL_sk_X509_NAME_value");

    return NULL;
}


void* wolfSSL_sk_X509_value(STACK_OF(WOLFSSL_X509)* sk, int i)
{
    (void)sk;
    (void)i;
    WOLFSSL_ENTER("wolfSSL_sk_X509_value");
    WOLFSSL_STUB("wolfSSL_sk_X509_value");

    return NULL;
}


int wolfSSL_version(WOLFSSL* ssl)
{
    WOLFSSL_ENTER("wolfSSL_version");
    if (ssl->version.major == SSLv3_MAJOR) {
        switch (ssl->version.minor) {
            case SSLv3_MINOR :
                return SSL3_VERSION;
            case TLSv1_MINOR :
            case TLSv1_1_MINOR :
            case TLSv1_2_MINOR :
                return TLS1_VERSION;
            default:
                return SSL_FAILURE;
        }
    }
    else if (ssl->version.major == DTLS_MAJOR) {
        switch (ssl->version.minor) {
            case DTLS_MINOR :
            case DTLSv1_2_MINOR :
                return DTLS1_VERSION;
            default:
                return SSL_FAILURE;
        }
    }
    return SSL_FAILURE;
}


STACK_OF(WOLFSSL_X509)* wolfSSL_get_peer_cert_chain(const WOLFSSL* ssl)
{
    (void)ssl;
    WOLFSSL_ENTER("wolfSSL_get_peer_cert_chain");
    WOLFSSL_STUB("wolfSSL_get_peer_cert_chain");

    return NULL;
}


WOLFSSL_CTX* wolfSSL_get_SSL_CTX(WOLFSSL* ssl)
{
    WOLFSSL_ENTER("wolfSSL_get_SSL_CTX");
    return ssl->ctx;
}

int wolfSSL_X509_NAME_get_sz(WOLFSSL_X509_NAME* name)
{
    WOLFSSL_ENTER("wolfSSL_X509_NAME_get_sz");
    if(!name)
        return -1;
    return name->sz;
}


const byte* wolfSSL_SESSION_get_id(WOLFSSL_SESSION* sess, unsigned int* idLen)
{
    WOLFSSL_ENTER("wolfSSL_SESSION_get_id");
    WOLFSSL_STUB("wolfSSL_SESSION_get_id");
    if(!sess || !idLen) {
        WOLFSSL_MSG("Bad func args. Please provide idLen");
        return NULL;
    }
    *idLen = sess->sessionIDSz;
    return sess->sessionID;
}

#ifdef HAVE_SNI
int wolfSSL_set_tlsext_host_name(WOLFSSL* ssl, const char* host_name)
{
    int ret;
    WOLFSSL_ENTER("wolfSSL_set_tlsext_host_name");
    ret = wolfSSL_UseSNI(ssl, WOLFSSL_SNI_HOST_NAME,
            host_name, XSTRLEN(host_name));
    WOLFSSL_LEAVE("wolfSSL_set_tlsext_host_name", ret);
    return ret;
}


#ifndef NO_WOLFSSL_SERVER
const char * wolfSSL_get_servername(WOLFSSL* ssl, byte type)
{
    void * serverName = NULL;
    if (ssl == NULL)
        return NULL;
    TLSX_SNI_GetRequest(ssl->extensions, type, &serverName);
    return (const char *)serverName;
}
#endif /* NO_WOLFSSL_SERVER */
#endif /* HAVE_SNI */


WOLFSSL_CTX* wolfSSL_set_SSL_CTX(WOLFSSL* ssl, WOLFSSL_CTX* ctx)
{
    if (ssl && ctx && SetSSL_CTX(ssl, ctx) == SSL_SUCCESS)
        return ssl->ctx;
    return NULL;
}


VerifyCallback wolfSSL_CTX_get_verify_callback(WOLFSSL_CTX* ctx)
{
    WOLFSSL_ENTER("wolfSSL_CTX_get_verify_callback");
    if(ctx)
        return ctx->verifyCallback;
    return NULL;
}


void wolfSSL_CTX_set_servername_callback(WOLFSSL_CTX* ctx, CallbackSniRecv cb)
{
    WOLFSSL_ENTER("wolfSSL_CTX_set_servername_callback");
    if (ctx)
        ctx->sniRecvCb = cb;
}


void wolfSSL_CTX_set_servername_arg(WOLFSSL_CTX* ctx, void* arg)
{
    WOLFSSL_ENTER("wolfSSL_CTX_set_servername_arg");
    if (ctx)
        ctx->sniRecvCbArg = arg;
}


long wolfSSL_CTX_clear_options(WOLFSSL_CTX* ctx, long opt)
{
    WOLFSSL_ENTER("SSL_CTX_clear_options");
    WOLFSSL_STUB("SSL_CTX_clear_options");
    (void)ctx;
    (void)opt;
    return opt;
}

void wolfSSL_THREADID_set_callback(void(*threadid_func)(void*))
{
    WOLFSSL_ENTER("wolfSSL_THREADID_set_callback");
    WOLFSSL_STUB("wolfSSL_THREADID_set_callback");
    (void)threadid_func;
    return;
}

void wolfSSL_THREADID_set_numeric(void* id, unsigned long val)
{
    WOLFSSL_ENTER("wolfSSL_THREADID_set_numeric");
    WOLFSSL_STUB("wolfSSL_THREADID_set_numeric");
    (void)id;
    (void)val;
    return;
}


WOLFSSL_X509* wolfSSL_X509_STORE_get1_certs(WOLFSSL_X509_STORE_CTX* ctx,
                                                WOLFSSL_X509_NAME* name)
{
    WOLFSSL_ENTER("wolfSSL_X509_STORE_get1_certs");
    WOLFSSL_STUB("wolfSSL_X509_STORE_get1_certs");
    (void)ctx;
    (void)name;
    return NULL;
}

void wolfSSL_sk_X509_pop_free(STACK_OF(WOLFSSL_X509)* sk, void f (WOLFSSL_X509*)){
    (void) sk;
    (void) f;
    WOLFSSL_ENTER("wolfSSL_sk_X509_pop_free");
    WOLFSSL_STUB("wolfSSL_sk_X509_pop_free");
}

#endif /* OPENSSL_EXTRA and HAVE_STUNNEL */


#if (defined(OPENSSL_EXTRA) && defined(HAVE_STUNNEL)) \
    || defined(WOLFSSL_MYSQL_COMPATIBLE)
int wolfSSL_CTX_get_verify_mode(WOLFSSL_CTX* ctx)
{
    int mode = 0;
    WOLFSSL_ENTER("wolfSSL_CTX_get_verify_mode");

    if(!ctx)
        return SSL_FATAL_ERROR;

    if (ctx->verifyPeer)
        mode |= SSL_VERIFY_PEER;
    else if (ctx->verifyNone)
        mode |= SSL_VERIFY_NONE;

    if (ctx->failNoCert)
        mode |= SSL_VERIFY_FAIL_IF_NO_PEER_CERT;

    if (ctx->failNoCertxPSK)
        mode |= SSL_VERIFY_FAIL_EXCEPT_PSK;

    WOLFSSL_LEAVE("wolfSSL_CTX_get_verify_mode", mode);
    return mode;
}
#endif

#if defined(OPENSSL_EXTRA) && defined(HAVE_CURVE25519)
/* return 1 if success, 0 if error
 * output keys are little endian format
 */
int wolfSSL_EC25519_generate_key(unsigned char *priv, unsigned int *privSz,
                                 unsigned char *pub, unsigned int *pubSz)
{
#ifndef WOLFSSL_KEY_GEN
    WOLFSSL_MSG("No Key Gen built in");
    (void) priv;
    (void) privSz;
    (void) pub;
    (void) pubSz;
    return SSL_FAILURE;
#else /* WOLFSSL_KEY_GEN */
    int ret = SSL_FAILURE;
    int initTmpRng = 0;
    WC_RNG *rng = NULL;
#ifdef WOLFSSL_SMALL_STACK
    WC_RNG *tmpRNG = NULL;
#else
    WC_RNG tmpRNG[1];
#endif

    WOLFSSL_ENTER("wolfSSL_EC25519_generate_key");

    if (priv == NULL || privSz == NULL || *privSz < CURVE25519_KEYSIZE ||
        pub == NULL || pubSz == NULL || *pubSz < CURVE25519_KEYSIZE) {
        WOLFSSL_MSG("Bad arguments");
        return SSL_FAILURE;
    }

#ifdef WOLFSSL_SMALL_STACK
    tmpRNG = (WC_RNG*)XMALLOC(sizeof(WC_RNG), NULL, DYNAMIC_TYPE_TMP_BUFFER);
    if (tmpRNG == NULL)
        return SSL_FAILURE;
#endif
    if (wc_InitRng(tmpRNG) == 0) {
        rng = tmpRNG;
        initTmpRng = 1;
    }
    else {
        WOLFSSL_MSG("Bad RNG Init, trying global");
        if (initGlobalRNG == 0)
            WOLFSSL_MSG("Global RNG no Init");
        else
            rng = &globalRNG;
    }

    if (rng) {
        curve25519_key key;

        if (wc_curve25519_init(&key) != MP_OKAY)
            WOLFSSL_MSG("wc_curve25519_init failed");
        else if (wc_curve25519_make_key(rng, CURVE25519_KEYSIZE, &key)!=MP_OKAY)
            WOLFSSL_MSG("wc_curve25519_make_key failed");
        /* export key pair */
        else if (wc_curve25519_export_key_raw_ex(&key, priv, privSz, pub,
                                                 pubSz, EC25519_LITTLE_ENDIAN)
                 != MP_OKAY)
            WOLFSSL_MSG("wc_curve25519_export_key_raw_ex failed");
        else
            ret = SSL_SUCCESS;

        wc_curve25519_free(&key);
    }

    if (initTmpRng)
        wc_FreeRng(tmpRNG);

#ifdef WOLFSSL_SMALL_STACK
    XFREE(tmpRNG, NULL, DYNAMIC_TYPE_TMP_BUFFER);
#endif

    return ret;
#endif /* WOLFSSL_KEY_GEN */
}

/* return 1 if success, 0 if error
 * input and output keys are little endian format
 */
int wolfSSL_EC25519_shared_key(unsigned char *shared, unsigned int *sharedSz,
                               const unsigned char *priv, unsigned int privSz,
                               const unsigned char *pub, unsigned int pubSz)
{
#ifndef WOLFSSL_KEY_GEN
    WOLFSSL_MSG("No Key Gen built in");
    (void) shared;
    (void) sharedSz;
    (void) priv;
    (void) privSz;
    (void) pub;
    (void) pubSz;
    return SSL_FAILURE;
#else /* WOLFSSL_KEY_GEN */
    int ret = SSL_FAILURE;
    curve25519_key privkey, pubkey;

    WOLFSSL_ENTER("wolfSSL_EC25519_shared_key");

    if (shared == NULL || sharedSz == NULL || *sharedSz < CURVE25519_KEYSIZE ||
        priv == NULL || privSz < CURVE25519_KEYSIZE ||
        pub == NULL || pubSz < CURVE25519_KEYSIZE) {
        WOLFSSL_MSG("Bad arguments");
        return SSL_FAILURE;
    }

    /* import private key */
    if (wc_curve25519_init(&privkey) != MP_OKAY) {
        WOLFSSL_MSG("wc_curve25519_init privkey failed");
        return ret;
    }
    if (wc_curve25519_import_private_ex(priv, privSz, &privkey,
                                        EC25519_LITTLE_ENDIAN) != MP_OKAY) {
        WOLFSSL_MSG("wc_curve25519_import_private_ex failed");
        wc_curve25519_free(&privkey);
        return ret;
    }

    /* import public key */
    if (wc_curve25519_init(&pubkey) != MP_OKAY) {
        WOLFSSL_MSG("wc_curve25519_init pubkey failed");
        wc_curve25519_free(&privkey);
        return ret;
    }
    if (wc_curve25519_import_public_ex(pub, pubSz, &pubkey,
                                       EC25519_LITTLE_ENDIAN) != MP_OKAY) {
        WOLFSSL_MSG("wc_curve25519_import_public_ex failed");
        wc_curve25519_free(&privkey);
        wc_curve25519_free(&pubkey);
        return ret;
    }

    if (wc_curve25519_shared_secret_ex(&privkey, &pubkey,
                                       shared, sharedSz,
                                       EC25519_LITTLE_ENDIAN) != MP_OKAY)
        WOLFSSL_MSG("wc_curve25519_shared_secret_ex failed");
    else
        ret = SSL_SUCCESS;

    wc_curve25519_free(&privkey);
    wc_curve25519_free(&pubkey);

    return ret;
#endif /* WOLFSSL_KEY_GEN */
}
#endif /* OPENSSL_EXTRA && HAVE_CURVE25519 */

#if defined(OPENSSL_EXTRA) && defined(HAVE_ED25519)
/* return 1 if success, 0 if error
 * output keys are little endian format
 */
int wolfSSL_ED25519_generate_key(unsigned char *priv, unsigned int *privSz,
                                 unsigned char *pub, unsigned int *pubSz)
{
#ifndef WOLFSSL_KEY_GEN
    WOLFSSL_MSG("No Key Gen built in");
    (void) priv;
    (void) privSz;
    (void) pub;
    (void) pubSz;
    return SSL_FAILURE;
#else /* WOLFSSL_KEY_GEN */
    int ret = SSL_FAILURE;
    int initTmpRng = 0;
    WC_RNG *rng = NULL;
#ifdef WOLFSSL_SMALL_STACK
    WC_RNG *tmpRNG = NULL;
#else
    WC_RNG tmpRNG[1];
#endif

    WOLFSSL_ENTER("wolfSSL_ED25519_generate_key");

    if (priv == NULL || privSz == NULL || *privSz < ED25519_PRV_KEY_SIZE ||
        pub == NULL || pubSz == NULL || *pubSz < ED25519_PUB_KEY_SIZE) {
        WOLFSSL_MSG("Bad arguments");
        return SSL_FAILURE;
    }

#ifdef WOLFSSL_SMALL_STACK
    tmpRNG = (WC_RNG*)XMALLOC(sizeof(WC_RNG), NULL, DYNAMIC_TYPE_TMP_BUFFER);
    if (tmpRNG == NULL)
        return SSL_FATAL_ERROR;
#endif
    if (wc_InitRng(tmpRNG) == 0) {
        rng = tmpRNG;
        initTmpRng = 1;
    }
    else {
        WOLFSSL_MSG("Bad RNG Init, trying global");
        if (initGlobalRNG == 0)
            WOLFSSL_MSG("Global RNG no Init");
        else
            rng = &globalRNG;
    }

    if (rng) {
        ed25519_key key;

        if (wc_ed25519_init(&key) != MP_OKAY)
            WOLFSSL_MSG("wc_ed25519_init failed");
        else if (wc_ed25519_make_key(rng, ED25519_KEY_SIZE, &key)!=MP_OKAY)
            WOLFSSL_MSG("wc_ed25519_make_key failed");
        /* export private key */
        else if (wc_ed25519_export_key(&key, priv, privSz, pub, pubSz)!=MP_OKAY)
            WOLFSSL_MSG("wc_ed25519_export_key failed");
        else
            ret = SSL_SUCCESS;

        wc_ed25519_free(&key);
    }

    if (initTmpRng)
        wc_FreeRng(tmpRNG);

#ifdef WOLFSSL_SMALL_STACK
    XFREE(tmpRNG, NULL, DYNAMIC_TYPE_TMP_BUFFER);
#endif

    return ret;
#endif /* WOLFSSL_KEY_GEN */
}

/* return 1 if success, 0 if error
 * input and output keys are little endian format
 * priv is a buffer containing private and public part of key
 */
int wolfSSL_ED25519_sign(const unsigned char *msg, unsigned int msgSz,
                         const unsigned char *priv, unsigned int privSz,
                         unsigned char *sig, unsigned int *sigSz)
{
#ifndef WOLFSSL_KEY_GEN
    WOLFSSL_MSG("No Key Gen built in");
    (void) msg;
    (void) msgSz;
    (void) priv;
    (void) privSz;
    (void) sig;
    (void) sigSz;
    return SSL_FAILURE;
#else /* WOLFSSL_KEY_GEN */
    ed25519_key key;
    int ret = SSL_FAILURE;

    WOLFSSL_ENTER("wolfSSL_ED25519_sign");

    if (priv == NULL || privSz != ED25519_PRV_KEY_SIZE ||
        msg == NULL || sig == NULL || *sigSz < ED25519_SIG_SIZE) {
        WOLFSSL_MSG("Bad arguments");
        return SSL_FAILURE;
    }

    /* import key */
    if (wc_ed25519_init(&key) != MP_OKAY) {
        WOLFSSL_MSG("wc_curve25519_init failed");
        return ret;
    }
    if (wc_ed25519_import_private_key(priv, privSz/2,
                                      priv+(privSz/2), ED25519_PUB_KEY_SIZE,
                                      &key) != MP_OKAY){
        WOLFSSL_MSG("wc_ed25519_import_private failed");
        wc_ed25519_free(&key);
        return ret;
    }

    if (wc_ed25519_sign_msg(msg, msgSz, sig, sigSz, &key) != MP_OKAY)
        WOLFSSL_MSG("wc_curve25519_shared_secret_ex failed");
    else
        ret = SSL_SUCCESS;

    wc_ed25519_free(&key);

    return ret;
#endif /* WOLFSSL_KEY_GEN */
}

/* return 1 if success, 0 if error
 * input and output keys are little endian format
 * pub is a buffer containing public part of key
 */
int wolfSSL_ED25519_verify(const unsigned char *msg, unsigned int msgSz,
                           const unsigned char *pub, unsigned int pubSz,
                           const unsigned char *sig, unsigned int sigSz)
{
#ifndef WOLFSSL_KEY_GEN
    WOLFSSL_MSG("No Key Gen built in");
    (void) msg;
    (void) msgSz;
    (void) pub;
    (void) pubSz;
    (void) sig;
    (void) sigSz;
    return SSL_FAILURE;
#else /* WOLFSSL_KEY_GEN */
    ed25519_key key;
    int ret = SSL_FAILURE, check = 0;

    WOLFSSL_ENTER("wolfSSL_ED25519_verify");

    if (pub == NULL || pubSz != ED25519_PUB_KEY_SIZE ||
        msg == NULL || sig == NULL || sigSz != ED25519_SIG_SIZE) {
        WOLFSSL_MSG("Bad arguments");
        return SSL_FAILURE;
    }

    /* import key */
    if (wc_ed25519_init(&key) != MP_OKAY) {
        WOLFSSL_MSG("wc_curve25519_init failed");
        return ret;
    }
    if (wc_ed25519_import_public(pub, pubSz, &key) != MP_OKAY){
        WOLFSSL_MSG("wc_ed25519_import_public failed");
        wc_ed25519_free(&key);
        return ret;
    }

    if ((ret = wc_ed25519_verify_msg((byte*)sig, sigSz, msg, msgSz,
                                     &check, &key)) != MP_OKAY) {
        WOLFSSL_MSG("wc_ed25519_verify_msg failed");
    }
    else if (!check)
        WOLFSSL_MSG("wc_ed25519_verify_msg failed (signature invalid)");
    else
        ret = SSL_SUCCESS;

    wc_ed25519_free(&key);

    return ret;
#endif /* WOLFSSL_KEY_GEN */
}

#endif /* OPENSSL_EXTRA && HAVE_ED25519 */

#ifdef WOLFSSL_JNI

int wolfSSL_set_jobject(WOLFSSL* ssl, void* objPtr)
{
    WOLFSSL_ENTER("wolfSSL_set_jobject");
    if (ssl != NULL)
    {
        ssl->jObjectRef = objPtr;
        return SSL_SUCCESS;
    }
    return SSL_FAILURE;
}

void* wolfSSL_get_jobject(WOLFSSL* ssl)
{
    WOLFSSL_ENTER("wolfSSL_get_jobject");
    if (ssl != NULL)
        return ssl->jObjectRef;
    return NULL;
}

#endif /* WOLFSSL_JNI */


#ifdef WOLFSSL_ASYNC_CRYPT
int wolfSSL_CTX_AsyncPoll(WOLFSSL_CTX* ctx, WOLF_EVENT** events, int maxEvents,
    WOLF_EVENT_FLAG flags, int* eventCount)
{
    if (ctx == NULL) {
        return BAD_FUNC_ARG;
    }

    return wolfAsync_EventQueuePoll(&ctx->event_queue, NULL,
                                        events, maxEvents, flags, eventCount);
}

int wolfSSL_AsyncPoll(WOLFSSL* ssl, WOLF_EVENT_FLAG flags)
{
    int ret, eventCount = 0;
    WOLF_EVENT* events[1];

    if (ssl == NULL) {
        return BAD_FUNC_ARG;
    }

    /* not filtering on "ssl", since its the asyncDev */
    ret = wolfAsync_EventQueuePoll(&ssl->ctx->event_queue, NULL,
        events, sizeof(events)/sizeof(events), flags, &eventCount);
    if (ret == 0 && eventCount > 0) {
        ret = 1; /* Success */
    }

    return ret;
}
#endif /* WOLFSSL_ASYNC_CRYPT */


#ifdef OPENSSL_EXTRA
int wolfSSL_CTX_set_msg_callback(WOLFSSL_CTX *ctx, SSL_Msg_Cb cb)
{
    WOLFSSL_STUB("SSL_CTX_set_msg_callback");
    (void)ctx;
    (void)cb;
    return SSL_FAILURE;
}
int wolfSSL_set_msg_callback(WOLFSSL *ssl, SSL_Msg_Cb cb)
{
    WOLFSSL_STUB("SSL_set_msg_callback");
    (void)ssl;
    (void)cb;
    return SSL_FAILURE;
}
int wolfSSL_CTX_set_msg_callback_arg(WOLFSSL_CTX *ctx, void* arg)
{
    WOLFSSL_STUB("SSL_CTX_set_msg_callback_arg");
    (void)ctx;
    (void)arg;
    return SSL_FAILURE;
}
int wolfSSL_set_msg_callback_arg(WOLFSSL *ssl, void* arg)
{
    WOLFSSL_STUB("SSL_set_msg_callback_arg");
    (void)ssl;
    (void)arg;
    return SSL_FAILURE;
}
#endif

#endif /* WOLFCRYPT_ONLY */
