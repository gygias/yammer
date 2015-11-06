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

BOOL        gPlexerTest1Running = YES;
#define     PlexerTest1ConcurrentRoundTrips 20
NSUInteger  gPlexerTest1AwaitingCloses = PlexerTest1ConcurrentRoundTrips;
BOOL        gPlexerTest2Running = YES;

typedef struct
{
    uint16_t length;
} UserMessageHeader;

@interface PlexerTests : XCTestCase

@end

PlexerTests *gRunningPlexerTest; // xctest seems to make a new object for each -test

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

const char *testMessage = "this is a test message. one, two, three. four. sometimes five.";
const char *testResponse = "もしもし。you are coming in loud and clear, rangoon! ご機嫌よ。";

- (void)testPlexerRoundTrip {
    gRunningPlexerTest = self;
    
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
    });
    
    dispatch_async(dispatch_get_global_queue(0, 0), ^{
        bool okay = YMPlexerStart(fakeRemotePlexer);
        XCTAssert(okay,@"slave did not start");
    });
    
    NSUInteger nSpawnConcurrentStreams = gPlexerTest1AwaitingCloses;
    while (nSpawnConcurrentStreams--)
    {
        dispatch_async(dispatch_get_global_queue(0, 0), ^{
            
            YMStreamRef aStream = YMPlexerCreateNewStream(localPlexer,__FUNCTION__,false);
            [self sendMessage:aStream :testMessage];
            
            void *response;
            uint16_t responseLen;
            [self receiveMessage:aStream :&response :&responseLen];
            
            int cmp = strcmp(response,testResponse);
            XCTAssert(cmp == 0, @"response: %@",response);
            
            bool localClose = YMPlexerCloseStream(localPlexer, aStream);
            XCTAssert(localClose, @"failed to close stream");
            
            
    #pragma message "what about a convenience for 'take this (new opaque 3rd-party file) and stream it automatically"
    #pragma message "todo could YMStream provide a convenience for not framing these sizes twice" \
                "like a 'write datagram of size' so that it could piggy-back off of the client's framing?"
        });
    }

//#define AND_MEASURE
#ifdef AND_MEASURE
    [self measureBlock:^{
#endif
        while ( ! gPlexerTest1Running )
            CFRunLoopRunInMode(kCFRunLoopDefaultMode,1,false);
#ifdef AND_MEASURE
    }];
#endif
}

- (void)sendMessage:(YMStreamRef)stream :(const char *)message
{
    uint16_t length = (uint16_t)strlen(message);
    [self sendMessage:stream :message :length];
}

- (void)sendMessage:(YMStreamRef)stream :(const void *)message :(uint16_t)length
{
    UserMessageHeader header = { length };
    bool okay = YMStreamWriteDown(stream, message, header.length);
    XCTAssert(okay,@"failed to write message length");
    okay = YMStreamWriteDown(stream, (void *)testMessage, (uint32_t)strlen(testMessage));
    XCTAssert(okay,@"failed to write message");
}

- (void)receiveMessage:(YMStreamRef)stream :(void **)outMessage :(uint16_t *)outLength
{
    UserMessageHeader header;
    bool okay = YMStreamReadUp(stream, &header, sizeof(header));
    XCTAssert(okay,@"failed to read header");
    uint8_t *buffer = malloc(header.length);
    okay = YMStreamReadUp(stream, buffer, header.length);
    XCTAssert(okay,@"failed to read buffer");
    
    *outMessage = buffer;
    *outLength = header.length;
}

void remote_plexer_interrupted(YMPlexerRef plexer)
{
    [gRunningPlexerTest interrupted:plexer];
}

- (void)interrupted:(YMPlexerRef)plexer
{
    NSLog(@"%s",__FUNCTION__);
}

void remote_plexer_new_stream(YMPlexerRef plexer, YMStreamRef stream)
{
    [gRunningPlexerTest newStream:plexer :stream];
}

- (void)newStream:(YMPlexerRef)plexer :(YMStreamRef)stream
{
    NSLog(@"%s",__FUNCTION__);
    char *message;
    uint16_t length;
    [self receiveMessage:stream :(void **)&message :&length];
    
    int cmp = strcmp(message, testMessage);
    XCTAssert(cmp == 0,@"received %s",message);
    
    bool badClose = YMPlexerCloseStream(plexer, stream);
    XCTAssert(!badClose,@"receiver allowed to close stream");
    
    [self sendMessage:stream :testResponse];
}

void remote_plexer_stream_closing(YMPlexerRef plexer, YMStreamRef stream)
{
    [gRunningPlexerTest closing:plexer :stream];
}

- (void)closing:(YMPlexerRef)plexer :(YMStreamRef)stream
{
    if ( gPlexerTest1AwaitingCloses-- )
    {
        NSLog(@"%s last stream closed, signaling runloop spin",__FUNCTION__);
        gPlexerTest1Running = NO;
    }
    
}

@end
