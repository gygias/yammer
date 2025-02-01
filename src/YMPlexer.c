//
//  YMPlexer.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMPlexer.h"
#include "YMPlexerPriv.h"

#include "YMUtilities.h"
#include "YMSemaphore.h"
#include "YMStreamPriv.h"
#include "YMDictionary.h"
#include "YMLock.h"
#include "YMThread.h"
#include "YMThreadPriv.h"

#define ymlog_pre "plexer[%s]: "
#define ymlog_args p->name?YMSTR(p->name):"*"
#define ymlog_type YMLogPlexer
#include "YMLog.h"

#ifdef USE_FTIME
#include <sys/timeb.h>
#error todo
#elif !defined(YMWIN32)
#include <sys/select.h>
#include <sys/time.h>
#include <pthread.h> // explicit for sigpipe
# if defined (YMLINUX)
# include <signal.h>
# endif
#else
#include <winsock2.h> // gettimeofday
#endif

#define YMPlexerBuiltInVersion ((uint32_t)1)

YM_EXTERN_C_PUSH

#define CHECK_REMOVE_RELEASE(local,streamID,remove) {   \
                                                YMLockLock(p->interruptionLock); bool _interrupted = ! p->active; YMLockUnlock(p->interruptionLock); \
                                                if ( ! _interrupted ) { \
                                                    YMLockRef _lock = local ? p->localAccessLock : p->remoteAccessLock; \
                                                    YMDictionaryRef _list = local ? p->localStreamsByID : p->remoteStreamsByID; \
                                                        YMLockLock(_lock); \
                                                        YMStreamRef _theStream; \
                                                        bool _okay = ( remove ? \
                                                                        ( ( _theStream = YMDictionaryRemove(_list,streamID) ) != NULL ) : \
                                                                          YMDictionaryContains(_list,streamID) ); \
                                                        if ( ! _okay ) { ymabort("plexer consistenty check failed"); } \
                                                        if ( remove ) { YMRelease(_theStream); } \
                                                    YMLockUnlock(_lock); } \
                                                }

typedef struct {
    uint32_t protocolVersion;
    YMPlexerStreamID masterStreamIDMax;
    uint8_t evenStreamIDs;
} YMPlexerInitializer;

typedef struct {
    uint32_t protocolVersion;
} YMPlexerSlaveAck;

// multiplexing
typedef int32_t YMPlexerCommandType;
typedef enum
{
    YMPlexerCommandClose = -1, // indicates to up/down threads to terminate
    YMPlexerCommandCloseStream = -2,
    YMPlexerCommandMin = YMPlexerCommandCloseStream
} YMPlexerCommand;

void YM_CALLING_CONVENTION __ym_plexer_notify_new_stream(YM_THREAD_PARAM context);
void YM_CALLING_CONVENTION __ym_plexer_notify_stream_closing(YM_THREAD_PARAM context);
void YM_CALLING_CONVENTION __ym_plexer_notify_interrupted(YM_THREAD_PARAM context);

// linked to 'private' definition in YMPlexerPriv
typedef struct __ym_plexer_stream_user_info
{
    YMStringRef name;
    YMPlexerStreamID streamID;
    
    YMPlexerRef plexer;
    bool isLocallyOriginated;
    struct timeval *lastServiceTime; // free me
    
    // get rid of select loop
    YMLockRef lock;
    uint64_t bytesAvailable;
    bool userClosed;
    
    uint64_t rawWritten;
    uint64_t muxerWritten;
    uint64_t rawRead;
    uint64_t muxerRead;
} __ym_plexer_stream_user_info;
typedef struct __ym_plexer_stream_user_info __ym_plexer_stream_user_info_t;

#undef YM_STREAM_INFO
#define YM_STREAM_INFO(x) ((__ym_plexer_stream_user_info_t *)_YMStreamGetUserInfo(x))

typedef struct {
    YMPlexerCommandType command;
    YMPlexerStreamID streamID;
} YMPlexerMessage;

typedef struct __ym_plexer
{
    _YMType _common;
    
    YMStringRef name;
    YMSecurityProviderRef provider;
    
    bool active; // intialized and happy
    bool stopped; // stop called before files start closing
    bool master;
    bool myStreamsEven;
    
    // the downstream
    YMDictionaryRef localStreamsByID;
    YMLockRef localAccessLock;
    uint8_t *localPlexBuffer;
    uint32_t localPlexBufferSize;
    YMPlexerStreamID localStreamIDLast;
    
    // the upstream
    YMDictionaryRef remoteStreamsByID;
    YMLockRef remoteAccessLock;
    uint8_t *remotePlexBuffer;
    uint32_t remotePlexBufferSize;
    
    YMThreadRef localServiceThread;
    YMThreadRef remoteServiceThread;
    YMThreadRef eventDeliveryThread;
    YMLockRef interruptionLock;
    YMSemaphoreRef downstreamReadySemaphore;
    
    // user
    ym_plexer_interrupted_func interruptedFunc;
    ym_plexer_new_upstream_func newIncomingFunc;
    ym_plexer_stream_closing_func closingFunc;
    void *callbackContext;
} __ym_plexer;
typedef struct __ym_plexer __ym_plexer_t;

// generic context pointer definition for "plexer & stream" entry points
typedef struct _ym_plexer_and_stream
{
    __ym_plexer_t *p;
    YMStreamRef s;
} _ym_plexer_and_stream;
typedef struct _ym_plexer_and_stream _ym_plexer_and_stream_t;

