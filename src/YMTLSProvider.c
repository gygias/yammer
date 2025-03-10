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
#include "YMThreadPriv.h"
#include "YMDictionary.h"
#include "YMLock.h"
#include "YMUtilities.h"

#include <sys/stat.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#if defined(YMWIN32)
# include <openssl/applink.c> // as of 1.0.2, openssl exits with "no OPENSSL_Applink"
#else
# include <pthread.h>
#endif

#define ymlog_pre "tls[%d]: "
#define ymlog_args tls->isServer
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

typedef struct __ym_tls_provider
{
    __ym_security_provider_t _common;
    
    bool isServer;
    bool usingGeneratedCert; // todo client-supplied
    bool preverified;
    SSL_CTX *sslCtx;
    //int sslCtxExDataIdx; // if only!
    SSL *ssl;
    BIO *rBio;
    BIO *wBio;
    
    YMX509CertificateRef localCertificate;
    YMX509CertificateRef peerCertificate;
    
    ym_tls_provider_get_certs localCertsFunc;
    void *localCertsContext;
    ym_tls_provider_should_accept peerCertsFunc;
    void *peerCertsContext;
} __ym_tls_provider;
typedef struct __ym_tls_provider __ym_tls_provider_t;

bool __YMTLSProviderInit(__ym_security_provider_t *);
bool __YMTLSProviderRead(__ym_security_provider_t *, uint8_t *, size_t);
bool __YMTLSProviderWrite(__ym_security_provider_t *, const uint8_t *, size_t );
bool __YMTLSProviderClose(__ym_security_provider_t *);

extern BIO_METHOD ym_bio_methods;

/*void ym_tls_thread_id_callback(__unused CRYPTO_THREADID *threadId)
{
    //ymlog("ym_tls_thread_id_callback");
    CRYPTO_THREADID_set_numeric(threadId, (unsigned long)_YMThreadGetCurrentThreadNumber());
}*/

void __YMTLSInitPlatform(void)
{
#if defined(YMWIN32)
	OPENSSL_Applink();
#endif
}

YM_ONCE_FUNC(__YMTLSInit,
{
	SSL_load_error_strings();
	// ``SSL_library_init() always returns "1", so it is safe to discard the return value.''
	SSL_library_init();
	OpenSSL_add_all_algorithms();
	__YMTLSInitPlatform();

	gYMTLSLocks = YMALLOC(CRYPTO_num_locks()*sizeof(YMLockRef));

    // seems to have been made a no-op at some point in the last decade
	//CRYPTO_THREADID_set_callback(ym_tls_thread_id_callback);
	CRYPTO_set_locking_callback(__ym_tls_lock_callback);

	gYMTLSExDataList = YMDictionaryCreate();
	gYMTLSExDataLock = YMLockCreateWithOptions(YMInternalLockType);
})

YM_ONCE_OBJ gYMInitTLSOnce = YM_ONCE_INIT;

YMTLSProviderRef YMTLSProviderCreate(YMFILE inFile, YMFILE outFile, bool isServer)
{
	YM_ONCE_DO(gYMInitTLSOnce,__YMTLSInit);
    
    __ym_tls_provider_t *tls = (__ym_tls_provider_t *)_YMAlloc(_YMTLSProviderTypeID,sizeof(__ym_tls_provider_t));
    
    tls->_common.inFile = inFile;
    tls->_common.outFile = outFile;
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
    tls->rBio = NULL;
    tls->wBio = NULL;
    
    return tls;
}

void YMTLSProviderSetLocalCertsFunc(YMTLSProviderRef tls_, ym_tls_provider_get_certs func, void *context)
{
    __ym_tls_provider_t *tls = (__ym_tls_provider_t *)tls_;
    tls->localCertsFunc = func;
    tls->localCertsContext = context;
}

void YMTLSProviderSetAcceptPeerCertsFunc(YMTLSProviderRef tls_, ym_tls_provider_should_accept func, void *context)
{
    __ym_tls_provider_t *tls = (__ym_tls_provider_t *)tls_;
    tls->peerCertsFunc = func;
    tls->peerCertsContext = context;
}

