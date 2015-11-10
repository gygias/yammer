//
//  YMX509Certificate.c
//  yammer
//
//  Created by david on 11/10/15.
//  Copyright © 2015 Combobulated Software. All rights reserved.
//

#include "YMX509Certificate.h"

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

typedef struct __YMX509Certificate
{
    YMTypeID _type;
    
    X509 *x509;
    
} _YMX509Certificate;

X509* __YMX509CertificateGetX509(YMRSAKeyPairRef keyPair)
{
    unsigned long opensslErr = ERR_LIB_NONE;
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
    
    X509 *x509 = X509_new(); // seems you need to own the prototype (and fixed arg list or varg) to tokenize this
    if ( ! x509 )
    {
        opensslFunc = YM_TOKEN_STR(X509_new);
        opensslErr = ERR_get_error();
        goto catch_return;
    }
    
    int result = X509_set_version(x509, 1); // const?
    if ( ERR_LIB_NONE != result )
    {
        opensslFunc = YM_TOKEN_STR(X509_set_version);
        opensslErr = ERR_get_error();
        goto catch_return;
    }
    
    serial = M_ASN1_INTEGER_new();
    if ( ! serial )
    {
        opensslFunc = YM_TOKEN_STR(M_ASN1_INTEGER_new);
    }
    
    result = X509_set_serialNumber(x509, serial);
    if ( ERR_LIB_NONE != result )
    {
        opensslFunc = YM_TOKEN_STR(X509_set_serialNumber);
        opensslErr = ERR_get_error();
        goto catch_return;
    }
    
    subject = X509_get_subject_name(x509); // why get and not new?
    if ( ERR_LIB_NONE != result )
    {
        opensslFunc = YM_TOKEN_STR(X509_get_subject_name);
        opensslErr = ERR_get_error();
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
            opensslErr = ERR_get_error();
            goto catch_return;
        }
    }
    
    result = X509_set_subject_name(x509, subject); // self-signed
    if ( ERR_LIB_NONE != result )
    {
        opensslFunc = YM_TOKEN_STR(X509_set_subject_name);
        opensslErr = ERR_get_error();
        goto catch_return;
    }
    
    result = X509_set_issuer_name(x509, subject); // self-signed
    if ( ERR_LIB_NONE != result )
    {
        opensslFunc = YM_TOKEN_STR(X509_set_issuer_name);
        opensslErr = ERR_get_error();
        goto catch_return;
    }
    
    ASN1_TIME * timeResult = X509_gmtime_adj(X509_get_notBefore(x509), 0);
    if ( ! timeResult ) // no man page or header comment, this is a guess
    {
        opensslFunc = YM_TOKEN_STR(X509_gmtime_adj);
        opensslErr = ERR_get_error();
        goto catch_return;
    }
    
    int maxOffset = INT32_MAX;
    timeResult = X509_gmtime_adj(X509_get_notAfter(x509), maxOffset);
    if ( ! timeResult ) // no man page or header comment, this is a guess
    {
        opensslFunc = YM_TOKEN_STR(X509_gmtime_adj);
        opensslErr = ERR_get_error();
        goto catch_return;
    }
    
    key = EVP_PKEY_new();
    if ( ! key )
    {
        opensslFunc = YM_TOKEN_STR(EVP_PKEY_new);
        opensslErr = ERR_get_error();
        goto catch_return;
    }
    
    RSA *rsa = YMRSAKeyPairGetRSA(keyPair);
    // assumes ownership of rsa and will free it when EVP_PKEY is free'd
    //EVP_PKEY_assign_RSA(key,rsa);
    
    result = EVP_PKEY_set1_RSA(key, rsa);
    if ( ERR_LIB_NONE != result )
    {
        opensslFunc = YM_TOKEN_STR(EVP_PKEY_set1_RSA);
        opensslErr = ERR_get_error();
        goto catch_return;
    }
    
    result = X509_set_pubkey(x509, key);
    if ( ERR_LIB_NONE != result )
    {
        opensslFunc = YM_TOKEN_STR(X509_set_pubkey);
        opensslErr = ERR_get_error();
        goto catch_return;
    }
    
    // $ man X509_sign
    // No manual entry for X509_sign
    // lol
    result = X509_sign(x509, key, EVP_md5()); // todo: md5 ok?
    if ( ERR_LIB_NONE != result )
    {
        opensslFunc = YM_TOKEN_STR(X509_sign);
        opensslErr = ERR_get_error();
        goto catch_return;
    }
    
catch_return:
    if ( SSL_ERROR_NONE != opensslErr )
    {
        ymerr("x509: %s failed: %lu (%s)",opensslFunc,opensslErr,ERR_error_string(opensslErr,NULL));
        if ( x509 )
        {
            X509_free(x509);
            x509 = NULL;
        }
    }
    
    if ( serial )
        M_ASN1_INTEGER_free(serial);
    if ( key )
        EVP_PKEY_free(key);
    
    return x509;
}

YMX509CertificateRef YMX509CertificateCreate(YMRSAKeyPairRef keyPair)
{
    X509 *x509 = __YMX509CertificateGetX509(keyPair);
    if ( ! x509 )
        return NULL;
    
    YMX509CertificateRef certificate = (YMX509CertificateRef)YMMALLOC(sizeof(struct __YMX509Certificate));
    certificate->_type = _YMX509CertificateTypeID;
    
    certificate->x509 = x509;
    
    return certificate;
}

void _YMX509CertificateFree(YMTypeRef object)
{
    YMX509CertificateRef cert = (YMX509CertificateRef)object;
    X509_free(cert->x509);
    free(cert);
}
