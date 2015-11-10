//
//  PlexerTests.m
//  yammer
//
//  Created by david on 11/4/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#import "YammerTests.h"

#include "YMBase.h"
#include "YMPipe.h"
#include "YMPlexer.h"
#include "YMSecurityProvider.h"
#include "YMStreamPriv.h"
#include "YMLock.h"

#define     PlexerTest1Threads 8
#define     PlexerTest1NewStreamPerRoundTrip true
#define     PlexerTest1RoundTripsPerThread 50

#define PlexerTest1TimeBased
#define PlexerTest1EndDate ([NSDate distantFuture])
BOOL    gTimeBasedEnd = NO;

#define     PlexerTest1RandomMessages true // todo
#define     PlexerTest1RandomMessageMaxLength 1024
#define     PlexerTest1StreamClosuresToObserve ( PlexerTest1Threads * ( PlexerTest1NewStreamPerRoundTrip ? PlexerTest1RoundTripsPerThread : 1 ) )

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
    NSUInteger streamsCompleted;
    NSUInteger bytesIn, bytesOut;
    
    NSMutableDictionary *lastMessageWrittenByStreamID;
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
    streamsCompleted = 0;
    lastMessageWrittenByStreamID = [NSMutableDictionary dictionary];
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
    
    BOOL localIsMaster = (BOOL)arc4random_uniform(2);
    NSLog(@"plexer test using pipes: L(%s)-i%d-o%d <-> i%d-o%d R(%s)",localIsMaster?"M":"S",readFromRemote,writeToRemote,readFromLocal,writeToLocal,localIsMaster?"S":"M");
    NSLog(@"plexer test using %u threads, %u trips per thread, %@ rounds per thread, %@ messages",PlexerTest1Threads,PlexerTest1RoundTripsPerThread,PlexerTest1NewStreamPerRoundTrip?@"new":@"one",PlexerTest1RandomMessages?@"random":@"fixed");
    
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
    
    NSUInteger nSpawnConcurrentStreams = PlexerTest1Threads;
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
#ifndef PlexerTest1TimeBased
        while ( plexerTest1Running )
            CFRunLoopRunInMode(kCFRunLoopDefaultMode,0.5,false);
#else
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, [PlexerTest1EndDate timeIntervalSinceDate:[NSDate date]], false);
#endif
#ifdef AND_MEASURE
    }];
#endif
    gTimeBasedEnd = YES;
    NSLog(@"plexer test finished %zu incoming round-trips on %d threads (%d round-trips per %s)",incomingStreamRoundTrips,
          PlexerTest1Threads,
          PlexerTest1RoundTripsPerThread,
          PlexerTest1NewStreamPerRoundTrip?"stream":"round-trip");
}

- (void)doLocalTest1:(YMPlexerRef)plexer
{
    YMStreamRef aStream = NULL;
#ifdef PlexerTest1TimeBased
    while ( ! gTimeBasedEnd )
#else
    for ( unsigned idx = 0; idx < PlexerTest1RoundTripsPerThread; idx++ )
#endif
    {
        YMStreamID streamID;
        if ( ! aStream || PlexerTest1NewStreamPerRoundTrip )
        {
            aStream = YMPlexerCreateNewStream(plexer,__FUNCTION__,false);
            streamID = _YMStreamGetUserInfo(aStream)->streamID;
        }
        
        NSData *outgoingMessage;
        if ( PlexerTest1RandomMessages )
            outgoingMessage = YMRandomDataWithMaxLength(PlexerTest1RandomMessageMaxLength);
        else
            outgoingMessage = [NSData dataWithBytesNoCopy:(void *)testLocalMessage length:strlen(testLocalMessage) + 1];
        
        BOOL protectTheList = ( PlexerTest1Threads > 1 );
        if ( protectTheList )
            YMLockLock(plexerTest1Lock);
        [lastMessageWrittenByStreamID setObject:outgoingMessage forKey:@(streamID)];
        if ( protectTheList )
            YMLockUnlock(plexerTest1Lock);
        [self sendMessage:aStream :outgoingMessage];
        
        NSData *incomingMessage = [self receiveMessage:aStream];
        if ( protectTheList )
            YMLockLock(plexerTest1Lock);
        NSData *lastMessageWritten = [lastMessageWrittenByStreamID objectForKey:@(streamID)];
        if ( protectTheList )
            YMLockUnlock(plexerTest1Lock);
        XCTAssert([incomingMessage length]&&[lastMessageWritten length]&&[incomingMessage isEqualToData:lastMessageWritten],@"incoming and last written do not match (i%zu o%zu)",[incomingMessage length],[lastMessageWritten length]);
        
        if ( PlexerTest1NewStreamPerRoundTrip )
            YMPlexerCloseStream(plexer, aStream);
        
        YMLockLock(plexerTest1Lock);
        bytesOut += [outgoingMessage length];
        bytesIn += [incomingMessage length];
        YMLockUnlock(plexerTest1Lock);
    }
    if ( ! PlexerTest1NewStreamPerRoundTrip )
        YMPlexerCloseStream(plexer, aStream);
}

