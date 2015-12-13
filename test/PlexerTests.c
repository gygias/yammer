//
//  PlexerTests.c
//  yammer
//
//  Created by david on 12/2/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "PlexerTests.h"

#include "YMPlexer.h"
#include "YMPlexerPriv.h"
#include "YMLocalSocketPair.h"
#include "YMStreamPriv.h"
#include "YMPipe.h"
#include "YMPipePriv.h"
#include "YMThread.h"
#include "YMLock.h"
#include "YMSemaphore.h"
#include "YMDictionary.h"

YM_EXTERN_C_PUSH

#define     PlexerTest1Threads 8
#define     PlexerTest1NewStreamPerRoundTrip true
#define     PlexerTest1RoundTripsPerThread 8

#define PlexerTest1TimeBased true
//#define PlexerTest1Indefinite
#ifdef PlexerTest1Indefinite
#define PlexerTest1EndDate (LONG_MAX)
#else
#define PlexerTest1EndDate (time(NULL) + 15)
#endif

#define     PlexerTest1RandomMessages true // todo
#define     PlexerTest1RandomMessageMaxLength 2048
#define     PlexerTest1StreamClosuresToObserve ( PlexerTest1Threads * ( PlexerTest1NewStreamPerRoundTrip ? PlexerTest1RoundTripsPerThread : 1 ) )

const char *testLocalMessage = "this is a test message. one, two, three. four. sometimes five.";
const char *testRemoteResponse = "もしもし。you are coming in loud and clear, rangoon! ご機嫌よ。";

typedef struct PlexerTest
{
    ym_test_assert_func assert;
    const void *context;
    
    uint64_t incomingStreamRoundTrips;
    YMLockRef plexerTest1Lock;
    bool plexerTest1Running;
    bool timeBasedTimeOver;
    bool awaitingInterrupt;
    YMThreadRef dispatchThread;
    
    // for comparing callback contexts
    YMPlexerRef localPlexer;
    YMPlexerRef fakeRemotePlexer;
    YMPlexerRef closedPlexer;
    YMSemaphoreRef interruptNotificationSem;
    
    uint64_t awaitingClosures;
    uint64_t streamsCompleted;
    uint64_t bytesIn;
    uint64_t bytesOut;
    
    YMDictionaryRef lastMessageWrittenByStreamID;
} PlexerTest;

void _DoManyRoundTripsTest(struct PlexerTest *);
void YM_CALLING_CONVENTION _init_local_plexer_proc(ym_thread_dispatch_ref dispatch);
void YM_CALLING_CONVENTION _handle_remote_stream(ym_thread_dispatch_ref ctx_);
YM_THREAD_RETURN YM_CALLING_CONVENTION _RunLocalPlexer(YM_THREAD_PARAM);
void _SendMessage(struct PlexerTest *theTest, YMStreamRef stream, uint8_t *message, uint16_t length);
uint8_t *_ReceiveMessage(struct PlexerTest *theTest, YMStreamRef stream, uint16_t *outLen);

void local_plexer_interrupted(YMPlexerRef plexer, void *context);
void local_plexer_new_stream(YMPlexerRef plexer, YMStreamRef stream, void *context);
void local_plexer_stream_closing(YMPlexerRef plexer, YMStreamRef stream, void *context);
void remote_plexer_interrupted(__unused YMPlexerRef plexer, void *context);
void remote_plexer_new_stream(YMPlexerRef plexer, YMStreamRef stream, void *context);
void remote_plexer_stream_closing(YMPlexerRef plexer, YMStreamRef stream, void *context);

void PlexerTestsRun(ym_test_assert_func assert, const void *context)
{
    struct PlexerTest theTest = { assert, context,
                                0, YMLockCreate(), true, false, false,
                                YMThreadDispatchCreate(NULL),
                                NULL, NULL, NULL, YMSemaphoreCreate(0),
                                0, 0, 0, 0, YMDictionaryCreate() };
	YMThreadStart(theTest.dispatchThread);
    
    _DoManyRoundTripsTest(&theTest);
    
    sleep(2); // or add a thread exit semaphore
    
    ymerr(" PlexerRoundTripsTest finished with %llub in %llub out",theTest.bytesIn,theTest.bytesOut);
    
    YMRelease(theTest.plexerTest1Lock);
    YMRelease(theTest.interruptNotificationSem);
    
    while ( YMDictionaryGetCount(theTest.lastMessageWrittenByStreamID) > 0 )
    {
        void *message = YMDictionaryRemove(theTest.lastMessageWrittenByStreamID, YMDictionaryGetRandomKey(theTest.lastMessageWrittenByStreamID));
        free(message);
    }
    YMRelease(theTest.lastMessageWrittenByStreamID);
    
    YMThreadDispatchJoin(theTest.dispatchThread);
    YMRelease(theTest.dispatchThread);
}

