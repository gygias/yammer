//
//  PlexerTests.m
//  yammer
//
//  Created by david on 11/4/15.
//  Copyright © 2015 Combobulated Software. All rights reserved.
//

#import <XCTest/XCTest.h>

#include "YMPipe.h"
#include "YMPlexer.h"

@interface PlexerTests : XCTestCase

@end

@implementation PlexerTests

- (void)setUp {
    [super setUp];
    // Put setup code here. This method is called before the invocation of each test method in the class.
}

- (void)tearDown {
    // Put teardown code here. This method is called after the invocation of each test method in the class.
    [super tearDown];
}

- (void)testPlexer {
    
    YMPipeRef networkSimPipe = YMPipeCreate("test-network-sim-pipe");
    
}

- (void)testPerformanceExample {
    // This is an example of a performance test case.
    [self measureBlock:^{
        // Put the code you want to measure the time of here.
    }];
}

@end
