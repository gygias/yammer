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
#define ymlog_args plexer->name?YMSTR(plexer->name):"*"
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
                                                YMSelfLock(plexer); bool _interrupted = ! plexer->active; YMSelfUnlock(plexer); \
                                                if ( ! _interrupted ) { \
                                                    YMDictionaryRef _list = local ? plexer->localStreamsByID : plexer->remoteStreamsByID; \
                                                    if ( local ) YMSelfLock(plexer); else YMLockLock(plexer->remoteStreamsLock); \
                                                    YMStreamRef _theStream; \
                                                    bool _okay = ( remove ? \
                                                                    ( ( _theStream = YMDictionaryRemove(_list,streamID) ) != NULL ) : \
                                                                      YMDictionaryContains(_list,streamID) ); \
                                                    if ( ! _okay ) { ymabort("plexer consistenty check failed"); } \
                                                    if ( remove ) { YMRelease(_theStream); } \
                                                    if ( local ) YMSelfUnlock(plexer); else YMLockUnlock(plexer->remoteStreamsLock); } \
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

void YM_CALLING_CONVENTION __ym_plexer_notify_new_stream(ym_thread_dispatch_ref);
void YM_CALLING_CONVENTION __ym_plexer_notify_stream_closing(ym_thread_dispatch_ref);
void YM_CALLING_CONVENTION __ym_plexer_notify_interrupted(ym_thread_dispatch_ref);

// linked to 'private' definition in YMPlexerPriv
typedef struct __ym_plexer_stream_user_info_t
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
} __ym_plexer_stream_user_info_t;
typedef struct __ym_plexer_stream_user_info_t * __ym_plexer_stream_user_info_ref;

#undef YM_STREAM_INFO
#define YM_STREAM_INFO(x) ((__ym_plexer_stream_user_info_ref)_YMStreamGetUserInfo(x))

typedef struct {
    YMPlexerCommandType command;
    YMPlexerStreamID streamID;
} YMPlexerMessage;

typedef struct __ym_plexer_t
{
    _YMType _type;
    
    YMStringRef name;
    YMSecurityProviderRef provider;
    
    bool active; // intialized and happy
    bool stopped; // stop called before files start closing
    bool master;
    bool myStreamsEven;
    
    // the downstream
    YMDictionaryRef localStreamsByID;
    uint8_t *localPlexBuffer;
    uint32_t localPlexBufferSize;
    YMPlexerStreamID localStreamIDLast;
    
    // the upstream
    YMDictionaryRef remoteStreamsByID;
    YMLockRef remoteStreamsLock;
    uint8_t *remotePlexBuffer;
    uint32_t remotePlexBufferSize;
    
    YMThreadRef localServiceThread;
    YMThreadRef remoteServiceThread;
    YMThreadRef eventDeliveryThread;
    YMSemaphoreRef downstreamReadySemaphore;
    
    // user
    ym_plexer_interrupted_func interruptedFunc;
    ym_plexer_new_upstream_func newIncomingFunc;
    ym_plexer_stream_closing_func closingFunc;
    void *callbackContext;
} __ym_plexer_t;
typedef struct __ym_plexer_t *__YMPlexerRef;

// generic context pointer definition for "plexer & stream" entry points
typedef struct __ym_dispatch_plexer_stream_def
{
    __YMPlexerRef plexer;
    YMStreamRef stream;
} _ym_dispatch_plexer_stream_def;
typedef struct __ym_dispatch_plexer_stream_def __ym_dispatch_plexer_and_stream;
typedef __ym_dispatch_plexer_and_stream *__ym_dispatch_plexer_and_stream_ref;

void __YMRegisterSigpipe();
void __ym_sigpipe_handler (int signum);
YMStreamRef __YMPlexerRetainReadyStream(__YMPlexerRef plexer);
void __YMPlexerDispatchFunctionWithName(__YMPlexerRef plexer, YMStreamRef stream, YMThreadRef targetThread, ym_thread_dispatch_func function, YMStringRef nameToRelease);
bool __YMPlexerStartServiceThreads(__YMPlexerRef plexer);
bool __YMPlexerDoInitialization(__YMPlexerRef plexer, bool master);
bool __YMPlexerInitAsMaster(__YMPlexerRef plexer);
bool __YMPlexerInitAsSlave(__YMPlexerRef plexer);
YM_THREAD_RETURN YM_CALLING_CONVENTION __ym_plexer_service_downstream_proc(YM_THREAD_PARAM);
YM_THREAD_RETURN YM_CALLING_CONVENTION __ym_plexer_service_upstream_proc(YM_THREAD_PARAM);
bool __YMPlexerServiceADownstream(__YMPlexerRef plexer, YMStreamRef servicingStream);
YMStreamRef __YMPlexerRetainOrCreateRemoteStreamWithID(__YMPlexerRef plexer, YMPlexerStreamID streamID);
YMStreamRef __YMPlexerCreateStreamWithID(__YMPlexerRef plexer, YMPlexerStreamID streamID, bool isLocal, YMStringRef userNameToRelease);
bool __YMPlexerInterrupt(__YMPlexerRef plexer);
void __ym_plexer_stream_data_available_proc(YMStreamRef stream, uint32_t bytes, void *ctx);
void __ym_plexer_free_stream_info(YMStreamRef stream);

