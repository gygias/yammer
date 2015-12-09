//
//  YMRSAKeyPair.c
//  yammer
//
//  Created by david on 11/9/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMRSAKeyPair.h"

#include "YMOpenssl.h"

#define ymlog_type YMLogSecurity
#include "YMLog.h"

#include <openssl/rsa.h>
#include <openssl/rand.h> // let's hope this is what the man page means by "the" pseudo-random number generator, despite the reference to "bad" rand(3)
#include <openssl/err.h>
#include <openssl/bn.h>

#ifndef WIN32
# include <pthread.h>
# include <sys/time.h>
#else
# include <winsock2.h> // time structures
# include "YMUtilities.h" // gettimeofday copy
#endif

YM_EXTERN_C_PUSH

// it's been a while since crypto 101
// public key (N,e), N:modulo e:public exponent
// private key (d)
// message^e must be greater than N

typedef struct __ym_rsa_keypair_t
{
    _YMType _typeID;
    
    int publicE;
    int moduloNBits;
    
    RSA *rsa;
} __ym_rsa_keypair_t;
typedef struct __ym_rsa_keypair_t *__YMRSAKeyPairRef;

void __YMRSAKeyPairSeed();

YMRSAKeyPairRef YMRSAKeyPairCreateWithModuloSize(int moduloBits, int publicExponent)
{
    if ( moduloBits > OPENSSL_RSA_MAX_MODULUS_BITS )
    {
        ymerr("rsa: requested modulus bits exceeds max");
        return NULL;
    }
    // there's also OPENSSL_RSA_MAX_PUBEXP_BITS
    
    RSA* rsa = RSA_new();
    if ( ! rsa )
    {
        unsigned long error = ERR_get_error();
        ymerr("RSA_new failed: %lu (%s)", error, ERR_error_string(error,NULL));
        return NULL;
    }
    
    __YMRSAKeyPairRef keyPair = (__YMRSAKeyPairRef)_YMAlloc(_YMRSAKeyPairTypeID,sizeof(struct __ym_rsa_keypair_t));
    
    keyPair->rsa = rsa;
    keyPair->moduloNBits = moduloBits;
    keyPair->publicE = publicExponent;
    return keyPair;
}

YMRSAKeyPairRef YMRSAKeyPairCreate()
{
    return YMRSAKeyPairCreateWithModuloSize(4096, RSA_F4);
}

void _YMRSAKeyPairFree(YMTypeRef object)
{
    __YMRSAKeyPairRef keyPair = (__YMRSAKeyPairRef)object;
    RSA_free(keyPair->rsa);
}

// e.g.
//struct rsa_st {
//    /*
//     * The first parameter is used to pickup errors where this is passed
//     * instead of aEVP_PKEY, it is set to 0
//     */
//    int pad;
//    long version;
//    const RSA_METHOD *meth;
//    /* functional reference if 'meth' is ENGINE-provided */
//    ENGINE *engine;
//    BIGNUM *n;
//    BIGNUM *e;
//    BIGNUM *d;
//    BIGNUM *p;
//    BIGNUM *q;
//    BIGNUM *dmp1;
//    BIGNUM *dmq1;
//    BIGNUM *iqmp;
//    /* be careful using this if the RSA structure is shared */
//    CRYPTO_EX_DATA ex_data;
//    int references;
//    int flags;
//    /* Used to cache montgomery values */
//    BN_MONT_CTX *_method_mod_n;
//    BN_MONT_CTX *_method_mod_p;
//    BN_MONT_CTX *_method_mod_q;
//    /*
//     * all BIGNUM values are actually in the following data, if it is not
//     * NULL
//     */
//    char *bignum_data;
//    BN_BLINDING *blinding;
//    BN_BLINDING *mt_blinding;
//};
//
//struct bignum_st {
//    BN_ULONG *d;                /* Pointer to an array of 'BN_BITS2' bit
//                                 * chunks. */
//    int top;                    /* Index of last used d +1. */
//    /* The next are internal book keeping for bn_expand. */
//    int dmax;                   /* Size of the d array. */
//    int neg;                    /* one if the number is negative */
//    int flags;
//};

bool YMRSAKeyPairGenerate(YMRSAKeyPairRef keyPair_)
{
    __YMRSAKeyPairRef keyPair = (__YMRSAKeyPairRef)keyPair_;
    
    // "OpenSSL makes sure that the PRNG state is unique for each thread.
    // On systems that provide /dev/urandom, the randomness device is used to seed the PRNG transparently."
    // leaving me unsure if this should be once'd. For now playing it safe.
    //pthread_once(&gYMRSAKeyPairSeedOnce, _YMRSAKeyPairSeed);
    __YMRSAKeyPairSeed();
    
#ifdef YMDEBUG
    struct timeval then;
    int timeResult = gettimeofday(&then,NULL);
#endif
    unsigned long rsaErr = 0;
    const char* rsaErrFunc = NULL;
    
    int result = openssl_fail;
    BIGNUM *e = BN_new();
    if ( ! e )
    {
        rsaErrFunc = "BN_new";
        goto catch_return;
    }
    
    result = BN_set_word(e, keyPair->publicE);
    if ( ERR_LIB_NONE != result )
    {
        rsaErrFunc = "BN_set_word";
        goto catch_return;
    }
    
    BN_set_word(e, keyPair->publicE);
    
    // os x man page doesn't actually state that 1 is success for _ex #yolo
    result = RSA_generate_key_ex(keyPair->rsa, keyPair->moduloNBits, e, NULL /*BN_GENCB *cb callback struct*/);
    if ( ERR_LIB_NONE != result )
    {
        rsaErrFunc = "RSA_generate_key_ex";
        goto catch_return;
    }
    
#ifdef YMDEBUG
    struct timeval now;
    if ( timeResult == 0 )
    {
        timeResult = gettimeofday(&now, NULL);
        if ( timeResult == 0 )
            ymlog("rsa: it took %ld seconds to generate rsa keypair with %d modulo bits",now.tv_sec - then.tv_sec,keyPair->moduloNBits);
    }
    if ( timeResult != 0 )
        ymerr("rsa: gettimeofday failed");
#endif
    
catch_return:
    if ( ERR_LIB_NONE != result )
    {
        rsaErr = ERR_get_error();
        ymerr("rsa: %s failed: %lu (%s)", rsaErrFunc, rsaErr, ERR_error_string(rsaErr,NULL));
    }
    
    if ( e )
        BN_free(e);
    
    return (result == openssl_success);
}

void __YMRSAKeyPairSeed()
{
#ifdef YMDEBUG
    struct timeval then;
    int timeResult = gettimeofday(&then,NULL);
#endif
    
    uint64_t iters = 0;
    do
    {
        uint32_t aRandom = arc4random();
        RAND_seed(&aRandom, sizeof(aRandom));
        iters++;
    } while ( RAND_status() == 0 ); // all the solar flares
    
#ifdef YMDEBUG
    struct timeval now;
    if ( timeResult == 0 )
    {
        timeResult = gettimeofday(&now, NULL);
        if ( timeResult == 0 )
            ymlog("rsa: it took %ld seconds and %llu words to seed openssl rand",now.tv_sec - then.tv_sec,iters);
    }
    if ( timeResult != 0 )
        ymlog("rsa: gettimeofday failed");
#endif
}

void *YMRSAKeyPairGetRSA(YMRSAKeyPairRef keyPair_)
{
    __YMRSAKeyPairRef keyPair = (__YMRSAKeyPairRef)keyPair_;
    return keyPair->rsa;
}

YM_EXTERN_C_POP
