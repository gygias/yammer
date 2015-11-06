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
#include "YMSecurityProvider.h"

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

void local_plexer_interrupted(YMPlexerRef plexer)
{
    NSLog(@"%s",__FUNCTION__);
}

void local_plexer_new_stream(YMPlexerRef plexer, YMStreamRef stream)
{
    NSLog(@"%s",__FUNCTION__);
}

void local_plexer_stream_closing(YMPlexerRef plexer, YMStreamRef stream)
{
    NSLog(@"%s",__FUNCTION__);
}

- (void)testPlexer {
    YMPipeRef networkSimPipeIn = YMPipeCreate("test-network-sim-pipe-in");
    int localWrite = YMPipeGetInputFile(networkSimPipeIn);
    int remoteRead = YMPipeGetOutputFile(networkSimPipeIn);
    YMPipeRef networkSimPipeOut = YMPipeCreate("test-network-sim-pipe-out");
    int remoteWrite = YMPipeGetInputFile(networkSimPipeOut);
    int localRead = YMPipeGetOutputFile(networkSimPipeOut);
    
    YMPlexerRef localPlexer = YMPlexerCreate(localWrite,localRead,true);
    YMPlexerSetSecurityProvider(localPlexer, YMSecurityProviderCreate(localWrite,localRead));
    YMPlexerSetInterruptedFunc(localPlexer, local_plexer_interrupted);
    YMPlexerSetNewIncomingStreamFunc(localPlexer, local_plexer_new_stream);
    YMPlexerSetStreamClosingFunc(localPlexer, local_plexer_stream_closing);
    
    YMPlexerRef fakeRemotePlexer = YMPlexerCreate(remoteWrite,remoteRead,false);
    YMPlexerSetSecurityProvider(fakeRemotePlexer, YMSecurityProviderCreate(remoteWrite,remoteRead));
    YMPlexerSetInterruptedFunc(fakeRemotePlexer, remote_plexer_interrupted);
    YMPlexerSetNewIncomingStreamFunc(fakeRemotePlexer, remote_plexer_new_stream);
    YMPlexerSetStreamClosingFunc(fakeRemotePlexer, remote_plexer_stream_closing);
    
    dispatch_async(dispatch_get_global_queue(0, 0), ^{
        bool okay = YMPlexerStart(localPlexer);
        XCTAssert(okay,@"master did not start");
        
        YMStreamRef aStream = YMPlexerCreateNewStream(localPlexer,"test stream 1",false);
        char *testMessage1 = "this is a test message. one, two, three. four. sometimes five.";
        uint32_t testMessage1Length = (uint32_t)strlen(testMessage1);
        okay = YMStreamWriteDown(aStream, (void *)&testMessage1Length, sizeof(testMessage1Length));
        XCTAssert(okay,@"failed to write message length");
        okay = YMStreamWriteDown(aStream, (void *)testMessage1, (uint32_t)strlen(testMessage1));
        XCTAssert(okay,@"failed to write message");
#pragma message "todo could YMStream provide a convenience for not framing these sizes twice" \
            "like a 'write datagram of size' so that it could piggy-back off of the client's framing?"
        
        YMLog("wrote user message");
    });
    dispatch_async(dispatch_get_global_queue(0, 0), ^{
        bool okay = YMPlexerStart(fakeRemotePlexer);
        XCTAssert(okay,@"slave did not start");
    });
    
    CFRunLoopRunInMode(kCFRunLoopDefaultMode,5*60,false);
}

void remote_plexer_interrupted(YMPlexerRef plexer)
{
    NSLog(@"%s",__FUNCTION__);
}

void remote_plexer_new_stream(YMPlexerRef plexer, YMStreamRef stream)
{
    NSLog(@"%s",__FUNCTION__);
    uint16_t messageLength;
    bool okay = YMStreamReadUp(stream, (void *)&messageLength, sizeof(messageLength));
    if ( ! okay ) YMLog("failed to read message length");
    char message[messageLength];
    okay = YMStreamReadUp(stream, (void *)message, messageLength);
    if ( ! okay ) YMLog("failed to read message");
}

void remote_plexer_stream_closing(YMPlexerRef plexer, YMStreamRef stream)
{
    NSLog(@"%s",__FUNCTION__);
}

//- (void)testPerformanceExample {
    //// This is an example of a performance test case.
    //[self measureBlock:^{
    //    // Put the code you want to measure the time of here.
    //}];
//}

@end
