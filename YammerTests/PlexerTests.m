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
#include "YMLock.h"

YMLockRef   gPlexerTest1Lock = NULL;
BOOL        gPlexerTest1Running = YES;
#define     PlexerTest1RoundTripThreads 1 // SATURDAY
#define     PlexerTest1RoundTripsPerThread 10000
#define     PlexerTest1NewStreamPerRoundTrip false // SATURDAY
#define     PlexerTest1StreamClosuresToObserve ( PlexerTest1RoundTripThreads * ( PlexerTest1NewStreamPerRoundTrip ? PlexerTest1RoundTripsPerThread : 1 ) )
NSUInteger  gPlexerTest1AwaitingCloses = PlexerTest1StreamClosuresToObserve;

typedef struct
{
    uint16_t length;
} UserMessageHeader;

@interface PlexerTests : XCTestCase

@end

PlexerTests *gRunningPlexerTest; // xctest seems to make a new object for each -test

@implementation PlexerTests

+ (void)initialize
{
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        gPlexerTest1Lock = YMLockCreateWithOptionsAndName(YMLockDefault, [[self className] UTF8String]);
    });
}

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
        while ( gPlexerTest1Running )
            CFRunLoopRunInMode(kCFRunLoopDefaultMode,0.5,false);
#ifdef AND_MEASURE
    }];
#endif
}

- (void)doLocalTest1:(YMPlexerRef)plexer
{
    YMStreamRef aStream = NULL;
    if ( ! PlexerTest1NewStreamPerRoundTrip )
        aStream = YMPlexerCreateNewStream(plexer,__FUNCTION__,false);
    for ( unsigned idx = 0; idx < PlexerTest1RoundTripsPerThread; idx++ )
    {
        if ( PlexerTest1NewStreamPerRoundTrip )
        {
            NSLog(@"VVV LOCAL creating stream VVV");
            aStream = YMPlexerCreateNewStream(plexer,__FUNCTION__,false);
            NSLog(@"^^^ LOCAL s%u created stream ^^^",_YMStreamGetUserInfo(aStream)->streamID);
        }
        
        NSLog(@"VVV LOCAL s%u sending message #%u VVV",_YMStreamGetUserInfo(aStream)->streamID,idx);
        [self sendMessage:aStream :testLocalMessage];
        NSLog(@"^^^ LOCAL s%u sent message #%u ^^^",_YMStreamGetUserInfo(aStream)->streamID,idx);
        
        char *response;
        uint16_t responseLen;
        NSLog(@"VVV LOCAL s%u receiving response #%u VVV",_YMStreamGetUserInfo(aStream)->streamID,idx);
        [self receiveMessage:aStream :(void **)&response :&responseLen];
        NSLog(@"^^^ LOCAL s%u received response #%u ^^^",_YMStreamGetUserInfo(aStream)->streamID,idx);
        
        int cmp = strcmp(response,testRemoteResponse);
        XCTAssert(cmp == 0, @"response: %s",response);
        
        if ( PlexerTest1NewStreamPerRoundTrip )
        {
            NSLog(@"VVV LOCAL s%u closing stream VVV",_YMStreamGetUserInfo(aStream)->streamID);
            YMPlexerCloseStream(plexer, aStream);
            NSLog(@"^^^ LOCAL s%u closing stream ^^^",_YMStreamGetUserInfo(aStream)->streamID);
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
    NSLog(@"%s",__FUNCTION__);
    dispatch_async(dispatch_get_global_queue(0, 0), ^{
        [self handleANewLocalStream:plexer :stream];
    });
}

- (void)handleANewLocalStream:(YMPlexerRef)plexer :(YMStreamRef)stream
{
    NSLog(@"%s",__FUNCTION__);
    char *inMessage;
    uint16_t length;
    
    for ( unsigned idx = 0; idx < PlexerTest1RoundTripsPerThread; idx++ )
    {
        NSLog(@"VVV REMOTE s%u receiving message m#%u VVV",_YMStreamGetUserInfo(stream)->streamID,idx);
        [self receiveMessage:stream :(void **)&inMessage :&length];
        NSLog(@"^^^ REMOTE s%u received message m#%u ^^^",_YMStreamGetUserInfo(stream)->streamID,idx);
        
        int cmp = strcmp(inMessage, testLocalMessage);
        XCTAssert(cmp == 0,@"received %s",inMessage);
        
        NSLog(@"VVV REMOTE s%u sending response m#%u VVV",_YMStreamGetUserInfo(stream)->streamID,idx);
        [self sendMessage:stream :testRemoteResponse];
        NSLog(@"^^^ REMOTE s%u sent response m#%u ^^^",_YMStreamGetUserInfo(stream)->streamID,idx);
    }
}

void remote_plexer_stream_closing(YMPlexerRef plexer, YMStreamRef stream)
{
    [gRunningPlexerTest closing:plexer :stream];
}

- (void)closing:(YMPlexerRef)plexer :(YMStreamRef)stream
{
    int last;
    YMLockLock(gPlexerTest1Lock);
    last = gPlexerTest1AwaitingCloses--;
    YMLockUnlock(gPlexerTest1Lock);
    NSLog(@"%s: *********** gPlexerTest1AwaitingCloses %u->%u!! *****************",__FUNCTION__,last,gPlexerTest1AwaitingCloses);
    NSLog(@"%s: *********** gPlexerTest1AwaitingCloses %u->%u!! *****************",__FUNCTION__,last,gPlexerTest1AwaitingCloses);
    NSLog(@"%s: *********** gPlexerTest1AwaitingCloses %u->%u!! *****************",__FUNCTION__,last,gPlexerTest1AwaitingCloses);
    NSLog(@"%s: *********** gPlexerTest1AwaitingCloses %u->%u!! *****************",__FUNCTION__,last,gPlexerTest1AwaitingCloses);
    NSLog(@"%s: *********** gPlexerTest1AwaitingCloses %u->%u!! *****************",__FUNCTION__,last,gPlexerTest1AwaitingCloses);
    NSLog(@"%s: *********** gPlexerTest1AwaitingCloses %u->%u!! *****************",__FUNCTION__,last,gPlexerTest1AwaitingCloses);
    NSLog(@"%s: *********** gPlexerTest1AwaitingCloses %u->%u!! *****************",__FUNCTION__,last,gPlexerTest1AwaitingCloses);
    NSLog(@"%s: *********** gPlexerTest1AwaitingCloses %u->%u!! *****************",__FUNCTION__,last,gPlexerTest1AwaitingCloses);
    if ( last - 1 == 0 )
    {
        NSLog(@"%s last stream closed, signaling runloop spin",__FUNCTION__);
        gPlexerTest1Running = NO;
    }
    
}

@end
