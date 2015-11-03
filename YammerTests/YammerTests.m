//
//  YammerTests.m
//  YammerTests
//
//  Created by david on 11/3/15.
//  Copyright © 2015 Combobulated Software. All rights reserved.
//

#import <XCTest/XCTest.h>

// for now, cuz nothing wraps it
#import "YMmDNSService.h"

@interface YammerTests : XCTestCase

@end

@implementation YammerTests

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
    
    YMmDNSServiceRef service = YMmDNSServiceCreate("_yammer._tcp", "lol", 5050);
    
    int nPairs = 2;
    YMmDNSTxtRecordKeyPair *keyPairs[nPairs];
    keyPairs[0] = calloc(nPairs,sizeof(YMmDNSTxtRecordKeyPair));
    keyPairs[0]->key = "test-key";
    keyPairs[0]->value = "test-value";
    keyPairs[0]->valueLen = strlen(keyPairs[0]->value);
    keyPairs[1] = calloc(nPairs,sizeof(YMmDNSTxtRecordKeyPair));
    keyPairs[1]->key = "test-key-blob";
    uint64_t blob = 0x012456789ABCDEF;
    keyPairs[1]->valueLen = sizeof(blob);
    
    YMmDNSServiceSetTXTRecord(service, keyPairs, nPairs);
    YMmDNSServiceStart(service);
    
    sleep(60);
    
    // browser for service here..
}

- (void)testPerformanceExample {
    // This is an example of a performance test case.
    [self measureBlock:^{
        // Put the code you want to measure the time of here.
    }];
}

@end
