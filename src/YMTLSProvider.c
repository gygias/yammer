//
//  YMTLSProvider.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMTLSProvider.h"

#include "YMOpenssl.h"
#include "YMSecurityProviderInternal.h"
#include "YMX509CertificatePriv.h"

#include "YMUtilities.h"
#include "YMThread.h"
#include "YMThreadPriv.h"
#include "YMDictionary.h"
#include "YMLock.h"
#include "YMUtilities.h"

#include <sys/stat.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#if !defined(YMWIN32)
# include <pthread.h>
#endif

#define ymlog_type YMLogSecurity
#include "YMLog.h"

YM_EXTERN_C_PUSH

static YMLockRef *gYMTLSLocks = NULL;
//static YMLockRef gYMTLSLocks[CRYPTO_NUM_LOCKS*sizeof(YMLockRef)];
void __ym_tls_lock_callback(int mode, int type, const char *file, int line);

#define YMTLSProviderVerifyDepth 0

// this shouldn't be a global (or at least a single value per role), but the get_ex_new_index
// functions seem to return a globally unique idx for all objects (or at least all objects
// within a particular namespace like SSL_ or RSA_. there's no way to unbox "this" without
// knowing the idx, so we can't store it in our object. technically need some kind of global
// list-of-objects to their ex_data idx? :/
//static int gYMTLSProviderServerExDataIdxLast = -1;
//static int gYMTLSProviderClientExDataIdxLast = -1;
static YMDictionaryRef gYMTLSExDataList; // maps ssl, which we can get without knowing the 'index', to its index...
static YMLockRef gYMTLSExDataLock;

typedef struct __ym_tls_provider_t
{
    struct __ym_security_provider_t _common;
    
    bool isServer;
    bool isWrappingSocket;
    bool usingGeneratedCert; // todo client-supplied
    bool preverified;
    SSL_CTX *sslCtx;
    //int sslCtxExDataIdx; // if only!
    SSL *ssl;
    BIO *bio;
    
    YMX509CertificateRef localCertificate;
    YMX509CertificateRef peerCertificate;
    
    ym_tls_provider_get_certs localCertsFunc;
    void *localCertsContext;
    ym_tls_provider_should_accept peerCertsFunc;
    void *peerCertsContext;
} __ym_tls_provider_t;
typedef struct __ym_tls_provider_t *__YMTLSProviderRef;

bool __YMTLSProviderInit(__YMSecurityProviderRef provider);
bool __YMTLSProviderRead(__YMSecurityProviderRef provider, uint8_t *buffer, size_t bytes);
bool __YMTLSProviderWrite(__YMSecurityProviderRef provider, const uint8_t *buffer, size_t bytes);
bool __YMTLSProviderClose(__YMSecurityProviderRef provider);

void ym_tls_thread_id_callback(CRYPTO_THREADID *threadId)
{
    //ymlog("ym_tls_thread_id_callback");
    CRYPTO_THREADID_set_numeric(threadId, (unsigned long)_YMThreadGetCurrentThreadNumber());
}

YM_ONCE_FUNC(__YMTLSInit,
{
	SSL_load_error_strings();
	// ``SSL_library_init() always returns "1", so it is safe to discard the return value.''
	SSL_library_init();
	OpenSSL_add_all_algorithms();

	gYMTLSLocks = calloc(CRYPTO_num_locks(),sizeof(YMLockRef));

	CRYPTO_THREADID_set_callback(ym_tls_thread_id_callback);
	CRYPTO_set_locking_callback(__ym_tls_lock_callback);

	gYMTLSExDataList = YMDictionaryCreate();
	gYMTLSExDataLock = YMLockCreate(YMInternalLockType);
})

// designated initializer
YMTLSProviderRef __YMTLSProviderCreateWithSocket(YMSOCKET socket, bool isWrappingSocket, bool isServer);

YMTLSProviderRef YMTLSProviderCreateWithSocket(YMSOCKET socket, bool isServer)
{
    return __YMTLSProviderCreateWithSocket(socket, false, isServer);
}

YM_ONCE_OBJ gYMInitTLSOnce = YM_ONCE_INIT;