void __YMRegisterSigpipe();
void __ym_sigpipe_handler (int signum);
YMStreamRef __YMPlexerRetainReadyStream(__ym_plexer_t *);
void __YMPlexerDispatchFunctionWithName(__ym_plexer_t *, YMStreamRef, YMThreadRef, ym_dispatch_user_func, YMStringRef);
bool __YMPlexerStartServiceThreads(__ym_plexer_t *);
bool __YMPlexerDoInitialization(__ym_plexer_t *, bool);
bool __YMPlexerInitAsMaster(__ym_plexer_t *);
bool __YMPlexerInitAsSlave(__ym_plexer_t *);
YM_THREAD_RETURN YM_CALLING_CONVENTION __ym_plexer_service_downstream_proc(YM_THREAD_PARAM);
YM_THREAD_RETURN YM_CALLING_CONVENTION __ym_plexer_service_upstream_proc(YM_THREAD_PARAM);
bool __YMPlexerServiceADownstream(__ym_plexer_t *, YMStreamRef);
YMStreamRef __YMPlexerRetainOrCreateRemoteStreamWithID(__ym_plexer_t *, YMPlexerStreamID);
YMStreamRef __YMPlexerCreateStreamWithID(__ym_plexer_t *, YMPlexerStreamID, bool, YMStringRef);
bool __YMPlexerInterrupt(__ym_plexer_t *);
void __ym_plexer_stream_data_available_proc(YMStreamRef, uint32_t, void *);
void __ym_plexer_free_stream_info(YMStreamRef);

#define YMPlexerDefaultBufferSize 16384

YMPlexerRef YMPlexerCreate(YMStringRef name, YMSecurityProviderRef provider, bool master)
{
	__YMRegisterSigpipe();
    
    __ym_plexer_t *p = (__ym_plexer_t *)_YMAlloc(_YMPlexerTypeID,sizeof(__ym_plexer_t));
    
    p->name = YMStringCreateWithFormat("%s:%s",master?"s":"c",name?YMSTR(name):"*",NULL);
    p->provider = YMRetain(provider);
    
    p->active = false;
    p->master = master;
    p->stopped = false;
    p->myStreamsEven = false;
    
    p->localStreamsByID = YMDictionaryCreate();
    YMStringRef aString = YMStringCreateWithFormat("%s-local",YMSTR(p->name), NULL);
    p->localAccessLock = YMLockCreateWithOptionsAndName(YMInternalLockType,aString);
    YMRelease(aString);
    p->localPlexBufferSize = YMPlexerDefaultBufferSize;
    p->localPlexBuffer = YMALLOC(p->localPlexBufferSize);
    
    p->remoteStreamsByID = YMDictionaryCreate();
    aString = YMStringCreateWithFormat("%s-remote",YMSTR(p->name),NULL);
    p->remoteAccessLock = YMLockCreateWithOptionsAndName(YMInternalLockType,aString);
    YMRelease(aString);
    p->remotePlexBufferSize = YMPlexerDefaultBufferSize;
    p->remotePlexBuffer = YMALLOC(p->remotePlexBufferSize);
    
    aString = YMStringCreateWithFormat("%s-down",YMSTR(p->name),NULL);
    p->localServiceThread = YMThreadCreate(aString, __ym_plexer_service_downstream_proc, (void *)YMRetain(p));
    YMRelease(aString);
    
    aString = YMStringCreateWithFormat("%s-up",YMSTR(p->name),NULL);
    p->remoteServiceThread = YMThreadCreate(aString, __ym_plexer_service_upstream_proc, (void *)YMRetain(p));
    YMRelease(aString);
    
    aString = YMStringCreateWithFormat("%s-event",YMSTR(p->name),NULL);
    p->eventDeliveryThread = YMThreadDispatchCreate(aString);
    YMRelease(aString);
    
    aString = YMStringCreateWithFormat("%s-interrupt",YMSTR(p->name),NULL);
    p->interruptionLock = YMLockCreateWithOptionsAndName(YMInternalLockType,aString);
    YMRelease(aString);
    
    aString = YMStringCreateWithFormat("%s-down-signal",YMSTR(p->name),NULL);
    p->downstreamReadySemaphore = YMSemaphoreCreateWithName(aString,0);
    YMRelease(aString);
    
    p->interruptedFunc = NULL;
    p->newIncomingFunc = NULL;
    p->closingFunc = NULL;
    p->callbackContext = NULL;
    
    return p;
}

void _YMPlexerFree(YMPlexerRef p_)
{
    __ym_plexer_t *p = (__ym_plexer_t *)p_;
    
    p->newIncomingFunc = NULL;
    p->closingFunc = NULL;
    p->interruptedFunc = NULL;
    
    // ensure that if we haven't been stopped, or interrupted, we hang up
    bool first = __YMPlexerInterrupt((__ym_plexer_t *)p);
    ymerr("deallocating (%s)",first?"stopping":"already stopped");
    
    YMRelease(p->name);
    YMRelease(p->provider);
    
    YMSemaphoreSignal(p->downstreamReadySemaphore);
    YMThreadJoin(p->localServiceThread);
    YMThreadJoin(p->remoteServiceThread);
    
    YMRelease(p->downstreamReadySemaphore);
    
    free(p->localPlexBuffer);
    YMRelease(p->localStreamsByID);
    YMRelease(p->localAccessLock);
    YMRelease(p->localServiceThread);
    free(p->remotePlexBuffer);
    YMRelease(p->remoteStreamsByID);
    YMRelease(p->remoteAccessLock);
    YMRelease(p->remoteServiceThread);
    
    YMRelease(p->eventDeliveryThread);
    YMRelease(p->interruptionLock);
}

