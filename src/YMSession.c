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

#ifndef WIN32
# if defined(RPI)
# define __USE_POSIX
# include <sys/socket.h>
# endif
#include <netinet/in.h>
#include <netdb.h> // struct hostent
#else
#include <winsock2.h>
#include <ws2tcpip.h>
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
void __YMSessionAddConnection(YMSessionRef session, YMConnectionRef connection);
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
        YMRelease(session->initConnectionDispatchThread);
    if ( session->connectDispatchThread )
        YMRelease(session->connectDispatchThread);
    
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
        ymerr("session[%s]: error: failed to create browser",YMSTR(session->logDescription));
        return false;
    }
    
    bool startOK = YMmDNSBrowserStart(session->browser);
    if ( ! startOK )
    {
        ymerr("session[%s]: error: failed to start browser",YMSTR(session->logDescription));
        return false;
    }
    
    ymlog("session[%s]: client started for '%s'",YMSTR(session->logDescription),YMSTR(session->type));
    
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
        ymerr("session[%s]: requested resolve of unknown peer: %s",YMSTR(session->logDescription),YMSTR(peerName));
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
    
    bool knownPeer = true;
    YMLockLock(session->knownPeersLock);
    YMStringRef peerName = YMPeerGetName(peer);
    if ( ! YMDictionaryContains(session->knownPeers, (YMDictionaryKey)peer) )
    {
        ymerr("session[%s]: requested connect to unknown peer: %s",YMSTR(session->logDescription),YMSTR(peerName));
        knownPeer = false;
    }
    YMLockUnlock(session->knownPeersLock);
    
    if ( ! knownPeer )
        return false;
    
    YMDictionaryRef addresses = YMPeerGetAddresses(peer);
    YMDictionaryKey aKey = YMDictionaryGetRandomKey(addresses);
    YMAddressRef address = (YMAddressRef)YMDictionaryGetItem(addresses, aKey);
    YMConnectionRef newConnection = YMConnectionCreate(address, YMConnectionStream, YMTLS);
    
    __ym_session_connect_async_context_ref context = (__ym_session_connect_async_context_ref)YMALLOC(sizeof(__ym_session_connect_async_context));
    context->session = (__YMSessionRef)YMRetain(session);
    context->peer = (YMPeerRef)YMRetain(peer);
    context->connection = (YMConnectionRef)YMRetain(newConnection);
    
    YMStringRef name = YMStringCreateWithFormat("session-async-connect-%s",YMSTR(YMAddressGetDescription(address)),NULL);
    struct ym_thread_dispatch_t connectDispatch = {__ym_session_connect_async_proc, 0, 0, context, name};
    
    if ( ! sync )
    {
        if ( ! session->connectDispatchThread )
        {
            session->connectDispatchThread = YMThreadDispatchCreate(name);
            if ( ! session->connectDispatchThread )
            {
                ymerr("session[%s]: error: failed to create async connect thread",YMSTR(session->logDescription));
                goto catch_fail;
            }
            bool okay = YMThreadStart(session->connectDispatchThread);
            if ( ! okay )
            {
                ymerr("session[%s]: error: failed to start async connect thread",YMSTR(session->logDescription));
                goto catch_fail;
            }
        }
        
        YMThreadDispatchDispatch(session->connectDispatchThread, connectDispatch);
    }
    else
        __ym_session_connect_async_proc(&connectDispatch);
    
    YMRelease(newConnection);
    YMRelease(name);
    return true;
    
catch_fail:
    free(context);
    YMRelease(name);
    return false;
}

void YM_CALLING_CONVENTION __ym_session_connect_async_proc(ym_thread_dispatch_ref dispatch)
{
    __ym_session_connect_async_context_ref context = (__ym_session_connect_async_context_ref)dispatch->context;
    __YMSessionRef session = context->session;
    YMPeerRef peer = context->peer;
    YMConnectionRef connection = context->connection;
    free(context);
    
    ymlog("session[%s]: __ym_session_connect_async_proc entered",YMSTR(session->logDescription));
    
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
    
    ymlog("session[%s]: __ym_session_connect_async_proc exiting: %s",YMSTR(session->logDescription),okay?"success":"fail");
}

