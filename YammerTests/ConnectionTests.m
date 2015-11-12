//
//  ConnectionTests.m
//  yammer
//
//  Created by david on 11/11/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#import <XCTest/XCTest.h>

#import "YMLocalSocketPair.h"
#import "YMConnection.h"

@interface ConnectionTests : XCTestCase

@end

@implementation ConnectionTests

- (void)setUp {
    [super setUp];
    // Put setup code here. This method is called before the invocation of each test method in the class.
}

- (void)tearDown {
    // Put teardown code here. This method is called after the invocation of each test method in the class.
    [super tearDown];
}

- (void)testConnection {
    
    YMLocalSocketPairRef socketPair = YMLocalSocketPairCreate("connection-test");
    
    //YMConnectionRef connectionA = YMConnectionCreate(<#YMAddressRef address#>, <#YMConnectionType type#>, <#YMConnectionSecurityType securityType#>)
    
}

- (void)testPerformanceExample {
    // This is an example of a performance test case.
    [self measureBlock:^{
        // Put the code you want to measure the time of here.
    }];
}

@end
