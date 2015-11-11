//
//  YMTLSProvider.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMTLSProvider.h"
#include "YMSecurityProviderVeryPriv.h"

#include "YMUtilities.h"
#include "YMThread.h"
#include "YMLock.h"

#include <sys/socket.h>
#include <sys/stat.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <pthread.h>

#include "YMLog.h"
#undef ymlog_type
#define ymlog_type YMLogSecurity
#if ( ymlog_type > ymlog_target )
#undef ymlog
#define ymlog(x,...) ;
#endif

#pragma message "danger! really time to convert to unions.."
typedef struct __YMTLSProvider
{
    YMTypeID _type;
    
    int readFile;
    int writeFile;
    ym_security_init_func   initFunc;
    ym_security_read_func   readFunc;
    ym_security_write_func  writeFunc;
    ym_security_close_func  closeFunc;
    
    bool isServer;
    bool isWrappingSocket;
    SSL *ssl;
    SSL_CTX *sslCtx;
    BIO *bio;
    
    ym_tls_provider_get_certs localCertsFunc;
    void *localCertsContext;
    ym_tls_provider_should_accept peerCertsFunc;
    void *peerCertsContext;
} _YMTLSProvider;

bool __YMTLSProviderInit(YMSecurityProviderRef provider);
bool __YMTLSProviderRead(YMSecurityProviderRef provider, uint8_t *buffer, size_t bytes);
bool __YMTLSProviderWrite(YMSecurityProviderRef provider, const uint8_t *buffer, size_t bytes);
bool __YMTLSProviderClose(YMSecurityProviderRef provider);

unsigned long ym_tls_thread_id_callback()
{
    ymlog("ym_tls_thread_id_callback");
    return (unsigned long)pthread_self();
}

YMLockRef gYMTLSLock = NULL;

void ym_tls_lock_callback(int mode, int type, __unused char *file, __unused int line)
{
    bool lock = (mode&CRYPTO_LOCK);
    bool unlock = (mode&CRYPTO_UNLOCK);
    ymlog("ym_tls_lock_callback: %04x %s %d(l%d u%d)",type,file,line,lock,unlock);
    if ( lock )
        YMLockLock(gYMTLSLock);
    else if ( unlock )
        YMLockUnlock(gYMTLSLock);
}

static pthread_once_t gYMInitSSLOnce = PTHREAD_ONCE_INIT;
void __YMSSLInit()
{
    SSL_load_error_strings();
    // ``SSL_library_init() always returns "1", so it is safe to discard the return value.''
    SSL_library_init();
    OpenSSL_add_all_algorithms();
    
    gYMTLSLock = YMLockCreateWithOptionsAndName(YMLockDefault, YM_TOKEN_STR(gYMTLSLock));
    
    CRYPTO_set_id_callback((unsigned long (*)())ym_tls_thread_id_callback);
    CRYPTO_set_locking_callback((void (*)())ym_tls_lock_callback);
}

// designated initializer
YMTLSProviderRef __YMTLSProviderCreateWithFullDuplexFile(int file, bool isWrappingSocket, bool isServer);

YMTLSProviderRef YMTLSProviderCreate(int inFile, int outFile, bool isServer)
{
    int sock;
    if ( inFile == outFile )
        sock = inFile;
    else
    {
        // todo? "raw" is only available to superuser, says the man page. protocol boxing not necessary here
        // even if this is currently only for the test case.
        sock = socket(PF_LOCAL, SOCK_STREAM, 0/* IP, /etc/sockets man 5 protocols*/);
        if ( sock == -1 )
        {
            ymerr("tls[%d]: failed to create socket for forwarding %d->%d: %d (%s)",isServer,inFile,outFile,errno,strerror(errno));
            return NULL;
        }
        
        bool okay = YMThreadDispatchForwardFile(outFile, sock);
        if ( ! okay )
        {
            ymerr("tls[%d]: dispatch forward file failed %d->%d",isServer,inFile,outFile);
            return NULL;
        }
    }
    
    YMTLSProviderRef tls = __YMTLSProviderCreateWithFullDuplexFile(sock,true, isServer);
    if ( ! tls )
        close(sock);
    
    return tls;
}

// designated initializer with shorter arguments list! am i doing it wrong?
YMTLSProviderRef YMTLSProviderCreateWithFullDuplexFile(int file, bool isServer)
{
    return __YMTLSProviderCreateWithFullDuplexFile(file, false, isServer);
}