YMTLSProviderRef __YMTLSProviderCreateWithSocket(YMSOCKET socket, bool isWrappingSocket, bool isServer)
{
	YM_ONCE_DO(gYMInitTLSOnce,__YMTLSInit);
    
#if !defined(YMWIN32)
    struct stat statbuf;
    fstat(socket, &statbuf);
    if ( ! S_ISSOCK(statbuf.st_mode) ) {
        ymerr("tls[%d]: error: file f%d is not a socket",isServer,socket);
        return NULL;
    }
#endif
    
    __YMTLSProviderRef tls = (__YMTLSProviderRef)_YMAlloc(_YMTLSProviderTypeID,sizeof(struct __ym_tls_provider_t));
    
    tls->_common.socket = socket;
    tls->isWrappingSocket = isWrappingSocket;
    tls->isServer = isServer;
    tls->usingGeneratedCert = false;
    tls->preverified = false;
    
    tls->localCertificate = NULL;
    tls->peerCertificate = NULL;
    
    tls->_common.initFunc = __YMTLSProviderInit;
    tls->_common.readFunc = __YMTLSProviderRead;
    tls->_common.writeFunc = __YMTLSProviderWrite;
    tls->_common.closeFunc = __YMTLSProviderClose;
    
    tls->localCertsFunc = NULL;
    tls->localCertsContext = NULL;
    tls->peerCertsFunc = NULL;
    tls->peerCertsContext = NULL;
    
    tls->sslCtx = NULL;
    //tls->sslCtxExDataIdx = -1;
    tls->ssl = NULL;
    tls->bio = NULL;
    
    return tls;
}

void YMTLSProviderSetLocalCertsFunc(YMTLSProviderRef tls_, ym_tls_provider_get_certs func, void *context)
{
    __YMTLSProviderRef tls = (__YMTLSProviderRef)tls_;
    tls->localCertsFunc = func;
    tls->localCertsContext = context;
}

void YMTLSProviderSetAcceptPeerCertsFunc(YMTLSProviderRef tls_, ym_tls_provider_should_accept func, void *context)
{
    __YMTLSProviderRef tls = (__YMTLSProviderRef)tls_;
    tls->peerCertsFunc = func;
    tls->peerCertsContext = context;
}

void _YMTLSProviderFree(YMTypeRef object)
{
    __YMTLSProviderRef tls = (__YMTLSProviderRef)object;
    
    YMLockLock(gYMTLSExDataLock);
    if ( YMDictionaryContains(gYMTLSExDataList, (YMDictionaryKey)tls->ssl) )
        YMDictionaryRemove(gYMTLSExDataList, (YMDictionaryKey)tls->ssl);
    YMLockUnlock(gYMTLSExDataLock);
    
    if ( tls->localCertificate )
        YMRelease(tls->localCertificate);
    if ( tls->peerCertificate )
        YMRelease(tls->peerCertificate);
    if ( tls->ssl )
        SSL_free(tls->ssl);
    if ( tls->sslCtx )
        SSL_CTX_free(tls->sslCtx);
}

void YMTLSProviderFreeGlobals()
{
//    if ( gYMTLSExDataList ) {
//        YMRelease(gYMTLSExDataList);
//        gYMTLSExDataList = NULL;
//    }
////    if ( gYMTLSExDataLock ) {
////        YMRelease(gYMTLSExDataLock);
////        gYMTLSExDataLock = NULL;
////    }
//    
//    if ( gYMTLSLocks ) {
//        for( int i = 0; i < CRYPTO_num_locks(); i++ ) {
//            YMLockRef aLock = gYMTLSLocks[i];
//            if ( aLock )
//                YMRelease(aLock);
//        }
//        free((void *)gYMTLSLocks);
//        gYMTLSLocks = NULL;
//    }
//    
//    
//	CRYPTO_THREADID_set_callback(NULL);
//    CRYPTO_set_locking_callback(NULL);
//    
//	YM_ONCE_OBJ onceAgain = YM_ONCE_INIT;
//	memcpy(&gYMInitTLSOnce, &onceAgain, sizeof(onceAgain));
}

void __ym_tls_lock_callback(int mode, int type, __unused const char *file, __unused int line)
{
    bool lock = mode & CRYPTO_LOCK;
    //ymlog("__ym_tls_lock_callback: %04x %s:%d #%d (%s)",mode,file,line,type,lock?"lock":"unlock");
    
    YMLockRef theLock = gYMTLSLocks[type];
    if ( ! theLock ) {
        YMStringRef name = YMStringCreateWithFormat("ym_tls_lock_callback-%d",type, NULL);
        theLock = YMLockCreateWithOptionsAndName(YMLockNone, name);
        YMRelease(name);
        
        gYMTLSLocks[type] = theLock;
    }
    
    // locking_function() must be able to handle up to CRYPTO_num_locks() different mutex locks. It sets the n-th lock if mode
    // & CRYPTO_LOCK, and releases it otherwise.
    if (lock)
        YMLockLock(theLock);
    else
        YMLockUnlock(theLock);
}

