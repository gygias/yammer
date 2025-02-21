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
#include "YMDispatch.h"
#include "YMDispatchUtils.h"

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

// queue released in __ym_plexer_source_destroy
#define CHECK_REMOVE(streamID)          YMLockLock(p->streamsLock); \
                                        YMStreamRef _stream = YMDictionaryGetItem(p->streamsByID,streamID);\
                                        if ( _stream ) { \
                                            YMDictionaryRemove(p->streamsByID, streamID); \
                                            __ym_plexer_stream_user_info_t *userInfo = YM_STREAM_INFO(_stream); \
                                            YMRelease(userInfo->plexer); \
                                            YMFREE(userInfo->upBuffer); \
                                            YMRelease(_stream); \
                                        } \
                                        YMLockUnlock(p->streamsLock);


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

YM_ENTRY_POINT(__ym_plexer_notify_new_stream);
YM_ENTRY_POINT(__ym_plexer_notify_stream_closing);
YM_ENTRY_POINT(__ym_plexer_notify_interrupted);

YM_ENTRY_POINT(__ym_plexer_destroy_source_async);

// linked to 'private' definition in YMPlexerPriv
typedef struct __ym_plexer_stream_user_info
{
    YMStringRef name;
    YMPlexerStreamID streamID;
    
    YMPlexerRef plexer;
    bool isLocallyOriginated;
    
    bool userClosed;
    
    uint64_t rawWritten;
    uint64_t muxerWritten;
    uint64_t rawRead;
    uint64_t muxerRead;

    uint8_t *upBuffer;
    uint32_t upBufferSize;
} __ym_plexer_stream_user_info;
typedef struct __ym_plexer_stream_user_info __ym_plexer_stream_user_info_t;

#undef YM_STREAM_INFO
#define YM_STREAM_INFO(x) ((__ym_plexer_stream_user_info_t *)_YMStreamGetUserInfo(x))

typedef struct {
    YMPlexerCommandType command;
    YMPlexerStreamID streamID;
} YMPlexerMessage;

typedef struct __ym_plexer_source_context __ym_plexer_source_context_t;

typedef struct __ym_plexer
{
    _YMType _common;
    
    YMStringRef name;
    YMSecurityProviderRef provider;

    __ym_plexer_source_context_t *upstreamSourceContext;
    YMArrayRef downstreamSourceContexts; // __ym_plexer_source_context_t
    
    bool active; // intialized and happy (or stopped, and sad?)
    bool master;
    bool myStreamsEven;
    
    YMDictionaryRef streamsByID;
    YMLockRef streamsLock;
    YMPlexerStreamID localStreamIDLast;
    
    YMDispatchQueueRef upQueue;
    YMDispatchQueueRef downQueue;
    
    YMDispatchQueueRef eventDeliveryQueue;
    YMLockRef interruptionLock;
    
    // user
    ym_plexer_interrupted_func interruptedFunc;
    ym_plexer_new_upstream_func newIncomingFunc;
    ym_plexer_stream_closing_func closingFunc;
    void *callbackContext;
} __ym_plexer;
typedef struct __ym_plexer __ym_plexer_t;

typedef struct __ym_plexer_event
{
    __ym_plexer_t *p;
    YMStreamRef stream;
} __ym_plexer_event;
typedef struct __ym_plexer_event __ym_plexer_event_t;

typedef struct __ym_plexer_source_context
{
    ym_dispatch_source_t source;
    __ym_plexer_event_t *event;
} __ym_plexer_source_context;
typedef struct __ym_plexer_source_context __ym_plexer_source_context_t;

YM_ENTRY_POINT(__ym_plexer_source_destroy);

// generic context pointer definition for "plexer & stream" entry points
typedef struct _ym_plexer_and_stream
{
    __ym_plexer_t *p;
    YMStreamRef s;
} _ym_plexer_and_stream;
typedef struct _ym_plexer_and_stream _ym_plexer_and_stream_t;

