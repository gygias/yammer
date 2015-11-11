//
//  TLSTests.m
//  yammer
//
//  Created by david on 11/10/15.
//  Copyright © 2015 Combobulated Software. All rights reserved.
//

#import "YammerTests.h"

#import "YMTLSProvider.h"
#import "YMPipe.h"
#import "YMUtilities.h"

@interface TLSTests : XCTestCase

@end

@implementation TLSTests

- (void)setUp {
    [super setUp];
    // Put setup code here. This method is called before the invocation of each test method in the class.
}

- (void)tearDown {
    // Put teardown code here. This method is called after the invocation of each test method in the class.
    [super tearDown];
}

- (void)testTLS1 {
    
    char *name = YMStringCreateWithFormat("%s-local",[[self className] UTF8String]);
    YMPipeRef localOutgoing = YMPipeCreate(name);
    free(name);
    name = YMStringCreateWithFormat("%s-remote",[[self className] UTF8String]);
    YMPipeRef remoteOutgoing = YMPipeCreate(name);
    free(name);
    
    // "output" and "input" get confusing with all these crossed files!
    int localOutput = YMPipeGetInputFile(localOutgoing);
    int remoteInput = YMPipeGetOutputFile(localOutgoing);
    int remoteOutput = YMPipeGetInputFile(remoteOutgoing);
    int localInput = YMPipeGetOutputFile(remoteOutgoing);
    
    NSLog(@"tls tests running with local:%d->%d, remote:%d<-%d",localOutput,remoteInput,localInput,remoteOutput);
    
    YMTLSProviderRef localProvider = YMTLSProviderCreate(localOutput, remoteInput);
    XCTAssert(localProvider,@"local provider didn't initialize");
    YMTLSProviderRef remoteProvider = YMTLSProviderCreate(remoteOutput, localInput);
    XCTAssert(remoteProvider,@"remote provider didn't initialize");
    
}

- (void)testPerformanceExample {
    // This is an example of a performance test case.
    [self measureBlock:^{
        // Put the code you want to measure the time of here.
    }];
}

@end
