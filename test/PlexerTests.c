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
#include "YMDispatch.h"
#include "YMDispatchUtils.h"
#include "YMLock.h"
#include "YMSemaphore.h"
#include "YMDictionary.h"
#include "YMUtilities.h"

YM_EXTERN_C_PUSH

#define     PlexerTest1Threads 2
#warning fix this when false on the handle_remote side, first dos's the rest, dispatch async
#define     PlexerTest1NewStreamPerRoundTrip false
#define     PlexerTest1RoundTripsPerThread 128
#define     PlexerTest1CompressionType YMCompressionNone

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
    
    // for comparing callback contexts
    YMPlexerRef localPlexer;
    YMDispatchQueueRef localQueue;
    YMPlexerRef fakeRemotePlexer;
    YMDispatchQueueRef fakeRemoteQueue;
    YMSemaphoreRef interruptNotificationSem;
    
    uint64_t streamsCompleted;
    uint64_t bytesIn;
    uint64_t bytesOut;
    
    YMDictionaryRef lastMessageWrittenByStreamID;
} PlexerTest;

void _DoManyRoundTripsTest(struct PlexerTest *);
YM_ENTRY_POINT(_init_local_plexer_proc);
YM_ENTRY_POINT(_handle_remote_stream);
YM_ENTRY_POINT(_RunLocalPlexer);
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
                                0, YMLockCreate(), true, false,
                                NULL, YMDispatchQueueCreate(YMSTRC("com.combobulated.PlexerTests.local")),
                                NULL, YMDispatchQueueCreate(YMSTRC("com.combobulated.PlexerTests.remote")),
                                YMSemaphoreCreate(0), 0, 0, 0, YMDictionaryCreate() };
    
    _DoManyRoundTripsTest(&theTest);
    
    sleep(2); // or add a thread exit semaphore
    
    ymerr(" PlexerRoundTripsTest finished with %lub in %lub out",theTest.bytesIn,theTest.bytesOut);
    
    YMRelease(theTest.plexerTest1Lock);
    YMRelease(theTest.interruptNotificationSem);
    
    while ( YMDictionaryGetCount(theTest.lastMessageWrittenByStreamID) > 0 ) {
        const void *message = YMDictionaryRemove(theTest.lastMessageWrittenByStreamID, YMDictionaryGetRandomKey(theTest.lastMessageWrittenByStreamID));
        free((void *)message);
    }
    YMRelease(theTest.lastMessageWrittenByStreamID);
    
    YMDispatchJoin(theTest.localQueue);
    YMDispatchJoin(theTest.fakeRemoteQueue);
}

