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
#include "YMLog.h"
#define YM_LOG_PRE "session[%s]: "
#define YM_LOG_DSC YMSTR(session->logDescription)

#if !defined(YMWIN32)
# if defined(YMLINUX)
#  define __USE_POSIX
#  include <sys/socket.h>
# endif
# include <netinet/in.h>
# include <netdb.h> // struct hostent
#else
# include <winsock2.h>
# include <ws2tcpip.h>
#endif

YM_EXTERN_C_PUSH

typedef struct __ym_session_t
{
    _YMType _type;
    
    // shared
    YMStringRef type;
    YMStringRef logDescription;
    YMDictionaryRef connectionsByAddress;
    YMLockRef connectionsByAddressLock;
    YMConnectionRef defaultConnection;
    bool interrupted;
    
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
    YMLockRef knownPeersLock;
    YMThreadRef connectDispatchThread;
    
    ym_session_added_peer_func addedFunc;
    ym_session_removed_peer_func removedFunc;
    ym_session_resolve_failed_func resolveFailedFunc;
    ym_session_resolved_peer_func resolvedFunc;
    ym_session_connect_failed_func connectFailedFunc;
    ym_session_should_accept_func shouldAcceptFunc;
    ym_session_connected_func connectedFunc;
    ym_session_interrupted_func interruptedFunc;
    ym_session_new_stream_func newStreamFunc;
    ym_session_stream_closing_func streamClosingFunc;
    void *callbackContext;
} __ym_session_t;
typedef struct __ym_session_t *__YMSessionRef;

#pragma mark setup

__YMSessionRef __YMSessionCreateShared(YMStringRef type, bool isServer);
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
    
    YMStringRef memberName = YMSTRC("connections-by-address");
    session->connectionsByAddressLock = YMLockCreate(YMInternalLockType, memberName);
    YMRelease(memberName);
    
    session->knownPeers = YMDictionaryCreate();
    memberName = YMSTRC("available-peers");
    session->knownPeersLock = YMLockCreateWithOptionsAndName(YMInternalLockType, memberName);
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

void YMSessionSetCommonCallbacks(YMSessionRef session_, ym_session_connected_func connected, ym_session_interrupted_func interrupted,
                                                        ym_session_new_stream_func new_, ym_session_stream_closing_func closing)
{
    __YMSessionRef session = (__YMSessionRef)session_;
    
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
    if ( session->initConnectionDispatchThread )
    {
        YMThreadDispatchJoin(session->initConnectionDispatchThread);
        YMRelease(session->initConnectionDispatchThread);
    }
    if ( session->connectDispatchThread )
    {
        YMThreadDispatchJoin(session->connectDispatchThread);
        YMRelease(session->connectDispatchThread);
    }
    
    // shared
    YMRelease(session->type);
    YMRelease(session->connectionsByAddress);
    YMRelease(session->connectionsByAddressLock);
    YMRelease(session->logDescription);
    
    // server
    if ( session->name )
        YMRelease(session->name);
    if ( session->service )
    {
        YMmDNSServiceStop(session->service, true); // xxx
        YMRelease(session->service);
    }
    
    if ( session->browser )
    {
        YMmDNSBrowserStop(session->browser);
        YMRelease(session->browser);
    }
    
    YMDictionaryEnumRef aEnum = YMDictionaryEnumeratorBegin(session->knownPeers);
    while ( aEnum )
    {
        YMPeerRef aPeer = aEnum->value;
        YMRelease(aPeer);
        aEnum = YMDictionaryEnumeratorGetNext(aEnum);
    }
    if ( aEnum ) YMDictionaryEnumeratorEnd(aEnum);
    
    YMRelease(session->knownPeers);
    YMRelease(session->knownPeersLock);
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
    if ( ! session->browser )
    {
        ymerr(YM_LOG_PRE "error: failed to create browser",YM_LOG_DSC);
        return false;
    }
    
    bool startOK = YMmDNSBrowserStart(session->browser);
    if ( ! startOK )
    {
        ymerr(YM_LOG_PRE "error: failed to start browser",YM_LOG_DSC);
        return false;
    }
    
    ymlog(YM_LOG_PRE "client started for '%s'",YM_LOG_DSC,YMSTR(session->type));
    
    return true;
}