#define YMPlexerDefaultBufferSize 16384

YMPlexerRef YMPlexerCreate(YMStringRef name, YMSecurityProviderRef provider, bool master)
{
	__YMRegisterSigpipe();
    
    __YMPlexerRef plexer = (__YMPlexerRef)_YMAlloc(_YMPlexerTypeID,sizeof(struct __ym_plexer_t));
    
    plexer->name = YMStringCreateWithFormat("%s(%s)",name?YMSTR(name):"*",master?"m":"s",NULL);
    plexer->provider = YMRetain(provider);
    
    plexer->active = false;
    plexer->master = master;
    plexer->stopped = false;
    plexer->myStreamsEven = false;
    
    plexer->localStreamsByID = YMDictionaryCreate();
    
    plexer->localPlexBufferSize = YMPlexerDefaultBufferSize;
    plexer->localPlexBuffer = YMALLOC(plexer->localPlexBufferSize);
    
    plexer->remoteStreamsByID = YMDictionaryCreate();
    YMStringRef aString = YMStringCreateWithFormat("%s-remote",YMSTR(plexer->name),NULL);
    plexer->remoteStreamsLock = YMLockCreateWithOptionsAndName(YMInternalLockType,aString);
    YMRelease(aString);
    plexer->remotePlexBufferSize = YMPlexerDefaultBufferSize;
    plexer->remotePlexBuffer = YMALLOC(plexer->remotePlexBufferSize);
    
    aString = YMStringCreateWithFormat("%s-down",YMSTR(plexer->name),NULL);
    plexer->localServiceThread = YMThreadCreate(aString, __ym_plexer_service_downstream_proc, (void *)YMRetain(plexer));
    YMRelease(aString);
    
    aString = YMStringCreateWithFormat("%s-up",YMSTR(plexer->name),NULL);
    plexer->remoteServiceThread = YMThreadCreate(aString, __ym_plexer_service_upstream_proc, (void *)YMRetain(plexer));
    YMRelease(aString);
    
    aString = YMStringCreateWithFormat("%s-event",YMSTR(plexer->name),NULL);
    plexer->eventDeliveryThread = YMThreadDispatchCreate(aString);
    YMRelease(aString);
    
    aString = YMStringCreateWithFormat("%s-down-signal",YMSTR(plexer->name),NULL);
    plexer->downstreamReadySemaphore = YMSemaphoreCreateWithName(aString,0);
    YMRelease(aString);
    
    plexer->interruptedFunc = NULL;
    plexer->newIncomingFunc = NULL;
    plexer->closingFunc = NULL;
    plexer->callbackContext = NULL;
    
    return plexer;
}

void _YMPlexerFree(YMPlexerRef plexer_)
{
    __YMPlexerRef plexer = (__YMPlexerRef)plexer_;
    
    // ensure that if we haven't been stopped, or interrupted, we hang up
    bool first = __YMPlexerInterrupt(plexer);
    ymerr("deallocating (%s)",first?"stopping":"already stopped");
    
    YMRelease(plexer->name);
    YMRelease(plexer->provider);
    
    YMSemaphoreSignal(plexer->downstreamReadySemaphore);
    YMThreadJoin(plexer->localServiceThread);
    YMThreadJoin(plexer->remoteServiceThread);
    
    YMRelease(plexer->downstreamReadySemaphore);
    
    free(plexer->localPlexBuffer);
    YMRelease(plexer->localStreamsByID);
    YMRelease(plexer->localServiceThread);
    free(plexer->remotePlexBuffer);
    YMRelease(plexer->remoteStreamsByID);
    YMRelease(plexer->remoteStreamsLock);
    YMRelease(plexer->remoteServiceThread);
    
    YMRelease(plexer->eventDeliveryThread);
}

void YMPlexerSetInterruptedFunc(YMPlexerRef plexer_, ym_plexer_interrupted_func func)
{
    __YMPlexerRef plexer = (__YMPlexerRef)plexer_;
    plexer->interruptedFunc = func;
}

void YMPlexerSetNewIncomingStreamFunc(YMPlexerRef plexer_, ym_plexer_new_upstream_func func)
{
    __YMPlexerRef plexer = (__YMPlexerRef)plexer_;
    plexer->newIncomingFunc = func;
}

void YMPlexerSetStreamClosingFunc(YMPlexerRef plexer_, ym_plexer_stream_closing_func func)
{
    __YMPlexerRef plexer = (__YMPlexerRef)plexer_;
    plexer->closingFunc = func;
}

void YMPlexerSetCallbackContext(YMPlexerRef plexer_, void *context)
{
    __YMPlexerRef plexer = (__YMPlexerRef)plexer_;
    plexer->callbackContext = context;
}