void YMPlexerSetInterruptedFunc(YMPlexerRef p, ym_plexer_interrupted_func func)
{
    ((__ym_plexer_t *)p)->interruptedFunc = func;
}

void YMPlexerSetNewIncomingStreamFunc(YMPlexerRef p, ym_plexer_new_upstream_func func)
{
    ((__ym_plexer_t *)p)->newIncomingFunc = func;
}

void YMPlexerSetStreamClosingFunc(YMPlexerRef p, ym_plexer_stream_closing_func func)
{
    ((__ym_plexer_t *)p)->closingFunc = func;
}

void YMPlexerSetCallbackContext(YMPlexerRef p, void *context)
{
    ((__ym_plexer_t *)p)->callbackContext = context;
}

bool YMPlexerStart(YMPlexerRef p_)
{
    __ym_plexer_t *p = (__ym_plexer_t *)p_;
    
    bool okay;
    
    if ( p->active ) {
        ymerr("user: this plexer is already initialized");
        return false;
    }
    
    if ( p->master )
        okay = __YMPlexerInitAsMaster(p);
    else
        okay = __YMPlexerInitAsSlave(p);
    
    if ( ! okay )
        goto catch_fail;
    
    ymlog("initialized as %s with %s stream ids",p->master?"master":"slave",p->myStreamsEven?"even":"odd");
    
    // this flag is used to let our threads exit, among other things
    p->active = true;
    
    okay = YMThreadStart(p->localServiceThread);
    if ( ! okay ) {
        ymerr("failed to detach down service thread");
        goto catch_fail;
    }
    
    okay = YMThreadStart(p->remoteServiceThread);
    if ( ! okay ) {
        ymerr("failed to detach up service thread");
        goto catch_fail;
    }
    
    okay = YMThreadStart(p->eventDeliveryThread);
    if ( ! okay ) {
        ymerr("failed to detach event thread");
        goto catch_fail;
    }
    
    ymlog("started");
    
catch_fail:
    return okay;
}

YMStreamRef YMAPI YMPlexerCreateStream(YMPlexerRef p_, YMStringRef name)
{
    __ym_plexer_t *p = (__ym_plexer_t *)p_;
    
    YMStreamRef newStream = NULL;
    YMLockLock(p->localAccessLock);
    do {
        if ( p->localStreamIDLast >= ( YMPlexerStreamIDMax - 1 ) )
            p->localStreamIDLast = p->myStreamsEven ? 0 : 1;
        else
            p->localStreamIDLast += 2;
        
        YMPlexerStreamID newStreamID = p->localStreamIDLast;
        if ( YMDictionaryContains(p->localStreamsByID, (YMDictionaryKey)newStreamID) ) {
            ymerr("fatal: plexer ran out of streams");
            __YMPlexerInterrupt(p);
            break;
        }
        
        YMStringRef userName = name ? YMRetain(name) : YMSTRC("*");
        newStream = __YMPlexerCreateStreamWithID(p, newStreamID, true, userName);
        
        YMDictionaryAdd(p->localStreamsByID, (YMDictionaryKey)newStreamID, (void *)newStream);
        YMRetain(newStream); // retain for user
        
    } while (false);
    YMLockUnlock(p->localAccessLock);
    
    return newStream;
}

// void: if this fails, it's either a bug or user error
void YMPlexerCloseStream(YMPlexerRef p_, YMStreamRef stream)
{
    __ym_plexer_t *p = (__ym_plexer_t *)p_;
    
    __ym_plexer_stream_user_info_t *userInfo = YM_STREAM_INFO(stream);
    YMPlexerStreamID streamID = userInfo->streamID;
    bool isLocal = userInfo->isLocallyOriginated;
    
    ymassert(!userInfo->userClosed,"stream double-close"); // only catches local
    
    if ( p->active && isLocal ) {
        userInfo->userClosed = true;
        // when stream commands were a thing
        YMSemaphoreSignal(p->downstreamReadySemaphore);
    }
    
    ymlog("user %s stream %lu", isLocal?"closing":"releasing", streamID);
    YMRelease(stream); // release user
}

bool YMPlexerStop(YMPlexerRef p_)
{
    __ym_plexer_t *p = (__ym_plexer_t *)p_;
    p->interruptedFunc = NULL;
    p->newIncomingFunc = NULL;
    p->closingFunc = NULL;
    bool interrupted = __YMPlexerInterrupt(p);
    
    // once we're stopped (or notified interrupted), we can be deallocated. service threads hold retains on the plexer object,
    // and x509 certificates embedded in the security provider actually retain a function pointer to
    // their lock callback, and will call it when the X509 is free'd! that means if the client
    // stops and immediately releases us on whatever thread, plexer might not be deallocated until
    // the final service thread actually exits, which in turn depends on Globals Free'd by user (test case).
    //
    // we can't do this from the common __Interrupt method, because a thread might be the first to report a
    // real error (disconnect), so clients must stop the plexer even after 'interrupted' event before deallocating
    YMThreadDispatchJoin(p->eventDeliveryThread);
    YMSemaphoreSignal(p->downstreamReadySemaphore);
    
    return interrupted;
}

#pragma mark internal

void __YMRegisterSigpipe()
{
    signal(SIGPIPE,__ym_sigpipe_handler);
}

void __ym_sigpipe_handler (__unused int signum)
{
    fprintf(stderr,"sigpipe happened\n");
}

const char YMPlexerMasterHello[] = "オス、王様でおるべし";
const char YMPlexerSlaveHello[] = "よろしくお願いいたします";