void _YMTLSProviderFree(YMTLSProviderRef tls)
{
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

void YMTLSProviderFreeGlobals(void)
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

#if defined(YMWIN32)
# define CRYPTO_LOCK 1
# pragma warning(bad:fixme)
#endif
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
    ymlogg("__ym_tls_lock_callback: %s: %p: %s %d",role,ssl,desc,val);
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
    __ym_tls_provider_t *tls;
    YMX509CertificateRef ymCert = NULL;
    SSL *ssl = X509_STORE_CTX_get_ex_data(x509_store_ctx, SSL_get_ex_data_X509_STORE_CTX_idx());
    //int state = SSL_get_state(ssl);
    //bool isServer = ( SSL_in_accept_init(ssl) );
    
    YMLockLock(gYMTLSExDataLock);
        int myIdx = (int)YMDictionaryGetItem(gYMTLSExDataList, (YMDictionaryKey)ssl);
    YMLockUnlock(gYMTLSExDataLock);
    
    tls = SSL_get_ex_data(ssl, myIdx);
    ymlog("__ym_tls_certificate_verify_callback[%d]: %d",myIdx,preverify_ok);
    
    X509 *err_cert = X509_STORE_CTX_get_current_cert(x509_store_ctx);
    int err = X509_STORE_CTX_get_error(x509_store_ctx);
    int depth = X509_STORE_CTX_get_error_depth(x509_store_ctx);
    
    if ( tls->preverified && preverify_ok )
        return 1;
    
    if ( depth > YMTLSProviderVerifyDepth ) {
        ymerr("verify: depth > %d",YMTLSProviderVerifyDepth);
        return 0;
    } else if ( ! preverify_ok ) {
        if ( ! tls->isServer && tls->usingGeneratedCert && ( err != X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT ) ) {
            ymerr("verify: preverify: %d (%s)",err,X509_verify_cert_error_string(err));
#if !defined(YMLINUX)
            return 0;
#else
            ymerr("BUG: linux: cynical of cert validity, buy raspberry pi a precision timepiece for precision timekeeping");
#endif
            
        }
    }
    
    ymCert = _YMX509CertificateCreateWithX509(err_cert, true);
    
    if ( tls->peerCertsFunc ) {
        bool okay = tls->peerCertsFunc(tls, &ymCert, 1, tls->peerCertsContext);
        
        if ( ! okay ) {
            ymerr("verify: client rejected peer cert");
            YMRelease(ymCert);
            return 0;
        }
    }
    else
        ymlog("user does not do validation, accepting self-signed certificate");
    
    tls->peerCertificate = ymCert;
    tls->preverified = true;
    
    return 1;
}

