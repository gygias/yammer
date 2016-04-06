//
//  YMSession.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMSession.h"

#include "YMmDNSService.h"
#include "YMmDNSBrowser.h"
#include "YMConnectionPriv.h"
#include "YMPlexerPriv.h" // streamid def
#include "YMStreamPriv.h"
#include "YMPeerPriv.h"
#include "YMLock.h"
#include "YMThread.h"
#include "YMAddress.h"
#include "YMUtilities.h"

#define ymlog_type YMLogSession
#define ymlog_pre "session[%s]: "
#define ymlog_args YMSTR(session->logDescription)
#include "YMLog.h"

#if !defined(YMWIN32)
# if defined(YMLINUX)
#  define __USE_POSIX
#  include <sys/socket.h>
# elif defined(YMAPPLE)
#  include <SystemConfiguration/SystemConfiguration.h>
# endif
# include <netinet/in.h>
# include <netdb.h> // struct hostent
#else
# include <winsock2.h>
# include <ws2tcpip.h>
# include <Objbase.h>
# include <Wbemidl.h>
# include <tchar.h>
#endif

#if defined(YMAPPLE)
#define __YMSessionObserveNetworkInterfaceChanges __YMSessionObserveNetworkInterfaceChangesMacos
#elif defined(YMLINUX)
#include "interface.h"
#define __YMSessionObserveNetworkInterfaceChanges __YMSessionObserveNetworkInterfaceChangesLinux
#elif defined(YMWIN32)
#define __YMSessionObserveNetworkInterfaceChanges __YMSessionObserveNetworkInterfaceChangesWin32
typedef struct YM_IWbemObjectSink
{
	CONST_VTBL struct IWbemObjectSinkVtbl *lpVtbl;
	void *that;
} YM_IWbemObjectSink;
#else
#error unknown platform
#endif

YM_EXTERN_C_PUSH

typedef struct __ym_session_t
{
    _YMType _type;
    
    // shared
    YMStringRef type;
    YMStringRef logDescription;
    YMDictionaryRef connectionsByAddress;
    YMConnectionRef defaultConnection;
    bool interrupted;

	// interface change observer shit
#if defined(YMAPPLE)
    YMThreadRef scObserverThread;
    bool scObserverThreadExitFlag;
#elif defined(YMWIN32)
    IWbemServices *gobbledygook;
    YM_IWbemObjectSink* my_gobbledygook;
#else
    YMThreadRef linuxPollThread;
    bool linuxPollThreadExitFlag;
#endif
    
    // server
    YMStringRef name;
    YMmDNSServiceRef service;
    YMSOCKET ipv4ListenSocket;
    YMSOCKET ipv6ListenSocket;
    YMThreadRef acceptThread;
    bool acceptThreadExitFlag;
    YMThreadRef initConnectionDispatchThread;
    
    // client
    YMmDNSBrowserRef browser;
    YMDictionaryRef knownPeers;
    YMThreadRef connectDispatchThread;
    
    ym_session_added_peer_func addedFunc;
    ym_session_removed_peer_func removedFunc;
    ym_session_resolve_failed_func resolveFailedFunc;
    ym_session_resolved_peer_func resolvedFunc;
    ym_session_connect_failed_func connectFailedFunc;
    ym_session_should_accept_func shouldAcceptFunc;
    ym_session_initializing_func initializingFunc;
    ym_session_connected_func connectedFunc;
    ym_session_interrupted_func interruptedFunc;
    ym_session_new_stream_func newStreamFunc;
    ym_session_stream_closing_func streamClosingFunc;
    void *callbackContext;
} __ym_session_t;
typedef struct __ym_session_t *__YMSessionRef;

#pragma mark setup

__YMSessionRef __YMSessionCreateShared(YMStringRef type, bool isServer);
bool __YMSessionObserveNetworkInterfaceChanges(__YMSessionRef session, bool startStop);
void __YMSessionUpdateNetworkConfigDate(__YMSessionRef sesison);
bool __YMSessionInterrupt(__YMSessionRef session, YMConnectionRef floatConnection);
bool __YMSessionCloseAllConnections(__YMSessionRef session);
void __YMSessionAddConnection(__YMSessionRef session, YMConnectionRef connection);
YM_THREAD_RETURN YM_CALLING_CONVENTION __ym_session_accept_proc(YM_THREAD_PARAM);
void YM_CALLING_CONVENTION __ym_session_init_incoming_connection_proc(ym_thread_dispatch_ref);
void YM_CALLING_CONVENTION __ym_session_connect_async_proc(ym_thread_dispatch_ref);

void __ym_session_new_stream_proc(YMConnectionRef connection, YMStreamRef stream, void *context);
void __ym_session_stream_closing_proc(YMConnectionRef connection, YMStreamRef stream, void *context);
void __ym_session_connection_interrupted_proc(YMConnectionRef connection, void *context);

void __ym_mdns_service_appeared_func(YMmDNSBrowserRef browser, YMmDNSServiceRecord * service, void *context);
void __ym_mdns_service_removed_func(YMmDNSBrowserRef browser, YMStringRef name, void *context);
void __ym_mdns_service_updated_func(YMmDNSBrowserRef browser, YMmDNSServiceRecord *service, void *context);
void __ym_mdns_service_resolved_func(YMmDNSBrowserRef browser, bool success, YMmDNSServiceRecord *service, void *context);

YMSessionRef YMSessionCreate(YMStringRef type)
{
	YMNetworkingInit();

    __YMSessionRef session = (__YMSessionRef)_YMAlloc(_YMSessionTypeID,sizeof(struct __ym_session_t));
    session->type = YMRetain(type);
    session->ipv4ListenSocket = NULL_SOCKET;
    session->ipv6ListenSocket = NULL_SOCKET;
    session->logDescription = YMStringCreateWithFormat("session:%s",YMSTR(type),NULL);
    session->service = NULL;
    session->browser = NULL;
    session->acceptThread = NULL;
    session->acceptThreadExitFlag = false;
    session->initConnectionDispatchThread = NULL;
    session->defaultConnection = NULL;
    session->connectionsByAddress = YMDictionaryCreate();

#if defined(YMAPPLE)
	session->scObserverThread = NULL;
	session->scObserverThreadExitFlag = false;
#elif defined(YMWIN32)
	session->gobbledygook = NULL;
	session->my_gobbledygook = NULL;
#else
	session->linuxPollThread = NULL;
	session->linuxPollThreadExitFlag = false;
#endif
    
    YMStringRef memberName = YMSTRC("connections-by-address");
    YMRelease(memberName);
    
    session->knownPeers = YMDictionaryCreate();
    memberName = YMSTRC("available-peers");
    YMRelease(memberName);
    
    return session;
}

