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
#include "YMPlexerPriv.h"
#include "YMLock.h"

#define     PlexerTest1Threads 8
#define     PlexerTest1NewStreamPerRoundTrip true
#define     PlexerTest1RoundTripsPerThread 1

#define PlexerTest1TimeBased
//#define PlexerTest1Indefinite
#ifdef PlexerTest1Indefinite
#define PlexerTest1EndDate ([NSDate distantFuture])
#else
#define PlexerTest1EndDate ([NSDate dateWithTimeIntervalSinceNow:10])
#endif

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
    
    // for comparing callback contexts
    YMPlexerRef localPlexer;
    YMPlexerRef fakeRemotePlexer;
    
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
    plexerTest1Lock = YMLockCreateWithOptionsAndName(YMInternalLockType, YMSTRCF("%s",[[self className] UTF8String],NULL));
    awaitingClosures = PlexerTest1StreamClosuresToObserve;
    self.continueAfterFailure = NO;
    streamsCompleted = 0;
    lastMessageWrittenByStreamID = [NSMutableDictionary dictionary];
}

- (void)tearDown {
    // Put teardown code here. This method is called after the invocation of each test method in the class.
    [super tearDown];
}

void local_plexer_interrupted(YMPlexerRef plexer, void *context)
{
    NSLog(@"%s",__FUNCTION__);
    [gRunningPlexerTest localInterrupted:plexer :context];
}

- (void)localInterrupted:(YMPlexerRef)plexer :(void *)context
{
    XCTAssert(plexer==localPlexer,@"localInterrupted not local");
    XCTAssert(context==(__bridge void *)gRunningPlexerTest,@"localInterrupted context doesn't match");
}

void local_plexer_new_stream(YMPlexerRef plexer, YMStreamRef stream, void *context)
{
    NSLog(@"%s",__FUNCTION__);
    [gRunningPlexerTest localNewStream:plexer :stream :context];
}

- (void)localNewStream:(YMPlexerRef)plexer :(YMStreamRef)stream :(void *)context
{
    XCTAssert(plexer==localPlexer,@"localNewStream not local");
    XCTAssert(stream,@"localNewStream null");
    XCTAssert(context==(__bridge void *)gRunningPlexerTest,@"localNewStream context doesn't match");
    XCTAssert(NO,@"localNewStream");
}

void local_plexer_stream_closing(YMPlexerRef plexer, YMStreamRef stream, void *context)
{
    NSLog(@"%s",__FUNCTION__);
    [gRunningPlexerTest localStreamClosing:plexer :stream :context];
}

- (void)localStreamClosing:(YMPlexerRef)plexer :(YMStreamRef)stream :(void *)context
{
    XCTAssert(plexer==localPlexer,@"localStreamClosing not local");
    XCTAssert(stream,@"localStreamClosing null");
    XCTAssert(context==(__bridge void *)gRunningPlexerTest,@"localStreamClosing context doesn't match");
    XCTAssert(NO,@"localStreamClosing");
}

const char *testLocalMessage = "this is a test message. one, two, three. four. sometimes five.";
const char *testRemoteResponse = "もしもし。you are coming in loud and clear, rangoon! ご機嫌よ。";

- (void)testManyPlexerRoundTrips {
    
    gRunningPlexerTest = self;
    
    YMStringRef aName = YMSTRC("test-network-sim-pipe-in");
    YMPipeRef networkSimPipeIn = YMPipeCreate(aName);
    YMRelease(aName);
    int writeToRemote = YMPipeGetInputFile(networkSimPipeIn);
    int readFromLocal = YMPipeGetOutputFile(networkSimPipeIn);
    aName = YMSTRC("test-network-sim-pipe-out");
    YMPipeRef networkSimPipeOut = YMPipeCreate(aName);
    YMRelease(aName);
    int writeToLocal = YMPipeGetInputFile(networkSimPipeOut);
    int readFromRemote = YMPipeGetOutputFile(networkSimPipeOut);
    
    BOOL localIsMaster = (BOOL)arc4random_uniform(2);
    NSLog(@"plexer test using pipes: L(%s)-i%d-o%d <-> i%d-o%d R(%s)",localIsMaster?"M":"S",readFromRemote,writeToRemote,readFromLocal,writeToLocal,localIsMaster?"S":"M");
    NSLog(@"plexer test using %u threads, %u trips per thread, %@ streams per thread, %@ messages",PlexerTest1Threads,PlexerTest1RoundTripsPerThread,PlexerTest1NewStreamPerRoundTrip?@"new":@"one",PlexerTest1RandomMessages?@"random":@"fixed");
    
    localPlexer = YMPlexerCreate(YMSTRC("L"),readFromRemote,writeToRemote,localIsMaster);
    YMPlexerSetSecurityProvider(localPlexer, YMSecurityProviderCreate(readFromRemote,writeToRemote));
    YMPlexerSetInterruptedFunc(localPlexer, local_plexer_interrupted);
    YMPlexerSetNewIncomingStreamFunc(localPlexer, local_plexer_new_stream);
    YMPlexerSetStreamClosingFunc(localPlexer, local_plexer_stream_closing);
    YMPlexerSetCallbackContext(localPlexer, (__bridge void *)(self));
    
    fakeRemotePlexer = YMPlexerCreate(YMSTRC("R"),readFromLocal,writeToLocal,!localIsMaster);
    YMPlexerSetSecurityProvider(fakeRemotePlexer, YMSecurityProviderCreate(readFromLocal,writeToLocal));
    YMPlexerSetInterruptedFunc(fakeRemotePlexer, remote_plexer_interrupted);
    YMPlexerSetNewIncomingStreamFunc(fakeRemotePlexer, remote_plexer_new_stream);
    YMPlexerSetStreamClosingFunc(fakeRemotePlexer, remote_plexer_stream_closing);
    YMPlexerSetCallbackContext(fakeRemotePlexer, (__bridge void *)(self));
    
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
    YMPlexerStop(localPlexer);
    YMPlexerStop(fakeRemotePlexer);
    gTimeBasedEnd = YES;
    // join on time based fallout, check fds we know about are closed
    NSLog(@"plexer test finished %zu incoming round-trips on %d threads (%d round-trips per %s)",incomingStreamRoundTrips,
          PlexerTest1Threads,
          PlexerTest1RoundTripsPerThread,
          PlexerTest1NewStreamPerRoundTrip?"stream":"round-trip");
}