static inline void __ym_tls_info_callback(const char *role, const SSL *ssl, int type, int val)
{
    const char *desc = "";
    
    switch(type) {
        case SSL_CB_HANDSHAKE_START:
            desc = "start handshake";
            break;
        case SSL_CB_HANDSHAKE_DONE:
            desc = "end handshake";
            break;
        case SSL_CB_READ:
            desc = "read";
            break;
        case SSL_CB_READ_ALERT:
            desc = "read alert";
            break;
        case SSL_CB_WRITE:
            desc = "write";
            break;
        case SSL_CB_WRITE_ALERT:
            desc = "write alert";
            break;
        case SSL_CB_ALERT:
            desc = "alert";
            break;
        case SSL_CB_ACCEPT_LOOP:
            desc = "accept loop";
            break;
        case SSL_CB_CONNECT_LOOP:
            desc = "connect loop";
            break;
        case SSL_CB_ACCEPT_EXIT:
            desc = "accept exit";
            break;
        case SSL_CB_CONNECT_EXIT:
            desc = "connect exit";
            break;
        default:
            desc = "*";
            break;
    }
    ymlog("__ym_tls_lock_callback: %s: %p: %s %d",role,ssl,desc,val);
}

void __ym_tls_info_server_callback(const SSL *ssl, int type, int val)
{
    __ym_tls_info_callback("server", ssl, type, val);
}
void __ym_tls_info_client_callback(const SSL *ssl, int type, int val)
{
    __ym_tls_info_callback("client", ssl, type, val);
}

int __ym_tls_certificate_verify_callback(int preverify_ok, X509_STORE_CTX *x509_store_ctx)
{
    __YMTLSProviderRef tls;
    YMX509CertificateRef ymCert = NULL;
    SSL *ssl = X509_STORE_CTX_get_ex_data(x509_store_ctx, SSL_get_ex_data_X509_STORE_CTX_idx());
    //int state = SSL_get_state(ssl);
    //bool isServer = ( SSL_in_accept_init(ssl) );
    
    YMLockLock(gYMTLSExDataLock);
        int myIdx = (int)YMDictionaryGetItem(gYMTLSExDataList, (YMDictionaryKey)ssl);
    YMLockUnlock(gYMTLSExDataLock);
    
    tls = SSL_get_ex_data(ssl, myIdx);
    ymlog("tls[%d]: __ym_tls_certificate_verify_callback[%d]: %d",tls->isServer,myIdx,preverify_ok);
    
    X509 *err_cert = X509_STORE_CTX_get_current_cert(x509_store_ctx);
    int err = X509_STORE_CTX_get_error(x509_store_ctx);
    int depth = X509_STORE_CTX_get_error_depth(x509_store_ctx);
    
    if ( tls->preverified && preverify_ok )
        return 1;
    
    if ( depth > YMTLSProviderVerifyDepth ) {
        ymerr("tls[%d]: verify error: depth > %d",tls->isServer,YMTLSProviderVerifyDepth);
        return 0;
    } else if ( ! preverify_ok ) {
        if ( ! tls->isServer && tls->usingGeneratedCert && ( err != X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT ) ) {
            ymerr("tls[%d]: verify error: preverify: %d (%s)",tls->isServer,err,X509_verify_cert_error_string(err));
            return 0;
        }
    }
    
    ymCert = _YMX509CertificateCreateWithX509(err_cert, true);
    
    if ( tls->peerCertsFunc ) {
        bool okay = tls->peerCertsFunc(tls, &ymCert, 1, tls->peerCertsContext);
        
        if ( ! okay ) {
            ymerr("tls[%d]: verify error: client rejected peer cert",tls->isServer);
            YMRelease(ymCert);
            return 0;
        }
    }
    else
        ymlog("tls[%d]: user does not do validation, accepting self-signed certificate",tls->isServer);
    
    tls->peerCertificate = ymCert;
    tls->preverified = true;
    
    return 1;
}

