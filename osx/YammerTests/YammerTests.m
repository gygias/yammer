//
//  YammerTests.m
//  yammer
//
//  Created by david on 12/2/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#import <XCTest/XCTest.h>

#import "DictionaryTests.h"

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

void ym_test_assert_proc(const void *ctx, bool exp, const char *fmt, ...)
{
    if ( ! exp )
    {
        va_list args;
        va_start(args,fmt);
        vprintf(fmt, args);
        va_end(args);
        
        YammerTests *SELF = (__bridge YammerTests *)(ctx);
        [SELF ymTestAssert];
    }
}

- (void)ymTestAssert
{
    XCTAssert(false,"custom assert, see stdout");
}

- (void)testDictionary {
    DictionaryTestRun(ym_test_assert_proc, (__bridge const void *)(self));
}

- (void)testPerformanceExample {
    // This is an example of a performance test case.
    [self measureBlock:^{
        // Put the code you want to measure the time of here.
    }];
}

@end