void YMSessionSetBrowsingCallbacks(YMSessionRef session_, ym_session_added_peer_func added, ym_session_removed_peer_func removed,
                                 ym_session_resolve_failed_func rFailed, ym_session_resolved_peer_func resolved,
                                 ym_session_connect_failed_func cFailed, void *context)
{
    __YMSessionRef session = (__YMSessionRef)session_;
    
    session->addedFunc = added;
    session->removedFunc = removed;
    session->resolveFailedFunc = rFailed;
    session->resolvedFunc = resolved;
    session->connectFailedFunc = cFailed;
    session->callbackContext = context;
}

void YMSessionSetAdvertisingCallbacks(YMSessionRef session_,ym_session_should_accept_func should, void* context)
{
    __YMSessionRef session = (__YMSessionRef)session_;
    
    session->shouldAcceptFunc = should;
    session->callbackContext = context;
}

void YMSessionSetCommonCallbacks(YMSessionRef session_, ym_session_initializing_func initializing, ym_session_connected_func connected,
                                                        ym_session_interrupted_func interrupted,
                                                        ym_session_new_stream_func new_, ym_session_stream_closing_func closing)
{
    __YMSessionRef session = (__YMSessionRef)session_;
    
    session->initializingFunc = initializing;
    session->connectedFunc = connected;
    session->interruptedFunc = interrupted;
    session->newStreamFunc = new_;
    session->streamClosingFunc = closing;
}

void _YMSessionFree(YMTypeRef object)
{
    __YMSessionRef session = (__YMSessionRef)object;
    
    __YMSessionInterrupt(session, NULL);
    
    // release these first, threads join on their exit flag when they're deallocated,
    // and we have async stuff that depends on other session members below
    session->acceptThreadExitFlag = false;
    if ( session->acceptThread )
        YMRelease(session->acceptThread);
    if ( session->initConnectionDispatchThread ) {
        YMThreadDispatchJoin(session->initConnectionDispatchThread);
        YMRelease(session->initConnectionDispatchThread);
    }
    if ( session->connectDispatchThread ) {
        YMThreadDispatchJoin(session->connectDispatchThread);
        YMRelease(session->connectDispatchThread);
    }
    
    // shared
    YMRelease(session->type);
    YMRelease(session->connectionsByAddress);
    YMRelease(session->logDescription);
    
    // server
    if ( session->name )
        YMRelease(session->name);
    if ( session->service ) {
        YMmDNSServiceStop(session->service, true); // xxx
        __YMSessionObserveNetworkInterfaceChanges(session, false);
        YMRelease(session->service);
    }
    
    if ( session->browser ) {
        YMmDNSBrowserStop(session->browser);
        YMRelease(session->browser);
    }
    
    YMDictionaryEnumRef aEnum = YMDictionaryEnumeratorBegin(session->knownPeers);
    while ( aEnum ) {
        YMPeerRef aPeer = aEnum->value;
        YMRelease(aPeer);
        aEnum = YMDictionaryEnumeratorGetNext(aEnum);
    }
    if ( aEnum ) YMDictionaryEnumeratorEnd(aEnum);
    
    YMRelease(session->knownPeers);
}

#pragma mark client

bool YMSessionStartBrowsing(YMSessionRef session_)
{
    __YMSessionRef session = (__YMSessionRef)session_;
    
    if ( session->browser || session->defaultConnection )
        return false;
    
    session->browser = YMmDNSBrowserCreateWithCallbacks(session->type,
                                                        __ym_mdns_service_appeared_func,
                                                        __ym_mdns_service_updated_func,
                                                        __ym_mdns_service_resolved_func,
                                                        __ym_mdns_service_removed_func,
                                                        session);
    if ( ! session->browser ) {
        ymerr("failed to create browser");
        return false;
    }
    
    bool startOK = YMmDNSBrowserStart(session->browser);
    if ( ! startOK ) {
        ymerr("failed to start browser");
        return false;
    }
    
    ymlog("client started for '%s'",YMSTR(session->type));
    
    return true;
}

bool YMSessionStopBrowsing(YMSessionRef session_)
{
    __YMSessionRef session = (__YMSessionRef)session_;
    
    bool okay = true;
    
    if ( session->browser ) {
        okay = YMmDNSBrowserStop(session->browser);
        YMRelease(session->browser);
        session->browser = NULL;
    }
    
    return okay;
}

bool YMSessionCloseAllConnections(YMSessionRef session_)
{
    __YMSessionRef session = (__YMSessionRef)session_;
    bool okay = __YMSessionInterrupt(session, NULL);
    return okay;
}

YMPeerRef YMSessionGetPeerNamed(YMSessionRef session_, YMStringRef peerName)
{
    __YMSessionRef session = (__YMSessionRef)session_;
    YMPeerRef thePeer = NULL;
    
    YMSelfLock(session);
    {
        YMDictionaryEnumRef peerEnum = YMDictionaryEnumeratorBegin(session->knownPeers);
        while ( peerEnum ) {
            if ( YMStringEquals(YMPeerGetName((YMPeerRef)peerEnum->value), peerName) ) {
                thePeer = peerEnum->value;
                break;
            }
            peerEnum = YMDictionaryEnumeratorGetNext(peerEnum);
        }
        YMDictionaryEnumeratorEnd(peerEnum);
    }
    YMSelfUnlock(session);
    
    return thePeer;
}

bool YMSessionResolvePeer(YMSessionRef session_, YMPeerRef peer)
{
    __YMSessionRef session = (__YMSessionRef)session_;
    
    YMSelfLock(session);
    YMStringRef peerName = YMPeerGetName(peer);
    bool knownPeer = true;
    if ( ! YMDictionaryContains(session->knownPeers, (YMDictionaryKey)peer) ) {
        ymerr("requested resolve of unknown peer: %s",YMSTR(peerName));
        knownPeer = false;
    }
    YMSelfUnlock(session);
    
    if ( ! knownPeer )
        return false;
    
    return YMmDNSBrowserResolve(session->browser, peerName);
}

typedef struct __ym_session_connect_async_context_t
{
    __YMSessionRef session;
    YMPeerRef peer;
    YMConnectionRef connection;
    bool moreComing;
    bool userSync;
} ___ym_session_connect_async_context_t;
typedef struct __ym_session_connect_async_context_t __ym_session_connect_async_context;
typedef __ym_session_connect_async_context *__ym_session_connect_async_context_ref;