void _DoManyRoundTripsTest(struct PlexerTest *theTest)
{
    YM_IO_BOILERPLATE
    
    YMLocalSocketPairRef socketPair = YMLocalSocketPairCreate(NULL, false);
    YMSOCKET socketA = YMLocalSocketPairGetA(socketPair);
    YMSOCKET socketB = YMLocalSocketPairGetB(socketPair);
    
    bool localIsMaster = arc4random_uniform(2);
    ymlog("plexer test using pipes: L(%s)-s%d <-> s%d-R(%s)",localIsMaster?"m":"s",socketA,socketB,localIsMaster?"s":"m");
    #define xstr(s) str(s)
    #define str(s) #s
    ymlog("plexer test using %u threads, %u trips per thread, %s stream per trip, %s messages, %s",PlexerTest1Threads,PlexerTest1RoundTripsPerThread,PlexerTest1NewStreamPerRoundTrip?"new":"one",PlexerTest1RandomMessages?"random":"fixed",xstr(PlexerTest1CompressionType));
    
    YMStringRef name = YMSTRC("L");
    YMSecurityProviderRef noSecurity = YMSecurityProviderCreate(socketA,socketA);
    theTest->localPlexer = YMPlexerCreate(name,noSecurity,localIsMaster,socketA);
    YMRelease(noSecurity);
    YMRelease(name);
    YMPlexerSetInterruptedFunc(theTest->localPlexer, local_plexer_interrupted);
    YMPlexerSetNewIncomingStreamFunc(theTest->localPlexer, local_plexer_new_stream);
    YMPlexerSetStreamClosingFunc(theTest->localPlexer, local_plexer_stream_closing);
    YMPlexerSetCallbackContext(theTest->localPlexer, theTest);
    
    name = YMSTRC("R");
    noSecurity = YMSecurityProviderCreate(socketB,socketB);
    theTest->fakeRemotePlexer = YMPlexerCreate(name,noSecurity,!localIsMaster,socketB);
    YMRelease(noSecurity);
    YMRelease(name);
    YMPlexerSetInterruptedFunc(theTest->fakeRemotePlexer, remote_plexer_interrupted);
    YMPlexerSetNewIncomingStreamFunc(theTest->fakeRemotePlexer, remote_plexer_new_stream);
    YMPlexerSetStreamClosingFunc(theTest->fakeRemotePlexer, remote_plexer_stream_closing);
    YMPlexerSetCallbackContext(theTest->fakeRemotePlexer, theTest);
    
    ym_dispatch_user_t dispatch = { _init_local_plexer_proc, theTest, NULL, ym_dispatch_user_context_noop };
    YMDispatchAsync(theTest->localQueue, &dispatch);
    
    bool okay = YMPlexerStart(theTest->fakeRemotePlexer);
    testassert(okay,"slave did not start");
    
    theTest->plexerTest1Running = true;

    int nSpawnConcurrentStreams = PlexerTest1Threads;
    while (nSpawnConcurrentStreams--) {
        ym_dispatch_user_t anotherDispatch = { _RunLocalPlexer, theTest, NULL, ym_dispatch_user_context_noop };
        YMDispatchAsync(YMDispatchGetGlobalQueue(),&anotherDispatch);
        YMRetain(theTest->localPlexer);
        YMRetain(theTest->plexerTest1Lock);
        YMRetain(theTest->lastMessageWrittenByStreamID);
    }
    
    // remote_stream_closing used to track the last stream to close, that got ripped out, to fix
    if ( ! PlexerTest1TimeBased ) {
        while ( theTest->plexerTest1Running )
			sleep(1);
	} else {
        time_t startTime = time(NULL);
        sleep((unsigned int)(PlexerTest1EndDate - startTime));
    }
    
    theTest->timeBasedTimeOver = true;
    YMLocalSocketPairShutdown(socketPair);
    
    YMSemaphoreWait(theTest->interruptNotificationSem);
    YMSemaphoreWait(theTest->interruptNotificationSem);
    
    YMRelease(theTest->localPlexer);
    YMRelease(theTest->fakeRemotePlexer);
    
    sleep(2); // let the system settle 3.0 (let threads exit before stack theTest goes out of scope without coordination)
    ymlog("plexer test finished %lu incoming round-trips on %d threads (%d round-trips per %s)",theTest->incomingStreamRoundTrips,
          PlexerTest1Threads,
          PlexerTest1RoundTripsPerThread,
          PlexerTest1NewStreamPerRoundTrip?"stream":"round-trip");
    
    YMRelease(socketPair);
}

YM_ENTRY_POINT(_init_local_plexer_proc)
{
    struct PlexerTest *theTest = context;
    bool okay = YMPlexerStart(theTest->localPlexer);
    testassert(okay,"master did not start");
}