void __YMSessionAddConnection(YMSessionRef session_, YMConnectionRef connection)
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
    if ( ! address )
    {
        ymerr("session[%s]: internal: connection has no address",YMSTR(session->logDescription));
        abort();
    }
    ymlog("session[%s]: adding %s connection for %s",YMSTR(session->logDescription),isNewDefault?"default":"aux",YMSTR(YMAddressGetDescription(address)));
    
    if ( isNewDefault )
        session->defaultConnection = connection;
    
    if ( session->defaultConnection == NULL )
        abort();
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
        ymerr("session[%s]: error: failed to reserve port for server start",YMSTR(session->logDescription));
        return false;
    }
    
    int aResult = listen(socket, 1);
    if ( aResult != 0 )
    {
        ymerr("session[%s]: error: failed to listen for server start",YMSTR(session->logDescription));
		int result, error; const char *errorStr;
        YM_CLOSE_SOCKET(socket);
        goto rewind_fail;
    }
    
    ymlog("session[%s]: listening on %u",YMSTR(session->logDescription),port);
    
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
        ymerr("session[%s]: error: failed to create accept thread",YMSTR(session->logDescription));
        goto rewind_fail;
    }
    session->acceptThreadExitFlag = false;
    bool threadOK = YMThreadStart(session->acceptThread);
    if ( ! threadOK )
    {
        ymerr("session[%s]: error: failed to start accept thread",YMSTR(session->logDescription));
        goto rewind_fail;
    }
    
    memberName = YMStringCreateWithFormat("session-init-%s",YMSTR(session->type),NULL);
    session->initConnectionDispatchThread = YMThreadDispatchCreate(memberName);
    YMRelease(memberName);
    if ( ! session->initConnectionDispatchThread )
    {
        ymerr("session[%s]: error: failed to create connection init thread",YMSTR(session->logDescription));
        goto rewind_fail;
    }
    threadOK = YMThreadStart(session->initConnectionDispatchThread);
    if ( ! threadOK )
    {
        ymerr("session[%s]: error: failed to start start connection init thread",YMSTR(session->logDescription));
        goto rewind_fail;
    }
    
    session->service = YMmDNSServiceCreate(session->type, session->name, (uint16_t)port);
    if ( ! session->service )
    {
        ymerr("session[%s]: error: failed to create mdns service",YMSTR(session->logDescription));
        goto rewind_fail;
    }
    mDNSOK = YMmDNSServiceStart(session->service);
    if ( ! mDNSOK )
    {
        ymerr("session[%s]: error: failed to start mdns service",YMSTR(session->logDescription));
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
    __YMSessionRef session = (__YMSessionRef)session_;
    session->acceptThreadExitFlag = true;
    bool okay = true;
	int result, error; const char *errorStr;
    if ( session->ipv4ListenSocket >= 0 )
    {
		YM_CLOSE_SOCKET(session->ipv4ListenSocket);
        if ( result != 0 )
        {
            ymerr("session[%s]: warning: failed to close ipv4 socket: %d %s",YMSTR(session->name),error,errorStr);
            okay = false;
        }
        session->ipv4ListenSocket = -1;
    }
    if ( session->ipv6ListenSocket >= 0 )
    {
        YM_CLOSE_SOCKET(session->ipv6ListenSocket);
        if ( result != 0 )
        {
            ymerr("session[%s]: warning: failed to close ipv6 socket: %d %s",YMSTR(session->name),error,errorStr);
            okay = false;
        }
        session->ipv6ListenSocket = -1;
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
        
        ymerr("session[%s]: accepting on sf%d...",YMSTR(session->logDescription),socket);
        int aResult = accept(socket, (struct sockaddr *)bigEnoughAddr, &thisLength);
        if ( aResult < 0 )
        {
            ymerr("session[%s]: accept(sf%d) failed: %d (%s)",YMSTR(session->logDescription),socket,errno,strerror(errno));
            free(bigEnoughAddr);
            continue;
        }
        
        ymerr("session[%s]: accepted sf%d, dispatching connection init",YMSTR(session->logDescription), aResult);
        
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
    
    ymlog("session[%s]: __ym_session_init_connection entered: sf%d %d %d",YMSTR(session->logDescription),socket,addrLen,ipv4);
    
    YMAddressRef address = NULL;
    YMPeerRef peer = NULL;
    YMConnectionRef newConnection = NULL;
    
    address = YMAddressCreate(addr, addrLen);
    peer = _YMPeerCreateWithAddress(address);
    if ( ! session->shouldAcceptFunc(session, peer, session->callbackContext) )
    {
        ymlog("session[%s]: client rejected peer %s",YMSTR(session->logDescription),YMSTR(YMAddressGetDescription(address)));
        goto catch_release;
    }
    
    newConnection = YMConnectionCreateIncoming(socket, address, YMConnectionStream, YMTLS);
    if ( ! newConnection )
    {
        ymlog("session[%s]: failed to create new connection",YMSTR(session->logDescription));
		if ( session->connectFailedFunc ) 
			session->connectFailedFunc(session, peer, session->callbackContext);
        goto catch_release;
    }
    
    ymlog("session[%s]: new connection %s",YMSTR(session->logDescription),YMSTR(YMAddressGetDescription(address)));
    
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

#pragma mark connected shared

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
                ymerr("session[%s]: releasing %s",YMSTR(session->logDescription),YMSTR(YMAddressGetDescription(YMConnectionGetAddress(aConnection))));
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
    ymlog("session[%s]: new incoming stream %u on %s",YMSTR(session->logDescription),streamID,YMSTR(YMAddressGetDescription(address)));
    
    if ( connection != session->defaultConnection )
        ymerr("session[%s]: warning: new stream on non-default connection",YMSTR(session->logDescription));
    
    // is it weird that we don't report 'connection' here, despite user only being concerned with "active"?
    if ( session->newStreamFunc )
        session->newStreamFunc(session,connection,stream,session->callbackContext);
}

void __ym_session_stream_closing_proc(YMConnectionRef connection, YMStreamRef stream, void *context)
{
    __YMSessionRef session = context;
    
    YMAddressRef address = YMConnectionGetAddress(connection);
    YMPlexerStreamID streamID = YM_STREAM_INFO(stream)->streamID;
    ymlog("session[%s]: remote stream %u closing on %s",YMSTR(session->logDescription),streamID,YMSTR(YMAddressGetDescription(address)));
    
    if ( connection != session->defaultConnection )
    {
        ymerr("session[%s]: warning: closing remote stream on non-default connection",YMSTR(session->logDescription));
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
    
    ymerr("session[%s]: connection interrupted: %s",YMSTR(session->logDescription),YMSTR(YMAddressGetDescription(YMConnectionGetAddress(connection))));
    
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
    ymlog("session[%s]: __ym_mdns_service_appeared_func: %s",YMSTR(session->logDescription),YMSTR(service->name));
    
    YMPeerRef peer = _YMPeerCreate(service->name, NULL, NULL);
    YMLockLock(session->knownPeersLock);
    YMDictionaryAdd(session->knownPeers, (YMDictionaryKey)peer, (void *)peer);
    YMLockUnlock(session->knownPeersLock);
    
    session->addedFunc(session,peer,session->callbackContext);
}

void __ym_mdns_service_removed_func(__unused YMmDNSBrowserRef browser, YMStringRef name, void *context)
{
    __YMSessionRef session = context;
    
    ymlog("session[%s]: __ym_mdns_service_removed_func %s",YMSTR(session->logDescription),YMSTR(name));
    
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
        
        if ( found )
            YMDictionaryRemove(session->knownPeers, mysteryKey);
        else
        {
            ymerr("session[%s]: notified of removal of unknown peer: %s",YMSTR(session->logDescription),YMSTR(name));
            abort();
        }
    }
    YMLockUnlock(session->knownPeersLock);
}

void __ym_mdns_service_updated_func(__unused YMmDNSBrowserRef browser, YMmDNSServiceRecord *service, void *context)
{
    __YMSessionRef session = context;
    ymlog("session[%s]: __ym_mdns_service_updated_func %s",YMSTR(session->logDescription),YMSTR(service->name));
}

void __ym_mdns_service_resolved_func(__unused YMmDNSBrowserRef browser, bool success, YMmDNSServiceRecord *service, void *context)
{
    __YMSessionRef session = context;
    
    ymlog("session[%s]: __ym_mdns_service_resolved_func %s",YMSTR(session->logDescription),YMSTR(service->name));
    
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
            }
            
            myEnum = YMDictionaryEnumeratorGetNext(myEnum);
        }
        YMDictionaryEnumeratorEnd(myEnum);
    }
    
    YMDictionaryRef addresses = YMDictionaryCreate();
    
	YM_ADDRINFO *addrInfoIter = service->addrinfo;
	while ( addrInfoIter )
	{
		if ( addrInfoIter->ai_family == AF_INET /*|| addrInfoIter->ai_family == AF_INET6*/ )
		{
			((struct sockaddr_in *)addrInfoIter->ai_addr)->sin_port = service->port;
			YMAddressRef address = YMAddressCreate(addrInfoIter->ai_addr, addrInfoIter->ai_addrlen);
			if ( address )
				YMDictionaryAdd(addresses, (YMDictionaryKey)address, address);
			ymlog("session[%s]: %s address with family %d proto %d length %d canon %s: %s",YMSTR(session->logDescription),address?"parsed":"failed to parse",
				  addrInfoIter->ai_family,addrInfoIter->ai_protocol,addrInfoIter->ai_addrlen,addrInfoIter->ai_canonname,address?YMSTR(YMAddressGetDescription(address)):"(null");
		}
		else
			ymlog("session[%s]: ignoring address with family %d proto %d length %d canon %s:",YMSTR(session->logDescription),
				  addrInfoIter->ai_family,addrInfoIter->ai_protocol,addrInfoIter->ai_addrlen,addrInfoIter->ai_canonname);
		addrInfoIter = addrInfoIter->ai_next;
	}
	
	_YMPeerSetAddresses(peer, addresses);
	_YMPeerSetPort(peer, service->port);
    
    YMRelease(addresses);
    
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
        ymerr("session[%s]: notified of resolution of unknown peer: %s",YMSTR(session->logDescription),YMSTR(service->name));
        abort();
    }
}

YM_EXTERN_C_POP
