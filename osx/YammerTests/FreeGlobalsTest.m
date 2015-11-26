//
//  FreeGlobalsTest.m
//  yammer
//
//  Created by david on 11/21/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#import "YammerTests.h"

@interface FreeGlobalsTest : XCTestCase

@end

@implementation FreeGlobalsTest

- (void)setUp {
    [super setUp];
    // Put setup code here. This method is called before the invocation of each test method in the class.
}

- (void)tearDown {
    // Put teardown code here. This method is called after the invocation of each test method in the class.
    [super tearDown];
}

- (void)testFreeGlobals {
    YMFreeGlobalResources();
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 2, false);
}

@end