void _DoManyRoundTripsTest(struct PlexerTest *theTest)
{
    YM_IO_BOILERPLATE
    
    YMLocalSocketPairRef socketPair = YMLocalSocketPairCreate(NULL, false);
    YMSOCKET socketA = YMLocalSocketPairGetA(socketPair);
    YMSOCKET socketB = YMLocalSocketPairGetB(socketPair);
    
    bool localIsMaster = arc4random_uniform(2);
    ymlog("plexer test using pipes: L(%s)-s%d <-> s%d R(%s)",localIsMaster?"M":"S",socketA,socketB,localIsMaster?"S":"M");
    ymlog("plexer test using %u threads, %u trips per thread, %s streams per thread, %s messages",PlexerTest1Threads,PlexerTest1RoundTripsPerThread,PlexerTest1NewStreamPerRoundTrip?"new":"one",PlexerTest1RandomMessages?"random":"fixed");
    
    YMStringRef name = YMSTRC("L");
    YMSecurityProviderRef noSecurity = YMSecurityProviderCreateWithSocket(socketA);
    theTest->localPlexer = YMPlexerCreate(name,noSecurity,localIsMaster);
    YMRelease(noSecurity);
    YMRelease(name);
    YMPlexerSetInterruptedFunc(theTest->localPlexer, local_plexer_interrupted);
    YMPlexerSetNewIncomingStreamFunc(theTest->localPlexer, local_plexer_new_stream);
    YMPlexerSetStreamClosingFunc(theTest->localPlexer, local_plexer_stream_closing);
    YMPlexerSetCallbackContext(theTest->localPlexer, theTest);
    
    name = YMSTRC("R");
    noSecurity = YMSecurityProviderCreateWithSocket(socketB);
    theTest->fakeRemotePlexer = YMPlexerCreate(name,noSecurity,!localIsMaster);
    YMRelease(noSecurity);
    YMRelease(name);
    YMPlexerSetInterruptedFunc(theTest->fakeRemotePlexer, remote_plexer_interrupted);
    YMPlexerSetNewIncomingStreamFunc(theTest->fakeRemotePlexer, remote_plexer_new_stream);
    YMPlexerSetStreamClosingFunc(theTest->fakeRemotePlexer, remote_plexer_stream_closing);
    YMPlexerSetCallbackContext(theTest->fakeRemotePlexer, theTest);
    
    struct ym_thread_dispatch_t dispatch = { _init_local_plexer_proc, NULL, false, theTest, NULL };
    YMThreadDispatchDispatch(theTest->dispatchThread, dispatch);
    
    bool okay = YMPlexerStart(theTest->fakeRemotePlexer);
    testassert(okay,"slave did not start");
    
    int nSpawnConcurrentStreams = PlexerTest1Threads;
    while (nSpawnConcurrentStreams--)
    {
        YMThreadRef aThread = YMThreadCreate(NULL, _RunLocalPlexer, theTest);
        YMRetain(theTest->localPlexer);
        YMRetain(theTest->plexerTest1Lock);
        YMRetain(theTest->lastMessageWrittenByStreamID);
        YMThreadStart(aThread);
        YMRelease(aThread);
    }
    
    if ( ! PlexerTest1TimeBased )
    {
        while ( theTest->plexerTest1Running )
			sleep(1);
	}
    else
    {
        time_t startTime = time(NULL);
        sleep((unsigned int)(PlexerTest1EndDate - startTime));
    }
    
    theTest->timeBasedTimeOver = true;
    if ( arc4random_uniform(2) )
    {
        ymlog("closing local");
        theTest->closedPlexer = theTest->localPlexer;
    }
    else
    {
        ymlog("closing remote");
        theTest->closedPlexer = theTest->fakeRemotePlexer;
    }
    theTest->awaitingInterrupt = true;
    
    // this used to be done with 2 YMPipes, and the 'interruption' was simulated by releasing (and close-in-dealloc'ing) those pipes.
    // this was problematic because if a service thread wasn't within a provider read/write, the fd would quickly get recycled
    // into something else (generally a new incoming stream on one side), and the service thread would get deadlocked on a valid
    // but different fd.
    YMLocalSocketPairShutdown(socketPair);
    
    YMSemaphoreWait(theTest->interruptNotificationSem);
    YMSemaphoreWait(theTest->interruptNotificationSem);
    
    YMRelease(theTest->localPlexer);
    YMRelease(theTest->fakeRemotePlexer);
    
    sleep(2); // let the system settle 3.0 (let threads exit before stack theTest goes out of scope without coordination)
    ymlog("plexer test finished %llu incoming round-trips on %d threads (%d round-trips per %s)",theTest->incomingStreamRoundTrips,
          PlexerTest1Threads,
          PlexerTest1RoundTripsPerThread,
          PlexerTest1NewStreamPerRoundTrip?"stream":"round-trip");
}