void __YMTLSProviderInitSslCtx(__ym_tls_provider_t *tls)
{
    int result;
    unsigned long sslError = SSL_ERROR_NONE;
    YMRSAKeyPairRef rsa = NULL;
    YMX509CertificateRef cert = NULL;

    tls->sslCtx = SSL_CTX_new(tls->isServer ? SSLv23_server_method() : SSLv23_client_method());    // `` Negotiate highest available
    //      SSL/TLS version ''
    if ( ! tls->sslCtx ) {
        sslError = ERR_get_error(); // todo "latest" or "earliest"? i'd have thought "latest", but X509_/RSA_ man pages say to get_error, while SSL_ merely "check the stack structure"
        ymerr("allocating ssl context: ssl err: %lu (%s)",sslError,ERR_error_string(sslError,NULL));
        goto catch_return;
    }
    
    SSL_CTX_set_options(tls->sslCtx, SSL_OP_SINGLE_DH_USE);
    
    int verifyMask = SSL_VERIFY_PEER|SSL_VERIFY_CLIENT_ONCE|SSL_VERIFY_FAIL_IF_NO_PEER_CERT;
    SSL_CTX_set_verify(tls->sslCtx, verifyMask, __ym_tls_certificate_verify_callback);
    SSL_CTX_set_verify_depth(tls->sslCtx, YMTLSProviderVerifyDepth + 1); // allow callback to know what's going on
    
    result = SSL_CTX_set_cipher_list(tls->sslCtx, "AES256-SHA"); // todo
    if ( result != openssl_success ) {
        sslError = ERR_get_error();
        ymerr("SSL_CTX_set_cipher_list failed: %d: ssl err: %lu (%s)",result,sslError,ERR_error_string(sslError,NULL));
        goto catch_return;
    }
    
    YMX509CertificateRef *certList = NULL;
    if ( tls->localCertsFunc ) {
        int nCerts = 0;
        certList = tls->localCertsFunc(tls, &nCerts, tls->localCertsContext);
        if ( ! certList || nCerts <= 0 ) {
            ymerr("user provided no local certs");
            goto catch_return;
        }
        else if ( nCerts > 1 )
            ymerr("yammer uses the first certificate specified");
        // todo, are we really going to do anything with a list here?
        cert = certList[0];
        YMFREE((void *)certList);
    } else {
        ymerr("user doesn't provide certificates, creating self-signed");
        tls->usingGeneratedCert = true;
        rsa = YMRSAKeyPairCreate();
        YMRSAKeyPairGenerate(rsa);
#if !defined(YMLINUX)
        cert = YMX509CertificateCreate(rsa);
#else
        ymerr("BUG: linux: using ±1 years validity");
        cert = YMX509CertificateCreate2(rsa, 365 * 1, 365 * 1); // rpi ntp doesn't seem to stick when it loses internet, it's generally ±days
#endif
    }
    
    if ( ! cert ) {
		ymerr("fatal: no local certificate");
		goto catch_return;
	}
    
    if ( ! tls->usingGeneratedCert )
        YMRetain(cert);
    tls->localCertificate = cert;
    
    result = SSL_CTX_use_certificate(tls->sslCtx, _YMX509CertificateGetX509(cert));
    if ( result != openssl_success ) {
        sslError = ERR_get_error(); // todo*n "latest" or "earliest"? i'd have thought "latest", but X509_/RSA_ man pages say to get_error, while SSL_ merely "check the stack structure"
        ymerr("SSL_CTX_use_certificate: %d: ssl err: %lu (%s)",result,sslError,ERR_error_string(sslError,NULL));
        goto catch_return;
    }
    result = SSL_CTX_use_RSAPrivateKey(tls->sslCtx, YMRSAKeyPairGetRSA(rsa));
    if ( result != openssl_success ) {
        sslError = ERR_get_error();
        ymerr("SSL_CTX_use_RSAPrivateKey: %d: ssl err: %lu (%s)",result,sslError,ERR_error_string(sslError,NULL));
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

bool __YMTLSProviderInit(__ym_security_provider_t *p)
{
    __ym_tls_provider_t * tls = (__ym_tls_provider_t *)p;
    unsigned long sslError = SSL_ERROR_NONE;
    bool initOkay = false;
    int result;
    
    if ( ! tls->sslCtx )
        __YMTLSProviderInitSslCtx(tls);
    if ( ! tls->sslCtx ) {
        ymerr("failed to init ssl ctx object");
        return false;
    }
    
    // SSL_CTX* seems like a 'prototype' you can define once and create n 'instances', inheriting
    // settings, which are SSL*.
    tls->ssl = SSL_new(tls->sslCtx);
    if ( ! tls->ssl ) {
        // ``check the error stack to find out the reason'' i don't know what this means
        sslError = ERR_get_error(); // todo "latest" or "earliest"? i'd have thought "latest", but X509_/RSA_ man pages say to get_error, while SSL_ merely "check the stack structure"
        ymerr("failed to allocate ssl object: ssl err: %lu (%s)",sslError,ERR_error_string(sslError,NULL));
        goto catch_return;
    }
    
    int myIdx = SSL_get_ex_new_index(0, NULL, NULL, NULL, NULL);
    if ( myIdx == -1 ) {
        ymerr("allocating ex_data idx");
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
        ymerr("setting user info on ssl ctx: ssl err: %lu (%s)", sslError, ERR_error_string(sslError, NULL));
        goto catch_return;
    }
    // assuming whatever memory was allocated here gets free'd in SSL_CTX_free
    
    //tls->rBio = BIO_new(&ym_bio_methods);
    //tls->wBio = tls->rBio;
    //tls->bio = BIO_new_socket(tls->_common.socket, BIO_NOCLOSE);
    tls->rBio = BIO_new_fd((int)tls->_common.inFile, BIO_NOCLOSE);
    if ( ! tls->rBio ) {
        sslError = ERR_get_error();
        ymerr("BIO_new r failed: %lu (%s)",sslError,ERR_error_string(sslError, NULL));
        goto catch_return;
    }
    
    tls->wBio = BIO_new_fd((int)tls->_common.outFile, BIO_NOCLOSE);
    if ( ! tls->wBio ) {
        sslError = ERR_get_error();
        ymerr("BIO_new w failed: %lu (%s)",sslError,ERR_error_string(sslError, NULL));
        goto catch_return;
    }
    
    SSL_set_bio(tls->ssl, tls->rBio, tls->wBio);
	BIO_set_app_data(tls->rBio, tls); // this is the elusive context pointer
	BIO_set_app_data(tls->wBio, tls);
    ymlog("rBio[%d], wBio[%d]",tls->_common.inFile,tls->_common.outFile);
    
    SSL_set_debug(tls->ssl, 1);
    
    if ( tls->isServer ) {
        SSL_set_info_callback(tls->ssl, __ym_tls_info_server_callback);
        SSL_set_accept_state(tls->ssl);
    } else {
        SSL_set_info_callback(tls->ssl, __ym_tls_info_client_callback);
        SSL_set_connect_state(tls->ssl);
    }

	ymlog("handshaking...");
    result = SSL_do_handshake(tls->ssl);
    
    if ( result != openssl_success ) {
        sslError = SSL_get_error(tls->ssl, result);
        ymerr("SSL_do_handshake: %d: ssl err: %lu (%s)",result,sslError,ERR_error_string(sslError,NULL));
        if ( sslError == SSL_ERROR_SYSCALL ) ymerr("          errno: %d (%s)",errno,strerror(errno));
        goto catch_return;
    }

    ymlog("SSL_do_handshake returned %d",result);
    
    initOkay = true;
    
catch_return:
    
    if ( ! initOkay ) {
        // seems that SSL* takes memory ownership of BIO
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

bool __YMTLSProviderRead(__ym_security_provider_t *p, uint8_t *buffer, size_t bytes)
{
    __ym_tls_provider_t *tls = (__ym_tls_provider_t *)p;
    
    // loop or something? hate to change the whole prototype for this, and i like having length-y things be unsigned
    if ( bytes > INT32_MAX )
        ymabort("tls[%d]: error: fix thy internal errors!",tls->isServer );
    
    int result = SSL_read(tls->ssl, buffer, (int)bytes);
    
    if ( result <= 0 ) {
        unsigned long sslError = SSL_get_error(tls->ssl, result);
        ymerr("SSL_read(%p,%p,%zu): %d: ssl err: %lu (%s) errno%s: %d (%s)",tls->ssl,buffer,bytes,result,sslError,ERR_error_string(sslError, NULL),( sslError == SSL_ERROR_SYSCALL )?"":" probably irrelevant",errno,strerror(errno));
        return false;
    }
    return true;
    
}

bool __YMTLSProviderWrite(__ym_security_provider_t *p, const uint8_t *buffer, size_t bytes)
{
    __ym_tls_provider_t *tls = (__ym_tls_provider_t *)p;
    
    ymassert(bytes <= INT32_MAX,"fix thy internal errors!");
    
    int result = SSL_write(tls->ssl, buffer, (int)bytes);
    
    if ( result <= 0 ) {
        unsigned long sslError = SSL_get_error(tls->ssl, result);
        ymerr("SSL_write failed(%p,%p,%zu): %d: ssl err: %lu (%s) errno%s: %d (%s)",tls->ssl,buffer,bytes,result,sslError,ERR_error_string(sslError, NULL),( sslError == SSL_ERROR_SYSCALL )?"":" probably irrelevant",errno,strerror(errno));
        return false;
    }
    
    return true;
}

bool __YMTLSProviderClose(__ym_security_provider_t *p)
{
    __ym_tls_provider_t *tls = (__ym_tls_provider_t *)p;
    
    int result = SSL_shutdown(tls->ssl); // TODO
    if ( result ) {
        unsigned long sslError = SSL_get_error(tls->ssl, result);
        ymerr("SSL_shutdown(%p) failed: %d: ssl err: %lu (%s) errno%s %d (%s)",tls->ssl,result,sslError,ERR_error_string(sslError, NULL),( sslError == SSL_ERROR_SYSCALL )?"":" probably irrelevant",errno,strerror(errno));
        return false;
    }
    
    return true;
}

/*
#pragma mark function bio example

int ym_tls_write(BIO *bio, const char *buffer, int length)
{
    YMTLSProviderRef tls = (YMTLSProviderRef)BIO_get_app_data(bio);
    ymlog("ym_tls_write: %p %p %d",bio,buffer,length);
    YMIOResult result = YMWriteFull(tls->_common.inFile, (const unsigned char *)buffer, length, NULL);
    if ( result != YMIOSuccess )
        return -1;
    return length;
}
int ym_tls_read(BIO *bio, char *buffer, int length)
{
    YMTLSProviderRef tls = (YMTLSProviderRef)BIO_get_app_data(bio);
    ymlog("ym_tls_read: %p %p %d",bio,buffer,length);
    YMIOResult result = YMReadFull(tls->_common.outFile, (unsigned char *)buffer, length, NULL);
    if ( result == YMIOError )
        return -1;
    else if ( result == YMIOEOF )
        return 0; // right?
    return length;
}
int ym_tls_puts(BIO *bio, const char * buffer)
{
    YMTLSProviderRef tls = (YMTLSProviderRef)BIO_get_app_data(bio);
    ymlog("ym_tls_puts: %p %p %s",bio,buffer,buffer);
    return 0;
}

int ym_tls_gets(BIO *bio, char * buffer, int length)
{
    YMTLSProviderRef tls = (YMTLSProviderRef)BIO_get_app_data(bio);
    ymlog("ym_tls_gets: %p %p %d",bio,buffer,length);
    return 0;
}

long ym_tls_ctrl (BIO *bio, int one, long two, void *three) { YMTLSProviderRef tls = (YMTLSProviderRef)BIO_get_app_data(bio); ymlog("ym_tls_ctrl: %p %d %ld %p",bio,one,two,three); return 1; }
int ym_tls_new(__unused BIO *bio) { //YMTLSProviderRef tls = (YMTLSProviderRef)bio->ptr; ymlog("ym_tls_new: %p",bio);
	return 1; }
int ym_tls_free(BIO *bio) { YMTLSProviderRef tls = (YMTLSProviderRef)BIO_get_app_data(bio); ymlog("ym_tls_free: %p",bio); return 1; }
long ym_tls_callback_ctrl(BIO *bio, int one, bio_info_cb * info) { YMTLSProviderRef tls = (YMTLSProviderRef)BIO_get_app_data(bio); ymlog("ym_tls_callback_ctrl: %p %d %p",bio,one,info); return 1; }

BIO_METHOD ym_bio_methods =
{
    BIO_TYPE_FD,
    "ymbio",
    ym_tls_write,
    ym_tls_read,
    ym_tls_puts,
    ym_tls_gets, // sock_gets
    ym_tls_ctrl,
    ym_tls_new,
    ym_tls_free,
    ym_tls_callback_ctrl,
};*/

YM_EXTERN_C_POP