bool YMPlexerStart(YMPlexerRef plexer_)
{
    __YMPlexerRef plexer = (__YMPlexerRef)plexer_;
    
    bool okay;
    
    if ( plexer->active ) {
        ymerr("user: this plexer is already initialized");
        return false;
    }
    
    if ( plexer->master )
        okay = __YMPlexerInitAsMaster(plexer);
    else
        okay = __YMPlexerInitAsSlave(plexer);
    
    if ( ! okay )
        goto catch_fail;
    
    ymlog("initialized as %s with %s stream ids",plexer->master?"master":"slave",plexer->myStreamsEven?"even":"odd");
    
    // this flag is used to let our threads exit, among other things
    plexer->active = true;
    
    okay = YMThreadStart(plexer->localServiceThread);
    if ( ! okay ) {
        ymerr("failed to detach down service thread");
        goto catch_fail;
    }
    
    okay = YMThreadStart(plexer->remoteServiceThread);
    if ( ! okay ) {
        ymerr("failed to detach up service thread");
        goto catch_fail;
    }
    
    okay = YMThreadStart(plexer->eventDeliveryThread);
    if ( ! okay ) {
        ymerr("failed to detach event thread");
        goto catch_fail;
    }
    
    ymlog("started");
    
catch_fail:
    return okay;
}

YMStreamRef YMAPI YMPlexerCreateStream(YMPlexerRef plexer_, YMStringRef name)
{
    __YMPlexerRef plexer = (__YMPlexerRef)plexer_;
    
    YMStreamRef newStream = NULL;
    YMSelfLock(plexer);
    do {
        if ( plexer->localStreamIDLast >= ( YMPlexerStreamIDMax - 1 ) )
            plexer->localStreamIDLast = plexer->myStreamsEven ? 0 : 1;
        else
            plexer->localStreamIDLast += 2;
        
        YMPlexerStreamID newStreamID = plexer->localStreamIDLast;
        if ( YMDictionaryContains(plexer->localStreamsByID, (YMDictionaryKey)newStreamID) ) {
            ymerr("fatal: plexer ran out of streams");
            __YMPlexerInterrupt(plexer);
            break;
        }
        
        YMStringRef userName = name ? YMRetain(name) : YMSTRC("*");
        newStream = __YMPlexerCreateStreamWithID(plexer, newStreamID, true, userName);
        
        YMDictionaryAdd(plexer->localStreamsByID, (YMDictionaryKey)newStreamID, (void *)newStream);
        YMRetain(newStream); // retain for user
        
    } while (false);
    YMSelfUnlock(plexer);
    
    return newStream;
}

// void: if this fails, it's either a bug or user error
void YMPlexerCloseStream(YMPlexerRef plexer_, YMStreamRef stream)
{
    __YMPlexerRef plexer = (__YMPlexerRef)plexer_;
    
    __ym_plexer_stream_user_info_ref userInfo = YM_STREAM_INFO(stream);
    YMPlexerStreamID streamID = userInfo->streamID;
    bool isLocal = userInfo->isLocallyOriginated;
    
    ymassert(!userInfo->userClosed,"stream double-close"); // only catches local
    
    if ( plexer->active && isLocal ) {
        userInfo->userClosed = true;
        // when stream commands were a thing
        YMSemaphoreSignal(plexer->downstreamReadySemaphore);
    }
    
    ymlog("user %s stream %llu", isLocal?"closing":"releasing", streamID);
    YMRelease(stream); // release user
}

