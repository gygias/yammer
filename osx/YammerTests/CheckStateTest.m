//
//  CheckStateTest.m
//  yammer
//
//  Created by david on 11/21/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#import <XCTest/XCTest.h>

#import "Tests.h"

@interface CheckStateTest : XCTestCase

@end

@implementation CheckStateTest

- (void)setUp {
    [super setUp];
    // Put setup code here. This method is called before the invocation of each test method in the class.
}

- (void)tearDown {
    // Put teardown code here. This method is called after the invocation of each test method in the class.
    [super tearDown];
}

- (void)testCheckStateTest {
    YMFreeGlobalResources();
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 2, false);
    [[NSTask launchedTaskWithLaunchPath:@"/usr/bin/sample" arguments:@[@"-file",@"/dev/stdout",@"xctest",@"1",@"1000"]] waitUntilExit];
}

@end