bool __YMPlexerInitAsMaster(__ym_plexer_t *p)
{
    bool okay = YMSecurityProviderWrite(p->provider, (void *)YMPlexerMasterHello, strlen(YMPlexerMasterHello));
    if ( ! okay ) {
        ymerr("m: master hello write");
        return false;
    }
    
    unsigned long inHelloLen = strlen(YMPlexerSlaveHello);
    char inHello[64];
    okay = YMSecurityProviderRead(p->provider, (void *)inHello, inHelloLen);
    if ( ! okay || memcmp(YMPlexerSlaveHello,inHello,inHelloLen) ) {
        ymerr("m: slave hello read");
        return false;
    }
    
    p->myStreamsEven = true;
    p->localStreamIDLast = YMPlexerStreamIDMax - ( p->myStreamsEven ? 1 : 0 );
    
    YMPlexerInitializer initializer = { YMPlexerBuiltInVersion, YMPlexerStreamIDMax, p->myStreamsEven };
    okay = YMSecurityProviderWrite(p->provider, (void *)&initializer, sizeof(initializer));
    if ( ! okay ) {
        ymerr("m: master init write");
        return false;
    }
    
    YMPlexerSlaveAck ack;
    okay = YMSecurityProviderRead(p->provider, (void *)&ack, sizeof(ack));
    if ( ! okay ) {
        ymerr("m: slave ack read");
        return false;
    }
    if ( ack.protocolVersion > YMPlexerBuiltInVersion ) {
        ymerr("m: protocol mismatch");
        return false;
    }
    
    return true;
}

bool __YMPlexerInitAsSlave(__ym_plexer_t *p)
{
    unsigned long inHelloLen = strlen(YMPlexerMasterHello);
    char inHello[64];
    bool okay = YMSecurityProviderRead(p->provider, (void *)inHello, inHelloLen);
    
    if ( ! okay || memcmp(YMPlexerMasterHello,inHello,inHelloLen) ) {
        ymerr("s: master hello read");
        return false;
    }
    
    okay = YMSecurityProviderWrite(p->provider, (void *)YMPlexerSlaveHello, strlen(YMPlexerSlaveHello));
    if ( ! okay ) {
        ymerr("s: slave hello write");
        return false;
    }
    
    YMPlexerInitializer initializer;
    okay = YMSecurityProviderRead(p->provider, (void *)&initializer, sizeof(initializer));
    if ( ! okay ) {
        ymerr("s: master init read");
        return false;
    }
    
    // todo, technically this should handle non-zero-based master min id, but doesn't
    p->myStreamsEven = ! initializer.evenStreamIDs;
    p->localStreamIDLast = YMPlexerStreamIDMax - ( p->myStreamsEven ? 1 : 0 );
    
    bool supported = initializer.protocolVersion <= YMPlexerBuiltInVersion;
    YMPlexerSlaveAck ack = { YMPlexerBuiltInVersion };
    okay = YMSecurityProviderWrite(p->provider, (void *)&ack, sizeof(ack));
    if ( ! okay ) {
        ymerr("s: slave hello read");
        return false;
    }
    
    if ( ! supported ) {
        ymerr("s: master requested protocol newer than built-in %u",YMPlexerBuiltInVersion);
        return false;
    }
    
    return true;
}

YM_THREAD_RETURN YM_CALLING_CONVENTION __ym_plexer_service_downstream_proc(YM_THREAD_PARAM ctx)
{
    __ym_plexer_t *p = (__ym_plexer_t *)ctx;
    //YMRetain(p); // retained on thread creation, matched at the end of this function
    
    ymlog("downstream service thread entered");
    
    while( p->active ) {
        ymlog("V awaiting signal");
        // there is only one thread consuming this semaphore, so i think it's ok not to actually lock around this loop iteration
        YMSemaphoreWait(p->downstreamReadySemaphore);
        
        if ( ! p->active ) {
            ymerr("V signaled to exit");
            break;
        }
        
        YMStreamRef servicingStream = __YMPlexerRetainReadyStream(p);
        
        ymlog("V signaled,");
        
        // todo about not locking until we've consumed as many semaphore signals as we can
        //while ( --readyStreams )
        {
            if ( ! servicingStream ) {
                //ymlog("coalescing signals");
                continue;
            }
            
            YM_DEBUG_ASSERT_MALLOC(servicingStream);
            bool okay = __YMPlexerServiceADownstream(p, servicingStream);
            YMRelease(servicingStream);
            if ( ! okay || ! p->active ) {
                if ( __YMPlexerInterrupt(p) )
                    ymerr("p: service downstream failed");
                break;
            }
        }
    }
    
    ymerr("downstream service thread exiting");
    
    YMRelease(p);

	YM_THREAD_END
}

