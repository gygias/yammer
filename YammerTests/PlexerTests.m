//
//  PlexerTests.m
//  yammer
//
//  Created by david on 11/4/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#import "YammerTests.h"

#include "YMPipe.h"
#include "YMPlexer.h"
#include "YMSecurityProvider.h"
#include "YMStreamPriv.h"
#include "YMLock.h"

#define     PlexerTest1RoundTripThreads 16
#define     PlexerTest1RoundTripsPerThread 500
#define     PlexerTest1NewStreamPerRoundTrip true
#define     PlexerTest1RandomMessages true // todo
#define     PlexerTest1RandomMessageMaxLength 1024
#define     PlexerTest1StreamClosuresToObserve ( PlexerTest1RoundTripThreads * ( PlexerTest1NewStreamPerRoundTrip ? PlexerTest1RoundTripsPerThread : 1 ) )

typedef struct
{
    uint16_t length;
} UserMessageHeader;

@interface PlexerTests : XCTestCase
{
    NSUInteger incomingStreamRoundTrips;
    YMLockRef plexerTest1Lock;
    BOOL plexerTest1Running;
    
    NSUInteger awaitingClosures;
}

@end

PlexerTests *gRunningPlexerTest; // xctest seems to make a new object for each -test

@implementation PlexerTests

- (void)setUp {
    [super setUp];
    
    plexerTest1Running = YES;
    plexerTest1Lock = YMLockCreateWithOptionsAndName(YMLockDefault, [[self className] UTF8String]);
    awaitingClosures = PlexerTest1StreamClosuresToObserve;
    self.continueAfterFailure = NO;
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

const char *testLocalMessage = "this is a test message. one, two, three. four. sometimes five.";
const char *testRemoteResponse = "もしもし。you are coming in loud and clear, rangoon! ご機嫌よ。";

- (void)testManyPlexerRoundTrips {
    
    gRunningPlexerTest = self;
    
    YMPipeRef networkSimPipeIn = YMPipeCreate("test-network-sim-pipe-in");
    int writeToRemote = YMPipeGetInputFile(networkSimPipeIn);
    int readFromLocal = YMPipeGetOutputFile(networkSimPipeIn);
    YMPipeRef networkSimPipeOut = YMPipeCreate("test-network-sim-pipe-out");
    int writeToLocal = YMPipeGetInputFile(networkSimPipeOut);
    int readFromRemote = YMPipeGetOutputFile(networkSimPipeOut);
    
    bool localIsMaster = (bool)arc4random_uniform(2);
    NSLog(@"plexer test using pipes: L(%s)-i%d-o%d <-> i%d-o%d R(%s)",localIsMaster?"M":"S",readFromRemote,writeToRemote,readFromLocal,writeToLocal,localIsMaster?"S":"M");
    
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
    
    NSUInteger nSpawnConcurrentStreams = PlexerTest1RoundTripThreads;
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
        while ( plexerTest1Running )
            CFRunLoopRunInMode(kCFRunLoopDefaultMode,0.5,false);
#ifdef AND_MEASURE
    }];
#endif
    
    NSLog(@"plexer test finished %zu incoming round-trips on %d threads (%d round-trips per %s)",incomingStreamRoundTrips,
          PlexerTest1RoundTripThreads,
          PlexerTest1RoundTripsPerThread,
          PlexerTest1NewStreamPerRoundTrip?"stream":"round-trip");
}

