//
//  CryptoTests.c
//  yammer
//
//  Created by david on 12/2/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "CryptoTests.h"

#include "YMRSAKeyPair.h"
#include "YMX509Certificate.h"

YM_EXTERN_C_PUSH

typedef struct CryptoTest
{
    ym_test_assert_func assert;
    const void *context;
} CryptoTest;

void _X509GenerationTestRun(struct CryptoTest *test);

void CryptoTestsRun(ym_test_assert_func assert, const void *context)
{
    struct CryptoTest theTest = { assert, context };
    
    _X509GenerationTestRun(&theTest);
    ymerr(" _X509GenerationTestRun completed");
}

void _X509GenerationTestRun(struct CryptoTest *theTest)
{    
    YMRSAKeyPairRef keyPair = YMRSAKeyPairCreate();
    testassert(YMRSAKeyPairGenerate(keyPair), "generate failed");
    YMX509CertificateRef cert = YMX509CertificateCreate(keyPair);
    testassert(cert, "cert creation failed");
    YMRelease(cert);
    YMRelease(keyPair);
    
    int bits = 4096;
    keyPair = YMRSAKeyPairCreateWithModuloSize(bits, 65537);
    testassert(YMRSAKeyPairGenerate(keyPair), "generate(%d) failed",bits);
    cert = YMX509CertificateCreate(keyPair);
    testassert(cert, "cert creation failed");
    YMRelease(cert);
    YMRelease(keyPair);
}

YM_EXTERN_C_POP