YMStreamRef __YMPlexerRetainReadyStream(__ym_plexer_t *p)
{
    YMStreamRef oldestStream = NULL;
    struct timeval newestTime = {0,0};
    YMGetTheEndOfPosixTimeForCurrentPlatform(&newestTime);
    
#define __YMOutgoingListIdx 0
#define __YMIncomingListIdx 1
#define __YMListMax 2
#define __YMLockListIdx 0
#define __YMListListIdx 1
    YMTypeRef *list[] = { (YMTypeRef[]) { p->localAccessLock, p->localStreamsByID },
        (YMTypeRef[]) { p->remoteAccessLock, p->remoteStreamsByID } };
    
    int listIdx = 0;
    for( ; listIdx < __YMListMax; listIdx++ ) {
        YMLockRef aLock = (YMLockRef)list[listIdx][__YMLockListIdx];
        YMDictionaryRef aStreamsById = (YMDictionaryRef)list[listIdx][__YMListListIdx];
        
        YMLockLock(aLock);
        {
            YMDictionaryEnumRef aStreamsEnum = YMDictionaryEnumeratorBegin(aStreamsById);
            YMDictionaryEnumRef aStreamsEnumPrev = NULL;
            while ( aStreamsEnum ) {
                YMStreamRef aStream = (YMStreamRef)aStreamsEnum->value;
                //YM_DEBUG_ABORT_IF_BOGUS(oldestStream);
                
                __ym_plexer_stream_user_info_t *userInfo = YM_STREAM_INFO(aStream);
                YMPlexerStreamID aStreamID = userInfo->streamID;
                
                ymlog("choose: considering %s downstream %lu",listIdx==__YMOutgoingListIdx?"local":"remote",aStreamID);
                
                // we are the only consumer of bytesAvailable, no need to lock here, but we will when we actually 'consume' them later on
                uint64_t aStreamBytesAvailable = 0;
                if ( userInfo->userClosed ) {
                    ymlog("choose: stream %lu is closing",aStreamID);
                    if ( ! userInfo->isLocallyOriginated )
                        ymabort("remote stream %lu locally closed",aStreamID);
                }
                else {
                    aStreamBytesAvailable = YM_STREAM_INFO(aStream)->bytesAvailable;
                    if ( aStreamBytesAvailable == 0 ) {
                        ymlog("choose: stream %lu is empty",aStreamID);
                        goto catch_continue;
                    }
                }
                
                ymlog("choose: %s stream %lu is ready! %s%lub",listIdx==__YMOutgoingListIdx?"local":"remote",aStreamID,userInfo->userClosed?"(closing) ":"",aStreamBytesAvailable);
                
                struct timeval *thisStreamLastService = YM_STREAM_INFO(aStream)->lastServiceTime;
                if ( YMTimevalCompare(thisStreamLastService, &newestTime ) != GreaterThan ) {
                    if ( oldestStream ) YMRelease(oldestStream);
                    oldestStream = YMRetain(aStream);
                }
                
            catch_continue:
                aStreamsEnumPrev = aStreamsEnum;
                aStreamsEnum = YMDictionaryEnumeratorGetNext(aStreamsEnum);
            }
            if ( aStreamsEnumPrev )
                YMDictionaryEnumeratorEnd(aStreamsEnum);
            else
                ymlog("choose: %s list is empty",listIdx==__YMOutgoingListIdx?"down stream":"up stream");
                
        }
        YMLockUnlock(aLock);
    }
    
    if ( oldestStream ) YM_DEBUG_ASSERT_MALLOC(oldestStream);
    return oldestStream;
}

bool __YMPlexerServiceADownstream(__ym_plexer_t *p, YMStreamRef stream)
{
    YM_DEBUG_ASSERT_MALLOC(stream);
    
    // update last service time on stream
    __ym_plexer_stream_user_info_t *userInfo = YM_STREAM_INFO(stream);
    YMPlexerStreamID streamID = userInfo->streamID;
    if ( 0 != gettimeofday(userInfo->lastServiceTime, NULL) ) {
        ymerr("V-s%lu setting initial service time for stream: %d (%s)",streamID,errno,strerror(errno));
        YMGetTheBeginningOfPosixTimeForCurrentPlatform(userInfo->lastServiceTime);
    }
    
    bool closing = userInfo->userClosed;
    uint64_t bytesAvailable,
            bytesRemaining,
            chunksHandled = 0;
    
    YMLockLock(userInfo->lock);
        bytesAvailable = userInfo->bytesAvailable;
        userInfo->bytesAvailable = 0;
    YMLockUnlock(userInfo->lock);
    
    ymlog("V-s%lu will flush %lu bytes",streamID,bytesAvailable);
    
    ymassert(bytesAvailable>0||closing,"a downstream state invalid");
    
    bytesRemaining = bytesAvailable;
    
    while ( bytesRemaining > 0 ) {
        uint32_t chunkLength = 0;
        
        // consume any signals we read ahead
        //if ( chunksHandled )
        //    YMSemaphoreWait(plexer->downstreamReadySemaphore);
        
        // never realloc, chunk chunks
        chunkLength = (uint32_t)bytesRemaining;
        if ( chunkLength > 16384 )
            chunkLength = 16384;
        
        _YMStreamReadDown(stream, p->localPlexBuffer, chunkLength);
        if ( ! p->active )
            return false;
        ymlog("V-s%lu read stream chunk",streamID);
        
        if ( bytesRemaining - chunkLength > bytesRemaining )
            ymabort("stream %lu overchunked",streamID);
        
        bytesRemaining -= chunkLength;
        chunksHandled++;
        
        YMPlexerMessage plexMessage = { chunkLength, streamID };
        size_t plexMessageLen = sizeof(plexMessage);
		bool okay = YMSecurityProviderWrite(p->provider, (uint8_t *)&plexMessage, plexMessageLen);
        if ( ! okay || ! p->active ) {
            ymerr("p: V-s%lu failed writing plex message size %zub: %d (%s)",streamID,plexMessageLen,errno,strerror(errno));
            return false;
        }
        userInfo->muxerWritten += plexMessageLen;
        
        ymlog("V-s%lu wrote plex header",streamID);
        
		okay = YMSecurityProviderWrite(p->provider, p->localPlexBuffer, chunkLength);
        if ( ! okay || ! p->active ) {
            bool interrupt = __YMPlexerInterrupt(p);
            if ( interrupt )
                ymerr("p: V-s%lu failed writing plex buffer %ub: %d (%s)",streamID,plexMessage.command,errno,strerror(errno));
            return false;
        }
        userInfo->rawWritten += chunkLength;
        
        ymlog("V-s%lu wrote plex buffer %ub",streamID,plexMessage.command);
    }
    
    ymlog("V-s%lu flushed %lu chunk(s) and %lu bytes",streamID,chunksHandled,bytesAvailable);
    
    if ( closing ) {
        if ( ! userInfo->isLocallyOriginated )
            ymabort("remote stream %lu closed locally",streamID);
        
        bool okay = true;
        YMPlexerMessage plexMessage = { YMPlexerCommandCloseStream, streamID };
        size_t plexMessageLen = sizeof(plexMessage);
        okay = YMSecurityProviderWrite(p->provider, (uint8_t *)&plexMessage, plexMessageLen);
        if ( ! okay || ! p->active ) {
            bool interrupt = __YMPlexerInterrupt(p);
            if ( interrupt )
                ymerr("V-s%lu perror: failed writing plex message size %zub: %d (%s)",streamID,plexMessageLen,errno,strerror(errno));
        }
        userInfo->muxerWritten += plexMessageLen;
        
        ymlog("V-s%lu stream closing (rW%lu,pW%lu,rR%lu,pR%lu): %p (%s)",streamID,userInfo->rawWritten,userInfo->muxerRead,userInfo->rawRead,userInfo->muxerRead,stream,YMSTR(_YMStreamGetName(stream)));
        
        CHECK_REMOVE_RELEASE(true, (YMDictionaryKey)streamID, true);
        
        return okay;
    }
    
    return true;
}