void YM_CALLING_CONVENTION _init_local_plexer_proc(ym_thread_dispatch_ref dispatch)
{
    struct PlexerTest *theTest = dispatch->context;
    bool okay = YMPlexerStart(theTest->localPlexer);
    testassert(okay,"master did not start");
}

YM_THREAD_RETURN YM_CALLING_CONVENTION _RunLocalPlexer(YM_THREAD_PARAM ctx_)
{
    struct PlexerTest *theTest = ctx_;
    YMPlexerRef plexer = theTest->localPlexer;
    
    YMStreamRef aStream = NULL;
    unsigned idx = 0;
    size_t staticMessageLen = strlen(testLocalMessage);
#ifdef PlexerTest1TimeBased
    while ( ! theTest->timeBasedTimeOver )
#else
    for ( ; idx < PlexerTest1RoundTripsPerThread; )
#endif
    {
        idx++;
        YMPlexerStreamID streamID;
        if ( ! aStream || PlexerTest1NewStreamPerRoundTrip )
        {
            YMStringRef name = YMSTRC(__FUNCTION__);
            aStream = YMPlexerCreateStream(plexer,name);
            YMRelease(name);
            streamID = ((ym_plexer_stream_user_info_ref)_YMStreamGetUserInfo(aStream))->streamID;
        }
        
        uint8_t *outgoingMessage;
        uint16_t outgoingMessageLen;
        if ( PlexerTest1RandomMessages )
            outgoingMessage = YMRandomDataWithMaxLength(PlexerTest1RandomMessageMaxLength, &outgoingMessageLen);
        else
        {
            outgoingMessage = (uint8_t *)testLocalMessage;
            outgoingMessageLen = (uint16_t)staticMessageLen + 1;
        }
        
        bool protectTheList = ( PlexerTest1Threads > 1 );
        if ( protectTheList )
            YMLockLock(theTest->plexerTest1Lock);
        if ( YMDictionaryContains(theTest->lastMessageWrittenByStreamID, (YMDictionaryKey)streamID) )
        {
            void *old = YMDictionaryRemove(theTest->lastMessageWrittenByStreamID, (YMDictionaryKey)streamID);
            free(old);
        }
        YMDictionaryAdd(theTest->lastMessageWrittenByStreamID, (YMDictionaryKey)streamID, outgoingMessage);
        if ( protectTheList )
            YMLockUnlock(theTest->plexerTest1Lock);
        
        _SendMessage(theTest, aStream, outgoingMessage, outgoingMessageLen);
        
        uint16_t incomingMessageLen;
        uint8_t *incomingMessage = _ReceiveMessage(theTest, aStream, &incomingMessageLen);
        if ( theTest->timeBasedTimeOver )
        {
            YMPlexerCloseStream(plexer, aStream);
            free(incomingMessage);
            goto catch_release;
        }
        testassert(incomingMessage, "incoming message");
        
        if ( protectTheList )
            YMLockLock(theTest->plexerTest1Lock);
        uint8_t *lastMessageWritten = (uint8_t *)YMDictionaryRemove(theTest->lastMessageWrittenByStreamID, (YMDictionaryKey)streamID);
        if ( protectTheList )
            YMLockUnlock(theTest->plexerTest1Lock);
        testassert(incomingMessageLen&&0==memcmp(incomingMessage,lastMessageWritten,incomingMessageLen),"incoming and last written do not match (i%llu ?)",incomingMessageLen);
        free(incomingMessage);
        
        if ( PlexerTest1NewStreamPerRoundTrip )
            YMPlexerCloseStream(plexer, aStream);
        
        if ( PlexerTest1NewStreamPerRoundTrip && lastMessageWritten )
            free(lastMessageWritten);
        
        YMLockLock(theTest->plexerTest1Lock);
        theTest->bytesOut += outgoingMessageLen;
        theTest->bytesIn += incomingMessageLen;
        YMLockUnlock(theTest->plexerTest1Lock);
    }

    if ( ! PlexerTest1NewStreamPerRoundTrip )
        YMPlexerCloseStream(plexer, aStream);
    
catch_release:
    YMRelease(plexer);
    YMRelease(theTest->plexerTest1Lock);
    YMRelease(theTest->lastMessageWrittenByStreamID);

	YM_THREAD_END
}


