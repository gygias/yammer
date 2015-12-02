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

typedef struct CryptoTest
{
    ym_test_assert_func assertFunc;
    const void *funcContext;
} CryptoTest;

void _X509GenerationTestRun(struct CryptoTest *test);

void CryptoTestRun(ym_test_assert_func assertFunc, const void *funcContext)
{
    struct CryptoTest theTest = { assertFunc, funcContext };
    
    _X509GenerationTestRun(&theTest);
}

void _X509GenerationTestRun(struct CryptoTest *theTest)
{
    // This is an example of a functional test case.
    // Use XCTAssert and related functions to verify your tests produce the correct results.
    
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