YM_THREAD_RETURN YM_CALLING_CONVENTION __ym_plexer_service_upstream_proc(YM_THREAD_PARAM ctx)
{
    __ym_plexer_t *p = (__ym_plexer_t *)ctx;
    
    ymlog("^ upstream service thread entered");
    
    bool okay = true;
    while ( okay && p->active ) {
        bool streamClosing = false;
        size_t chunkLength = 0;
        YMPlexerMessage plexerMessage;
        
		okay = YMSecurityProviderRead(p->provider, (uint8_t *)&plexerMessage, sizeof(plexerMessage));
        if ( ! okay || ! p->active || plexerMessage.command < YMPlexerCommandMin || plexerMessage.command > UINT16_MAX ) {
            bool interrupt = __YMPlexerInterrupt(p);
            if ( interrupt )
                ymerr("^ perror: failed reading plex header: %d (%s)",errno,strerror(errno));
            break;
        } else if ( plexerMessage.command == YMPlexerCommandCloseStream )
            streamClosing = true;
        else {
            chunkLength = plexerMessage.command;
            ymlog("^-s%lu read plex header %zub",plexerMessage.streamID,chunkLength);
        }
        
        YMPlexerStreamID streamID = plexerMessage.streamID;
        YMStreamRef theStream = __YMPlexerRetainOrCreateRemoteStreamWithID(p, streamID); // todo retain this until this iter is done
        if ( ! theStream || ! p->active ) {
            bool interrupt = __YMPlexerInterrupt(p);
            if ( interrupt )
                ymerr("fatal: ^-s%lu stream lookup",streamID);
            if ( theStream ) YMRelease(theStream);
            break;
        }
        
        __ym_plexer_stream_user_info_t *userInfo = YM_STREAM_INFO(theStream);
        userInfo->muxerRead += sizeof(plexerMessage);
        
        if ( streamClosing ) {
            YMStringRef memberName = YMStringCreateWithFormat("%s-s%lu-%s",YMSTR(p->name),streamID,YM_TOKEN_STR(__ym_plexer_notify_stream_closing), NULL);
            // close 'read up', so that if client (or forward file) is reading unbounded data it will get signaled
            _YMStreamCloseWriteUp(theStream);
			if ( p->closingFunc )
				__YMPlexerDispatchFunctionWithName(p, theStream, p->eventDeliveryThread, __ym_plexer_notify_stream_closing, memberName);
            ymlog("^-s%lu stream closing (rW%lu,pW%lu,rR%lu,pR%lu)",plexerMessage.streamID,userInfo->rawWritten,userInfo->muxerWritten,userInfo->rawRead,userInfo->muxerRead);
            YMRelease(theStream);
            continue;
        }
        
        ymassert(chunkLength<=UINT16_MAX&&chunkLength!=0,"upstream chuck length");
        
        okay = YMSecurityProviderRead(p->provider, p->remotePlexBuffer, chunkLength);
        if ( ! okay || ! p->active ) {
            bool interrupt = __YMPlexerInterrupt(p);
            if ( interrupt )
                ymerr("p: ^-s%lu failed reading plex buffer of length %zub: %d (%s)",streamID,chunkLength,errno,strerror(errno));
            YMRelease(theStream);
            break;
        }
        userInfo->rawRead += chunkLength;
        ymlog("^-s%lu read plex buffer %zub",streamID,chunkLength);
        
        YMIOResult result = _YMStreamWriteUp(theStream, p->remotePlexBuffer, (uint32_t)chunkLength);
        if ( result != YMIOSuccess || ! p->active ) {
            bool interrupt = __YMPlexerInterrupt(p);
            if ( interrupt )
                ymerr("internal fatal: ^-s%lu failed writing plex buffer of length %zub: %d (%s)",streamID,chunkLength,errno,strerror(errno));
            YMRelease(theStream);
            break;
        }
        
        ymlog("^-s%lu wrote plex buffer %zub",streamID,chunkLength);
        YMRelease(theStream);
    }
    
    ymerr("upstream service thread exiting");
    
    YMRelease(p);

	YM_THREAD_END
}

