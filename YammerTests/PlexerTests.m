//
//  PlexerTests.m
//  yammer
//
//  Created by david on 11/4/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#import <XCTest/XCTest.h>

#import "YammerTestUtilities.h"

#include "YMPipe.h"
#include "YMPlexer.h"
#include "YMSecurityProvider.h"
#include "YMStreamPriv.h"

BOOL        gPlexerTest1Running = YES;
#define     PlexerTest1ConcurrentRoundTrips 1
#define     PlexerTest1RoundTripsPerThread 1
#define     PlexerTest1StreamClosuresToObserve ( PlexerTest1ConcurrentRoundTrips * PlexerTest1RoundTripsPerThread )
NSUInteger  gPlexerTest1AwaitingCloses = PlexerTest1StreamClosuresToObserve;

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
        dispatch_async(dispatch_get_global_queue(0, 0), ^{
            [self doLocalTest1:localPlexer];
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

- (void)doLocalTest1:(YMPlexerRef)plexer
{
    for ( unsigned idx = 0; idx < PlexerTest1RoundTripsPerThread; idx++ )
    {
        NSLog(@"VVV LOCAL creating stream VVV");
        YMStreamRef aStream = YMPlexerCreateNewStream(plexer,__FUNCTION__,false);
        NSLog(@"^^^ LOCAL %u created stream ^^^",_YMStreamGetUserInfo(aStream)->streamID);
        
        NSLog(@"VVV LOCAL %u sending message %u VVV",_YMStreamGetUserInfo(aStream)->streamID,idx);
        [self sendMessage:aStream :testMessage];
        NSLog(@"^^^ LOCAL %u sent message %u ^^^",_YMStreamGetUserInfo(aStream)->streamID,idx);
        
        void *response;
        uint16_t responseLen;
        NSLog(@"VVV LOCAL %u receiving response %u VVV",_YMStreamGetUserInfo(aStream)->streamID,idx);
        [self receiveMessage:aStream :&response :&responseLen];
        NSLog(@"^^^ LOCAL %u received response %u ^^^",_YMStreamGetUserInfo(aStream)->streamID,idx);
        
        int cmp = strcmp(response,testResponse);
        XCTAssert(cmp == 0, @"response: %@",response);
        
        NSLog(@"VVV LOCAL %u closing stream VVV",_YMStreamGetUserInfo(aStream)->streamID);
        YMPlexerCloseStream(plexer, aStream);
        NSLog(@"^^^ LOCAL %u closing stream ^^^",_YMStreamGetUserInfo(aStream)->streamID);
    }
}

- (void)sendMessage:(YMStreamRef)stream :(const char *)message
{
    uint16_t length = (uint16_t)strlen(message) + 1;
    [self sendMessage:stream :message :length];
}

- (void)sendMessage:(YMStreamRef)stream :(const void *)message :(uint16_t)length
{
    UserMessageHeader header = { length };
    YMStreamWriteDown(stream, (void *)&header, sizeof(header));
    //XCTAssert(okay,@"failed to write message length");
    YMStreamWriteDown(stream, (void *)testMessage, length);
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
    dispatch_async(dispatch_get_global_queue(0, 0), ^{
        [self handleANewLocalStream:plexer :stream];
    });
}

- (void)handleANewLocalStream:(YMPlexerRef)plexer :(YMStreamRef)stream
{
    for ( unsigned idx = 0; idx < PlexerTest1RoundTripsPerThread; idx++ )
    {
        NSLog(@"%s",__FUNCTION__);
        char *inMessage;
        uint16_t length;
        NSLog(@"VVV REMOTE %u receiving message %u VVV",_YMStreamGetUserInfo(stream)->streamID,idx);
        [self receiveMessage:stream :(void **)&inMessage :&length];
        NSLog(@"^^^ REMOTE %u received message %u ^^^",_YMStreamGetUserInfo(stream)->streamID,idx);
        
        int cmp = strcmp(inMessage, testMessage);
        XCTAssert(cmp == 0,@"received %s",inMessage);
        
        // made a hard user error
        //            NSLog(@"VVV REMOTE %u going rogue VVV",_YMStreamGetUserInfo(stream)->streamID);
        //            YMPlexerCloseStream(plexer, stream);
        //            //XCTAssert(!badClose,@"receiver allowed to close stream");
        //            NSLog(@"^^^ REMOTE %u going rogue ^^^",_YMStreamGetUserInfo(stream)->streamID);
        
        NSLog(@"VVV REMOTE %u sending response %u VVV",_YMStreamGetUserInfo(stream)->streamID,idx);
        [self sendMessage:stream :testResponse];
        NSLog(@"^^^ REMOTE %u sent response %u ^^^",_YMStreamGetUserInfo(stream)->streamID,idx);
    }
}

void remote_plexer_stream_closing(YMPlexerRef plexer, YMStreamRef stream)
{
    [gRunningPlexerTest closing:plexer :stream];
}

- (void)closing:(YMPlexerRef)plexer :(YMStreamRef)stream
{
    NSLog(@"%s: gPlexerTest1AwaitingCloses %lu",__FUNCTION__,(unsigned long)gPlexerTest1AwaitingCloses);
    if ( --gPlexerTest1AwaitingCloses == 0 )
    {
        NSLog(@"%s last stream closed, signaling runloop spin",__FUNCTION__);
        gPlexerTest1Running = NO;
    }
    
}

@end