bool YMSessionConnectToPeer(YMSessionRef session_, YMPeerRef peer, bool sync)
{
    __YMSessionRef session = (__YMSessionRef)session_;
    
    YMStringRef name = NULL;
    __ym_session_connect_async_context_ref context = NULL;
    YMDictionaryEnumRef addrEnum = NULL;
    
    bool knownPeer = true;
    YMSelfLock(session);
    YMStringRef peerName = YMPeerGetName(peer);
    if ( ! YMDictionaryContains(session->knownPeers, (YMDictionaryKey)peer) ) {
        ymerr("requested connect to unknown peer: %s",YMSTR(peerName));
        knownPeer = false;
    }
    YMSelfUnlock(session);
    
    if ( ! knownPeer )
        return false;
    
    YMArrayRef addresses = YMPeerGetAddresses(peer);
    for ( int64_t i = 0; i < YMArrayGetCount(addresses); i++ ) {
        YMAddressRef address = (YMAddressRef)YMArrayGet(addresses, i);
        YMConnectionRef newConnection = YMConnectionCreate(address, YMConnectionStream, YMTLS, true);
        
        context = (__ym_session_connect_async_context_ref)YMALLOC(sizeof(__ym_session_connect_async_context));
        context->session = (__YMSessionRef)YMRetain(session);
        context->peer = (YMPeerRef)YMRetain(peer);
        context->connection = (YMConnectionRef)YMRetain(newConnection);
        context->moreComing = ( i < YMArrayGetCount(addresses) - 1 );
        context->userSync = sync;
        
        name = YMSTRC("session-async-connect");
        struct ym_thread_dispatch_t connectDispatch = {__ym_session_connect_async_proc, 0, 0, context, name};
        
        if ( ! sync ) {
            if ( ! session->connectDispatchThread ) {
                session->connectDispatchThread = YMThreadDispatchCreate(name);
                if ( ! session->connectDispatchThread ) {
                    ymerr("failed to create async connect thread");
                    goto catch_fail;
                }
                bool okay = YMThreadStart(session->connectDispatchThread);
                if ( ! okay ) {
                    ymerr("failed to start async connect thread");
                    goto catch_fail;
                }
            }
            
            YMThreadDispatchDispatch(session->connectDispatchThread, connectDispatch);
        }
        else
            __ym_session_connect_async_proc(&connectDispatch);
        
        YMRelease(newConnection);
        YMRelease(name);
    }
    
    return true;
    
catch_fail:
    free(context);
    if ( name )
        YMRelease(name);
    if ( addrEnum )
        YMDictionaryEnumeratorEnd(addrEnum);
    return false;
}

void YM_CALLING_CONVENTION __ym_session_connect_async_proc(ym_thread_dispatch_ref dispatch)
{
    __ym_session_connect_async_context_ref context = (__ym_session_connect_async_context_ref)dispatch->context;
    __YMSessionRef session = context->session;
    YMPeerRef peer = context->peer;
    YMConnectionRef connection = context->connection;
    bool moreComing = context->moreComing;
    bool userSync = context->userSync;
    free(context);
    
    ymlog("__ym_session_connect_async_proc entered");
    
    bool okay = YMConnectionConnect(connection);
    
    if ( okay && session->initializingFunc )
        session->initializingFunc(session, session->callbackContext);
    
    okay = YMConnectionInit(connection);
    
    if ( okay ) {
        __YMSessionAddConnection(session, connection);
        if ( ! userSync )
            session->connectedFunc(session, connection, session->callbackContext);
    }
    else {
        if ( ! userSync )
            session->connectFailedFunc(session, peer, moreComing, session->callbackContext);
    }
    
    YMRelease(session);
    YMRelease(peer);
    YMRelease(connection);
    
    ymlog("__ym_session_connect_async_proc exiting: %s",okay?"success":"fail");
}

#pragma mark server

typedef struct __ym_session_accept_thread_context_t
{
    __YMSessionRef session;
    bool ipv4;
} _ym_session_accept_thread_context_t;
typedef struct __ym_session_accept_thread_context_t *__ym_session_accept_thread_context_ref;

bool YMSessionStartAdvertising(YMSessionRef session_, YMStringRef name)
{
    __YMSessionRef session = (__YMSessionRef)session_;
    
    if ( session->service )
        return false;
    
    session->name = YMRetain(name);
    
    bool okay = false;
    int socket = -1;
    bool ipv4 = true;
    
    int32_t port = YMPortReserve(ipv4, &socket);
    if ( port < 0 || socket == -1 || socket > UINT16_MAX ) {
        ymerr("failed to reserve port for server start");
        return false;
    }
    
    int aResult = listen(socket, 1);
    if ( aResult != 0 ) {
        ymerr("failed to listen for server start");
		int result, error; const char *errorStr;
        YM_CLOSE_SOCKET(socket);
        goto rewind_fail;
    }
    
    ymlog("listening on %u",port);
    
    if ( ipv4 )
        session->ipv4ListenSocket = socket;
    else
        session->ipv6ListenSocket = socket;
    
    YMStringRef memberName = YMStringCreateWithFormat("session-accept-%s",YMSTR(session->type),NULL);
    __ym_session_accept_thread_context_ref ctx = YMALLOC(sizeof(struct __ym_session_accept_thread_context_t));
    ctx->session = (__YMSessionRef)YMRetain(session);
    ctx->ipv4 = ipv4;
    session->acceptThread = YMThreadCreate(memberName, __ym_session_accept_proc, ctx);
    YMRelease(memberName);
    if ( ! session->acceptThread ) {
        ymerr("failed to create accept thread");
        goto rewind_fail;
    }
    session->acceptThreadExitFlag = false;
    okay = YMThreadStart(session->acceptThread);
    if ( ! okay ) {
        ymerr("failed to start accept thread");
        goto rewind_fail;
    }
    
    memberName = YMStringCreateWithFormat("session-init-%s",YMSTR(session->type),NULL);
    session->initConnectionDispatchThread = YMThreadDispatchCreate(memberName);
    YMRelease(memberName);
    if ( ! session->initConnectionDispatchThread ) {
        ymerr("failed to create connection init thread");
        goto rewind_fail;
    }
    okay = YMThreadStart(session->initConnectionDispatchThread);
    if ( ! okay ) {
        ymerr("failed to start start connection init thread");
        goto rewind_fail;
    }
    
    session->service = YMmDNSServiceCreate(session->type, session->name, (uint16_t)port);
    if ( ! session->service ) {
        ymerr("failed to create mdns service");
        goto rewind_fail;
    }
    okay = YMmDNSServiceStart(session->service);
    if ( ! okay ) {
        ymerr("failed to start mdns service");
        goto rewind_fail;
    }
    
    okay = __YMSessionObserveNetworkInterfaceChanges(session,true);
    
    return true;
    
rewind_fail:
    if ( socket >= 0 ) {
		int result, error; const char *errorStr;
        YM_CLOSE_SOCKET(socket);
	}
    session->ipv4ListenSocket = NULL_SOCKET;
    session->ipv6ListenSocket = NULL_SOCKET;
    if ( session->acceptThread ) {
        session->acceptThreadExitFlag = true;
        YMRelease(session->acceptThread);
        session->acceptThread = NULL;
    }
    if ( session->service ) {
        YMmDNSServiceStop(session->service, false);
        YMRelease(session->service);
        session->service = NULL;
    }
    return false;
}