YMTLSProviderRef __YMTLSProviderCreateWithFullDuplexFile(int file, bool isWrappingSocket, bool isServer)
{
    pthread_once(&gYMInitSSLOnce, __YMSSLInit);
    
    struct stat statbuf;
    fstat(file, &statbuf);
    if ( ! S_ISSOCK(statbuf.st_mode) )
    {
        ymerr("tls[%d]: error: file %d is not a socket",isServer,file);
        return NULL;
    }
    
    YMTLSProviderRef tls = (YMTLSProviderRef)YMALLOC(sizeof(struct __YMTLSProvider));
    tls->_type = _YMTLSProviderTypeID;
    
    tls->readFile = file;
    tls->writeFile = file;
    tls->isWrappingSocket = isWrappingSocket;
    tls->isServer = isServer;
    
    tls->localCertsFunc = NULL;
    tls->localCertsContext = NULL;
    tls->peerCertsFunc = NULL;
    tls->peerCertsContext = NULL;
    
    tls->initFunc = __YMTLSProviderInit;
    tls->readFunc = __YMTLSProviderRead;
    tls->writeFunc = __YMTLSProviderWrite;
    tls->closeFunc = __YMTLSProviderClose;
    
    return tls;
}

void YMTLSProviderSetLocalCertsFunc(YMTLSProviderRef tls, ym_tls_provider_get_certs func, void *context)
{
    tls->localCertsFunc = func;
    tls->localCertsContext = context;
}

void YMTLSProviderSetAcceptPeerCertsFunc(YMTLSProviderRef tls, ym_tls_provider_should_accept func, void *context)
{
    tls->peerCertsFunc = func;
    tls->peerCertsContext = context;
}

void _YMTLSProviderFree(YMTypeRef object)
{
    YMTLSProviderRef tls = (YMTLSProviderRef)object;
    if ( tls->isWrappingSocket )
        close(tls->readFile);
    if ( tls->bio )
        BIO_free(tls->bio);
    if ( tls->ssl )
        SSL_free(tls->ssl);
    if ( tls->sslCtx )
        SSL_CTX_free(tls->sslCtx);
    free(tls);
#pragma message "BIG TODO - we can't fire and forget a forwarding thread, need a proper struct and flags such as a 'finished' callout (and maybe what to do with the output files when done)"
}

int ym_tls_write(BIO *bio, const char *buffer, int length)
{
    ymlog("ym_tls_write: %p %p %d",bio,buffer,length);
    YMTLSProviderRef tls = (YMTLSProviderRef)bio->ptr;
    YMIOResult result = YMWriteFull(tls->writeFile, (const unsigned char *)buffer, length);
    if ( result != YMIOSuccess )
        return -1;
    return length;
}
int ym_tls_read(BIO *bio, char *buffer, int length)
{
    ymlog("ym_tls_read: %p %p %d",bio,buffer,length);
    YMTLSProviderRef tls = (YMTLSProviderRef)bio->ptr;
    YMIOResult result = YMReadFull(tls->readFile, (unsigned char *)buffer, length);
    if ( result == YMIOError )
        return -1;
    else if ( result == YMIOEOF )
        return 0; // right?
    return length;
}
int ym_tls_puts(BIO *bio, const char * buffer)
{
    ymlog("ym_tls_puts: %p %p %s",bio,buffer,buffer);
    return 0;
}

int ym_tls_gets(BIO *bio, char * buffer, int length)
{
    ymlog("ym_tls_gets: %p %p %d",bio,buffer,length);
    return 0;
}

long ym_tls_ctrl (BIO *bio, int one, long two, void *three) { ymlog("ym_tls_ctrl: %p %d %ld %p",bio,one,two,three); return 1; }
int ym_tls_new(BIO *bio) { ymlog("ym_tls_new: %p",bio); return 1; }
int ym_tls_free(BIO *bio) { ymlog("ym_tls_free: %p",bio); return 1; }
long ym_tls_callback_ctrl(BIO *bio, int one, bio_info_cb * info) { ymlog("ym_tls_callback_ctrl: %p %d %p",bio,one,info); return 1; }

static BIO_METHOD ym_bio_methods =
{
    BIO_TYPE_SOCKET,
    "socket",
    ym_tls_write,
    ym_tls_read,
    ym_tls_puts,
    ym_tls_gets, /* sock_gets, */
    ym_tls_ctrl,
    ym_tls_new,
    ym_tls_free,
    ym_tls_callback_ctrl,
};