void __YMRegisterSigpipe();
void __ym_sigpipe_handler (int signum);
void __YMPlexerCallbackFunctionWithName(__ym_plexer_t *, YMStreamRef, YMDispatchQueueRef, ym_entry_point);
bool __YMPlexerStartServiceThreads(__ym_plexer_t *);
bool __YMPlexerDoInitialization(__ym_plexer_t *, bool);
bool __YMPlexerInitAsMaster(__ym_plexer_t *);
bool __YMPlexerInitAsSlave(__ym_plexer_t *);
YM_ENTRY_POINT(__ym_plexer_service_downstream);
YM_ENTRY_POINT(__ym_plexer_service_upstream);
YMStreamRef __YMPlexerChooseStreamWithID(__ym_plexer_t *, YMPlexerStreamID, bool *);
YMStreamRef __YMPlexerCreateStreamWithID(__ym_plexer_t *, YMPlexerStreamID, bool, YMStringRef);
bool __YMPlexerInterrupt(__ym_plexer_t *);
void __YMPlexerDestroySources(__ym_plexer_t *, bool, void *, const char *);

#define YMPlexerDefaultBufferSize UINT16_MAX

YMPlexerRef YMPlexerCreate(YMStringRef name, YMSecurityProviderRef provider, bool master, YMFILE upIn)
{
	__YMRegisterSigpipe();
    
    __ym_plexer_t *p = (__ym_plexer_t *)_YMAlloc(_YMPlexerTypeID,sizeof(__ym_plexer_t));
    
    p->name = YMStringCreateWithFormat("%s:%s",master?"s":"c",name?YMSTR(name):"*",NULL);
    p->provider = YMRetain(provider);
    
    p->active = false;
    p->master = master;
    p->myStreamsEven = false;
    
    p->streamsByID = YMDictionaryCreate();
    YMStringRef aString = YMStringCreateWithFormat("%s-local",YMSTR(p->name), NULL);
    p->streamsLock = YMLockCreateWithOptionsAndName(YMInternalLockType,aString);
    YMRelease(aString);
    
    YMStringRef queueName = YMStringCreateWithFormat("%s-upstream",YMSTR(p->name),NULL);
    p->upQueue = YMDispatchQueueCreate(queueName);
    YMRelease(queueName);

    queueName = YMStringCreateWithFormat("%s-downstream",YMSTR(p->name),NULL);
    p->downQueue = YMDispatchQueueCreate(queueName);
    YMRelease(queueName);
    
    aString = YMStringCreateWithFormat("com.combobulated.plexer.%s-event",YMSTR(p->name),NULL);
    p->eventDeliveryQueue = YMDispatchQueueCreate(aString);
    YMRelease(aString);
    
    aString = YMStringCreateWithFormat("%s-interrupt",YMSTR(p->name),NULL);
    p->interruptionLock = YMLockCreateWithOptionsAndName(YMInternalLockType,aString);
    YMRelease(aString);

    __ym_plexer_event_t *e = YMALLOC(sizeof(__ym_plexer_event_t));
    e->p = (__ym_plexer_t *)YMRetain(p);
    e->stream = NULL;
    ym_dispatch_user_t user = {__ym_plexer_service_upstream,e,__ym_plexer_source_destroy,ym_dispatch_user_context_noop};
    ym_dispatch_source_t upstreamSource = YMDispatchSourceCreate(p->upQueue,ym_dispatch_source_readable,upIn,&user);
    __ym_plexer_source_context_t *c = YMALLOC(sizeof(__ym_plexer_source_context_t));
    c->source = upstreamSource;
    c->event = e;
    p->upstreamSourceContext = c;

    p->downstreamSourceContexts = YMArrayCreate();

    ymlog("SOURCES registered observer for upstream fd %d %p %p %p",upIn,e,upstreamSource,c);

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
    
    YMRelease(p->streamsByID);
    YMRelease(p->streamsLock);
    
    YMRelease(p->eventDeliveryQueue);
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
    
    ymlog("%p started",p);
    
catch_fail:
    return okay;
}

YMStreamRef YMAPI YMPlexerCreateStream(YMPlexerRef p_, YMStringRef name)
{
    __ym_plexer_t *p = (__ym_plexer_t *)p_;
    
    YMStreamRef newStream = NULL;
    YMLockLock(p->streamsLock);
    do {
        if ( p->localStreamIDLast >= ( YMPlexerStreamIDMax - 1 ) )
            p->localStreamIDLast = p->myStreamsEven ? 0 : 1;
        else
            p->localStreamIDLast += 2;
        
        YMPlexerStreamID newStreamID = p->localStreamIDLast;
        if ( YMDictionaryContains(p->streamsByID, (YMDictionaryKey)newStreamID) ) {
            ymerr("fatal: plexer ran out of streams");
            __YMPlexerInterrupt(p);
            break;
        }
        
        YMStringRef userName = name ? YMRetain(name) : YMSTRC("*");
        newStream = __YMPlexerCreateStreamWithID(p, newStreamID, true, userName);
        
        YMDictionaryAdd(p->streamsByID, (YMDictionaryKey)newStreamID, (void *)newStream);
        YMRetain(newStream); // retain for user
        
    } while (false);
    YMLockUnlock(p->streamsLock);
    
    return newStream;
}