bool YMSessionStopAdvertising(YMSessionRef session_)
{
    YM_IO_BOILERPLATE
    
    __YMSessionRef session = (__YMSessionRef)session_;
    
    session->acceptThreadExitFlag = true;
    bool okay = true;
    if ( session->ipv4ListenSocket != NULL_SOCKET ) {
		YM_CLOSE_SOCKET(session->ipv4ListenSocket);
        if ( result != 0 ) {
            ymerr("warning: failed to close ipv4 socket: %d %s",error,errorStr);
            okay = false;
        }
        session->ipv4ListenSocket = NULL_SOCKET;
    }
    if ( session->ipv6ListenSocket != NULL_SOCKET ) {
        YM_CLOSE_SOCKET(session->ipv6ListenSocket);
        if ( result != 0 ) {
            ymerr("warning: failed to close ipv6 socket: %d %s",error,errorStr);
            okay = false;
        }
        session->ipv6ListenSocket = NULL_SOCKET;
    }
    
    if ( session->service ) {
        bool mdnsOK = YMmDNSServiceStop(session->service, false);
        YMRelease(session->service);
        session->service = NULL;    
        if ( ! mdnsOK )
            okay = false;
    }
    
    __YMSessionObserveNetworkInterfaceChanges(session, false);
    
    return okay;
}

typedef struct __ym_connection_init_context_def
{
    __YMSessionRef session;
    int socket;
    struct sockaddr *addr;
    socklen_t addrLen; // redundant?
    bool ipv4;
} ___ym_connection_init_context_def;
typedef struct __ym_connection_init_context_def *__ym_connection_init_context;

YM_THREAD_RETURN YM_CALLING_CONVENTION __ym_session_accept_proc(YM_THREAD_PARAM ctx)
{
    __ym_session_accept_thread_context_ref context = ctx;
    __YMSessionRef session = context->session;
    bool ipv4 = context->ipv4;
    
    while ( ! session->acceptThreadExitFlag ) {
        int socket = ipv4 ? session->ipv4ListenSocket : session->ipv6ListenSocket;
        
        struct sockaddr_in6 *bigEnoughAddr = calloc(1,sizeof(struct sockaddr_in6));
        socklen_t thisLength = ipv4 ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);
        
        ymerr("accepting on sf%d...",socket);
        int aResult = accept(socket, (struct sockaddr *)bigEnoughAddr, &thisLength);
        if ( aResult < 0 ) {
            ymerr("accept(sf%d) failed: %d (%s)",socket,errno,strerror(errno));
            free(bigEnoughAddr);
            continue;
        }
        
        ymerr("accepted sf%d, dispatching connection init", aResult);
        
        if ( session->initializingFunc )
            session->initializingFunc(session,session->callbackContext);
        
        __ym_connection_init_context initCtx = (__ym_connection_init_context)YMALLOC(sizeof(struct __ym_connection_init_context_def));
        initCtx->session = (__YMSessionRef)YMRetain(session);
        initCtx->socket = aResult;
        initCtx->addr = (struct sockaddr *)bigEnoughAddr;
        initCtx->addrLen = thisLength;
        initCtx->ipv4 = ipv4;
        YMStringRef description = YMSTRC("init-connection");
        struct ym_thread_dispatch_t dispatch = { __ym_session_init_incoming_connection_proc, NULL, false, initCtx, description };
        YMThreadDispatchDispatch(session->initConnectionDispatchThread, dispatch);
        YMRelease(description);
    }
    
    session->acceptThreadExitFlag = false;
    
    YMRelease(session);
    free(context);
	YM_THREAD_END
}

void YM_CALLING_CONVENTION __ym_session_init_incoming_connection_proc(ym_thread_dispatch_ref dispatch)
{
    __ym_connection_init_context initCtx = dispatch->context;
    __YMSessionRef session = initCtx->session;
    int socket = initCtx->socket;
    struct sockaddr *addr = initCtx->addr;
    socklen_t addrLen = initCtx->addrLen;
    __unused bool ipv4 = initCtx->ipv4;
    
    YMAddressRef peerAddress = NULL;
    YMPeerRef peer = NULL;
    YMConnectionRef newConnection = NULL;
    
    ymlog("__ym_session_init_connection entered: sf%d %d %d",socket,addrLen,ipv4);
    
    peerAddress = YMAddressCreate(addr,ipv4?ntohs(((struct sockaddr_in *)addr)->sin_port):ntohs(((struct sockaddr_in6 *)addr)->sin6_port));
    peer = _YMPeerCreateWithAddress(peerAddress);
    if ( ! session->shouldAcceptFunc(session, peer, session->callbackContext) ) {
        ymlog("client rejected peer %s",YMSTR(YMAddressGetDescription(peerAddress)));
        goto catch_release;
    }
    
    newConnection = YMConnectionCreateIncoming(socket, peerAddress, YMConnectionStream, YMTLS, true);
    if ( ! newConnection ) {
        ymlog("failed to create new connection");
		if ( session->connectFailedFunc ) 
			session->connectFailedFunc(session, peer, false, session->callbackContext);
        goto catch_release;
    }
    
    ymlog("new connection %s",YMSTR(YMAddressGetDescription(peerAddress)));
    
    __YMSessionAddConnection(session, newConnection);
    ymassert(session->defaultConnection,"connection init incoming");
    session->connectedFunc(session,newConnection,session->callbackContext);
    
catch_release:
    
    if ( peerAddress )
        YMRelease(peerAddress);
    if ( peer )
        YMRelease(peer);
    if ( newConnection )
        YMRelease(newConnection);
    YMRelease(session);
    free(addr);
    free(initCtx);
}

#pragma mark shared

YMConnectionRef YMSessionGetDefaultConnection(YMSessionRef session_)
{
    __YMSessionRef session = (__YMSessionRef)session_;
    return session->defaultConnection;
}

