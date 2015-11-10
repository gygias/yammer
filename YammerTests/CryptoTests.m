//
//  CryptoTests.m
//  yammer
//
//  Created by david on 11/9/15.
//  Copyright © 2015 Combobulated Software. All rights reserved.
//

#import <XCTest/XCTest.h>

#include "YMBase.h"

#include "YMRSAKeyPair.h"

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
    YMFree(keyPair);
    
    keyPair = YMRSAKeyPairCreateWithModuloSize(4096, 65537);
    XCTAssert(YMRSAKeyPairGenerate(keyPair), @"generate(4096) failed");
    YMFree(keyPair);
}

- (void)testPerformanceExample {
    // This is an example of a performance test case.
    [self measureBlock:^{
        // Put the code you want to measure the time of here.
    }];
}

@end
