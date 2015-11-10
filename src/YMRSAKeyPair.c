//
//  YMRSAKeyPair.c
//  yammer
//
//  Created by david on 11/9/15.
//  Copyright © 2015 Combobulated Software. All rights reserved.
//

#include "YMRSAKeyPair.h"
#include "YMPrivate.h"

#include "YMLog.h"
#undef ymlog_type
#define ymlog_type YMLogSecurity
#if ( ymlog_type > ymlog_target )
#undef ymlog
#define ymlog(x,...) ;
#endif

#include <openssl/rsa.h>
#include <openssl/rand.h> // let's hope this is what the man page means by "the" pseudo-random number generator, despite the reference to "bad" rand(3)
#include <openssl/err.h>
#include <pthread.h>
#include <sys/time.h>

// it's been a while since crypto 101
// public key (N,e), N:modulo e:public exponent
// private key (d)
// message^e must be greater than N

#define openssl_success 1

typedef struct __YMRSAKeyPair
{
    YMTypeID _typeID;
    
    int publicE;
    int moduloNBits;
    
    RSA *rsa;
} _YMRSAKeyPair;

//static pthread_once_t gYMRSAKeyPairSeedOnce = PTHREAD_ONCE_INIT;
void _YMRSAKeyPairSeed();

YMRSAKeyPairRef YMRSAKeyPairCreateWithModuloSize(int moduloBits, int publicExponent)
{
    RSA* rsa = RSA_new();
    if ( ! rsa )
    {
        unsigned long error = ERR_get_error();
        ymlog("RSA_new failed: %lu (%s)", error, ERR_error_string(error,NULL));
        return NULL;
    }
    
    YMRSAKeyPairRef keyPair = (YMRSAKeyPairRef)malloc(sizeof(struct __YMRSAKeyPair));
    keyPair->_typeID = _YMRSAKeyPairTypeID;
    
    keyPair->rsa = rsa;
    keyPair->moduloNBits = moduloBits;
    keyPair->publicE = publicExponent;
    return keyPair;
}

YMRSAKeyPairRef YMRSAKeyPairCreate()
{
    return YMRSAKeyPairCreateWithModuloSize(1024, 65537);
}

void _YMRSAKeyPairFree(YMRSAKeyPairRef keyPair)
{
    RSA_free(keyPair->rsa);
    free(keyPair);
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

bool YMRSAKeyPairGenerate(YMRSAKeyPairRef keyPair)
{
    // "OpenSSL makes sure that the PRNG state is unique for each thread.
    // On systems that provide /dev/urandom, the randomness device is used to seed the PRNG transparently."
    // leaving me unsure if this should be once'd. For now playing it safe.
    //pthread_once(&gYMRSAKeyPairSeedOnce, _YMRSAKeyPairSeed);
    _YMRSAKeyPairSeed();
    
    
#ifdef YM_DEBUG_INFO
    struct timeval then;
    int timeResult = gettimeofday(&then,NULL);
#endif
    unsigned long rsaErr = 0;
    const char* rsaErrFunc = "";
    
    BIGNUM *e = BN_new();
    if ( ! e )
    {
        rsaErrFunc = "BN_new";
        goto catch_return;
    }
    
    int result = BN_set_word(e, keyPair->publicE);
    if ( openssl_success != result )
    {
        rsaErrFunc = "BN_set_word";
        goto catch_return;
    }
    
    BN_set_word(e, keyPair->publicE);
    
    // os x man page doesn't actually state that 1 is success for _ex.
    result = RSA_generate_key_ex(keyPair->rsa, keyPair->moduloNBits, e	, NULL /*BN_GENCB *cb callback struct*/);
    if ( openssl_success != result )
    {
        rsaErrFunc = "RSA_generate_key_ex";
        goto catch_return;
    }
    
#ifdef YM_DEBUG_INFO
    struct timeval now;
    if ( timeResult == 0 )
    {
        timeResult = gettimeofday(&now, NULL);
        if ( timeResult == 0 )
            ymlog("rsa: it took %ld seconds to generate rsa keypair with %d modulo bits",now.tv_sec - then.tv_sec,keyPair->moduloNBits);
    }
    if ( timeResult != 0 )
        ymlog("rsa: gettimeofday failed");
#endif
    
catch_return:
    if ( result != openssl_success )
    {
        rsaErr = ERR_get_error();
        ymlog("rsa: %s failed: %lu (%s)", rsaErrFunc, rsaErr, ERR_error_string(rsaErr,NULL));
    }
    
    if ( e )
        BN_free(e);
    
    return (result == openssl_success);
}

void _YMRSAKeyPairSeed()
{
#ifdef YM_DEBUG_INFO
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
    
#ifdef YM_DEBUG_INFO
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