- (void)doLocalTest1:(YMPlexerRef)plexer
{
    YMStreamRef aStream = NULL;
    for ( unsigned idx = 0; idx < PlexerTest1RoundTripsPerThread; idx++ )
    {
        YMStreamID streamID;
        if ( ! aStream || PlexerTest1NewStreamPerRoundTrip )
        {
            TestLog(@"VVV LOCAL creating stream VVV");
            aStream = YMPlexerCreateNewStream(plexer,__FUNCTION__,false);
            streamID = _YMStreamGetUserInfo(aStream)->streamID;
            TestLog(@"^^^ LOCAL s%u created stream ^^^",streamID);
        }
        
        uint16_t messageLen;
        const void *message;
        if ( PlexerTest1RandomMessages )
        {
            NSData *randomMessage = YMRandomDataWithMaxLength(PlexerTest1RandomMessageMaxLength);
            message = [randomMessage bytes];
            messageLen = [randomMessage length];
        }
        else
        {
            message = testLocalMessage;
            messageLen = strlen(testLocalMessage) + 1;
        }
        
        TestLog(@"VVV LOCAL s%u sending message #%u VVV",streamID,idx);
        [self sendMessage:aStream :message :messageLen];
        TestLog(@"^^^ LOCAL s%u sent message #%u ^^^",streamID,idx);
        
        const void *response;
        uint16_t responseLen;
        TestLog(@"VVV LOCAL s%u receiving response #%u VVV",streamID,idx);
        [self receiveMessage:aStream :(void **)&response :&responseLen];
        TestLog(@"^^^ LOCAL s%u received response #%u ^^^",streamID,idx);
        
        if ( ! PlexerTest1RandomMessages )
        {
            int cmp = strcmp(response,testRemoteResponse);
            XCTAssert(cmp == 0, @"response: %s",response);
        }
        else
        {
            // todo way to comp these
            TestLog(@"--- LOCAL s%u received random response %ub",streamID,responseLen);
        }
        
        if ( PlexerTest1NewStreamPerRoundTrip )
        {
            TestLog(@"VVV LOCAL s%u closing stream VVV",streamID);
            YMPlexerCloseStream(plexer, aStream);
            TestLog(@"^^^ LOCAL s%u closing stream ^^^",streamID);
        }
    }
    if ( ! PlexerTest1NewStreamPerRoundTrip )
        YMPlexerCloseStream(plexer, aStream);
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
    YMStreamWriteDown(stream, (void *)message, length);
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
    TestLog(@"%s",__FUNCTION__);
    dispatch_async(dispatch_get_global_queue(0, 0), ^{
        [self handleANewLocalStream:plexer :stream];
    });
}

- (void)handleANewLocalStream:(YMPlexerRef)plexer :(YMStreamRef)stream
{
    YMStreamID streamID = _YMStreamGetUserInfo(stream)->streamID;
    TestLog(@"VVV REMOTE -newStream[%u] entered",streamID);
    
    unsigned iterations = PlexerTest1NewStreamPerRoundTrip ? 1 : PlexerTest1RoundTripsPerThread;
    for ( unsigned idx = 0; idx < iterations; idx++ )
    {
        uint16_t messageLen;
        void *message;
        
        TestLog(@"VVV REMOTE s%u receiving message m#%u VVV",streamID,idx);
        [self receiveMessage:stream :&message :&messageLen];
        TestLog(@"^^^ REMOTE s%u received message m#%u ^^^",streamID,idx);
        
        if ( ! PlexerTest1RandomMessages )
        {
            int cmp = strcmp(message,testLocalMessage);
            XCTAssert(cmp == 0, @"response: %s",(char *)message);
        }
        else
        {
            // todo way to comp these?
            TestLog(@"--- REMOTE s%u received random message %ub",streamID,messageLen);
        }
        
        if ( PlexerTest1RandomMessages )
        {
            NSData *randomMessage = YMRandomDataWithMaxLength(PlexerTest1RandomMessageMaxLength);
            message = (void *)[randomMessage bytes];
            messageLen = [randomMessage length];
        }
        else
        {
            message = (void *)testRemoteResponse;
            messageLen = strlen(testRemoteResponse) + 1;
        }
        
        TestLog(@"VVV REMOTE s%u sending response m#%u VVV",streamID,idx);
        [self sendMessage:stream :message :messageLen];
        TestLog(@"^^^ REMOTE s%u sent response m#%u ^^^",streamID,idx);
        
        incomingStreamRoundTrips++;
    }
    
    TestLog(@"^^^ REMOTE -newStream [%u] exiting (and remoteReleasing)",streamID);
    YMPlexerRemoteStreamRelease(plexer, stream);
}

void remote_plexer_stream_closing(YMPlexerRef plexer, YMStreamRef stream)
{
    [gRunningPlexerTest closing:plexer :stream];
}

- (void)closing:(YMPlexerRef)plexer :(YMStreamRef)stream
{
    NSUInteger last;
    YMLockLock(plexerTest1Lock);
    last = awaitingClosures--;
    YMLockUnlock(plexerTest1Lock);
    TestLog(@"%s: *********** gPlexerTest1AwaitingCloses %zu->%zu!! *****************",__FUNCTION__,last,awaitingClosures);
    if ( last - 1 == 0 )
    {
        NSLog(@"%s last stream closed, signaling exit",__FUNCTION__);
        plexerTest1Running = NO;
    }
    
}

@end