YMDictionaryRef YMSessionGetConnections(YMSessionRef session_)
{
    __YMSessionRef session = (__YMSessionRef)session_;
    session = NULL;
    return *(YMDictionaryRef *)session; // todo, sync
}

void YMSessionStop(YMSessionRef session)
{
    YMSelfLock(session);
    {
        while ( YMDictionaryGetCount(session->connectionsByAddress) ) {
            YMDictionaryKey aKey = YMDictionaryGetRandomKey(session->connectionsByAddress);
            YMConnectionRef aConnection = (YMConnectionRef)YMDictionaryRemove(session->connectionsByAddress, aKey);
            YMConnectionClose(aConnection);
            YMRelease(aConnection);
        }
    }
    YMSelfUnlock(session);
}

void __YMSessionAddConnection(__YMSessionRef session_, YMConnectionRef connection)
{
    __YMSessionRef session = (__YMSessionRef)session_;
    
    YMSelfLock(session);
    {
        YMDictionaryKey key = (YMDictionaryKey)connection;
        ymassert(!YMDictionaryContains(session->connectionsByAddress, key), "connections by address state");
        YMDictionaryAdd(session->connectionsByAddress, key, (void *)YMRetain(connection));
    }
    YMSelfUnlock(session);
    
    YMConnectionSetCallbacks(connection, __ym_session_new_stream_proc, session,
                             __ym_session_stream_closing_proc, session,
                             __ym_session_connection_interrupted_proc, session);
    
    bool isNewDefault = (session->defaultConnection == NULL);
    YMAddressRef address = YMConnectionGetAddress(connection);
    ymassert(address, "could not initialize addr for connection");
    
    ymlog("adding %s connection for %s",isNewDefault?"default":"aux",YMSTR(YMAddressGetDescription(address)));
    
    if ( isNewDefault )
        session->defaultConnection = connection;
    
    ymassert(session->defaultConnection!= NULL,"no default connection");
}

bool __YMSessionInterrupt(__YMSessionRef session, YMConnectionRef floatConnection)
{
    bool first = false;
    YMSelfLock(session);
    {
        if ( ! session->interrupted ) {
            first = true;
            session->interrupted = true;
            session->defaultConnection = NULL;
    
            while ( YMDictionaryGetCount(session->connectionsByAddress) ) {
                YMDictionaryKey aKey = YMDictionaryGetRandomKey(session->connectionsByAddress);
                YMConnectionRef aConnection = (YMConnectionRef)YMDictionaryRemove(session->connectionsByAddress, aKey);
                ymassert(aConnection,"connection list state");
                ymerr("releasing %s",YMSTR(YMAddressGetDescription(YMConnectionGetAddress(aConnection))));
                if ( aConnection != floatConnection )
                    YMRelease(aConnection);
            }
        }
    }
    YMSelfUnlock(session);
    
    return first;
}

#pragma mark connection callbacks

void __ym_session_new_stream_proc(YMConnectionRef connection, YMStreamRef stream, void *context)
{
    __YMSessionRef session = context;
    
    YMAddressRef address = YMConnectionGetAddress(connection);
    YMPlexerStreamID streamID = YM_STREAM_INFO(stream)->streamID;
    ymlog("new incoming stream %llu on %s",streamID,YMSTR(YMAddressGetDescription(address)));
    
    if ( connection != session->defaultConnection )
        ymerr("warning: new stream on non-default connection");
    
    // is it weird that we don't report 'connection' here, despite user only being concerned with "active"?
    if ( session->newStreamFunc )
        session->newStreamFunc(session,connection,stream,session->callbackContext);
}

void __ym_session_stream_closing_proc(YMConnectionRef connection, YMStreamRef stream, void *context)
{
    __YMSessionRef session = context;
    
    YMAddressRef address = YMConnectionGetAddress(connection);
    YMPlexerStreamID streamID = YM_STREAM_INFO(stream)->streamID;
    ymlog("remote stream %llu closing on %s",streamID,YMSTR(YMAddressGetDescription(address)));
    
    if ( connection != session->defaultConnection ) {
        ymerr("warning: closing remote stream on non-default connection");
        return;
    }
    
    if ( session->streamClosingFunc )
        session->streamClosingFunc(session,connection,stream,session->callbackContext);
}

void __ym_session_connection_interrupted_proc(YMConnectionRef connection, void *context)
{
    __YMSessionRef session = context;
    
    bool first = __YMSessionInterrupt(session, connection);
    if ( ! first )
        return;
    
    bool isDefault = ( connection == session->defaultConnection );
    
	    ymerr("connection interrupted: %s",YMSTR(YMAddressGetDescription(YMConnectionGetAddress(connection))));
    
    if ( isDefault ) {
        if ( session->interruptedFunc )
            session->interruptedFunc(session,session->callbackContext);
    }
    
    YMRelease(connection);
}

#pragma mark client mdns callbacks

void __ym_mdns_service_appeared_func(__unused YMmDNSBrowserRef browser, YMmDNSServiceRecord * service, void *context)
{
    __YMSessionRef session = context;
    ymlog("__ym_mdns_service_appeared_func: %s",YMSTR(service->name));
    
    YMPeerRef peer = _YMPeerCreate(service->name, NULL, NULL);
    YMSelfLock(session);
    YMDictionaryAdd(session->knownPeers, (YMDictionaryKey)peer, (void *)peer);
    YMSelfUnlock(session);
    
    session->addedFunc(session,peer,session->callbackContext);
}

void __ym_mdns_service_removed_func(__unused YMmDNSBrowserRef browser, YMStringRef name, void *context)
{
    __YMSessionRef session = context;
    
    ymlog("__ym_mdns_service_removed_func %s",YMSTR(name));
    
    YMSelfLock(session);
    {
        YMDictionaryKey mysteryKey = (YMDictionaryKey)MAX_OF(uint64_t);
        bool found = false;
        YMDictionaryEnumRef myEnum = YMDictionaryEnumeratorBegin(session->knownPeers);
        while ( myEnum ) {
            YMPeerRef peer = (YMPeerRef)myEnum->value;
            if ( strcmp(YMSTR(YMPeerGetName(peer)),YMSTR(name)) == 0 ) {
                found = true;
                mysteryKey = myEnum->key;
            }
            myEnum = YMDictionaryEnumeratorGetNext(myEnum);
        }
        YMDictionaryEnumeratorEnd(myEnum);
        
        if ( ! found ) {
            ymerr("warning: notified of removal of unknown peer: %s", YMSTR(name));
            return;
        }
        
        YMDictionaryRemove(session->knownPeers, mysteryKey);
    }
    YMSelfUnlock(session);
}