void __YMTLSProviderInitSslCtx(__YMTLSProviderRef tls)
{
    int result;
    unsigned long sslError = SSL_ERROR_NONE;
    YMRSAKeyPairRef rsa = NULL;
    YMX509CertificateRef cert = NULL;

    tls->sslCtx = SSL_CTX_new(tls->isServer ? SSLv23_server_method() : SSLv23_client_method());    // `` Negotiate highest available
    //      SSL/TLS version ''
    if ( ! tls->sslCtx ) {
        sslError = ERR_get_error(); // todo "latest" or "earliest"? i'd have thought "latest", but X509_/RSA_ man pages say to get_error, while SSL_ merely "check the stack structure"
        ymerr("tls[%d]: failed to allocate ssl context: ssl err: %lu (%s)",tls->isServer , sslError, ERR_error_string(sslError,NULL));
        goto catch_return;
    }
    
    SSL_CTX_set_options(tls->sslCtx, SSL_OP_SINGLE_DH_USE);
    
    int verifyMask = SSL_VERIFY_PEER|SSL_VERIFY_CLIENT_ONCE|SSL_VERIFY_FAIL_IF_NO_PEER_CERT;
    SSL_CTX_set_verify(tls->sslCtx, verifyMask, __ym_tls_certificate_verify_callback);
    SSL_CTX_set_verify_depth(tls->sslCtx, YMTLSProviderVerifyDepth + 1); // allow callback to know what's going on
    
    result = SSL_CTX_set_cipher_list(tls->sslCtx, "AES256-SHA"); // todo
    if ( result != openssl_success ) {
        sslError = ERR_get_error();
        ymerr("tls[%d]: SSL_CTX_set_cipher_list failed: %d: ssl err: %lu (%s)",tls->isServer , result, sslError, ERR_error_string(sslError,NULL));
        goto catch_return;
    }
    
    YMX509CertificateRef *certList = NULL;
    if ( tls->localCertsFunc ) {
        int nCerts = 0;
        certList = tls->localCertsFunc(tls, &nCerts, tls->localCertsContext);
        if ( ! certList || nCerts <= 0 ) {
            ymerr("tls[%d]: user provided no local certs",tls->isServer);
            goto catch_return;
        }
        else if ( nCerts > 1 )
            ymerr("tls[%d]: warning: yammer uses the first certificate specified",tls->isServer);
        // todo, are we really going to do anything with a list here?
        cert = certList[0];
        free((void *)certList);
    } else {
        ymerr("tls[%d]: user doesn't provide certificates, creating self-signed",tls->isServer);
        tls->usingGeneratedCert = true;
        rsa = YMRSAKeyPairCreate();
        YMRSAKeyPairGenerate(rsa);
        cert = YMX509CertificateCreate(rsa);
    }
    
    if ( ! cert ) {
		ymerr("tls[%d]: fatal: no local certificate",tls->isServer);
		goto catch_return;
	}
    
    if ( ! tls->usingGeneratedCert )
        YMRetain(cert);
    tls->localCertificate = cert;
    
    result = SSL_CTX_use_certificate(tls->sslCtx, _YMX509CertificateGetX509(cert));
    if ( result != openssl_success ) {
        sslError = ERR_get_error(); // todo*n "latest" or "earliest"? i'd have thought "latest", but X509_/RSA_ man pages say to get_error, while SSL_ merely "check the stack structure"
        ymerr("tls[%d]: SSL_CTX_use_certificate failed: %d: ssl err: %lu (%s)",tls->isServer , result, sslError, ERR_error_string(sslError,NULL));
        goto catch_return;
    }
    result = SSL_CTX_use_RSAPrivateKey(tls->sslCtx, YMRSAKeyPairGetRSA(rsa));
    if ( result != openssl_success ) {
        sslError = ERR_get_error();
        ymerr("tls[%d]: SSL_CTX_use_RSAPrivateKey failed: %d: ssl err: %lu (%s)",tls->isServer , result, sslError, ERR_error_string(sslError,NULL));
        goto catch_return;
    }
    
    goto success_return;
    
catch_return:
    if ( tls->sslCtx ) {
        SSL_CTX_free(tls->sslCtx);
        tls->sslCtx = NULL;
    }
	if ( cert )
		YMRelease(cert);
success_return:
    if ( rsa )
        YMRelease(rsa);
}