YM_ENTRY_POINT(__ym_plexer_close_stream)
{
    ymlogg("%s %p",__FUNCTION__,context);
    YMStreamRef stream = context;
    __ym_plexer_stream_user_info_t *userInfo = YM_STREAM_INFO(stream);
    userInfo->userClosed = true;
    #warning would putting this on the stream userinfo downqueue solve this apparent race? \
            easy to trigger with session test middleman pipe and its odd operation
    char buf = '%';
    YMIOResult result = _YMStreamPlexWriteDown(stream,&buf,1);
    ymassert(result==YMIOSuccess,"failed to write stream cl%%se");
}

void YMPlexerCloseStream(YMPlexerRef p_, YMStreamRef stream)
{
    __ym_plexer_t *p = (__ym_plexer_t *)p_;
    
    __ym_plexer_stream_user_info_t *userInfo = YM_STREAM_INFO(stream);
    YMPlexerStreamID streamID = userInfo->streamID;
    
    ymassert(!userInfo->userClosed,"stream double-close"); // only catches local
    
    if ( userInfo->isLocallyOriginated && p->active ) {
        // move queue release here (where it's probably more appropriate) from source destroy callback and don't need to dispatch this
        ym_dispatch_user_t user = { __ym_plexer_close_stream, stream, NULL, ym_dispatch_user_context_noop };
        YMDispatchAsync(p->downQueue,&user);
    }

    ymlog("user %s stream %lu", userInfo->isLocallyOriginated?"closing":"releasing", streamID);
    YMRelease(stream); // for user, still retained by internal list until finalized
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
    YMDispatchJoin(p->eventDeliveryQueue);
    
    return interrupted;
}