bool YMSessionStopBrowsing(YMSessionRef session_)
{
    __YMSessionRef session = (__YMSessionRef)session_;
    
    bool okay = true;
    
    if ( session->browser )
    {
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
    
    YMLockLock(session->knownPeersLock);
    {
        YMDictionaryEnumRef peerEnum = YMDictionaryEnumeratorBegin(session->knownPeers);
        while ( peerEnum )
        {
            if ( YMStringEquals(YMPeerGetName((YMPeerRef)peerEnum->value), peerName) )
            {
                thePeer = peerEnum->value;
                break;
            }
            peerEnum = YMDictionaryEnumeratorGetNext(peerEnum);
        }
        YMDictionaryEnumeratorEnd(peerEnum);
    }
    YMLockUnlock(session->knownPeersLock);
    
    return thePeer;
}

bool YMSessionResolvePeer(YMSessionRef session_, YMPeerRef peer)
{
    __YMSessionRef session = (__YMSessionRef)session_;
    
    YMLockLock(session->knownPeersLock);
    YMStringRef peerName = YMPeerGetName(peer);
    bool knownPeer = true;
    if ( ! YMDictionaryContains(session->knownPeers, (YMDictionaryKey)peer) )
    {
        ymerr(YM_LOG_PRE "requested resolve of unknown peer: %s",YM_LOG_DSC,YMSTR(peerName));
        knownPeer = false;
    }
    YMLockUnlock(session->knownPeersLock);
    
    if ( ! knownPeer )
        return false;
    
    return YMmDNSBrowserResolve(session->browser, peerName);
}

typedef struct __ym_session_connect_async_context_t
{
    __YMSessionRef session;
    YMPeerRef peer;
    YMConnectionRef connection;
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
    YMLockLock(session->knownPeersLock);
    YMStringRef peerName = YMPeerGetName(peer);
    if ( ! YMDictionaryContains(session->knownPeers, (YMDictionaryKey)peer) )
    {
        ymerr(YM_LOG_PRE "requested connect to unknown peer: %s",YM_LOG_DSC,YMSTR(peerName));
        knownPeer = false;
    }
    YMLockUnlock(session->knownPeersLock);
    
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
        
        name = YMSTRC("session-async-connect");
        struct ym_thread_dispatch_t connectDispatch = {__ym_session_connect_async_proc, 0, 0, context, name};
        
        if ( ! sync )
        {
            if ( ! session->connectDispatchThread )
            {
                session->connectDispatchThread = YMThreadDispatchCreate(name);
                if ( ! session->connectDispatchThread )
                {
                    ymerr(YM_LOG_PRE "error: failed to create async connect thread",YM_LOG_DSC);
                    goto catch_fail;
                }
                bool okay = YMThreadStart(session->connectDispatchThread);
                if ( ! okay )
                {
                    ymerr(YM_LOG_PRE "error: failed to start async connect thread",YM_LOG_DSC);
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
    free(context);
    
    ymlog(YM_LOG_PRE "__ym_session_connect_async_proc entered",YM_LOG_DSC);
    
    bool okay = YMConnectionConnect(connection);
    
    if ( okay )
    {
        __YMSessionAddConnection(session, connection);
        session->connectedFunc(session, connection, session->callbackContext);
    }
    else
        session->connectFailedFunc(session, peer, session->callbackContext);
    
    YMRelease(session);
    YMRelease(peer);
    YMRelease(connection);
    
    ymlog(YM_LOG_PRE "__ym_session_connect_async_proc exiting: %s",YM_LOG_DSC,okay?"success":"fail");
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
    
    bool mDNSOK = false;
    int socket = -1;
    bool ipv4 = true;
    
    int32_t port = YMPortReserve(ipv4, &socket);
    if ( port < 0 || socket == -1 || socket > UINT16_MAX )
    {
        ymerr(YM_LOG_PRE "error: failed to reserve port for server start",YM_LOG_DSC);
        return false;
    }
    
    int aResult = listen(socket, 1);
    if ( aResult != 0 )
    {
        ymerr(YM_LOG_PRE "error: failed to listen for server start",YM_LOG_DSC);
		int result, error; const char *errorStr;
        YM_CLOSE_SOCKET(socket);
        goto rewind_fail;
    }
    
    ymlog(YM_LOG_PRE "listening on %u",YM_LOG_DSC,port);
    
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
    if ( ! session->acceptThread )
    {
        ymerr(YM_LOG_PRE "error: failed to create accept thread",YM_LOG_DSC);
        goto rewind_fail;
    }
    session->acceptThreadExitFlag = false;
    bool threadOK = YMThreadStart(session->acceptThread);
    if ( ! threadOK )
    {
        ymerr(YM_LOG_PRE "error: failed to start accept thread",YM_LOG_DSC);
        goto rewind_fail;
    }
    
    memberName = YMStringCreateWithFormat("session-init-%s",YMSTR(session->type),NULL);
    session->initConnectionDispatchThread = YMThreadDispatchCreate(memberName);
    YMRelease(memberName);
    if ( ! session->initConnectionDispatchThread )
    {
        ymerr(YM_LOG_PRE "error: failed to create connection init thread",YM_LOG_DSC);
        goto rewind_fail;
    }
    threadOK = YMThreadStart(session->initConnectionDispatchThread);
    if ( ! threadOK )
    {
        ymerr(YM_LOG_PRE "error: failed to start start connection init thread",YM_LOG_DSC);
        goto rewind_fail;
    }
    
    session->service = YMmDNSServiceCreate(session->type, session->name, (uint16_t)port);
    if ( ! session->service )
    {
        ymerr(YM_LOG_PRE "error: failed to create mdns service",YM_LOG_DSC);
        goto rewind_fail;
    }
    mDNSOK = YMmDNSServiceStart(session->service);
    if ( ! mDNSOK )
    {
        ymerr(YM_LOG_PRE "error: failed to start mdns service",YM_LOG_DSC);
        goto rewind_fail;
    }
    
    return true;
    
rewind_fail:
    if ( socket >= 0 )
	{
		int result, error; const char *errorStr;
        YM_CLOSE_SOCKET(socket);
	}
    session->ipv4ListenSocket = NULL_SOCKET;
    session->ipv6ListenSocket = NULL_SOCKET;
    if ( session->acceptThread )
    {
        session->acceptThreadExitFlag = true;
        YMRelease(session->acceptThread);
        session->acceptThread = NULL;
    }
    if ( session->service )
    {
        if ( mDNSOK )
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
    if ( session->ipv4ListenSocket != NULL_SOCKET )
    {
		YM_CLOSE_SOCKET(session->ipv4ListenSocket);
        if ( result != 0 )
        {
            ymerr(YM_LOG_PRE "warning: failed to close ipv4 socket: %d %s",YM_LOG_DSC,error,errorStr);
            okay = false;
        }
        session->ipv4ListenSocket = NULL_SOCKET;
    }
    if ( session->ipv6ListenSocket != NULL_SOCKET )
    {
        YM_CLOSE_SOCKET(session->ipv6ListenSocket);
        if ( result != 0 )
        {
            ymerr(YM_LOG_PRE "warning: failed to close ipv6 socket: %d %s",YM_LOG_DSC,error,errorStr);
            okay = false;
        }
        session->ipv6ListenSocket = NULL_SOCKET;
    }
    
    if ( session->service )
    {
        bool mdnsOK = YMmDNSServiceStop(session->service, false);
        YMRelease(session->service);
        session->service = NULL;    
        if ( ! mdnsOK )
            okay = false;
    }
    
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
    
    while ( ! session->acceptThreadExitFlag )
    {
        int socket = ipv4 ? session->ipv4ListenSocket : session->ipv6ListenSocket;
        
        struct sockaddr_in6 *bigEnoughAddr = calloc(1,sizeof(struct sockaddr_in6));
        socklen_t thisLength = ipv4 ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);
        
        ymerr(YM_LOG_PRE "accepting on sf%d...",YM_LOG_DSC,socket);
        int aResult = accept(socket, (struct sockaddr *)bigEnoughAddr, &thisLength);
        if ( aResult < 0 )
        {
            ymerr(YM_LOG_PRE "accept(sf%d) failed: %d (%s)",YM_LOG_DSC,socket,errno,strerror(errno));
            free(bigEnoughAddr);
            continue;
        }
        
        ymerr(YM_LOG_PRE "accepted sf%d, dispatching connection init",YM_LOG_DSC, aResult);
        
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
    
    YMAddressRef address = NULL;
    YMPeerRef peer = NULL;
    YMConnectionRef newConnection = NULL;
    
    ymlog(YM_LOG_PRE "__ym_session_init_connection entered: sf%d %d %d",YM_LOG_DSC,socket,addrLen,ipv4);
    
    address = YMAddressCreate(addr,ipv4?ntohs(((struct sockaddr_in *)addr)->sin_port):ntohs(((struct sockaddr_in6 *)addr)->sin6_port));
    peer = _YMPeerCreateWithAddress(address);
    if ( ! session->shouldAcceptFunc(session, peer, session->callbackContext) )
    {
        ymlog(YM_LOG_PRE "client rejected peer %s",YM_LOG_DSC,YMSTR(YMAddressGetDescription(address)));
        goto catch_release;
    }
    
    newConnection = YMConnectionCreateIncoming(socket, address, YMConnectionStream, YMTLS, true);
    if ( ! newConnection )
    {
        ymlog(YM_LOG_PRE "failed to create new connection",YM_LOG_DSC);
		if ( session->connectFailedFunc ) 
			session->connectFailedFunc(session, peer, session->callbackContext);
        goto catch_release;
    }
    
    ymlog(YM_LOG_PRE "new connection %s",YM_LOG_DSC,YMSTR(YMAddressGetDescription(address)));
    
    __YMSessionAddConnection(session, newConnection);
    ymassert(session->defaultConnection,"connection init incoming");
    session->connectedFunc(session,newConnection,session->callbackContext);
    
catch_release:
    
    if ( address )
        YMRelease(address);
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

void YMAPI YMSessionStop(YMSessionRef session)
{
    YMLockLock(session->connectionsByAddressLock);
    {
        while ( YMDictionaryGetCount(session->connectionsByAddress) )
        {
            YMDictionaryKey aKey = YMDictionaryGetRandomKey(session->connectionsByAddress);
            YMConnectionRef aConnection = (YMConnectionRef)YMDictionaryRemove(session->connectionsByAddress, aKey);
            YMConnectionClose(aConnection);
            YMRelease(aConnection);
        }
    }
    YMLockUnlock(session->connectionsByAddressLock);
}

void __YMSessionAddConnection(__YMSessionRef session_, YMConnectionRef connection)
{
    __YMSessionRef session = (__YMSessionRef)session_;
    
    YMLockLock(session->connectionsByAddressLock);
    {
        YMDictionaryKey key = (YMDictionaryKey)connection;
        ymassert(!YMDictionaryContains(session->connectionsByAddress, key), "connections by address state");
        YMDictionaryAdd(session->connectionsByAddress, key, (void *)YMRetain(connection));
    }
    YMLockUnlock(session->connectionsByAddressLock);
    
    YMConnectionSetCallbacks(connection, __ym_session_new_stream_proc, session,
                             __ym_session_stream_closing_proc, session,
                             __ym_session_connection_interrupted_proc, session);
    
    bool isNewDefault = (session->defaultConnection == NULL);
    YMAddressRef address = YMConnectionGetAddress(connection);
    ymassert(address, YM_LOG_PRE "could not initialize addr for connection",YM_LOG_DSC);
    
    ymlog(YM_LOG_PRE "adding %s connection for %s",YM_LOG_DSC,isNewDefault?"default":"aux",YMSTR(YMAddressGetDescription(address)));
    
    if ( isNewDefault )
        session->defaultConnection = connection;
    
    ymassert(session->defaultConnection!= NULL,"no default connection");
}

bool __YMSessionInterrupt(__YMSessionRef session, YMConnectionRef floatConnection)
{
    bool first = false;
    YMLockLock(session->connectionsByAddressLock);
    {
        if ( ! session->interrupted )
        {
            first = true;
            session->interrupted = true;
            session->defaultConnection = NULL;
    
            while ( YMDictionaryGetCount(session->connectionsByAddress) )
            {
                YMDictionaryKey aKey = YMDictionaryGetRandomKey(session->connectionsByAddress);
                YMConnectionRef aConnection = (YMConnectionRef)YMDictionaryRemove(session->connectionsByAddress, aKey);
                ymassert(aConnection,"connection list state");
                ymerr(YM_LOG_PRE "releasing %s",YM_LOG_DSC,YMSTR(YMAddressGetDescription(YMConnectionGetAddress(aConnection))));
                if ( aConnection != floatConnection )
                    YMRelease(aConnection);
            }
        }
    }
    YMLockUnlock(session->connectionsByAddressLock);
    
    return first;
}

#pragma mark connection callbacks

void __ym_session_new_stream_proc(YMConnectionRef connection, YMStreamRef stream, void *context)
{
    __YMSessionRef session = context;
    
    YMAddressRef address = YMConnectionGetAddress(connection);
    YMPlexerStreamID streamID = YM_STREAM_INFO(stream)->streamID;
    ymlog(YM_LOG_PRE "new incoming stream %u on %s",YM_LOG_DSC,streamID,YMSTR(YMAddressGetDescription(address)));
    
    if ( connection != session->defaultConnection )
        ymerr(YM_LOG_PRE "warning: new stream on non-default connection",YM_LOG_DSC);
    
    // is it weird that we don't report 'connection' here, despite user only being concerned with "active"?
    if ( session->newStreamFunc )
        session->newStreamFunc(session,connection,stream,session->callbackContext);
}

void __ym_session_stream_closing_proc(YMConnectionRef connection, YMStreamRef stream, void *context)
{
    __YMSessionRef session = context;
    
    YMAddressRef address = YMConnectionGetAddress(connection);
    YMPlexerStreamID streamID = YM_STREAM_INFO(stream)->streamID;
    ymlog(YM_LOG_PRE "remote stream %u closing on %s",YM_LOG_DSC,streamID,YMSTR(YMAddressGetDescription(address)));
    
    if ( connection != session->defaultConnection )
    {
        ymerr(YM_LOG_PRE "warning: closing remote stream on non-default connection",YM_LOG_DSC);
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
    
	    ymerr(YM_LOG_PRE "connection interrupted: %s",YM_LOG_DSC,YMSTR(YMAddressGetDescription(YMConnectionGetAddress(connection))));
    
    if ( isDefault )
    {
        if ( session->interruptedFunc )
            session->interruptedFunc(session,session->callbackContext);
    }
    
    YMRelease(connection);
}

#pragma mark client mdns callbacks

void __ym_mdns_service_appeared_func(__unused YMmDNSBrowserRef browser, YMmDNSServiceRecord * service, void *context)
{
    __YMSessionRef session = context;
    ymlog(YM_LOG_PRE "__ym_mdns_service_appeared_func: %s",YM_LOG_DSC,YMSTR(service->name));
    
    YMPeerRef peer = _YMPeerCreate(service->name, NULL, NULL);
    YMLockLock(session->knownPeersLock);
    YMDictionaryAdd(session->knownPeers, (YMDictionaryKey)peer, (void *)peer);
    YMLockUnlock(session->knownPeersLock);
    
    session->addedFunc(session,peer,session->callbackContext);
}

void __ym_mdns_service_removed_func(__unused YMmDNSBrowserRef browser, YMStringRef name, void *context)
{
    __YMSessionRef session = context;
    
    ymlog(YM_LOG_PRE "__ym_mdns_service_removed_func %s",YM_LOG_DSC,YMSTR(name));
    
    YMLockLock(session->knownPeersLock);
    {
        YMDictionaryKey mysteryKey = MAX_OF(YMDictionaryKey);
        bool found = false;
        YMDictionaryEnumRef myEnum = YMDictionaryEnumeratorBegin(session->knownPeers);
        while ( myEnum )
        {
            YMPeerRef peer = (YMPeerRef)myEnum->value;
            if ( strcmp(YMSTR(YMPeerGetName(peer)),YMSTR(name)) == 0 )
            {
                found = true;
                mysteryKey = myEnum->key;
            }
            myEnum = YMDictionaryEnumeratorGetNext(myEnum);
        }
        YMDictionaryEnumeratorEnd(myEnum);
        
        if ( ! found ) {
            ymerr(YM_LOG_PRE "warning: notified of removal of unknown peer: %s", YM_LOG_DSC, YMSTR(name));
            return;
        }
        
        YMDictionaryRemove(session->knownPeers, mysteryKey);
    }
    YMLockUnlock(session->knownPeersLock);
}

void __ym_mdns_service_updated_func(__unused YMmDNSBrowserRef browser, YMmDNSServiceRecord *service, void *context)
{
    __YMSessionRef session = context;
    ymlog(YM_LOG_PRE "__ym_mdns_service_updated_func %s",YM_LOG_DSC,YMSTR(service->name));
}

void __ym_mdns_service_resolved_func(__unused YMmDNSBrowserRef browser, bool success, YMmDNSServiceRecord *service, void *context)
{
    __YMSessionRef session = context;
    
    ymlog(YM_LOG_PRE "__ym_mdns_service_resolved_func %s",YM_LOG_DSC,YMSTR(service->name));
    
    bool found = false;
    YMPeerRef peer = NULL;
    YMLockLock(session->knownPeersLock);
    {
        YMDictionaryEnumRef myEnum = YMDictionaryEnumeratorBegin(session->knownPeers);
        while ( myEnum )
        {
            peer = (YMPeerRef)myEnum->value;
            if ( strcmp(YMSTR(YMPeerGetName(peer)),YMSTR(service->name)) == 0 )
            {
                found = true;
                peer = (YMPeerRef)myEnum->value;
                break;
            }
            
            myEnum = YMDictionaryEnumeratorGetNext(myEnum);
        }
        YMDictionaryEnumeratorEnd(myEnum);
    }
	
	_YMPeerSetAddresses(peer, service->sockaddrList);
	_YMPeerSetPort(peer, service->port);
    
    YMLockUnlock(session->knownPeersLock);
    
    if ( found )
    {
        if ( success )
            session->resolvedFunc(session,peer,session->callbackContext);
        else
            session->resolveFailedFunc(session,peer,session->callbackContext);
    }
    else
    {
        ymerr(YM_LOG_PRE "notified of resolution of unknown peer: %s",YM_LOG_DSC,YMSTR(service->name));
        abort();
    }
}

YM_EXTERN_C_POP