bool __YMTLSProviderInit(__YMSecurityProviderRef provider)
{
    __YMTLSProviderRef tls = (__YMTLSProviderRef)provider;
    unsigned long sslError = SSL_ERROR_NONE;
    bool initOkay = false;
    int result;
    
    if ( ! tls->sslCtx )
        __YMTLSProviderInitSslCtx(tls);
    if ( ! tls->sslCtx ) {
        ymerr("tls[%d]: failed to init ssl ctx object",tls->isServer);
        return false;
    }
    
    // SSL_CTX* seems like a 'prototype' you can define once and create n 'instances', inheriting
    // settings, which are SSL*.
    tls->ssl = SSL_new(tls->sslCtx);
    if ( ! tls->ssl ) {
        // ``check the error stack to find out the reason'' i don't know what this means
        sslError = ERR_get_error(); // todo "latest" or "earliest"? i'd have thought "latest", but X509_/RSA_ man pages say to get_error, while SSL_ merely "check the stack structure"
        ymerr("tls[%d]: failed to allocate ssl object: ssl err: %lu (%s)",tls->isServer , sslError, ERR_error_string(sslError,NULL));
        goto catch_return;
    }
    
    int myIdx = SSL_get_ex_new_index(0, NULL, NULL, NULL, NULL);
    if ( myIdx == -1 ) {
        ymerr("tls[%d]: failed to allocate ex_data idx",tls->isServer);
        goto catch_return;
    }
    
    YMLockLock(gYMTLSExDataLock);
        YMDictionaryAdd(gYMTLSExDataList, (YMDictionaryKey)tls->ssl, (YMDictionaryValue)(uint64_t)myIdx); // todo CRASH 1
    YMLockUnlock(gYMTLSExDataLock);
    
    // and there's no CTX version of SSL_get_ex_data_X509_STORE_CTX_idx that i can see,
    // so this needs to be set at the 'object' level.
    result = SSL_set_ex_data(tls->ssl, myIdx, tls);
    if ( result != openssl_success ) {
        sslError = ERR_get_error();
        ymerr("tls[%d]: failed to set user info on ssl ctx: ssl err: %lu (%s)",tls->isServer, sslError, ERR_error_string(sslError, NULL));
        goto catch_return;
    }
    // assuming whatever memory was allocated here gets free'd in SSL_CTX_free
    
    //tls->bio = BIO_new(&ym_bio_methods);
    tls->bio = BIO_new_socket(tls->_common.socket, BIO_NOCLOSE);
    if ( ! tls->bio ) {
        sslError = ERR_get_error();
        ymerr("tls[%d]: BIO_new failed: %lu (%s)",tls->isServer ,sslError,ERR_error_string(sslError, NULL));
        goto catch_return;
    }
    
    SSL_set_bio(tls->ssl, tls->bio, tls->bio);
    tls->bio->ptr = tls; // this is the elusive context pointer
    
    SSL_set_debug(tls->ssl, 1);
    
    if ( tls->isServer ) {
        //SSL_set_info_callback(tls->ssl, __ym_tls_info_server_callback);
        SSL_set_accept_state(tls->ssl);
    } else {
        //SSL_set_info_callback(tls->ssl, __ym_tls_info_client_callback);
        SSL_set_connect_state(tls->ssl);
    }

	ymlog("tls[%d]: handshaking...", tls->isServer);
    result = SSL_do_handshake(tls->ssl);
    
    if ( result != openssl_success ) {
        sslError = SSL_get_error(tls->ssl, result);
        ymerr("tls[%d]: SSL_do_handshake failed: %d: ssl err: %lu (%s)",tls->isServer, result, sslError, ERR_error_string(sslError,NULL));
        goto catch_return;
    }

    ymlog("tls[%d]: SSL_do_handshake returned %d",tls->isServer, result);
    
    initOkay = true;
    
catch_return:
    
    if ( ! initOkay ) {
        // seems that SSL* takes ownership of BIO
        if ( tls->ssl ) {
            SSL_free(tls->ssl);
            tls->ssl = NULL;
        }
        if ( tls->sslCtx ) {
            SSL_CTX_free(tls->sslCtx);
            tls->sslCtx = NULL;
        }
    }
    
    return initOkay;
}