bool YMPlexerStop(YMPlexerRef plexer_)
{
    __YMPlexerRef plexer = (__YMPlexerRef)plexer_;
    plexer->interruptedFunc = NULL;
    plexer->newIncomingFunc = NULL;
    plexer->closingFunc = NULL;
    bool interrupted = __YMPlexerInterrupt(plexer);
    
    // once we're stopped (or notified interrupted), we can be deallocated. service threads hold retains on the plexer object,
    // and x509 certificates embedded in the security provider actually retain a function pointer to
    // their lock callback, and will call it when the X509 is free'd! that means if the client
    // stops and immediately releases us on whatever thread, plexer might not be deallocated until
    // the final service thread actually exits, which in turn depends on Globals Free'd by user (test case).
    //
    // we can't do this from the common __Interrupt method, because a thread might be the first to report a
    // real error (disconnect), so clients must stop the plexer even after 'interrupted' event before deallocating
    YMThreadDispatchJoin(plexer->eventDeliveryThread);
    YMSemaphoreSignal(plexer->downstreamReadySemaphore);
    
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

bool __YMPlexerInitAsMaster(__YMPlexerRef plexer_)
{
    __YMPlexerRef plexer = (__YMPlexerRef)plexer_;
    
    bool okay = YMSecurityProviderWrite(plexer->provider, (void *)YMPlexerMasterHello, strlen(YMPlexerMasterHello));
    if ( ! okay ) {
        ymerr("m: master hello write");
        return false;
    }
    
    unsigned long inHelloLen = strlen(YMPlexerSlaveHello);
    char inHello[64];
    okay = YMSecurityProviderRead(plexer->provider, (void *)inHello, inHelloLen);
    if ( ! okay || memcmp(YMPlexerSlaveHello,inHello,inHelloLen) ) {
        ymerr("m: slave hello read");
        return false;
    }
    
    plexer->myStreamsEven = true;
    plexer->localStreamIDLast = YMPlexerStreamIDMax - ( plexer->myStreamsEven ? 1 : 0 );
    
    YMPlexerInitializer initializer = { YMPlexerBuiltInVersion, YMPlexerStreamIDMax, plexer->myStreamsEven };
    okay = YMSecurityProviderWrite(plexer->provider, (void *)&initializer, sizeof(initializer));
    if ( ! okay ) {
        ymerr("m: master init write");
        return false;
    }
    
    YMPlexerSlaveAck ack;
    okay = YMSecurityProviderRead(plexer->provider, (void *)&ack, sizeof(ack));
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

bool __YMPlexerInitAsSlave(__YMPlexerRef plexer)
{
    unsigned long inHelloLen = strlen(YMPlexerMasterHello);
    char inHello[64];
    bool okay = YMSecurityProviderRead(plexer->provider, (void *)inHello, inHelloLen);
    
    if ( ! okay || memcmp(YMPlexerMasterHello,inHello,inHelloLen) ) {
        ymerr("s: master hello read");
        return false;
    }
    
    okay = YMSecurityProviderWrite(plexer->provider, (void *)YMPlexerSlaveHello, strlen(YMPlexerSlaveHello));
    if ( ! okay ) {
        ymerr("s: slave hello write");
        return false;
    }
    
    YMPlexerInitializer initializer;
    okay = YMSecurityProviderRead(plexer->provider, (void *)&initializer, sizeof(initializer));
    if ( ! okay ) {
        ymerr("s: master init read");
        return false;
    }
    
    // todo, technically this should handle non-zero-based master min id, but doesn't
    plexer->myStreamsEven = ! initializer.evenStreamIDs;
    plexer->localStreamIDLast = YMPlexerStreamIDMax - ( plexer->myStreamsEven ? 1 : 0 );
    
    bool supported = initializer.protocolVersion <= YMPlexerBuiltInVersion;
    YMPlexerSlaveAck ack = { YMPlexerBuiltInVersion };
    okay = YMSecurityProviderWrite(plexer->provider, (void *)&ack, sizeof(ack));
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
    __YMPlexerRef plexer = (__YMPlexerRef)ctx;
    //YMRetain(plexer); // retained on thread creation, matched at the end of this function
    
    ymlog("downstream service thread entered");
    
    while( plexer->active ) {
        ymlog("V awaiting signal");
        // there is only one thread consuming this semaphore, so i think it's ok not to actually lock around this loop iteration
        YMSemaphoreWait(plexer->downstreamReadySemaphore);
        
        if ( ! plexer->active ) {
            ymerr("V signaled to exit");
            break;
        }
        
        YMStreamRef servicingStream = __YMPlexerRetainReadyStream(plexer);
        
        ymlog("V signaled,");
        
        // todo about not locking until we've consumed as many semaphore signals as we can
        //while ( --readyStreams )
        {
            if ( ! servicingStream ) {
                //ymlog("coalescing signals");
                continue;
            }
            
            YM_DEBUG_ASSERT_MALLOC(servicingStream);
            bool okay = __YMPlexerServiceADownstream(plexer, servicingStream);
            YMRelease(servicingStream);
            if ( ! okay || ! plexer->active ) {
                if ( __YMPlexerInterrupt(plexer) )
                    ymerr("p: service downstream failed");
                break;
            }
        }
    }
    
    ymerr("downstream service thread exiting");
    
    YMRelease(plexer);

	YM_THREAD_END
}

YMStreamRef __YMPlexerRetainReadyStream(__YMPlexerRef plexer)
{
    YMStreamRef oldestStream = NULL;
    struct timeval newestTime = {0,0};
    YMGetTheEndOfPosixTimeForCurrentPlatform(&newestTime);
    
    for( int i = 0; i < 2; i++ ) {
        YMDictionaryRef aStreamsById = ( i == 0 ) ? plexer->localStreamsByID : plexer->remoteStreamsByID;
        
        if ( i == 0 ) YMSelfLock(plexer) ; else YMLockLock(plexer->remoteStreamsLock);
        {
            YMDictionaryEnumRef aStreamsEnum = YMDictionaryEnumeratorBegin(aStreamsById);
            YMDictionaryEnumRef aStreamsEnumPrev = NULL;
            while ( aStreamsEnum ) {
                YMStreamRef aStream = (YMStreamRef)aStreamsEnum->value;
                //YM_DEBUG_ABORT_IF_BOGUS(oldestStream);
                
                __ym_plexer_stream_user_info_ref userInfo = YM_STREAM_INFO(aStream);
                YMPlexerStreamID aStreamID = userInfo->streamID;
                
                ymlog("choose: considering %s downstream %llu",(i==0)?"local":"remote",aStreamID);
                
                // we are the only consumer of bytesAvailable, no need to lock here, but we will when we actually 'consume' them later on
                uint64_t aStreamBytesAvailable = 0;
                if ( userInfo->userClosed ) {
                    ymlog("choose: stream %llu is closing",aStreamID);
                    if ( ! userInfo->isLocallyOriginated )
                        ymabort("remote stream %llu locally closed",aStreamID);
                }
                else {
                    aStreamBytesAvailable = YM_STREAM_INFO(aStream)->bytesAvailable;
                    if ( aStreamBytesAvailable == 0 ) {
                        ymlog("choose: stream %llu is empty",aStreamID);
                        goto catch_continue;
                    }
                }
                
                ymlog("choose: %s stream %llu is ready! %s%llub",(i==0)?"local":"remote",aStreamID,userInfo->userClosed?"(closing) ":"",aStreamBytesAvailable);
                
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
                ymlog("choose: %s list is empty",(i==0)?"down stream":"up stream");
                
        }
        if ( i == 0 ) YMSelfUnlock(plexer); else YMLockUnlock(plexer->remoteStreamsLock);
    }
    
    if ( oldestStream ) YM_DEBUG_ASSERT_MALLOC(oldestStream);
    return oldestStream;
}

bool __YMPlexerServiceADownstream(__YMPlexerRef plexer, YMStreamRef stream)
{
    YM_DEBUG_ASSERT_MALLOC(stream);
    
    // update last service time on stream
    __ym_plexer_stream_user_info_ref userInfo = YM_STREAM_INFO(stream);
    YMPlexerStreamID streamID = userInfo->streamID;
    if ( 0 != gettimeofday(userInfo->lastServiceTime, NULL) ) {
        ymerr("V-s%llu setting initial service time for stream: %d (%s)",streamID,errno,strerror(errno));
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
    
    ymlog("V-s%llu will flush %llu bytes",streamID,bytesAvailable);
    
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
        
        _YMStreamReadDown(stream, plexer->localPlexBuffer, chunkLength);
        if ( ! plexer->active )
            return false;
        ymlog("V-s%llu read stream chunk",streamID);
        
        if ( bytesRemaining - chunkLength > bytesRemaining )
            ymabort("stream %llu overchunked",streamID);
        
        bytesRemaining -= chunkLength;
        chunksHandled++;
        
        YMPlexerMessage plexMessage = { chunkLength, streamID };
        size_t plexMessageLen = sizeof(plexMessage);
		bool okay = YMSecurityProviderWrite(plexer->provider, (uint8_t *)&plexMessage, plexMessageLen);
        if ( ! okay || ! plexer->active ) {
            ymerr("p: V-s%llu failed writing plex message size %zub: %d (%s)",streamID,plexMessageLen,errno,strerror(errno));
            return false;
        }
        userInfo->muxerWritten += plexMessageLen;
        
        ymlog("V-s%llu wrote plex header",streamID);
        
		okay = YMSecurityProviderWrite(plexer->provider, plexer->localPlexBuffer, chunkLength);
        if ( ! okay || ! plexer->active ) {
            bool interrupt = __YMPlexerInterrupt(plexer);
            if ( interrupt )
                ymerr("p: V-s%llu failed writing plex buffer %ub: %d (%s)",streamID,plexMessage.command,errno,strerror(errno));
            return false;
        }
        userInfo->rawWritten += chunkLength;
        
        ymlog("V-s%llu wrote plex buffer %ub",streamID,plexMessage.command);
    }
    
    ymlog("V-s%llu flushed %llu chunk(s) and %llu bytes",streamID,chunksHandled,bytesAvailable);
    
    if ( closing ) {
        if ( ! userInfo->isLocallyOriginated )
            ymabort("remote stream %llu closed locally",streamID);
        
        bool okay = true;
        YMPlexerMessage plexMessage = { YMPlexerCommandCloseStream, streamID };
        size_t plexMessageLen = sizeof(plexMessage);
        okay = YMSecurityProviderWrite(plexer->provider, (uint8_t *)&plexMessage, plexMessageLen);
        if ( ! okay || ! plexer->active ) {
            bool interrupt = __YMPlexerInterrupt(plexer);
            if ( interrupt )
                ymerr("V-s%llu perror: failed writing plex message size %zub: %d (%s)",streamID,plexMessageLen,errno,strerror(errno));
        }
        userInfo->muxerWritten += plexMessageLen;
        
        ymlog("V-s%llu stream closing (rW%llu,pW%llu,rR%llu,pR%llu): %p (%s)",streamID,userInfo->rawWritten,userInfo->muxerRead,userInfo->rawRead,userInfo->muxerRead,stream,YMSTR(_YMStreamGetName(stream)));
        
        CHECK_REMOVE_RELEASE(true, (YMDictionaryKey)streamID, true);
        
        return okay;
    }
    
    return true;
}

YM_THREAD_RETURN YM_CALLING_CONVENTION __ym_plexer_service_upstream_proc(YM_THREAD_PARAM ctx)
{
    __YMPlexerRef plexer = (__YMPlexerRef)ctx;
    
    ymlog("^ upstream service thread entered");
    
    bool okay = true;
    while ( okay && plexer->active ) {
        bool streamClosing = false;
        size_t chunkLength = 0;
        YMPlexerMessage plexerMessage;
        
		okay = YMSecurityProviderRead(plexer->provider, (uint8_t *)&plexerMessage, sizeof(plexerMessage));
        if ( ! okay || ! plexer->active || plexerMessage.command < YMPlexerCommandMin || plexerMessage.command > UINT16_MAX ) {
            bool interrupt = __YMPlexerInterrupt(plexer);
            if ( interrupt )
                ymerr("^ perror: failed reading plex header: %d (%s)",errno,strerror(errno));
            break;
        } else if ( plexerMessage.command == YMPlexerCommandCloseStream )
            streamClosing = true;
        else {
            chunkLength = plexerMessage.command;
            ymlog("^-s%llu read plex header %zub",plexerMessage.streamID,chunkLength);
        }
        
        YMPlexerStreamID streamID = plexerMessage.streamID;
        YMStreamRef theStream = __YMPlexerRetainOrCreateRemoteStreamWithID(plexer, streamID); // todo retain this until this iter is done
        if ( ! theStream || ! plexer->active ) {
            bool interrupt = __YMPlexerInterrupt(plexer);
            if ( interrupt )
                ymerr("fatal: ^-s%llu stream lookup",streamID);
            if ( theStream ) YMRelease(theStream);
            break;
        }
        
        __ym_plexer_stream_user_info_ref userInfo = YM_STREAM_INFO(theStream);
        userInfo->muxerRead += sizeof(plexerMessage);
        
        if ( streamClosing ) {
            YMStringRef memberName = YMStringCreateWithFormat("%s-s%llu-%s",YMSTR(plexer->name),streamID,YM_TOKEN_STR(__ym_plexer_notify_stream_closing), NULL);
            // close 'read up', so that if client (or forward file) is reading unbounded data it will get signaled
            _YMStreamCloseWriteUp(theStream);
			if ( plexer->closingFunc )
				__YMPlexerDispatchFunctionWithName(plexer, theStream, plexer->eventDeliveryThread, __ym_plexer_notify_stream_closing, memberName);
            ymlog("^-s%llu stream closing (rW%llu,pW%llu,rR%llu,pR%llu)",plexerMessage.streamID,userInfo->rawWritten,userInfo->muxerWritten,userInfo->rawRead,userInfo->muxerRead);
            YMRelease(theStream);
            continue;
        }
        
        ymassert(chunkLength<=UINT16_MAX&&chunkLength!=0,"upstream chuck length");
        
        okay = YMSecurityProviderRead(plexer->provider, plexer->remotePlexBuffer, chunkLength);
        if ( ! okay || ! plexer->active ) {
            bool interrupt = __YMPlexerInterrupt(plexer);
            if ( interrupt )
                ymerr("p: ^-s%llu failed reading plex buffer of length %zub: %d (%s)",streamID,chunkLength,errno,strerror(errno));
            YMRelease(theStream);
            break;
        }
        userInfo->rawRead += chunkLength;
        ymlog("^-s%llu read plex buffer %zub",streamID,chunkLength);
        
        YMIOResult result = _YMStreamWriteUp(theStream, plexer->remotePlexBuffer, (uint32_t)chunkLength);
        if ( result != YMIOSuccess || ! plexer->active ) {
            bool interrupt = __YMPlexerInterrupt(plexer);
            if ( interrupt )
                ymerr("internal fatal: ^-s%llu failed writing plex buffer of length %zub: %d (%s)",streamID,chunkLength,errno,strerror(errno));
            YMRelease(theStream);
            break;
        }
        
        ymlog("^-s%llu wrote plex buffer %zub",streamID,chunkLength);
        YMRelease(theStream);
    }
    
    ymerr("upstream service thread exiting");
    
    YMRelease(plexer);

	YM_THREAD_END
}

YMStreamRef __YMPlexerRetainOrCreateRemoteStreamWithID(__YMPlexerRef plexer, YMPlexerStreamID streamID)
{
    YMStreamRef theStream = NULL;
    
    YMSelfLock(plexer);
    {
        theStream = (YMStreamRef)YMDictionaryGetItem(plexer->localStreamsByID,(YMDictionaryKey)streamID);
        if ( theStream ) {
            ymlog("L-%llu found existing local stream",streamID);
            YMRetain(theStream);
        }
    }
    YMSelfUnlock(plexer);
    
    if ( ! theStream ) {
        YMLockLock(plexer->remoteStreamsLock);
        {
            theStream = (YMStreamRef)YMDictionaryGetItem(plexer->remoteStreamsByID,(YMDictionaryKey)streamID);
            if ( theStream ) {
                ymlog("L-%llu found existing remote stream",streamID);
                YMRetain(theStream);
            }
        
            // new stream
            if ( ! theStream ) {
                if ( ( streamID % 2 == 0 ) && plexer->myStreamsEven ) {
                    YMLockUnlock(plexer->remoteStreamsLock);
                    
                    if ( __YMPlexerInterrupt(plexer) )
                        ymerr("internal: L-%llu stream id collision",streamID);
                    
                    return NULL;
                }
                
                ymlog("L-%llu notifying new incoming",streamID);
                
                YMStringRef name = YMSTRC("remote");
                theStream = __YMPlexerCreateStreamWithID(plexer,streamID,false,name);
                
                YMDictionaryAdd(plexer->remoteStreamsByID, (YMDictionaryKey)streamID, theStream);
                YMRetain(theStream); // retain for user (complements close)
                YMRetain(theStream); // retain for function
                
                name = YMStringCreateWithFormat("%s-notify-new-%llu",YMSTR(plexer->name),streamID,NULL);
				if ( plexer->newIncomingFunc )
					__YMPlexerDispatchFunctionWithName(plexer, theStream, plexer->eventDeliveryThread, __ym_plexer_notify_new_stream, name);
            }
        }    
        YMLockUnlock(plexer->remoteStreamsLock);
    }
    
    return theStream;
}

YMStreamRef __YMPlexerCreateStreamWithID(__YMPlexerRef plexer, YMPlexerStreamID streamID, bool isLocal, YMStringRef userNameToRelease)
{
    __ym_plexer_stream_user_info_ref userInfo = (__ym_plexer_stream_user_info_ref)YMALLOC(sizeof(struct __ym_plexer_stream_user_info_t));
    userInfo->plexer = YMRetain(plexer);
    userInfo->streamID = streamID;
    userInfo->isLocallyOriginated = isLocal;
    userInfo->lastServiceTime = (struct timeval *)YMALLOC(sizeof(struct timeval));
    if ( 0 != gettimeofday(userInfo->lastServiceTime, NULL) ) {
        ymlog("warning: error setting initial service time for s%llu: %d (%s)",streamID,errno,strerror(errno));
        YMGetTheBeginningOfPosixTimeForCurrentPlatform(userInfo->lastServiceTime);
    }
    
    YMStringRef memberName = YMStringCreateWithFormat("%s-%s-s%llu-%s",plexer->master?"m":"s",isLocal?">":"<",streamID,YMSTR(userNameToRelease),NULL);
    userInfo->lock = YMLockCreateWithOptionsAndName(YMInternalLockType, memberName);
    userInfo->bytesAvailable = 0;
    userInfo->userClosed = false;
    userInfo->rawWritten = 0;
    userInfo->muxerWritten = 0;
    userInfo->rawRead = 0;
    userInfo->muxerRead = 0;
    YMStreamRef theStream = _YMStreamCreate(memberName, (ym_stream_user_info_ref)userInfo, __ym_plexer_free_stream_info);
    YMRelease(memberName);
    _YMStreamSetDataAvailableCallback(theStream, __ym_plexer_stream_data_available_proc, plexer);
    
    YMRelease(userNameToRelease);
    
    return theStream;
}

void __ym_plexer_free_stream_info(YMStreamRef stream)
{
    __ym_plexer_stream_user_info_ref userInfo = YM_STREAM_INFO(stream);
    YMRelease(userInfo->lock);
    YMRelease(userInfo->plexer);
    free(userInfo->lastServiceTime);
    free(userInfo);
}

void __ym_plexer_stream_data_available_proc(YMStreamRef stream, uint32_t bytes, void *ctx)
{
    __YMPlexerRef plexer = (__YMPlexerRef)ctx;
    __ym_plexer_stream_user_info_ref userInfo = YM_STREAM_INFO(stream);
    YMPlexerStreamID streamID = userInfo->streamID;
    ymlog("s%llu reports it has %u bytes ready",streamID,bytes);
    YMLockLock(userInfo->lock);
        userInfo->bytesAvailable += bytes;
    YMLockUnlock(userInfo->lock);
    YMSemaphoreSignal(plexer->downstreamReadySemaphore);
}

// tears down the plexer, returning whether or not this was the 'first call' to interrupted
// (i/o calls should treat their errors as 'real', and subsequent errors on other threads
// can return quietly)
bool __YMPlexerInterrupt(__YMPlexerRef plexer)
{
    bool firstInterrupt = false;
    YMSelfLock(plexer);
    {
        firstInterrupt = plexer->active;
        
        // before closing fds to ensure up/down threads wake up and exit, flag them
        plexer->active = false;
    }
    YMSelfLock(plexer);
    
    if ( ! firstInterrupt )
        return false;
    
    ymerr("interrupted");
    
    YMSecurityProviderClose(plexer->provider);
    
    YMSemaphoreSignal(plexer->downstreamReadySemaphore);

    for ( int i = 0; i < 2; i++ ) {
        YMDictionaryRef aList = ( i == 0 ) ? plexer->localStreamsByID : plexer->remoteStreamsByID;
        if ( i == 0 ) YMSelfLock(plexer); else YMLockLock(plexer->remoteStreamsLock);
        {
            while ( YMDictionaryGetCount(aList) > 0 ) {
                YMDictionaryKey aRandomKey = YMDictionaryGetRandomKey(aList);
                YMStreamRef aStream = YMDictionaryGetItem(aList, aRandomKey);
                YMPlexerStreamID aStreamID = YM_STREAM_INFO(aStream)->streamID;
                YMDictionaryRemove(aList, aRandomKey);
                ymerr("hanging up s%llu",aStreamID);
                _YMStreamCloseWriteUp(aStream);
                YMRelease(aStream);
            }
        }
        if ( i == 0 ) YMSelfUnlock(plexer); else YMLockUnlock(plexer->remoteStreamsLock);
    }
    
    // if the client stops us, they don't expect a callback
    if ( plexer->interruptedFunc )
		__YMPlexerDispatchFunctionWithName(plexer, NULL, plexer->eventDeliveryThread, __ym_plexer_notify_interrupted, YMSTRC("plexer-interrupted"));
    
//    // also created on init, so long as we're locking might be redundant
//    if ( plexer->eventDeliveryThread )
//    {
//        // releasing a dispatch thread should cause YMThreadDispatch to set the stop flag
//        // in the context struct (allocated on creation, free'd by the exiting thread)
//        // allowing it to go away. don't null it, or we'd need another handshake on the
//        // thread referencing itself via plexer and exiting.
//        YMRelease(plexer->eventDeliveryThread);
//    }
    
    if ( _YMThreadGetCurrentThreadNumber() != _YMThreadGetThreadNumber(plexer->eventDeliveryThread) )
        YMThreadDispatchJoin(plexer->eventDeliveryThread);
    
    return true;
}

#pragma mark dispatch

void __YMPlexerDispatchFunctionWithName(__YMPlexerRef plexer, YMStreamRef stream, YMThreadRef targetThread, ym_thread_dispatch_func function, YMStringRef nameToRelease)
{
    __ym_dispatch_plexer_and_stream_ref notifyDef = (__ym_dispatch_plexer_and_stream_ref)YMALLOC(sizeof(__ym_dispatch_plexer_and_stream));
    notifyDef->plexer = (__YMPlexerRef)YMRetain(plexer);
    notifyDef->stream = stream ? YMRetain(stream) : NULL;
    struct ym_thread_dispatch_t dispatch = { function, NULL, true, notifyDef, nameToRelease };
    
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

void YM_CALLING_CONVENTION __ym_plexer_notify_new_stream(ym_thread_dispatch_ref dispatch)
{
    __ym_dispatch_plexer_and_stream_ref notifyDef = dispatch->context;
    __YMPlexerRef plexer = notifyDef->plexer;
    YMStreamRef stream = notifyDef->stream;
    YMPlexerStreamID streamID = YM_STREAM_INFO(stream)->streamID;
    
    ymlog("ym_notify_new_stream entered s%llu", streamID);
    if ( plexer->newIncomingFunc )
        plexer->newIncomingFunc(plexer,stream,plexer->callbackContext);
    ymlog("ym_notify_new_stream exiting s%llu", streamID);
    
    YMRelease(plexer);
    YMRelease(stream);
    //YMRelease(dispatch->description); // done by ThreadDispatch
}

void YM_CALLING_CONVENTION __ym_plexer_notify_stream_closing(ym_thread_dispatch_ref dispatch)
{
    __ym_dispatch_plexer_and_stream_ref notifyDef = dispatch->context;
    __YMPlexerRef plexer = notifyDef->plexer;
    YMStreamRef stream = notifyDef->stream;
    YMPlexerStreamID streamID = YM_STREAM_INFO(stream)->streamID;
    
    ymlog("ym_notify_stream_closing entered s%llu", streamID);
    if ( plexer->closingFunc )
        plexer->closingFunc(plexer,stream,plexer->callbackContext);
    
    CHECK_REMOVE_RELEASE(false, (YMDictionaryKey)streamID, true);
    
    ymlog("ym_notify_stream_closing exiting s%llu", streamID);
    
    YMRelease(plexer);
    YMRelease(stream);
    //YMRelease(dispatch->description); // done by ThreadDispatch
}

void YM_CALLING_CONVENTION __ym_plexer_notify_interrupted(ym_thread_dispatch_ref dispatch)
{
    _ym_dispatch_plexer_stream_def *notifyDef = (_ym_dispatch_plexer_stream_def *)dispatch->context;
    __YMPlexerRef plexer = notifyDef->plexer;
    
    ymlog("ym_notify_interrupted entered");
    if ( plexer->interruptedFunc )
        plexer->interruptedFunc(plexer,plexer->callbackContext);
    ymlog("ym_notify_interrupted exiting");
    
    YMRelease(plexer);
    //YMRelease(dispatch->description); // done by ThreadDispatch
}

YM_EXTERN_C_POP