- (void)doLocalTest1:(YMPlexerRef)plexer
{
    YMStreamRef aStream = NULL;
    unsigned idx = 0;
#ifdef PlexerTest1TimeBased
    while ( ! gTimeBasedEnd )
#else
    for ( ; idx < PlexerTest1RoundTripsPerThread; )
#endif
    {
        idx++;
        YMPlexerStreamID streamID;
        if ( ! aStream || PlexerTest1NewStreamPerRoundTrip )
        {
            aStream = YMPlexerCreateNewStream(plexer,YMSTRC(__FUNCTION__));
            streamID = ((ym_plexer_stream_user_info_ref)_YMStreamGetUserInfo(aStream))->streamID;
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
    uint16_t outLength = 0, length = sizeof(header);
    YMIOResult result = YMStreamReadUp(stream, &header, length, &outLength);
    XCTAssert(result==YMIOSuccess,@"failed to read header");
    XCTAssert(outLength==length,@"outLength!=length");
    XCTAssert(header.length>0,@"header.length<=0");
    uint8_t *buffer = malloc(header.length);
    outLength = 0; length = header.length;
    result = YMStreamReadUp(stream, buffer, length, &outLength);
    XCTAssert(outLength==length,@"outLength!=length");
    XCTAssert(result==YMIOSuccess,@"failed to read buffer");
    
    return [NSData dataWithBytesNoCopy:buffer length:header.length freeWhenDone:YES];
}

void remote_plexer_interrupted(__unused YMPlexerRef plexer, void *context)
{
    [gRunningPlexerTest remoteInterrupted:plexer :context];
}

- (void)remoteInterrupted:(YMPlexerRef)plexer :(void *)context
{
    XCTAssert(plexer==fakeRemotePlexer,@"remoteInterrupted not remote");
    XCTAssert(context==(__bridge void *)gRunningPlexerTest,@"remoteInterrupted context doesn't match");
    NSLog(@"%s",__FUNCTION__);
}

void remote_plexer_new_stream(YMPlexerRef plexer, YMStreamRef stream, void *context)
{
    [gRunningPlexerTest remoteNewStream:plexer :stream :context];
}

- (void)remoteNewStream:(YMPlexerRef)plexer :(YMStreamRef)stream :(void *)context
{    
    TestLog(@"%s",__FUNCTION__);
    XCTAssert(plexer==fakeRemotePlexer,@"remoteNewStream not remote");
    XCTAssert(stream,@"remoteNewStream null");
    XCTAssert(context==(__bridge void *)gRunningPlexerTest,@"remoteNewStream context doesn't match");
    dispatch_async(dispatch_get_global_queue(0, 0), ^{
        [self handleANewRemoteStream:plexer :stream];
    });
}

- (void)handleANewRemoteStream:(YMPlexerRef)plexer :(YMStreamRef)stream
{
    YMPlexerStreamID streamID = ((ym_plexer_stream_user_info_ref)_YMStreamGetUserInfo(stream))->streamID;
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
}

void remote_plexer_stream_closing(YMPlexerRef plexer, YMStreamRef stream, void *context)
{
    [gRunningPlexerTest remoteClosing:plexer :stream :context];
}

- (void)remoteClosing:(YMPlexerRef)plexer :(YMStreamRef)stream :(void *)context
{
    XCTAssert(plexer==fakeRemotePlexer,@"remoteClosing not local");
    XCTAssert(stream,@"remoteClosing null");
    XCTAssert(context==(__bridge void *)gRunningPlexerTest,@"remoteClosing context doesn't match");
    
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