YMStreamRef __YMPlexerRetainOrCreateRemoteStreamWithID(__ym_plexer_t *p, YMPlexerStreamID streamID)
{
    YMStreamRef theStream = NULL;
    
    YMLockLock(p->localAccessLock);
    {
        theStream = (YMStreamRef)YMDictionaryGetItem(p->localStreamsByID,(YMDictionaryKey)streamID);
        if ( theStream ) {
            ymlog("L-%lu found existing local stream",streamID);
            YMRetain(theStream);
        }
    }
    YMLockUnlock(p->localAccessLock);
    
    if ( ! theStream ) {
        YMLockLock(p->remoteAccessLock);
        {
            theStream = (YMStreamRef)YMDictionaryGetItem(p->remoteStreamsByID,(YMDictionaryKey)streamID);
            if ( theStream ) {
                ymlog("L-%lu found existing remote stream",streamID);
                YMRetain(theStream);
            }
        
            // new stream
            if ( ! theStream ) {
                if ( ( streamID % 2 == 0 ) && p->myStreamsEven ) {
                    YMLockUnlock(p->remoteAccessLock);
                    
                    if ( __YMPlexerInterrupt(p) )
                        ymerr("internal: L-%lu stream id collision",streamID);
                    
                    return NULL;
                }
                
                ymlog("L-%lu notifying new incoming",streamID);
                
                YMStringRef name = YMSTRC("remote");
                theStream = __YMPlexerCreateStreamWithID(p,streamID,false,name);
                
                YMDictionaryAdd(p->remoteStreamsByID, (YMDictionaryKey)streamID, theStream);
                YMRetain(theStream); // retain for user (complements close)
                YMRetain(theStream); // retain for function
                
                name = YMStringCreateWithFormat("%s-notify-new-%lu",YMSTR(p->name),streamID,NULL);
				if ( p->newIncomingFunc )
					__YMPlexerDispatchFunctionWithName(p, theStream, p->eventDeliveryThread, __ym_plexer_notify_new_stream, name);
            }
        }    
        YMLockUnlock(p->remoteAccessLock);
    }
    
    return theStream;
}

YMStreamRef __YMPlexerCreateStreamWithID(__ym_plexer_t *p, YMPlexerStreamID streamID, bool isLocal, YMStringRef userNameToRelease)
{
    __ym_plexer_stream_user_info_t *userInfo = (__ym_plexer_stream_user_info_t *)YMALLOC(sizeof(__ym_plexer_stream_user_info_t));
    userInfo->plexer = YMRetain(p);
    userInfo->streamID = streamID;
    userInfo->isLocallyOriginated = isLocal;
    userInfo->lastServiceTime = (struct timeval *)YMALLOC(sizeof(struct timeval));
    if ( 0 != gettimeofday(userInfo->lastServiceTime, NULL) ) {
        ymlog("warning: error setting initial service time for s%lu: %d (%s)",streamID,errno,strerror(errno));
        YMGetTheBeginningOfPosixTimeForCurrentPlatform(userInfo->lastServiceTime);
    }
    
    YMStringRef memberName = YMStringCreateWithFormat("%s-%s-s%lu-%s",p->master?"m":"s",isLocal?">":"<",streamID,YMSTR(userNameToRelease),NULL);
    userInfo->lock = YMLockCreateWithOptionsAndName(YMInternalLockType, memberName);
    userInfo->bytesAvailable = 0;
    userInfo->userClosed = false;
    userInfo->rawWritten = 0;
    userInfo->muxerWritten = 0;
    userInfo->rawRead = 0;
    userInfo->muxerRead = 0;
    YMStreamRef theStream = _YMStreamCreate(memberName, (ym_stream_user_info_t *)userInfo, __ym_plexer_free_stream_info);
    YMRelease(memberName);
    _YMStreamSetDataAvailableCallback(theStream, __ym_plexer_stream_data_available_proc, p);
    
    YMRelease(userNameToRelease);
    
    return theStream;
}

void __ym_plexer_free_stream_info(YMStreamRef stream)
{
    __ym_plexer_stream_user_info_t *userInfo = YM_STREAM_INFO(stream);
    YMRelease(userInfo->lock);
    YMRelease(userInfo->plexer);
    free(userInfo->lastServiceTime);
    free(userInfo);
}

void __ym_plexer_stream_data_available_proc(YMStreamRef stream, uint32_t bytes, void *ctx)
{
    __ym_plexer_t *p = (__ym_plexer_t *)ctx;
    __ym_plexer_stream_user_info_t *userInfo = YM_STREAM_INFO(stream);
    YMPlexerStreamID streamID = userInfo->streamID;
    ymlog("s%lu reports it has %u bytes ready",streamID,bytes);
    YMLockLock(userInfo->lock);
        userInfo->bytesAvailable += bytes;
    YMLockUnlock(userInfo->lock);
    YMSemaphoreSignal(p->downstreamReadySemaphore);
}