void _SendMessage(__unused struct PlexerTest *theTest, YMStreamRef stream, uint8_t *message, uint16_t length)
{
    YMStreamWriteDown(stream, (void *)&length, sizeof(length));
    //XCTAssert(okay,@"failed to write message length");
    YMStreamWriteDown(stream, message, length);
    //XCTAssert(okay,@"failed to write message");
}

uint8_t *_ReceiveMessage(struct PlexerTest *theTest, YMStreamRef stream, uint16_t *outLen)
{
    uint16_t length, outLength = 0;
    YMIOResult result = YMStreamReadUp(stream, &length, sizeof(length), &outLength);
    if ( theTest->timeBasedTimeOver )
        return NULL;
    testassert(result==YMIOSuccess,"failed to read header");
    testassert(outLength==sizeof(length),"outLength!=length");
    testassert(length>0,"header.length<=0");
    uint8_t *buffer = malloc(length);
    outLength = 0;
    result = YMStreamReadUp(stream, buffer, length, &outLength);
    if ( theTest->timeBasedTimeOver )
    {
        free(buffer);
        return NULL;
    }
    testassert(outLength==length,"outLength!=length");
    testassert(result==YMIOSuccess,"failed to read buffer");
    
    *outLen = length;
    return buffer;
}

void local_plexer_interrupted(YMPlexerRef plexer, void *context)
{
    ymerr("%s",__FUNCTION__);
    struct PlexerTest *theTest = context;
    testassert(theTest,"local interrupt context");
    testassert(plexer==theTest->localPlexer,"localInterrupted not local");
    
    YMSemaphoreSignal(theTest->interruptNotificationSem);
}

void local_plexer_new_stream(YMPlexerRef plexer, YMStreamRef stream, void *context)
{
    ymlog("%s",__FUNCTION__);
    struct PlexerTest *theTest = context;
    testassert(theTest,"local new context");
    testassert(plexer==theTest->localPlexer,"localNewStream not local");
    testassert(stream,"localNewStream null");
    testassert(false,"localNewStream");
    
    YMPlexerCloseStream(plexer,stream);
}

void local_plexer_stream_closing(YMPlexerRef plexer, YMStreamRef stream, void *context)
{
    ymlog("%s",__FUNCTION__);
    struct PlexerTest *theTest = context;
    testassert(theTest,"local new context");
    testassert(plexer==theTest->localPlexer,"localStreamClosing not local");
    testassert(stream,"localStreamClosing null");
    testassert(false,"localStreamClosing");
    
    YMPlexerCloseStream(plexer, stream);
}

void remote_plexer_interrupted(__unused YMPlexerRef plexer, void *context)
{
    ymerr("%s",__FUNCTION__);
    struct PlexerTest *theTest = context;
    testassert(theTest,"remote interrupt context");
    testassert(plexer==theTest->fakeRemotePlexer,"remote interrupted not remote");
    
    YMSemaphoreSignal(theTest->interruptNotificationSem);
}

typedef struct HandleStreamContext
{
    struct PlexerTest *theTest;
    YMStreamRef stream;
} HandleStreamContext;

void remote_plexer_new_stream(YMPlexerRef plexer, YMStreamRef stream, void *context)
{
    NoisyTestLog("%s",__FUNCTION__);
    struct PlexerTest *theTest = context;
    testassert(theTest,"remote new context");
    testassert(plexer==theTest->fakeRemotePlexer,"remoteNewStream not remote");
    testassert(stream,"remoteNewStream null");
    
    struct HandleStreamContext *hContext = malloc(sizeof(struct HandleStreamContext));
    hContext->theTest = theTest;
    hContext->stream = YMRetain(stream);
    
    struct ym_thread_dispatch_t dispatchDef = { _handle_remote_stream, NULL, true, hContext, NULL };
    YMThreadDispatchDispatch(theTest->dispatchThread, dispatchDef);
}

