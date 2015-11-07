//
//  PlexerTests.m
//  yammer
//
//  Created by david on 11/4/15.
//  Copyright © 2015 Combobulated Software. All rights reserved.
//

#import <XCTest/XCTest.h>

#import "YammerTestUtilities.h"

#include "YMPipe.h"
#include "YMPlexer.h"
#include "YMSecurityProvider.h"

BOOL        gPlexerTest1Running = YES;
#pragma message "will crash in realloc"
#define     PlexerTest1ConcurrentRoundTrips 1
NSUInteger  gPlexerTest1AwaitingCloses = PlexerTest1ConcurrentRoundTrips;

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

- (void)testManyPlexerRoundTrips {
    gRunningPlexerTest = self;
    
    YMPipeRef networkSimPipeIn = YMPipeCreate("test-network-sim-pipe-in");
    int writeToRemote = YMPipeGetInputFile(networkSimPipeIn);
    int readFromLocal = YMPipeGetOutputFile(networkSimPipeIn);
    YMPipeRef networkSimPipeOut = YMPipeCreate("test-network-sim-pipe-out");
    int writeToLocal = YMPipeGetInputFile(networkSimPipeOut);
    int readFromRemote = YMPipeGetOutputFile(networkSimPipeOut);
    
    bool localIsMaster = (bool)arc4random_uniform(2);
    NSLog(@"TEST CASE L(%s)-i%d-o%d <-> i%d-o%d R(%s)",localIsMaster?"M":"S",readFromRemote,writeToRemote,readFromLocal,writeToLocal,localIsMaster?"S":"M");
    
    YMPlexerRef localPlexer = YMPlexerCreate("L",readFromRemote,writeToRemote,localIsMaster);
    YMPlexerSetSecurityProvider(localPlexer, YMSecurityProviderCreate(readFromRemote,writeToRemote));
    YMPlexerSetInterruptedFunc(localPlexer, local_plexer_interrupted);
    YMPlexerSetNewIncomingStreamFunc(localPlexer, local_plexer_new_stream);
    YMPlexerSetStreamClosingFunc(localPlexer, local_plexer_stream_closing);
    
    YMPlexerRef fakeRemotePlexer = YMPlexerCreate("R",readFromLocal,writeToLocal,!localIsMaster);
    YMPlexerSetSecurityProvider(fakeRemotePlexer, YMSecurityProviderCreate(readFromLocal,writeToLocal));
    YMPlexerSetInterruptedFunc(fakeRemotePlexer, remote_plexer_interrupted);
    YMPlexerSetNewIncomingStreamFunc(fakeRemotePlexer, remote_plexer_new_stream);
    YMPlexerSetStreamClosingFunc(fakeRemotePlexer, remote_plexer_stream_closing);
    
    dispatch_async(dispatch_get_global_queue(0, 0), ^{
        bool okay = YMPlexerStart(localPlexer);
        XCTAssert(okay,@"master did not start");
    });
    
    dispatch_sync(dispatch_get_global_queue(0, 0), ^{
        bool okay = YMPlexerStart(fakeRemotePlexer);
        XCTAssert(okay,@"slave did not start");
    });
    
    NSUInteger nSpawnConcurrentStreams = gPlexerTest1AwaitingCloses;
    while (nSpawnConcurrentStreams--)
    {
        const char *caller = __FUNCTION__;
        dispatch_async(dispatch_get_global_queue(0, 0), ^{
            
            NSLog(@"VVV LOCAL CREATING STREAM VVV");
            YMStreamRef aStream = YMPlexerCreateNewStream(localPlexer,caller,false);
            NSLog(@"^^^ LOCAL CREATING STREAM ^^^");
            
            NSLog(@"VVV LOCAL WRITING A USER MESSAGE VVV");
            [self sendMessage:aStream :testMessage];
            NSLog(@"^^^ LOCAL WRITING A USER MESSAGE ^^^");
            
//            void *response;
//            uint16_t responseLen;
//            NSLog(@"VVV MASTER READING A USER MESSAGE VVV");
//            [self receiveMessage:aStream :&response :&responseLen];
//            NSLog(@"^^^ MASTER READING A USER MESSAGE ^^^");
//            
//            int cmp = strcmp(response,testResponse);
//            XCTAssert(cmp == 0, @"response: %@",response);
            
            NSLog(@"VVV LOCAL CLOSING STREAM VVV");
            YMPlexerCloseStream(localPlexer, aStream);
            NSLog(@"^^^ LOCAL CLOSING STREAM ^^^");
            
#pragma message "THE THING YOU SPENT THE LAST 12 HOURS ON IS THAT SIGNAL BEFORE WAIT DOESN'T RELEASE WAIT"
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(1 * NSEC_PER_SEC)), dispatch_get_global_queue(0, 0), ^{
                NSLog(@" YOLO SWAGG");
                YMPlexerCloseStream(localPlexer, aStream);
            });
            
            
    #pragma message "what about a convenience for 'take this (new opaque 3rd-party file) and stream it automatically"
    #pragma message "todo could YMStream provide a convenience for not framing these sizes twice" \
                "like a 'write datagram of size' so that it could piggy-back off of the client's framing?"
        });
    }

//#define AND_MEASURE
#ifdef AND_MEASURE
    [self measureBlock:^{
#endif
        while ( gPlexerTest1Running )
            CFRunLoopRunInMode(kCFRunLoopDefaultMode,0.5,false);
#ifdef AND_MEASURE
    }];
#endif
}

- (void)sendMessage:(YMStreamRef)stream :(const char *)message
{
    uint16_t length = (uint16_t)strlen(message);
    [self sendMessage:stream :message :length];
}

- (void)sendMessage:(YMStreamRef)stream:(const void *)message :(uint16_t)length
{
    UserMessageHeader header = { length };
    YMStreamWriteDown(stream, (void *)&header, sizeof(header));
    //XCTAssert(okay,@"failed to write message length");
    YMStreamWriteDown(stream, (void *)testMessage, (uint32_t)strlen(testMessage));
    //XCTAssert(okay,@"failed to write message");
}

- (void)receiveMessage:(YMStreamRef)stream :(void **)outMessage :(uint16_t *)outLength
{
    UserMessageHeader header;
    YMStreamReadUp(stream, &header, sizeof(header));
    //XCTAssert(okay,@"failed to read header");
    uint8_t *buffer = malloc(header.length);
    YMStreamReadUp(stream, buffer, header.length);
    //XCTAssert(okay,@"failed to read buffer");
    
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
    NSLog(@"VVV REMOTE receiving message VVV");
    [self receiveMessage:stream :(void **)&message :&length];
    NSLog(@"^^^ REMOTE receiving message ^^^");
    
    int cmp = strcmp(message, testMessage);
    XCTAssert(cmp == 0,@"received %s",message);
    
    NSLog(@"VVV REMOTE going rogue VVV");
    YMPlexerCloseStream(plexer, stream);
    //XCTAssert(!badClose,@"receiver allowed to close stream");
    NSLog(@"^^^ REMOTE going rogue ^^^");
    
    NSLog(@"VVV SLAVE receiving message VVV");
    [self sendMessage:stream :testResponse];
    NSLog(@"^^^ REMOTE receiving message ^^^");
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
