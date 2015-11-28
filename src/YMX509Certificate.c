//
//  YMX509Certificate.c
//  yammer
//
//  Created by david on 11/10/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMX509Certificate.h"
#include "YMX509CertificatePriv.h"

#include "YMOpenssl.h"

#include <openssl/x509.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "YMLog.h"
#undef ymlog_type
#define ymlog_type YMLogSecurity
#if ( ymlog_type > ymlog_target )
#undef ymlog
#define ymlog(x,...) ;
#endif

typedef struct __ym_x509_certificate
{
    _YMType _type;
    
    X509 *x509;
} ___ym_x509_certificate;
typedef struct __ym_x509_certificate __YMX509Certificate;
typedef __YMX509Certificate *__YMX509CertificateRef;

X509* __YMX509CertificateCreateX509(YMRSAKeyPairRef keyPair)
{
    const char *opensslFunc = NULL;
    ASN1_INTEGER *serial = NULL;
    X509_NAME *subject = NULL;
    EVP_PKEY *key = NULL;
    //time_t nowish = time(0);
    
    // was a cute idea, but due to "i" i don't know if it's possible
//    int nFuncsAndArgs = 3;
//    const void **funcsAndArgs[] =
//    {
//        (const void *[]) { "X509_new", YM_TOKEN_STR(void), NULL, NULL },
//        (const void *[]) { "X509_set_version", YM_TOKEN_STR(int),(void *)1, NULL, NULL },
//        (const void *[]) { "X509_set_serialNumber", YM_TOKEN_STR(ASN1_INTEGER), (void *)YM_VERSION, NULL, NULL },
//    };
//    
//    for ( int i = 0; i < nFuncsAndArgs; i++ )
//    {
//        const void ** funcAndArgs = funcsAndArgs[i];
//        const char* func = (const char *)funcAndArgs[0];
//        const void * type1 = funcAndArgs[1];
//        const void * arg1 = funcAndArgs[2];
//        const void * arg2 = funcAndArgs[3];
//        YM_CALL_V(YM_TOKEN_STR(func), type1, arg1, arg2); // <-- ??
//    }
    //YM_CALL_V(openssl_lol, int, 1, 2, 3);
    
    int result = 0;
    X509 *x509 = X509_new(); // seems you need to own the prototype (and fixed arg list or varg) to tokenize this
    if ( ! x509 )
    {
        opensslFunc = YM_TOKEN_STR(X509_new);
        goto catch_return;
    }
    
    result = X509_set_version(x509, 1); // const?
    if ( ERR_LIB_NONE != result )
    {
        opensslFunc = YM_TOKEN_STR(X509_set_version);
        goto catch_return;
    }
    
    serial = ASN1_INTEGER_new();
    if ( ! serial )
    {
        opensslFunc = YM_TOKEN_STR(M_ASN1_INTEGER_new);
		goto catch_return;
    }
    
    result = X509_set_serialNumber(x509, serial);
    if ( ERR_LIB_NONE != result )
    {
		ASN1_INTEGER_free(serial);
        opensslFunc = YM_TOKEN_STR(X509_set_serialNumber);
        goto catch_return;
    }
	ASN1_INTEGER_free(serial);
    
    subject = X509_get_subject_name(x509); // why get and not new?
    if ( ERR_LIB_NONE != result )
    {
        opensslFunc = YM_TOKEN_STR(X509_get_subject_name);
        goto catch_return;
    }
    
    int nKeyValues = 3;
    const char ** keysValues[] = {
        (const char *[]) { "C", "JP" },
        (const char *[]) { "O", "combobulated" },
        (const char *[]) { "CN", "libyammer" }
    };
    
    for ( int i = 0; i < nKeyValues; i++ )
    {
        result = X509_NAME_add_entry_by_txt(subject, keysValues[i][0],
                                            MBSTRING_ASC, (const unsigned char *)keysValues[i][1],
                                            -1,
                                            -1,
                                            0);
        if ( ERR_LIB_NONE != result )
        {
            opensslFunc = YM_TOKEN_STR(X509_NAME_add_entry_by_txt);
            goto catch_return;
        }
    }
    
    result = X509_set_subject_name(x509, subject); // self-signed
    if ( ERR_LIB_NONE != result )
    {
        opensslFunc = YM_TOKEN_STR(X509_set_subject_name);
        goto catch_return;
    }
    
    result = X509_set_issuer_name(x509, subject); // self-signed
    if ( ERR_LIB_NONE != result )
    {
        opensslFunc = YM_TOKEN_STR(X509_set_issuer_name);
        goto catch_return;
    }
    
    X509_gmtime_adj(X509_get_notBefore(x509), 0);
    X509_time_adj_ex(X509_get_notAfter(x509), 365, 0, NULL);
    
    key = EVP_PKEY_new(); // todo leaks in happy case?
    if ( ! key )
    {
        opensslFunc = YM_TOKEN_STR(EVP_PKEY_new);
        goto catch_return;
    }
    
    RSA *rsa = YMRSAKeyPairGetRSA(keyPair);
    // assumes ownership of rsa and will free it when EVP_PKEY is free'd
    //EVP_PKEY_assign_RSA(key,rsa);
    
    result = EVP_PKEY_set1_RSA(key, rsa);
    if ( ERR_LIB_NONE != result )
    {
        opensslFunc = YM_TOKEN_STR(EVP_PKEY_set1_RSA);
        goto catch_return;
    }
    
    result = X509_set_pubkey(x509, key);
    if ( ERR_LIB_NONE != result )
    {
        opensslFunc = YM_TOKEN_STR(X509_set_pubkey);
        goto catch_return;
    }
    
    // $ man X509_sign
    // No manual entry for X509_sign
    result = X509_sign(x509, key, EVP_sha1());
    if ( ERR_LIB_NONE != result )
    {
		if ( result != 512 )
		{
			opensslFunc = YM_TOKEN_STR(X509_sign);
			goto catch_return;
		}
		else
			result = ERR_LIB_NONE;
    }
    
catch_return:
    if ( ERR_LIB_NONE != result )
    {
		unsigned long sslErr = ERR_get_error();
        ymerr("x509: %s failed: r%d s%lu (%s)",opensslFunc, result, sslErr, ERR_error_string(sslErr,NULL));
        if ( x509 )
        {
            X509_free(x509);
            x509 = NULL;
        }
    }
    
    if ( key )
        EVP_PKEY_free(key);
    
    return x509;
}

YMX509CertificateRef YMX509CertificateCreate(YMRSAKeyPairRef keyPair)
{
    X509 *x509 = __YMX509CertificateCreateX509(keyPair);
    if ( ! x509 )
        return NULL;
    
    return _YMX509CertificateCreateWithX509(x509, false);
}

YMX509CertificateRef _YMX509CertificateCreateWithX509(X509 *x509, bool copy)
{
    __YMX509CertificateRef certificate = (__YMX509CertificateRef)_YMAlloc(_YMX509CertificateTypeID,sizeof(__YMX509Certificate));
    
    certificate->x509 = copy ? X509_dup(x509) : x509;
    
    return certificate;
}

void _YMX509CertificateFree(YMTypeRef object)
{
    __YMX509CertificateRef cert = (__YMX509CertificateRef)object;
    X509_free(cert->x509);
}

size_t YMX509CertificateGetPublicKeyData(void *buffer)
{
    buffer = NULL;
    return 0;
}

X509 *_YMX509CertificateGetX509(YMX509CertificateRef cert_)
{
    __YMX509CertificateRef cert = (__YMX509CertificateRef)cert_;
    return cert->x509;
}