void __ym_mdns_service_updated_func(__unused YMmDNSBrowserRef browser, YMmDNSServiceRecord *service, void *context)
{
    __unused __YMSessionRef session = context;
    ymlog("__ym_mdns_service_updated_func %s",YMSTR(service->name));
}

void __ym_mdns_service_resolved_func(__unused YMmDNSBrowserRef browser, bool success, YMmDNSServiceRecord *service, void *context)
{
    __YMSessionRef session = context;
    
    ymlog("__ym_mdns_service_resolved_func %s",YMSTR(service->name));
    
    bool found = false;
    YMPeerRef peer = NULL;
    YMSelfLock(session);
    {
        YMDictionaryEnumRef myEnum = YMDictionaryEnumeratorBegin(session->knownPeers);
        while ( myEnum ) {
            peer = (YMPeerRef)myEnum->value;
            if ( strcmp(YMSTR(YMPeerGetName(peer)),YMSTR(service->name)) == 0 ) {
                found = true;
                peer = (YMPeerRef)myEnum->value;
                break;
            }
            
            myEnum = YMDictionaryEnumeratorGetNext(myEnum);
        }
        YMDictionaryEnumeratorEnd(myEnum);
        
        _YMPeerSetAddresses(peer, service->sockaddrList);
        _YMPeerSetPort(peer, service->port);
    }
    YMSelfUnlock(session);
    
    if ( found ) {
        if ( success )
            session->resolvedFunc(session,peer,session->callbackContext);
        else
            session->resolveFailedFunc(session,peer,session->callbackContext);
    } else
        ymabort("notified of resolution of unknown peer: %s",YMSTR(service->name));
}

#pragma mark config change observing

void __YMSessionUpdateNetworkConfigDate(__YMSessionRef session)
{
	ymerr("network config changed on %p", session);
}

#if defined(YMAPPLE)

void __ym_SCDynamicStoreCallBack(__unused SCDynamicStoreRef store, __unused CFArrayRef changedKeys, void *info)
{
	__YMSessionUpdateNetworkConfigDate(info);
}

YM_THREAD_RETURN YM_CALLING_CONVENTION __ym_session_macos_sc_runloop_proc(YM_THREAD_PARAM ctx)
{
    __YMSessionRef session = ctx;

    SCDynamicStoreContext storeCtx = { 0, session, NULL, NULL, NULL };
    
#if !defined(YMIOS)
    SCDynamicStoreRef scStore = SCDynamicStoreCreate(NULL, CFSTR("libyammer"), __ym_SCDynamicStoreCallBack, &storeCtx);
    const CFStringRef names[2] = { CFSTR("State:/Network/Global/IPv4/*"), CFSTR("State:/Network/Global/IPv6/*") };
    CFArrayRef namesArray = CFArrayCreate(NULL, (const void **)names, 2, NULL);
    SCDynamicStoreSetNotificationKeys(scStore, NULL, namesArray);
    CFRelease(namesArray);

    CFRunLoopRef aRunloop = CFRunLoopGetCurrent();
    CFRunLoopSourceRef storeSource = SCDynamicStoreCreateRunLoopSource(NULL, scStore, 0);
    CFRunLoopAddSource(aRunloop, storeSource, kCFRunLoopDefaultMode);
    
    ymerr("network interface change observer entered");
    
    while ( ! session->scObserverThreadExitFlag )
      CFRunLoopRunInMode(kCFRunLoopDefaultMode, 1, false);

    CFRunLoopRemoveSource(aRunloop, storeSource, kCFRunLoopDefaultMode);
    CFRelease(storeSource);

    ymerr("network interface change observer exiting");
    
#else
    // todo Reachability?
    storeCtx.version = 6969;
    ymerr("warning: network interface change observing unimplemented on ios");
#endif
    
    YM_THREAD_END
}

bool __YMSessionObserveNetworkInterfaceChangesMacos(__YMSessionRef session, bool startStop)
{
	if (startStop) {
		if (session->scObserverThread)
			return true;
		
		session->scObserverThread = YMThreadCreate(NULL,__ym_session_macos_sc_runloop_proc,session);
		session->scObserverThreadExitFlag = false;
		return session->scObserverThread && YMThreadStart(session->scObserverThread);
	}
	else {
		if (!session->scObserverThread)
			return false;

		ymdbg("stopped observing network interface changes");
		session->scObserverThreadExitFlag = true;
		YMThreadJoin(session->scObserverThread);
		YMRelease(session->scObserverThread);
		session->scObserverThread = NULL;
	}

	return true;
}

#elif defined(YMLINUX)

YM_THREAD_RETURN YM_CALLING_CONVENTION __ym_session_linux_proc_net_dev_scrape_proc(YM_THREAD_PARAM ctx)
{
	__YMSessionRef session = ctx;
	
	YMDictionaryRef thisIter = NULL, prevIter = NULL;
	// this /proc scraping method was cribbed from ifplugd. thanks ifplugd!
	// PF_NETLINK approach didn't work after not a whole lot of trying on raspbian
	while (!session->linuxPollThreadExitFlag) {
		FILE *f;
		char ln[256];

		if (!(f = fopen("/proc/net/dev", "r"))) {
			ymerr("failed to open /proc/net/dev: %d %s", errno, strerror(errno));
			YM_THREAD_END
		}
		
		fgets(ln,sizeof(ln),f);
		fgets(ln,sizeof(ln),f);
		
		thisIter = YMDictionaryCreate();
		
		while (fgets(ln,sizeof(ln),f)) {
			char *p, *e;
			p = ln + strspn(ln, " \t");
			if (!(e = strchr(p, ':'))) {
				ymerr("failed to parse /proc/net/dev");
				fclose(f);
				YM_THREAD_END
			}

			*e = '\0';

			int fd;
			if ((fd = socket(PF_INET, SOCK_DGRAM, 0)) < 0) {
				ymerr("failed to open /proc/net/dev: %d %s",errno,strerror(errno));
				fclose(f);
				YM_THREAD_END
			}
			
	        interface_status_t s;
	        if ((s = interface_detect_beat_ethtool(fd, p)) == IFSTATUS_ERR)
	            if ((s = interface_detect_beat_mii(fd, p)) == IFSTATUS_ERR)
	                if ((s = interface_detect_beat_wlan(fd, p)) == IFSTATUS_ERR)
	                    s = interface_detect_beat_iff(fd, p);
	        close(fd);
	        
			bool somethingMoved = false;
	        if ( prevIter ) {
				bool found = false;
				YMDictionaryEnumRef dEnum = YMDictionaryEnumeratorBegin(prevIter);
				while ( dEnum ) {
					if ( strcmp((char *)dEnum->key,p) == 0 ) {
						found = true;
						interface_status_t prevStatus = (interface_status_t)dEnum->value;
						somethingMoved = prevStatus != s;
						if ( somethingMoved ) {
							ymerr("%s: changed status: %s",p,s==IFSTATUS_UP?"up":(s==IFSTATUS_DOWN?"down":"?"));
							__YMSessionUpdateNetworkConfigDate(session);
						}
					}
					dEnum = YMDictionaryEnumeratorGetNext(dEnum);
				}
				
				if ( ! found ) {
					ymerr("%s: new interface status: %s",p,s==IFSTATUS_UP?"up":(s==IFSTATUS_DOWN?"down":"?"));
					__YMSessionUpdateNetworkConfigDate(session);
				}
			}
	        char *ifCopy = strdup(p);
	        YMDictionaryAdd(thisIter,(YMDictionaryKey)ifCopy,(YMDictionaryValue)s);
	
	        switch(s) {
	            case IFSTATUS_UP:
	                ymdbg("%s: up", p);
	                break;	                
	            case IFSTATUS_DOWN:
	                ymdbg("%s: down", p);
	                break;	
	            default:
					ymdbg("%s: not supported", p);
	                break;
	        }
		}

		fclose(f);

		if (session->linuxPollThreadExitFlag)
			break;
		sleep(1);
		
		while ( prevIter && ( YMDictionaryGetCount(prevIter) > 0 ) ) {
			YMDictionaryKey key = YMDictionaryGetRandomKey(prevIter);
			YMDictionaryRemove(prevIter,key);
			free((void *)key);
		}
		if ( prevIter ) YMRelease(prevIter);
		prevIter = thisIter;
	}

	YM_THREAD_END
}