bool __YMTLSProviderRead(__YMSecurityProviderRef provider, uint8_t *buffer, size_t bytes)
{
    __YMTLSProviderRef tls = (__YMTLSProviderRef)provider;
    
    // loop or something? hate to change the whole prototype for this, and i like having length-y things be unsigned
    if ( bytes > INT32_MAX ) {
        ymerr("tls[%d]: error: fix thy internal errors!",tls->isServer );
        abort();
    }
    
    int result = SSL_read(tls->ssl, buffer, (int)bytes);
    
    if ( result <= 0 ) {
        unsigned long sslError = ERR_get_error();
        ymerr("tls[%d]: SSL_read failed: %d: ssl err: %lu (%s)",tls->isServer ,result,sslError,ERR_error_string(sslError, NULL));
        return false;
    }
    return true;
    
}

bool __YMTLSProviderWrite(__YMSecurityProviderRef provider, const uint8_t *buffer, size_t bytes)
{
    __YMTLSProviderRef tls = (__YMTLSProviderRef)provider;
    
    ymassert(bytes <= INT32_MAX,"tls[%d]: error: fix thy internal errors!",tls->isServer );
    
    int result = SSL_write(tls->ssl, buffer, (int)bytes);
    
    if ( result <= 0 ) {
        unsigned long sslError = ERR_get_error();
        ymerr("tls[%d]: SSL_write failed: %d: ssl err: %lu (%s)",tls->isServer ,result,sslError,ERR_error_string(sslError, NULL));
        return false;
    }
    
    return true;
}

bool __YMTLSProviderClose(__YMSecurityProviderRef provider)
{
    __YMTLSProviderRef tls = (__YMTLSProviderRef)provider;
    
    int result = SSL_shutdown(tls->ssl); // TODO
    if ( result ) {
        unsigned long sslError = SSL_get_error(tls->ssl, result);
        ymerr("tls[%d]: SSL_shutdown failed: %d: ssl err: %lu (%s)",tls->isServer ,result,sslError,ERR_error_string(sslError, NULL));
        return false;
    }
    
    return true;
}

#pragma mark function bio example

//int ym_tls_write(BIO *bio, const char *buffer, int length)
//{
//    ymlog("ym_tls_write: %p %p %d",bio,buffer,length);
//    YMTLSProviderRef tls = (YMTLSProviderRef)bio->ptr;
//    YMIOResult result = YMWriteFull(tls->socket, (const unsigned char *)buffer, length);
//    if ( result != YMIOSuccess )
//        return -1;
//    return length;
//}
//int ym_tls_read(BIO *bio, char *buffer, int length)
//{
//    ymlog("ym_tls_read: %p %p %d",bio,buffer,length);
//    YMTLSProviderRef tls = (YMTLSProviderRef)bio->ptr;
//    YMIOResult result = YMReadFull(tls->socket, (unsigned char *)buffer, length);
//    if ( result == YMIOError )
//        return -1;
//    else if ( result == YMIOEOF )
//        return 0; // right?
//    return length;
//}
//int ym_tls_puts(BIO *bio, const char * buffer)
//{
//    ymlog("ym_tls_puts: %p %p %s",bio,buffer,buffer);
//    return 0;
//}
//
//int ym_tls_gets(BIO *bio, char * buffer, int length)
//{
//    ymlog("ym_tls_gets: %p %p %d",bio,buffer,length);
//    return 0;
//}
//
//long ym_tls_ctrl (BIO *bio, int one, long two, void *three) { ymlog("ym_tls_ctrl: %p %d %ld %p",bio,one,two,three); return 1; }
//int ym_tls_new(BIO *bio) { ymlog("ym_tls_new: %p",bio); return 1; }
//int ym_tls_free(BIO *bio) { ymlog("ym_tls_free: %p",bio); return 1; }
//long ym_tls_callback_ctrl(BIO *bio, int one, bio_info_cb * info) { ymlog("ym_tls_callback_ctrl: %p %d %p",bio,one,info); return 1; }
//
//__unused static BIO_METHOD ym_bio_methods =
//{
//    BIO_TYPE_SOCKET,
//    "socket",
//    ym_tls_write,
//    ym_tls_read,
//    ym_tls_puts,
//    ym_tls_gets, /* sock_gets, */
//    ym_tls_ctrl,
//    ym_tls_new,
//    ym_tls_free,
//    ym_tls_callback_ctrl,
//};

YM_EXTERN_C_POP