bool __YMTLSProviderInit(YMSecurityProviderRef provider)
{
    YMTLSProviderRef tls = (YMTLSProviderRef)provider;
    unsigned long sslError = SSL_ERROR_NONE;
    bool initOkay = false;
    
    tls->sslCtx = SSL_CTX_new(tls->isServer ? SSLv23_server_method() : SSLv23_client_method ());    // `` Negotiate highest available
                                                                                                    //      SSL/TLS version ''
    if ( ! tls->sslCtx )
    {
        sslError = ERR_get_error(); // todo "latest" or "earliest"? i'd have thought "latest", but X509_/RSA_ man pages say to get_error, while SSL_ merely "check the stack structure"
        ymerr("tls[%d]: failed to allocate ssl context: ssl err: %lu (%s)",tls->isServer , sslError, ERR_error_string(sslError,NULL));
        goto catch_return;
    }
    
    SSL_CTX_set_options(tls->sslCtx, SSL_OP_SINGLE_DH_USE);
    
    tls->ssl = SSL_new(tls->sslCtx);
    if ( ! tls->ssl )
    {
        // ``check the error stack to find out the reason'' i don't know what this means
        sslError = ERR_get_error(); // todo "latest" or "earliest"? i'd have thought "latest", but X509_/RSA_ man pages say to get_error, while SSL_ merely "check the stack structure"
        ymerr("tls[%d]: failed to allocate ssl object: ssl err: %lu (%s)",tls->isServer , sslError, ERR_error_string(sslError,NULL));
        goto catch_return;
    }
    
    tls->bio = BIO_new(&ym_bio_methods);
    if ( ! tls->bio )
    {
        sslError = ERR_get_error();
        ymerr("tls[%d]: BIO_new failed: %lu (%s)",tls->isServer ,sslError,ERR_error_string(sslError, NULL));
        goto catch_return;
    }
    
    SSL_set_bio(tls->ssl, tls->bio, tls->bio);
    tls->bio->ptr = tls; // this is the elusive context pointer
    
    // todo: judging by higher level TLS libraries, there's probably a way to get called out to mid-accept
    // to do things like specify local certs and validate remote ones, but i'll be damned if i can find it in the ocean that is ssl.h.
    // it doesn't really matter if we do it within SSL_accept() or afterwards, though.
    int result = SSL_accept(tls->ssl);
    if ( SSL_ERROR_NONE != result )
    {
        sslError = ERR_get_error(); // todo "latest" or "earliest"? i'd have thought "latest", but X509_/RSA_ man pages say to get_error, while SSL_ merely "check the stack structure"
        ymerr("tls[%d]: SSL_accept failed: %d: ssl err: %lu (%s)",tls->isServer , result, sslError, ERR_error_string(sslError,NULL));
        goto catch_return;
    }
    
    ymlog("tls[%d]: SSL_accept returned %d",tls->isServer ,result);
    
    initOkay = true;
    
catch_return:
    
    if ( ! initOkay )
    {
        if ( tls->bio )
        {
            BIO_free(tls->bio);
            tls->bio = NULL;
        }
        if ( tls->ssl )
        {
            SSL_free(tls->ssl);
            tls->ssl = NULL;
        }
        if ( tls->sslCtx )
        {
            SSL_CTX_free(tls->sslCtx);
            tls->sslCtx = NULL;
        }
    }
    
    return initOkay;
}

#pragma message "i'm pretty sure all the 'call it twice' cases for these i/o wrappers are for non-blocking files, which we're (thank god) not using, but go back through"
bool __YMTLSProviderRead(YMSecurityProviderRef provider, uint8_t *buffer, size_t bytes)
{
    YMTLSProviderRef tls = (YMTLSProviderRef)provider;
    
    // loop or something? hate to change the whole prototype for this, and i like having length-y things be unsigned
    if ( bytes > INT32_MAX )
    {
        ymerr("tls[%d]: error: fix thy internal errors!",tls->isServer );
        abort();
    }
    
    int result = SSL_read(tls->ssl, buffer, (int)bytes);
    if ( result <= 0 )
    {
        unsigned long sslError = ERR_get_error();
        ymerr("tls[%d]: SSL_read failed: %d: ssl err: %lu (%s)",tls->isServer ,result,sslError,ERR_error_string(sslError, NULL));
        return false;
    }
    return true;
    
}

bool __YMTLSProviderWrite(YMSecurityProviderRef provider, const uint8_t *buffer, size_t bytes)
{
    YMTLSProviderRef tls = (YMTLSProviderRef)provider;
    
    if ( bytes > INT32_MAX )
    {
        ymerr("tls[%d]: error: fix thy internal errors!",tls->isServer );
        abort();
    }
    
    int result = SSL_write(tls->ssl, buffer, (int)bytes);
    if ( result <= 0 )
    {
        unsigned long sslError = ERR_get_error();
        ymerr("tls[%d]: SSL_write failed: %d: ssl err: %lu (%s)",tls->isServer ,result,sslError,ERR_error_string(sslError, NULL));
        return false;
    }
    
    return true;
}

bool __YMTLSProviderClose(YMSecurityProviderRef provider)
{
    YMTLSProviderRef tls = (YMTLSProviderRef)provider;
    
    int result = SSL_shutdown(tls->ssl);
    if ( result )
    {
        unsigned long sslError = ERR_get_error();
        ymerr("tls[%d]: SSL_shutdown failed: %d: ssl err: %lu (%s)",tls->isServer ,result,sslError,ERR_error_string(sslError, NULL));
        return false;
    }
    
    return true;
}