void __YMPlexerDestroySources(__ym_plexer_t *p, bool up, void *context, const char *who)
{
    if ( ! p->active )
        return;

    YMSelfLock(p);
    if ( up && p->upstreamSourceContext ) {
        __ym_plexer_source_context_t *c = p->upstreamSourceContext;
        p->upstreamSourceContext = NULL;
        YMSelfUnlock(p);
        ym_dispatch_user_t user = { __ym_plexer_destroy_source_async, c, NULL, ym_dispatch_user_context_noop };
        ymlog("%s destroying plexer upstream source %p %p %p",who,c->source,c->event,c->event->stream);
        YMDispatchSync(YMDispatchGetGlobalQueue(),&user);
        return;
    }

    __ym_plexer_source_context_t *c = NULL;
    bool local = false;
    for(int i = 0; i < YMArrayGetCount(p->downstreamSourceContexts); i++) {
        c = (__ym_plexer_source_context_t *)YMArrayGet(p->downstreamSourceContexts,i);
        if ( context && ( context != c->event->stream ) )
            continue;
        YMArrayRemove(p->downstreamSourceContexts,i);
        local = YM_STREAM_INFO(c->event->stream)->isLocallyOriginated;
        break;
    }
    YMSelfUnlock(p);

    if ( ! p->active )
        return;

    ymassert(c,"%s(%p,%d,%p,%s): not found",__FUNCTION__,p,up,context,who);
    ymlog("%s destroying downstream source for %s originated stream %p %p %p",who,local?"locally":"remotely",c->source,c->event,c->event->stream);
    ym_dispatch_user_t user = { __ym_plexer_destroy_source_async, c, NULL, ym_dispatch_user_context_noop };
    YMDispatchSync(YMDispatchGetGlobalQueue(),&user);

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
    
    #warning debug logging of the wire overhead of 64 bit stream ids lol
    YMPlexerInitializer initializer = { YMPlexerBuiltInVersion, YMPlexerStreamIDMax, p->myStreamsEven }; // endian?
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

YM_ENTRY_POINT(__ym_plexer_source_destroy)
{
    __ym_plexer_event_t *e = context;

    __ym_plexer *p = e->p;
    ymlog("YMRelease(%p) YMRelease(%p) free(%p)",e->p,e->stream,e);

    if ( e->p ) YMRelease(e->p);
    if ( e->stream ) YMRelease(e->stream); // nil for upstream
    YMFREE(e);

    // removed from downstreamSourceContexts by whatever initiated this callback
}

typedef struct __ym_plexer_security_down_funnel_context
{
    __ym_plexer_t *p;
    YMStreamRef stream;
    const void *buf;
    uint16_t len;
    bool closeThis;
    bool closeAfter;
} __ym_plexer_security_down_funnel_context;
typedef struct __ym_plexer_security_down_funnel_context __ym_plexer_security_down_funnel_context_t;

YM_ENTRY_POINT(__ym_plexer_security_down_funnel)
{
    __ym_plexer_security_down_funnel_context_t *c = context;
    __ym_plexer_t *p = c->p;

    __ym_plexer_stream_user_info_t *userInfo = YM_STREAM_INFO(c->stream);
    YMPlexerStreamID streamID = userInfo->streamID;

    if ( ! c->closeThis ) {
        YMPlexerMessage plexMessage = { c->len, streamID };
        size_t plexMessageLen = sizeof(plexMessage);
        bool okay = YMSecurityProviderWrite(p->provider, (uint8_t *)&plexMessage, plexMessageLen);
        if ( ! okay || ! p->active ) {
            bool interrupt = __YMPlexerInterrupt(p);
            ymerr("p: V-s%lu failed writing plex message size %zub: %d (%s)%s",streamID,plexMessageLen,errno,strerror(errno),interrupt?" interrupted":"");
            goto catch_release;
        }

        ymdbg("V-s%lu wrote %zub plex header",streamID,plexMessageLen);

        okay = YMSecurityProviderWrite(p->provider, c->buf, c->len);
        if ( ! okay || ! p->active ) {
            bool interrupt = __YMPlexerInterrupt(p);
            ymerr("p: V-s%lu failed writing plex buffer %ub: %d (%s)%s",streamID,plexMessage.command,errno,strerror(errno),interrupt?" interrupted":"");
            goto catch_release;
        }

        ymlog("V-s%lu funnel wrote %zu + %hub chunk",streamID,plexMessageLen,c->len);
    }

    if ( c->closeThis ) {
        ymlog("%s stream %lu closed locally",userInfo->isLocallyOriginated?"local":"remote", streamID);

        if ( userInfo->isLocallyOriginated ) {
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
        }

        __YMPlexerDestroySources(p,false,c->stream,"DOWNSTREAM");

        ymlog("V-s%lu stream closing (rW%lu,pW%lu,rR%lu,pR%lu): %p (%s)",streamID,userInfo->rawWritten,userInfo->muxerRead,userInfo->rawRead,userInfo->muxerRead,c->stream,YMSTR(_YMStreamGetName(c->stream)));

        CHECK_REMOVE((YMDictionaryKey)streamID);
    }

catch_release:
    YMFREE(c->buf);
    YMRelease(p);
    YMRelease(c->stream);
}

#warning dispatch async interrupts and callbacks in this function
YM_ENTRY_POINT(__ym_plexer_service_downstream)
{
    __ym_plexer_event_t *e = context;
    __ym_plexer_t *p = e->p;
    YMStreamRef stream = e->stream;

    if ( ! p->active )
        return;

    ymassert(context != p->upstreamSourceContext,"%s called on upstream context %p",__FUNCTION__,context);
    
    __ym_plexer_stream_user_info_t *userInfo = YM_STREAM_INFO(stream);
    YMPlexerStreamID streamID = userInfo->streamID;

    uint32_t chunkMaxLength = 16384;
    uint8_t *aDownBuf = YMALLOC(chunkMaxLength);

    YM_IO_RESULT chunkLength = _YMStreamPlexReadDown(stream, aDownBuf, chunkMaxLength);
    if ( chunkLength <= 0 ) {
        ymlog("eof reading chunk length...")
        return;
    }

    ymdbg("%s V-s%lu servicing %zdb chunk...",__FUNCTION__,streamID,chunkLength);

    bool closeAfterWrite = false;
    bool closeThisIter = false;
    if ( userInfo->userClosed ) {
        if ( chunkLength == 1 ) {
            if ( (aDownBuf)[0] == '%' ) {
                ymlog("user closed... stream %lu closing RIGHT NOW!!",streamID);
                closeThisIter = true;
                goto catch_close;
            }
#warning this is dangerous if userClosed but user data HAPPENS to end in not-our-%
        } else if ( (aDownBuf)[chunkLength-1] == '%' ) {
            ymlog("user closed[%zd]... stream %lu closing THIS ITERATION!!",chunkLength,streamID);
            closeAfterWrite = true;
            chunkLength--;
        } else
            ymlog("user closed[%zd]... stream %lu NOT closing THIS ITERATION!!",chunkLength,streamID);
    }

    if ( ! p->active ) //dispatchify cruft?
        return;

catch_close:
__ym_plexer_security_down_funnel_context_t *ctx = YMALLOC(sizeof(__ym_plexer_security_down_funnel_context_t));
    ctx->p = YMRetain(p);
    ctx->stream = YMRetain(stream);
    ctx->buf = aDownBuf;
    ctx->len = chunkLength;
    ctx->closeThis = closeThisIter;
    ctx->closeAfter = closeAfterWrite;
    ym_dispatch_user_t user = { __ym_plexer_security_down_funnel, ctx, NULL, ym_dispatch_user_context_free };
    YMDispatchAsync(p->downQueue,&user);
}

YM_ENTRY_POINT(__ym_plexer_service_upstream)
{
    __ym_plexer_event_t *e = context;
    __ym_plexer_t *p = e->p;

    if ( ! p->active )
        return;

    bool streamClosing = false;
    size_t chunkLength = 0;
    YMPlexerMessage plexerMessage;

    bool okay = YMSecurityProviderRead(p->provider, (uint8_t *)&plexerMessage, sizeof(plexerMessage));
    if ( ! okay || ! p->active || plexerMessage.command < YMPlexerCommandMin || plexerMessage.command > YMPlexerDefaultBufferSize ) {
        bool interrupt = __YMPlexerInterrupt(p);
        if ( interrupt )
            ymerr("^ fatal: failed reading, or plex header invalid: %d %d %d (%s)",plexerMessage.command,errno,strerror(errno));
        return;
    } else if ( plexerMessage.command == YMPlexerCommandCloseStream )
        streamClosing = true;
    else {
        chunkLength = plexerMessage.command;
        ymdbg("^-s%lu read plex header %zub",plexerMessage.streamID,chunkLength);
    }
    
    YMPlexerStreamID streamID = plexerMessage.streamID;
    bool newUp = false;
    YMStreamRef theStream = __YMPlexerChooseStreamWithID(p, streamID, &newUp);
    if ( ! theStream || ! p->active ) {
        bool interrupt = __YMPlexerInterrupt(p);
        if ( interrupt )
            ymerr("fatal: ^-s%lu stream lookup",streamID);
        return;
    }
    
    __ym_plexer_stream_user_info_t *userInfo = YM_STREAM_INFO(theStream);
    userInfo->muxerRead += sizeof(plexerMessage);

    if ( streamClosing ) {
        // close 'read up', so that if client (or forward file) is reading unbounded data it will get signaled
        _YMStreamCloseWriteUp(theStream);
        if ( p->closingFunc )
            __YMPlexerCallbackFunctionWithName(p, theStream, p->eventDeliveryQueue, __ym_plexer_notify_stream_closing);
        ymlog("^-s%lu stream closing (rW%lu,pW%lu,rR%lu,pR%lu)",plexerMessage.streamID,userInfo->rawWritten,userInfo->muxerWritten,userInfo->rawRead,userInfo->muxerRead);
        return;
    }

#warning want to review min sizes through this process, we don't and didn't previous realloc up/down buffers
    ymassert(chunkLength<=YMPlexerDefaultBufferSize&&chunkLength!=0,"upstream chuck length");

    okay = YMSecurityProviderRead(p->provider, userInfo->upBuffer, chunkLength);
    if ( ! okay || ! p->active ) {
        bool interrupt = __YMPlexerInterrupt(p);
        if ( interrupt )
            ymerr("p: ^-s%lu failed reading plex buffer of length %zub: %d (%s)",streamID,chunkLength,errno,strerror(errno));
        return;
    }
    userInfo->rawRead += chunkLength;

    YMIOResult result = _YMStreamPlexWriteUp(theStream, userInfo->upBuffer, (uint32_t)chunkLength);
    if ( result != YMIOSuccess || ! p->active ) {
        bool interrupt = __YMPlexerInterrupt(p);
        if ( interrupt )
            ymerr("internal fatal: ^-s%lu failed writing plex buffer of length %zub: %d (%s)",streamID,chunkLength,errno,strerror(errno));
        return;
    }

    ymlog("^-s%lu wrote plex buffer %zub",streamID,chunkLength);

    if ( newUp ) {
        ymlog("L-%lu notifying new incoming",streamID);

        if ( p->newIncomingFunc )
            __YMPlexerCallbackFunctionWithName(p, theStream, p->eventDeliveryQueue, __ym_plexer_notify_new_stream);
    }
}

YMStreamRef __YMPlexerChooseStreamWithID(__ym_plexer_t *p, YMPlexerStreamID streamID, bool *newUp)
{
    YMStreamRef theStream = NULL;
    
    YMLockLock(p->streamsLock);
    {
        theStream = (YMStreamRef)YMDictionaryGetItem(p->streamsByID,(YMDictionaryKey)streamID);
    }
    YMLockUnlock(p->streamsLock);
    
    if ( ! theStream ) {
        if ( ( streamID % 2 == 0 ) && p->myStreamsEven ) {
            if ( __YMPlexerInterrupt(p) )
                ymerr("internal: L-%lu stream id collision",streamID);
            return NULL;
        }

        theStream = __YMPlexerCreateStreamWithID(p,streamID,false,YMSTRC("remote"));
        YMRetain(theStream); // our list

        YMLockLock(p->streamsLock);
        YMDictionaryAdd(p->streamsByID, (YMDictionaryKey)streamID, theStream);
        YMLockUnlock(p->streamsLock);

        if ( newUp ) *newUp = true;
    }
    
    return theStream;
}

YMStreamRef __YMPlexerCreateStreamWithID(__ym_plexer_t *p, YMPlexerStreamID streamID, bool isLocal, YMStringRef userNameToRelease)
{
    __ym_plexer_stream_user_info_t *userInfo = (__ym_plexer_stream_user_info_t *)YMALLOC(sizeof(__ym_plexer_stream_user_info_t));
    userInfo->plexer = YMRetain(p);
    userInfo->streamID = streamID;
    userInfo->isLocallyOriginated = isLocal;
    
    userInfo->userClosed = false;
    userInfo->rawWritten = 0;
    userInfo->muxerWritten = 0;
    userInfo->rawRead = 0;
    userInfo->muxerRead = 0;

    userInfo->upBuffer = YMALLOC(YMPlexerDefaultBufferSize);
    userInfo->upBufferSize = YMPlexerDefaultBufferSize;

    YMStringRef memberName = YMStringCreateWithFormat("%s-%s-s%lu-%s",p->master?"m":"s",isLocal?">":"<",streamID,YMSTR(userNameToRelease),NULL);

    YMFILE downOut;
    YMStreamRef theStream = _YMStreamCreate(memberName,(ym_stream_user_info_t *)userInfo,&downOut);

    __ym_plexer_event_t *event = YMALLOC(sizeof(__ym_plexer_event_t));
    event->p = (__ym_plexer_t *)YMRetain(p);
    event->stream = YMRetain(theStream);
    ym_dispatch_user_t user = {__ym_plexer_service_downstream,event,__ym_plexer_source_destroy,ym_dispatch_user_context_noop };
    // stream writes are globally async, synchronized on the underlying write
    ym_dispatch_source_t source = YMDispatchSourceCreate(YMDispatchGetGlobalQueue(),ym_dispatch_source_readable,downOut,&user);

    __ym_plexer_source_context_t *c = YMALLOC(sizeof(__ym_plexer_source_context_t));
    c->source = source;
    c->event = event;

    YMSelfLock(p);
    YMArrayAdd(p->downstreamSourceContexts,c);
    YMSelfUnlock(p);

    ymlog("SOURCES registered observer for downstream fd %d %p %p (e%p p%p s%p)",downOut,source,__ym_plexer_service_downstream,event,event->p,event->stream);

    YMRelease(memberName);
    YMRelease(userNameToRelease);
    
    return theStream;
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

    if ( p->upstreamSourceContext ) {
        __YMPlexerDestroySources(p,true,NULL,"INTERRUPT");
    }
    __YMPlexerDestroySources(p,false,NULL,"INTERRUPT");
    
    YMSecurityProviderClose(p->provider);

    YMLockLock(p->streamsLock);
    YMDictionaryEnumRef e = YMDictionaryEnumeratorBegin(p->streamsByID);
    while ( e ) {
        YMStreamRef aStream = e->value;
        ymerr("hanging up s%lu",e->key);
        _YMStreamCloseWriteUp(aStream);
        YMRelease(aStream);
        e = YMDictionaryEnumeratorGetNext(e);
    }
    YMRelease(p->streamsByID);
    p->streamsByID = YMDictionaryCreate();
    YMLockUnlock(p->streamsLock);
    
    // if the client stops us, they don't expect a callback
    if ( p->interruptedFunc )
		__YMPlexerCallbackFunctionWithName(p, NULL, p->eventDeliveryQueue, __ym_plexer_notify_interrupted);
    
    YMDispatchJoin(p->eventDeliveryQueue);
    
    return true;
}

#pragma mark dispatch

void __YMPlexerCallbackFunctionWithName(__ym_plexer_t *p, YMStreamRef stream, YMDispatchQueueRef queue, ym_entry_point function)
{
    _ym_plexer_and_stream_t *notifyDef = (_ym_plexer_and_stream_t *)YMALLOC(sizeof(_ym_plexer_and_stream_t));
    notifyDef->p = (__ym_plexer_t *)YMRetain(p);
    notifyDef->s = stream ? YMRetain(stream) : NULL;

    ym_dispatch_user_t dispatch = { function, notifyDef, NULL, ym_dispatch_user_context_free };
    YMDispatchSync(queue, &dispatch);
}

YM_ENTRY_POINT(__ym_plexer_notify_new_stream)
{
    _ym_plexer_and_stream_t *notifyDef = (_ym_plexer_and_stream_t *)context;
    __ym_plexer_t *p = notifyDef->p;
    YMStreamRef stream = notifyDef->s;
    YMPlexerStreamID streamID = YM_STREAM_INFO(stream)->streamID;
    
    ymlog("ym_notify_new_stream entered s%lu (%p)", streamID, p->newIncomingFunc);
    if ( p->newIncomingFunc )
        p->newIncomingFunc(p,stream,p->callbackContext);
    ymlog("ym_notify_new_stream exiting s%lu", streamID);
    
    YMRelease(p);
    YMRelease(stream);
}

YM_ENTRY_POINT(__ym_plexer_destroy_source_async)
{
    __ym_plexer_source_context_t *c = context;
    ym_dispatch_source_t s = c->source;
    ymlogg("%s entered for %p",__FUNCTION__,s);
    YMDispatchSourceDestroy(s);

    YMFREE(c);
}

YM_ENTRY_POINT(__ym_plexer_notify_stream_closing)
{
    _ym_plexer_and_stream_t *notifyDef = (_ym_plexer_and_stream_t *)context;
    __ym_plexer_t *p = notifyDef->p;
    YMStreamRef stream = notifyDef->s;
    __ym_plexer_stream_user_info_t *userInfo = YM_STREAM_INFO(stream);
    YMPlexerStreamID streamID = userInfo->streamID;
    
    ymlog("%s entered s%lu",__FUNCTION__, streamID);
    if ( p->closingFunc )
        p->closingFunc(p,stream,p->callbackContext);

    __YMPlexerDestroySources(p,false,stream,"NOTIFY");

    CHECK_REMOVE((YMDictionaryKey)streamID);
    
    ymlog("%s exiting s%lu",__FUNCTION__, streamID);
    
    YMRelease(p);
    YMRelease(stream);
}

YM_ENTRY_POINT(__ym_plexer_notify_interrupted)
{
    _ym_plexer_and_stream_t *notifyDef = (_ym_plexer_and_stream_t *)context;
    __ym_plexer_t *p = notifyDef->p;
    
    ymlog("ym_notify_interrupted entered");
    if ( p->interruptedFunc )
        p->interruptedFunc(p,p->callbackContext);
    ymlog("ym_notify_interrupted exiting");
    
    YMRelease(p);
}

YM_EXTERN_C_POP