void YM_CALLING_CONVENTION _handle_remote_stream(ym_thread_dispatch_ref ctx_)
{
    struct HandleStreamContext *ctx = ctx_->context;
    struct PlexerTest *theTest = ctx->theTest;
    YMStreamRef stream = ctx->stream;
    YMPlexerStreamID streamID = ((ym_plexer_stream_user_info_ref)_YMStreamGetUserInfo(stream))->streamID;
    bool protectTheList = ( PlexerTest1Threads > 1 );
    
    unsigned iterations = PlexerTest1NewStreamPerRoundTrip ? 1 : PlexerTest1RoundTripsPerThread;
    for ( unsigned idx = 0; idx < iterations; idx++ )
    {
        uint16_t incomingMessageLen;
        uint8_t *incomingMessage = _ReceiveMessage(theTest, stream, &incomingMessageLen);
        if ( theTest->timeBasedTimeOver )
        {
            free(incomingMessage);
            goto catch_return;
        }
        testassert(incomingMessage,"incoming message");
        
        if ( protectTheList )
            YMLockLock(theTest->plexerTest1Lock);
        uint8_t *lastMessageWritten = (uint8_t *)YMDictionaryGetItem(theTest->lastMessageWrittenByStreamID, (YMDictionaryKey)streamID);
        if ( protectTheList )
            YMLockUnlock(theTest->plexerTest1Lock);
        
        testassert(incomingMessageLen&&0==memcmp(incomingMessage,lastMessageWritten,incomingMessageLen),"incoming and last written do not match (i%zu ?)",incomingMessageLen);
        free(incomingMessage);
        
        uint8_t *outgoingMessage;
        uint16_t outgoingMessageLen;
        if ( PlexerTest1RandomMessages )
            outgoingMessage = YMRandomDataWithMaxLength(PlexerTest1RandomMessageMaxLength, &outgoingMessageLen);
        else {
            outgoingMessage = (void*)testRemoteResponse;
            outgoingMessageLen = (uint16_t)strlen(testRemoteResponse) + 1;
        }
    
        if ( protectTheList )
            YMLockLock(theTest->plexerTest1Lock);
        if ( YMDictionaryContains(theTest->lastMessageWrittenByStreamID, (YMDictionaryKey)streamID) )
        {
            void *old = YMDictionaryRemove(theTest->lastMessageWrittenByStreamID, (YMDictionaryKey)streamID);
            free(old);
        }
        YMDictionaryAdd(theTest->lastMessageWrittenByStreamID, (YMDictionaryKey)streamID, outgoingMessage);
        if ( protectTheList )
            YMLockUnlock(theTest->plexerTest1Lock);
        _SendMessage(theTest, stream, outgoingMessage, outgoingMessageLen);
        
        theTest->incomingStreamRoundTrips++;
    }
catch_return:
    
    YMPlexerCloseStream(theTest->fakeRemotePlexer,stream);
    YMRelease(stream);
    
    NoisyTestLog("^^^ REMOTE -newStream [%u] exiting (and remoteReleasing)",streamID);
}

void remote_plexer_stream_closing(YMPlexerRef plexer, YMStreamRef stream, void *context)
{
    struct PlexerTest *theTest = context;
    testassert(context,"remote stream closing context");
    testassert(plexer==theTest->fakeRemotePlexer,"remote stream closing plexer not local");
    testassert(stream,"remote stream closing nil");
    
    
    YMLockLock(theTest->plexerTest1Lock);
    bool last = --theTest->awaitingClosures == 0;
    theTest->streamsCompleted++;
    YMLockUnlock(theTest->plexerTest1Lock);
    
    NoisyTestLog("%s: gPlexerTest1AwaitingCloses: %llu",__FUNCTION__,theTest->awaitingClosures);
    if ( PlexerTest1TimeBased )
    {
        if ( theTest->streamsCompleted % 10000 == 0 )
            ymlog("handled %lluth stream, approx %llumb in, %llumb out",theTest->streamsCompleted,theTest->bytesIn/1024/1024,theTest->bytesOut/1024/1024);
    }
    else
    {
        if ( last )
        {
            ymlog("%s last stream closed, signaling exit",__FUNCTION__);
            theTest->plexerTest1Running = false;
        }
    }
}

YM_EXTERN_C_POP