YM_ENTRY_POINT(_RunLocalPlexer)
{
    struct PlexerTest *theTest = context;
    YMPlexerRef plexer = theTest->localPlexer;
    
    YMStreamRef aStream = NULL;
    size_t staticMessageLen = strlen(testLocalMessage) + 1;

    unsigned idx = 0;
#ifdef PlexerTest1TimeBased
    while ( ! theTest->timeBasedTimeOver ) {
#else
    for ( ; idx < PlexerTest1RoundTripsPerThread; idx++ ) {
#endif
        //ymlog("_RunLocalPlexer loop %u",idx++);
        YMPlexerStreamID streamID;
        if ( ! aStream || PlexerTest1NewStreamPerRoundTrip ) {
            YMStringRef name = YMSTRC(__FUNCTION__);
            aStream = YMPlexerCreateStream(plexer,name);
            testassert(aStream,"YMPlexerCreateStream");
            bool okay = _YMStreamSetCompression(aStream,PlexerTest1CompressionType);
            testassert(okay,"YMPlexerCreateStream:_YMStreamSetCompression");
            YMRelease(name);
            streamID = ((ym_pstream_user_info_t *)_YMStreamGetUserInfo(aStream))->streamID;
        }
        
        uint8_t *outgoingMessage = NULL;
        uint16_t outgoingMessageLen;
        if ( PlexerTest1RandomMessages ) {
            outgoingMessageLen = arc4random_uniform(PlexerTest1RandomMessageMaxLength) + 1;
            outgoingMessage = calloc(1,outgoingMessageLen);
            YMRandomDataWithLength(outgoingMessage,outgoingMessageLen);
        } else {
            outgoingMessage = (uint8_t *)strdup(testLocalMessage);
            outgoingMessageLen = staticMessageLen;
        }
        
        bool protectTheList = ( PlexerTest1Threads > 1 );
        if ( protectTheList )
            YMLockLock(theTest->plexerTest1Lock);
        if ( YMDictionaryContains(theTest->lastMessageWrittenByStreamID, (YMDictionaryKey)streamID) ) {
            const void *old = YMDictionaryRemove(theTest->lastMessageWrittenByStreamID, (YMDictionaryKey)streamID);
            free((void *)old);
        }
        YMDictionaryAdd(theTest->lastMessageWrittenByStreamID, (YMDictionaryKey)streamID, outgoingMessage);
        if ( protectTheList )
            YMLockUnlock(theTest->plexerTest1Lock);
        
        _SendMessage(theTest, aStream, outgoingMessage, outgoingMessageLen);
        
        uint16_t incomingMessageLen;
        uint8_t *incomingMessage = _ReceiveMessage(theTest, aStream, &incomingMessageLen);
        if ( theTest->timeBasedTimeOver ) {
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
        testassert(incomingMessageLen&&0==memcmp(incomingMessage,lastMessageWritten,incomingMessageLen),"incoming and last written do not match (o%llu i%llu?)",outgoingMessageLen,incomingMessageLen);
        free(incomingMessage);
        
        if ( PlexerTest1NewStreamPerRoundTrip ) {
            YMPlexerCloseStream(plexer, aStream);
            YMRelease(aStream);
        }
        
        if ( PlexerTest1NewStreamPerRoundTrip && lastMessageWritten )
            free(lastMessageWritten);
        
        YMLockLock(theTest->plexerTest1Lock);
        theTest->bytesOut += outgoingMessageLen;
        theTest->bytesIn += incomingMessageLen;
        YMLockUnlock(theTest->plexerTest1Lock);
    }

    if ( ! PlexerTest1NewStreamPerRoundTrip ) {
        YMPlexerCloseStream(plexer, aStream);
        YMRelease(aStream);
    }
    
catch_release:
    YMRelease(plexer);
    YMRelease(theTest->plexerTest1Lock);
    YMRelease(theTest->lastMessageWrittenByStreamID);

    theTest->plexerTest1Running = false;
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
    YMIOResult result = YMStreamReadUp(stream, (uint8_t *)&length, sizeof(length), &outLength);
    if ( theTest->timeBasedTimeOver )
        return NULL;
    testassert(result==YMIOSuccess,"failed to read header");
    testassert(outLength==sizeof(length),"outLength!=length");
    testassert(length>0,"header.length<=0");
    uint8_t *buffer = malloc(length);
    outLength = 0;
    result = YMStreamReadUp(stream, buffer, length, &outLength);
    if ( theTest->timeBasedTimeOver ) {
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
}

void local_plexer_stream_closing(YMPlexerRef plexer, YMStreamRef stream, void *context)
{
    ymlog("%s",__FUNCTION__);
    struct PlexerTest *theTest = context;
    testassert(theTest,"local new context");
    testassert(plexer==theTest->localPlexer,"localStreamClosing not local");
    testassert(stream,"localStreamClosing null");
    testassert(false,"localStreamClosing");
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
    ymdbg("%s",__FUNCTION__);
    struct PlexerTest *theTest = context;
    testassert(theTest,"remote new context");
    testassert(plexer==theTest->fakeRemotePlexer,"remoteNewStream not remote");
    testassert(stream,"remoteNewStream null");
    
    struct HandleStreamContext *hContext = malloc(sizeof(struct HandleStreamContext));
    hContext->theTest = theTest;
    hContext->stream = stream;
    
    bool okay = _YMStreamSetCompression(stream,PlexerTest1CompressionType);
    testassert(okay,"remote_plexer_new_stream:_YMStreamSetCompression");
    ym_dispatch_user_t dispatchDef = { _handle_remote_stream, hContext, NULL, ym_dispatch_user_context_free };
    YMDispatchAsync(theTest->fakeRemoteQueue, &dispatchDef);

    YMRelease(plexer);
}

YM_ENTRY_POINT(_handle_remote_stream)
{
    struct HandleStreamContext *ctx = context;
    struct PlexerTest *theTest = ctx->theTest;
    YMStreamRef stream = ctx->stream;
    YMPlexerStreamID streamID = ((ym_pstream_user_info_t *)_YMStreamGetUserInfo(stream))->streamID;
    bool protectTheList = ( PlexerTest1Threads > 1 );
    size_t staticMessageLen = strlen(testRemoteResponse) + 1;
    
    unsigned iterations = PlexerTest1NewStreamPerRoundTrip ? 1 :
                                    PlexerTest1TimeBased ? UINT_MAX : PlexerTest1RoundTripsPerThread;
    for ( unsigned idx = 0; idx < iterations; idx++ ) {
        //ymlog("%s stream %u iter %u",__FUNCTION__,streamID,idx);
        uint16_t incomingMessageLen;
        uint8_t *incomingMessage = _ReceiveMessage(theTest, stream, &incomingMessageLen);
        if ( theTest->timeBasedTimeOver ) {
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
        if ( PlexerTest1RandomMessages ) {
            outgoingMessageLen = arc4random_uniform(PlexerTest1RandomMessageMaxLength) + 1;
            outgoingMessage = calloc(1,outgoingMessageLen);
            YMRandomDataWithLength(outgoingMessage, outgoingMessageLen);
        } else {
            outgoingMessage = (uint8_t *)strdup(testRemoteResponse);
            outgoingMessageLen = staticMessageLen;
        }
    
        if ( protectTheList )
            YMLockLock(theTest->plexerTest1Lock);
        if ( YMDictionaryContains(theTest->lastMessageWrittenByStreamID, (YMDictionaryKey)streamID) ) {
            const void *old = YMDictionaryRemove(theTest->lastMessageWrittenByStreamID, (YMDictionaryKey)streamID);
            free((void *)old);
        }
        YMDictionaryAdd(theTest->lastMessageWrittenByStreamID, (YMDictionaryKey)streamID, outgoingMessage);
        if ( protectTheList )
            YMLockUnlock(theTest->plexerTest1Lock);
        _SendMessage(theTest, stream, outgoingMessage, outgoingMessageLen);
        
        theTest->incomingStreamRoundTrips++;
    }
catch_return:
    
    //YMPlexerCloseStream(theTest->fakeRemotePlexer,stream);
    YMRelease(stream);
    
    ymdbg("^^^ REMOTE -newStream [%lu] exiting (and remoteReleasing)",streamID);
}

void remote_plexer_stream_closing(YMPlexerRef plexer, YMStreamRef stream, void *context)
{
    ymlog("%s entered",__FUNCTION__);
    struct PlexerTest *theTest = context;
    testassert(context,"remote stream closing context");
    testassert(plexer==theTest->fakeRemotePlexer,"remote stream closing plexer not local");
    testassert(stream,"remote stream closing nil");

    YMLockLock(theTest->plexerTest1Lock);
    uint64_t completed = theTest->streamsCompleted++;
    YMLockUnlock(theTest->plexerTest1Lock);

    if ( completed && ( completed % 10000 == 0 ) )
        ymlog("handled %luth stream, approx %lumb in, %lumb out",completed,theTest->bytesIn/1024/1024,theTest->bytesOut/1024/1024);
    ymlog("%s exiting %lu",__FUNCTION__,completed);
}

YM_EXTERN_C_POP