- (void)sendMessage:(YMStreamRef)stream :(NSData *)message
{
    UserMessageHeader header = { [message length] };
    YMStreamWriteDown(stream, (void *)&header, sizeof(header));
    //XCTAssert(okay,@"failed to write message length");
    YMStreamWriteDown(stream, [message bytes], [message length]);
    //XCTAssert(okay,@"failed to write message");
}

- (NSData *)receiveMessage:(YMStreamRef)stream
{
    UserMessageHeader header;
    YMStreamReadUp(stream, &header, sizeof(header));
    //XCTAssert(okay,@"failed to read header");
    uint8_t *buffer = malloc(header.length);
    YMStreamReadUp(stream, buffer, header.length);
    //XCTAssert(okay,@"failed to read buffer");
    
    return [NSData dataWithBytesNoCopy:buffer length:header.length freeWhenDone:YES];
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
    BOOL protectTheList = ( PlexerTest1Threads > 1 );
    
    unsigned iterations = PlexerTest1NewStreamPerRoundTrip ? 1 : PlexerTest1RoundTripsPerThread;
    for ( unsigned idx = 0; idx < iterations; idx++ )
    {
        NSData *incomingMessage = [self receiveMessage:stream];
        
        if ( protectTheList )
            YMLockLock(plexerTest1Lock);
        NSData *lastMessageWritten = [lastMessageWrittenByStreamID objectForKey:@(streamID)];
        if ( protectTheList )
            YMLockUnlock(plexerTest1Lock);
        
        XCTAssert([incomingMessage length]&&[lastMessageWritten length]&&[incomingMessage isEqualToData:lastMessageWritten],@"incoming and last written do not match (i%zu o%zu)",[incomingMessage length],[lastMessageWritten length]);
        
        NSData *outgoingMessage;
        if ( PlexerTest1RandomMessages )
            outgoingMessage = YMRandomDataWithMaxLength(PlexerTest1RandomMessageMaxLength);
        else
            outgoingMessage = [NSData dataWithBytesNoCopy:(void*)testRemoteResponse length:strlen(testRemoteResponse) + 1];
        
        if ( protectTheList )
            YMLockLock(plexerTest1Lock);
        [lastMessageWrittenByStreamID setObject:outgoingMessage forKey:@(streamID)];
        if ( protectTheList )
            YMLockUnlock(plexerTest1Lock);
        [self sendMessage:stream :outgoingMessage];
        
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
    YMLockLock(plexerTest1Lock);
#ifdef PlexerTest1TimeBased
    streamsCompleted++;
#else
    NSUInteger last = awaitingClosures--;
#endif
    YMLockUnlock(plexerTest1Lock);
    TestLog(@"%s: *********** gPlexerTest1AwaitingCloses %zu->%zu!! *****************",__FUNCTION__,last,awaitingClosures);
#ifdef PlexerTest1TimeBased
    if ( streamsCompleted % 10000 == 0 )
    {
        NSLog(@"handled %zuth stream, approx %zumb in, %zumb out",streamsCompleted,bytesIn/1024/1024,bytesOut/1024/1024);
    }
#else
    if ( last - 1 == 0 )
    {
        NSLog(@"%s last stream closed, signaling exit",__FUNCTION__);
        plexerTest1Running = NO;
    }
#endif
    
}

@end