bool __YMSessionObserveNetworkInterfaceChangesLinux(__YMSessionRef session, bool startStop)
{
	if (!startStop) {
		if (!session->linuxPollThread)
			return false;

		session->linuxPollThreadExitFlag = true;
		YMThreadJoin(session->linuxPollThread);
		YMRelease(session->linuxPollThread);
		session->linuxPollThread = NULL;

		return true;
	}

	session->linuxPollThread = YMThreadCreate(NULL, __ym_session_linux_proc_net_dev_scrape_proc, session);
	session->linuxPollThreadExitFlag = false;
	bool okay = YMThreadStart(session->linuxPollThread);
	if (!okay) {
		YMRelease(session->linuxPollThread);
		session->linuxPollThread = NULL;
	}
}

#else

HRESULT STDMETHODCALLTYPE ymsink_QueryInterface(__RPC__in IWbemObjectSink * This,/* [in] */ __RPC__in REFIID riid,/* [annotation][iid_is][out] */_COM_Outptr_  void **ppvObject)
{
	LPOLESTR iName;
	StringFromIID(riid, &iName);
	ymerrg("ymsink_QueryInterface %S", iName);
	CoTaskMemFree(iName);

	// IMarshal:			{00000003-0000-0000-C000-000000000046}
	// IUnknown:			{00000000-0000-0000-C000-000000000046}
	// "IdentityUnmarshal":	{0000001B-0000-0000-C000-000000000046}

	bool isIUnknown = IsEqualIID(riid, &IID_IUnknown);
	bool isWOS = IsEqualIID(riid, &IID_IWbemObjectSink);
    if (isIUnknown || isWOS) {
		*ppvObject = (IWbemObjectSink *)This;
		This->lpVtbl->AddRef(This);
		return S_OK;
	}

	*ppvObject = NULL;
	return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE ymsink_AddRef(__RPC__in IWbemObjectSink * This)
{
	ymerrg("ymsink_AddRef");
	return 1;
}

ULONG STDMETHODCALLTYPE ymsink_Release(__RPC__in IWbemObjectSink * This)
{
	ymerrg("ymsink_Release");
	return 1;
}

HRESULT STDMETHODCALLTYPE ymsink_Indicate(__RPC__in IWbemObjectSink * This,/* [in] */ long lObjectCount,/* [size_is][in] */ __RPC__in_ecount_full(lObjectCount) IWbemClassObject **apObjArray)
{
	ymerrg("ymsink_Indicate");
	__YMSessionUpdateNetworkConfigDate(((YM_IWbemObjectSink *)This)->that);
	return WBEM_S_NO_ERROR;
}

HRESULT STDMETHODCALLTYPE ymsink_SetStatus(__RPC__in IWbemObjectSink * This,/* [in] */ long lFlags,/* [in] */ HRESULT hResult,/* [unique][in] */ __RPC__in_opt BSTR strParam,/* [unique][in] */ __RPC__in_opt IWbemClassObject *pObjParam)
{
	ymerrg("ymsink_SetStatus");
	if (lFlags == WBEM_STATUS_COMPLETE) {
		ymlogg("call complete: hResult 0x%X", hResult);
	} else if (lFlags == WBEM_STATUS_PROGRESS) {
		ymlogg("call in progress.");
	}

	return WBEM_S_NO_ERROR;
}

bool __YMSessionObserveNetworkInterfaceChangesWin32(__YMSessionRef session, bool startStop)
{
	static BSTR gbNamespace = NULL;
	static BSTR gbWql = NULL;
	static BSTR gbQuery = NULL;

	if (!startStop) {
		if (session->gobbledygook) {
			session->gobbledygook->lpVtbl->CancelAsyncCall(session->gobbledygook, (IWbemObjectSink *)session->my_gobbledygook);
			session->gobbledygook->lpVtbl->Release(session->gobbledygook);
			session->gobbledygook = NULL;

			YMRelease(session->my_gobbledygook->that);
			session->my_gobbledygook->lpVtbl->Release((IWbemObjectSink *)session->my_gobbledygook);
			// these malloc'd pointers move on the first QueryInterface call
			// as if some standard com thing wraps them in something else.
			// presumably they still need to be free'd, wherever it is they went.
			// or maybe that's done for us in Release?
			//free(session->my_gobbledygook->lpVtbl);
			//free(session->my_gobbledygook);
			session->my_gobbledygook = NULL;

			return true;
		}

		return false;
	}

	HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);    // Initialize COM
	if (FAILED(hr)) {
		ymerr("CoInitializeEx failed");
		goto catch_return;
	}

	IWbemLocator *pLoc = NULL;
	IUnsecuredApartment* pUnsecApp = NULL;
	IUnknown* pStubUnk = NULL;

	if (!gbNamespace) {
#define YM_WMI_DEFAULT_NAMESPACE	"ROOT\\CIMV2"
#define YM_WMI_WQL					"WQL"
#define YM_WMI_ALL_INTERFACES		"SELECT * FROM __InstanceModificationEvent WITHIN 1 WHERE TargetInstance ISA 'Win32_NetworkAdapter'"
		gbNamespace = SysAllocString(_T(YM_WMI_DEFAULT_NAMESPACE));
		gbWql = SysAllocString(_T(YM_WMI_WQL));
		gbQuery = SysAllocString(_T(YM_WMI_ALL_INTERFACES));
	}

	hr = CoInitializeSecurity(NULL,                       // security descriptor
		-1,                          // use this simple setting
		NULL,                        // use this simple setting
		NULL,                        // reserved
		RPC_C_AUTHN_LEVEL_DEFAULT,   // authentication level
		RPC_C_IMP_LEVEL_DELEGATE, // impersonation level
		NULL,                        // use this simple setting
		EOAC_NONE,                   // no special capabilities
		NULL);                          // reserved

	if (FAILED(hr)) {
		ymerr("CoInitializeSecurity failed");
		goto catch_release;
	}

	hr = CoCreateInstance(&CLSID_WbemLocator, 0,
		CLSCTX_INPROC_SERVER, &IID_IWbemLocator, (LPVOID *)&pLoc);

	if (FAILED(hr)) {
		ymerr("CoCreateInstance failed");
		goto catch_release;
	}

	// Connect to the root\default namespace with the current user.
	hr = pLoc->lpVtbl->ConnectServer(pLoc, gbNamespace, NULL, NULL, NULL, 0, NULL, NULL, &session->gobbledygook);
	if (FAILED(hr)) {
		ymerr("ConnectServer failed");
		goto catch_release;
	}

	hr = CoSetProxyBlanket((IUnknown *)session->gobbledygook, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL, RPC_C_AUTHN_LEVEL_CALL,
		RPC_C_IMP_LEVEL_DELEGATE, NULL, EOAC_NONE);

	if (FAILED(hr)) {
		ymerr("CoSetProxyBlanket failed");
		goto catch_release;
	}

	hr = CoCreateInstance(&CLSID_UnsecuredApartment, NULL,
		CLSCTX_LOCAL_SERVER, &IID_IUnsecuredApartment,
		(void**)&pUnsecApp);

	session->my_gobbledygook = YMALLOC(sizeof(YM_IWbemObjectSink));
	session->my_gobbledygook->that = (void *)YMRetain(session);
	session->my_gobbledygook->lpVtbl = YMALLOC(sizeof(IWbemObjectSinkVtbl));
	session->my_gobbledygook->lpVtbl->AddRef = ymsink_AddRef;
	session->my_gobbledygook->lpVtbl->Indicate = ymsink_Indicate;
	session->my_gobbledygook->lpVtbl->QueryInterface = ymsink_QueryInterface;
	session->my_gobbledygook->lpVtbl->Release = ymsink_Release;
	session->my_gobbledygook->lpVtbl->SetStatus = ymsink_SetStatus;

	hr = pUnsecApp->lpVtbl->CreateObjectStub(pUnsecApp, (IUnknown *)session->my_gobbledygook, &pStubUnk);

	if (FAILED(hr)) {
		ymerr("CreateObjectStub failed");
		goto catch_release;
	}

	hr = pStubUnk->lpVtbl->QueryInterface(pStubUnk, &IID_IWbemObjectSink, (void **)&session->my_gobbledygook);

	if (FAILED(hr)) {
		ymerr("QueryInterface failed");
		goto catch_release;
	}

	hr = session->gobbledygook->lpVtbl->ExecNotificationQueryAsync(session->gobbledygook,
		gbWql,
		gbQuery,
		WBEM_FLAG_SEND_STATUS,
		NULL,
		(IWbemObjectSink *)session->my_gobbledygook);

	if (FAILED(hr)) {
		ymerr("ExecNotificationQueryAsync failed");
		goto catch_release;
	}

	ymdbg("started observing interface changes");

	return true;

catch_release:

	if (session->my_gobbledygook) {
		free(session->my_gobbledygook->lpVtbl);
		free(session->my_gobbledygook);
		session->my_gobbledygook = NULL;
	}
	if (session->gobbledygook) {
		session->gobbledygook->lpVtbl->Release(session->gobbledygook);
		session->gobbledygook = NULL;
	}
	if (pLoc)
		pLoc->lpVtbl->Release(pLoc);

	CoUninitialize();
catch_return:
	return false;

	//    IEnumWbemClassObject* pEnumerator = NULL;
	//    hr = pSvc->lpVtbl->ExecQuery(pSvc, gbWql, gbQuery,
	//                         WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL, &pEnumerator);
	//    
	//    if (FAILED(hr))
	//    {
	//        pSvc->lpVtbl->Release(pSvc);
	//        pLoc->lpVtbl->Release(pLoc);
	//        CoUninitialize();
	//        return false;
	//    }
	//    
	//    IWbemClassObject *pClassObj;
	//    ULONG ulReturnVal;
	//	int i = 0;
	//	if ( pEnumerator ) {
	//		while ( 1 ) {
	//			hr = pEnumerator->lpVtbl->Next(pEnumerator, WBEM_INFINITE, 1, &pClassObj, &ulReturnVal);
	//        
	//			if ( FAILED(hr) || ulReturnVal == 0 )
	//				break;
	//
	//			VARIANT value;
	//			CIMTYPE cimType;
	//			long flavor;
	//			hr = pClassObj->lpVtbl->Get(pClassObj,_T("Description"),0,&value,&cimType,&flavor);
	//			if ( FAILED(hr) ) {
	//				ymerr("failed to query status of a network interface");
	//			} else {
	//				if ( value.vt == VT_NULL )
	//					ymerr("the %dth interface has a '?' of null",i);
	//				else if ( value.vt == VT_BSTR )
	//					ymerr("the %dth interface has description: %S",i,value.bstrVal);
	//				else if ( value.vt == VT_LPSTR || value.vt == VT_LPWSTR )
	//					ymerr("the %dth interface has a '?' of some kind of string", i);
	//				else if ( value.vt == VT_BOOL )
	//					ymerr("the %dth interface has a '?' of %s",i,value.boolVal ? "true" : "false");
	//				else
	//					ymerr("the %dth interface has a '?' of 'something happened'", i);
	//			}
	//        
	//			pClassObj->lpVtbl->Release(pClassObj);
	//			i++;
	//		}
	//
	//		pEnumerator->lpVtbl->Release(pEnumerator);
	//	} else
	//		return false;
}

#endif

YM_EXTERN_C_POP
