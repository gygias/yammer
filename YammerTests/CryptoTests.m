//
//  CryptoTests.m
//  yammer
//
//  Created by david on 11/9/15.
//  Copyright © 2015 Combobulated Software. All rights reserved.
//

#import "YammerTests.h"

#include "YMBase.h"

#include "YMRSAKeyPair.h"
#include "YMX509Certificate.h"

@interface CryptoTests : XCTestCase

@end

@implementation CryptoTests

- (void)setUp {
    [super setUp];
    // Put setup code here. This method is called before the invocation of each test method in the class.
}

- (void)tearDown {
    // Put teardown code here. This method is called after the invocation of each test method in the class.
    [super tearDown];
}

- (void)testExample {
    // This is an example of a functional test case.
    // Use XCTAssert and related functions to verify your tests produce the correct results.
    
    YMRSAKeyPairRef keyPair = YMRSAKeyPairCreate();
    XCTAssert(YMRSAKeyPairGenerate(keyPair), @"generate failed");
    YMX509CertificateRef cert = YMX509CertificateCreate(keyPair);
    XCTAssert(cert, @"cert creation failed");
    YMFree(cert);
    YMFree(keyPair);
    
    keyPair = YMRSAKeyPairCreateWithModuloSize(4096, 65537);
    XCTAssert(YMRSAKeyPairGenerate(keyPair), @"generate(4096) failed");
    cert = YMX509CertificateCreate(keyPair);
    XCTAssert(cert, @"cert creation failed");
    YMFree(cert);
    YMFree(keyPair);
}

- (void)testPerformanceExample {
    // This is an example of a performance test case.
    [self measureBlock:^{
        // Put the code you want to measure the time of here.
    }];
}

@end