// tears down the plexer, returning whether or not this was the 'first call' to interrupted
// (i/o calls should treat their errors as 'real', and subsequent errors on other threads
// can return quietly)
bool __YMPlexerInterrupt(__ym_plexer_t *p)
{
    bool firstInterrupt = false;
    YMLockLock(p->interruptionLock);
    {
        firstInterrupt = p->active;
        
        // before closing fds to ensure up/down threads wake up and exit, flag them
        p->active = false;
    }
    YMLockUnlock(p->interruptionLock);
    
    if ( ! firstInterrupt )
        return false;
    
    ymerr("interrupted");
    
    YMSecurityProviderClose(p->provider);
    
    YMSemaphoreSignal(p->downstreamReadySemaphore);

	YMTypeRef *listOfLocksAndLists[] = { (YMTypeRef[]) { p->localAccessLock, p->localStreamsByID },
		(YMTypeRef[]) { p->remoteAccessLock, p->remoteStreamsByID } };

    for ( int i = 0; i < __YMListMax; i++ ) {
        YMLockRef aLock = listOfLocksAndLists[i][__YMLockListIdx];
        YMDictionaryRef aList = listOfLocksAndLists[i][__YMListListIdx];
        YMLockLock(aLock);
        {
            while ( YMDictionaryGetCount(aList) > 0 ) {
                YMDictionaryKey aRandomKey = YMDictionaryGetRandomKey(aList);
                YMStreamRef aStream = YMDictionaryGetItem(aList, aRandomKey);
                YMPlexerStreamID aStreamID = YM_STREAM_INFO(aStream)->streamID;
                YMDictionaryRemove(aList, aRandomKey);
                ymerr("hanging up s%lu",aStreamID);
                _YMStreamCloseWriteUp(aStream);
                YMRelease(aStream);
            }
        }
        YMLockUnlock(aLock);
    }
    
    // if the client stops us, they don't expect a callback
    if ( p->interruptedFunc )
		__YMPlexerDispatchFunctionWithName(p, NULL, p->eventDeliveryThread, __ym_plexer_notify_interrupted, YMSTRC("plexer-interrupted"));
    
//    // also created on init, so long as we're locking might be redundant
//    if ( plexer->eventDeliveryThread )
//    {
//        // releasing a dispatch thread should cause YMThreadDispatch to set the stop flag
//        // in the context struct (allocated on creation, free'd by the exiting thread)
//        // allowing it to go away. don't null it, or we'd need another handshake on the
//        // thread referencing itself via plexer and exiting.
//        YMRelease(plexer->eventDeliveryThread);
//    }
    
    if ( _YMThreadGetCurrentThreadNumber() != _YMThreadGetThreadNumber(p->eventDeliveryThread) )
        YMThreadDispatchJoin(p->eventDeliveryThread);
    
    return true;
}

#pragma mark dispatch

void __YMPlexerDispatchFunctionWithName(__ym_plexer_t *p, YMStreamRef stream, YMThreadRef targetThread, ym_dispatch_user_func function, YMStringRef nameToRelease)
{
    _ym_plexer_and_stream_t *notifyDef = (_ym_plexer_and_stream_t *)YMALLOC(sizeof(_ym_plexer_and_stream_t));
    notifyDef->p = (__ym_plexer_t *)YMRetain(p);
    notifyDef->s = stream ? YMRetain(stream) : NULL;
    ym_thread_dispatch_user_t dispatch = { function, NULL, true, notifyDef, nameToRelease };
    
//#define YMPLEXER_NO_EVENT_QUEUE // for debugging
#ifdef YMPLEXER_NO_EVENT_QUEUE
    __unused const void *silence = targetThread;
    function(&dispatch);
    free(notifyDef);
#else
    YMThreadDispatchDispatch(targetThread, dispatch);
#endif
    
    YMRelease(nameToRelease);
}

void YM_CALLING_CONVENTION __ym_plexer_notify_new_stream(YM_THREAD_PARAM context)
{
    _ym_plexer_and_stream_t *notifyDef = (_ym_plexer_and_stream_t *)context;
    __ym_plexer_t *p = notifyDef->p;
    YMStreamRef stream = notifyDef->s;
    YMPlexerStreamID streamID = YM_STREAM_INFO(stream)->streamID;
    
    ymlog("ym_notify_new_stream entered s%lu", streamID);
    if ( p->newIncomingFunc )
        p->newIncomingFunc(p,stream,p->callbackContext);
    ymlog("ym_notify_new_stream exiting s%lu", streamID);
    
    YMRelease(p);
    YMRelease(stream);
    //YMRelease(dispatch->description); // done by ThreadDispatch
}

void YM_CALLING_CONVENTION __ym_plexer_notify_stream_closing(YM_THREAD_PARAM context)
{
    _ym_plexer_and_stream_t *notifyDef = (_ym_plexer_and_stream_t *)context;
    __ym_plexer_t *p = notifyDef->p;
    YMStreamRef stream = notifyDef->s;
    YMPlexerStreamID streamID = YM_STREAM_INFO(stream)->streamID;
    
    ymlog("ym_notify_stream_closing entered s%lu", streamID);
    if ( p->closingFunc )
        p->closingFunc(p,stream,p->callbackContext);
    
    CHECK_REMOVE_RELEASE(false, (YMDictionaryKey)streamID, true);
    
    ymlog("ym_notify_stream_closing exiting s%lu", streamID);
    
    YMRelease(p);
    YMRelease(stream);
    //YMRelease(dispatch->description); // done by ThreadDispatch
}

void YM_CALLING_CONVENTION __ym_plexer_notify_interrupted(YM_THREAD_PARAM context)
{
    _ym_plexer_and_stream_t *notifyDef = (_ym_plexer_and_stream_t *)context;
    __ym_plexer_t *p = notifyDef->p;
    
    ymlog("ym_notify_interrupted entered");
    if ( p->interruptedFunc )
        p->interruptedFunc(p,p->callbackContext);
    ymlog("ym_notify_interrupted exiting");
    
    YMRelease(p);
    //YMRelease(dispatch->description); // done by ThreadDispatch
}

YM_EXTERN_C_POP
