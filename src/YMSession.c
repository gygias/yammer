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
#include "YMDispatch.h"
#include "YMDispatchUtils.h"
#include "YMSemaphore.h"
#include "YMAddress.h"
#include "YMUtilities.h"

#define ymlog_type YMLogSession
#define ymlog_pre "session[%s]: "
#define ymlog_args YMSTR(s->logDescription)
#include "YMLog.h"

#if !defined(YMWIN32)
# if defined(YMLINUX)
#  ifndef __USE_POSIX
#   define __USE_POSIX
#  endif
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

typedef struct __ym_session
{
    _YMType _common;
    
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
    YMSOCKET listenSocket;
    YMDispatchQueueRef acceptQueue;
    bool acceptExitFlag;
    
    // client
    YMmDNSBrowserRef browser;
    YMDictionaryRef knownPeers;

    bool ipv4;
    
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
} __ym_session;
typedef struct __ym_session __ym_session_t;

#pragma mark setup

__ym_session_t *__YMSessionCreateShared(YMStringRef type, bool isServer);
bool __YMSessionObserveNetworkInterfaceChanges(__ym_session_t *, bool startStop);
void __YMSessionUpdateNetworkConfigDate(__ym_session_t *);
bool __YMSessionInterrupt(__ym_session_t *, YMConnectionRef floatConnection);
bool __YMSessionCloseAllConnections(__ym_session_t *);
void __YMSessionAddConnection(__ym_session_t *, YMConnectionRef connection);
YM_ENTRY_POINT(__ym_session_accept_proc);
YM_ENTRY_POINT(__ym_session_init_incoming_connection_proc);
YM_ENTRY_POINT(__ym_session_connect_async_proc);
YM_ENTRY_POINT(__ym_session_listen_proc);

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

    __ym_session_t *session = (__ym_session_t *)_YMAlloc(_YMSessionTypeID,sizeof(__ym_session_t));
    session->type = YMRetain(type);
    session->listenSocket = NULL_SOCKET;
    session->logDescription = YMStringCreateWithFormat("session:%s",YMSTR(type),NULL);
    session->service = NULL;
    session->browser = NULL;
    session->acceptExitFlag = false;
    session->acceptQueue = NULL;
    session->defaultConnection = NULL;
    session->connectionsByAddress = YMDictionaryCreate();

    session->ipv4 = true;

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

void YMSessionSetBrowsingCallbacks(YMSessionRef s_, ym_session_added_peer_func added, ym_session_removed_peer_func removed,
                                 ym_session_resolve_failed_func rFailed, ym_session_resolved_peer_func resolved,
                                 ym_session_connect_failed_func cFailed, void *context)
{
    __ym_session_t * session = (__ym_session_t *)s_;
    
    session->addedFunc = added;
    session->removedFunc = removed;
    session->resolveFailedFunc = rFailed;
    session->resolvedFunc = resolved;
    session->connectFailedFunc = cFailed;
    session->callbackContext = context;
}

void YMSessionSetAdvertisingCallbacks(YMSessionRef s_,ym_session_should_accept_func should, void* context)
{
    __ym_session_t *s = (__ym_session_t *)s_;
    
    s->shouldAcceptFunc = should;
    s->callbackContext = context;
}

void YMSessionSetCommonCallbacks(YMSessionRef s_, ym_session_initializing_func initializing, ym_session_connected_func connected,
                                                        ym_session_interrupted_func interrupted,
                                                        ym_session_new_stream_func new_, ym_session_stream_closing_func closing)
{
    __ym_session_t *s = (__ym_session_t *)s_;
    
    s->initializingFunc = initializing;
    s->connectedFunc = connected;
    s->interruptedFunc = interrupted;
    s->newStreamFunc = new_;
    s->streamClosingFunc = closing;
}

void _YMSessionFree(YMTypeRef o_)
{
    __ym_session_t *s = (__ym_session_t *)o_;
    YMSessionSetCommonCallbacks(s, NULL, NULL, NULL, NULL, NULL);
    YMSessionSetAdvertisingCallbacks(s, NULL, NULL);
    YMSessionSetBrowsingCallbacks(s, NULL, NULL, NULL, NULL, NULL, NULL);
    
    __YMSessionInterrupt(s, NULL);
    
    if ( s->acceptQueue ) {
        YMDispatchJoin(s->acceptQueue);
        YMDispatchQueueRelease(s->acceptQueue);
    }
    
    // shared
    YMRelease(s->type);
    YMRelease(s->connectionsByAddress);
    YMRelease(s->logDescription);
    
    // server
    if ( s->name )
        YMRelease(s->name);
    if ( s->service ) {
        YMmDNSServiceStop(s->service); // xxx
        __YMSessionObserveNetworkInterfaceChanges(s, false);
        YMRelease(s->service);
    }
    
    if ( s->browser ) {
        YMmDNSBrowserStop(s->browser);
        YMRelease(s->browser);
    }
    
    YMDictionaryEnumRef aEnum = YMDictionaryEnumeratorBegin(s->knownPeers);
    while ( aEnum ) {
        YMPeerRef aPeer = aEnum->value;
        YMRelease(aPeer);
        aEnum = YMDictionaryEnumeratorGetNext(aEnum);
    }
    if ( aEnum ) YMDictionaryEnumeratorEnd(aEnum);
    
    YMRelease(s->knownPeers);
}

#pragma mark client

bool YMSessionStartBrowsing(YMSessionRef s_)
{
    __ym_session_t *s = (__ym_session_t *)s_;
    
    if ( s->browser || s->defaultConnection )
        return false;
    
    s->browser = YMmDNSBrowserCreateWithCallbacks(s->type,
                                                  __ym_mdns_service_appeared_func,
                                                  __ym_mdns_service_updated_func,
                                                  __ym_mdns_service_resolved_func,
                                                  __ym_mdns_service_removed_func,
                                                  s);
    if ( ! s->browser ) {
        ymerr("failed to create browser");
        return false;
    }
    
    bool startOK = YMmDNSBrowserStart(s->browser);
    if ( ! startOK ) {
        ymerr("failed to start browser");
        return false;
    }
    
    ymlog("client started for '%s'",YMSTR(s->type));
    
    return true;
}

bool YMSessionStopBrowsing(YMSessionRef s_)
{
    __ym_session_t *s = (__ym_session_t *)s_;
    
    bool okay = true;
    
    if ( s->browser ) {
        okay = YMmDNSBrowserStop(s->browser);
        YMRelease(s->browser);
        s->browser = NULL;
    }
    
    return okay;
}

bool YMSessionCloseAllConnections(YMSessionRef s)
{
    bool okay = __YMSessionInterrupt((__ym_session_t *)s, NULL);
    return okay;
}

YMPeerRef YMSessionGetPeerNamed(YMSessionRef s, YMStringRef peerName)
{
    YMPeerRef thePeer = NULL;
    
    YMSelfLock(s);
    {
        YMDictionaryEnumRef peerEnum = YMDictionaryEnumeratorBegin(s->knownPeers);
        while ( peerEnum ) {
            if ( YMStringEquals(YMPeerGetName((YMPeerRef)peerEnum->value), peerName) ) {
                thePeer = peerEnum->value;
                break;
            }
            peerEnum = YMDictionaryEnumeratorGetNext(peerEnum);
        }
        YMDictionaryEnumeratorEnd(peerEnum);
    }
    YMSelfUnlock(s);
    
    return thePeer;
}

bool YMSessionResolvePeer(YMSessionRef s, YMPeerRef peer)
{
    YMSelfLock(s);
    YMStringRef peerName = YMPeerGetName(peer);
    bool knownPeer = true;
    if ( ! YMDictionaryContains(s->knownPeers, (YMDictionaryKey)peer) ) {
        ymerr("requested resolve of unknown peer: %s",YMSTR(peerName));
        knownPeer = false;
    }
    YMSelfUnlock(s);
    
    if ( ! knownPeer )
        return false;
    
    return YMmDNSBrowserResolve(s->browser, peerName);
}

typedef struct __ym_session_connect
{
    YMSessionRef session;
    YMPeerRef peer;
    YMConnectionRef connection;
    bool moreComing;
    bool userSync;
} __ym_session_connect;
typedef struct __ym_session_connect __ym_session_connect_t;

bool YMSessionConnectToPeer(YMSessionRef s_, YMPeerRef peer, bool sync)
{
    __ym_session_t *s = (__ym_session_t *)s_;
    
    __ym_session_connect_t *context = NULL;
    
    bool knownPeer = true;
    YMSelfLock(s);
    YMStringRef peerName = YMPeerGetName(peer);
    if ( ! YMDictionaryContains(s->knownPeers, (YMDictionaryKey)peer) ) {
        ymerr("requested connect to unknown peer: %s",YMSTR(peerName));
        knownPeer = false;
    }
    YMSelfUnlock(s);
    
    if ( ! knownPeer )
        return false;
    
    YMArrayRef addresses = YMPeerGetAddresses(peer);
    for ( int64_t i = 0; i < YMArrayGetCount(addresses); i++ ) {
        YMAddressRef address = (YMAddressRef)YMArrayGet(addresses, i);
        
        bool existing = false;
        YMDictionaryEnumRef denum = YMDictionaryEnumeratorBegin(s->connectionsByAddress);
        while ( denum ) {
            if ( YMAddressIsEqualIncludingPort(address, address, false) ) {
                YMDictionaryEnumeratorEnd(denum);
                existing = true;
                break;
            }
            denum = YMDictionaryEnumeratorGetNext(denum);
        }
        if ( existing ) {
            ymerr("already connected to %s",YMSTR(YMAddressGetDescription(address)));
            continue;
        }
        
        YMConnectionRef newConnection = YMConnectionCreate(address, YMConnectionStream, YMTLS, true);
        
        context = (__ym_session_connect_t *)YMALLOC(sizeof(__ym_session_connect_t));
        context->session = YMRetain(s);
        context->peer = (YMPeerRef)YMRetain(peer);
        context->connection = (YMConnectionRef)YMRetain(newConnection);
        context->moreComing = ( i < YMArrayGetCount(addresses) - 1 );
        context->userSync = sync;
        
        ym_dispatch_user_t connectDispatch = {__ym_session_connect_async_proc, context, NULL, ym_dispatch_user_context_noop};
        
        if ( ! sync ) {
            YMDispatchAsync(YMDispatchGetGlobalQueue(),&connectDispatch);
        } else {
            YMDispatchSync(YMDispatchGetGlobalQueue(),&connectDispatch);
        }
        
        YMRelease(newConnection);
        
        // https://github.com/gygias/yammer/issues/2
#if defined(YMAPPLE)
        for ( int j = 0; j < YMArrayGetCount(addresses); j++ ) {
            YMAddressRef anotherAddress = (YMAddressRef)YMArrayGet(addresses,j);
            const char *desc = YMSTR(YMAddressGetDescription(anotherAddress));
            if ( strstr(desc,"127.0.0.1") ) {
                ymerr("limiting localhost session to one connection on apple: %s",YMSTR(YMAddressGetDescription(address)));
                return true;
            }
        }
#endif
    }
    
    return true;
}

YM_ENTRY_POINT(__ym_session_connect_async_proc)
{
    __ym_session_connect_t *sc = context;
    YMSessionRef s = sc->session;
    YMPeerRef peer = sc->peer;
    YMConnectionRef connection = sc->connection;
    bool moreComing = sc->moreComing;
    bool userSync = sc->userSync;
    
    ymlog("__ym_session_connect_async_proc entered");
    
    bool okay = YMConnectionConnect(connection);
    
    if ( s->interrupted )
        goto catch_release;
    
    if ( okay ) {
        if ( s->initializingFunc )
            s->initializingFunc(s, s->callbackContext);
        okay = YMConnectionInit(connection);
        
        if ( s->interrupted )
            goto catch_release;
    
        if ( okay ) {
            __YMSessionAddConnection((__ym_session_t *)s, connection);
            if ( ! userSync )
                s->connectedFunc(s, connection, s->callbackContext);
        }
    }
    else {
        if ( ! userSync )
            s->connectFailedFunc(s, peer, moreComing, s->callbackContext);
    }
    
catch_release:
    
    YMRelease(s);
    YMRelease(peer);
    YMRelease(connection);

    YMFREE(sc);
    
    ymlog("__ym_session_connect_async_proc exiting: %s",okay?"success":"fail");
}

#pragma mark server

// for some reason i found this hilarious
typedef struct __ym_session_async_bool
{
    __ym_session_t *s;
    bool *okay;
} __ym_session_accept;
typedef struct __ym_session_async_bool __ym_session_async_bool;

bool YMSessionStartAdvertising(YMSessionRef s_, YMStringRef name)
{
    __ym_session_t *s = (__ym_session_t *)s_;
    
    if ( s->service )
        return false;
    if ( s->acceptQueue )
        return false;
    
    s->name = YMRetain(name);

    YMStringRef queueName = YMStringCreateWithFormat("com.combobulated.dispatch.ymsession.accept:%s",YMSTR(name),NULL);
    s->acceptQueue = YMDispatchQueueCreate(queueName);
    YMRelease(queueName);
    
    bool okay = false;
    __ym_session_async_bool asyncBool = {s,&okay};
    ym_dispatch_user_t listenUser = { __ym_session_listen_proc, &asyncBool, NULL, ym_dispatch_user_context_noop };
    YMDispatchSync(s->acceptQueue,&listenUser);
    return okay;
}

YM_ENTRY_POINT(__ym_session_listen_proc)
{
    YM_IO_BOILERPLATE

    __ym_session_async_bool *asyncBool = context;
    __ym_session_t *s = asyncBool->s;


    *(asyncBool->okay) = false;
    int socket = -1;
    
    int32_t port = YMPortReserve(s->ipv4, &socket);
    if ( port < 0 || socket == -1 || socket > UINT16_MAX ) {
        ymerr("failed to reserve port for server start");
        return;
    }
    
    int aResult = listen(socket, 1);
    if ( aResult != 0 ) {
        ymerr("failed to listen for server start");
        YM_CLOSE_SOCKET(socket);
        goto rewind_fail;
    }
    
    ymlog("listening on %u",port);
    
    s->listenSocket = socket;
        
    ym_dispatch_user_t acceptUser = { __ym_session_accept_proc, (void *)YMRetain(s), NULL, ym_dispatch_user_context_noop };
    YMDispatchAsync(s->acceptQueue, &acceptUser);
    
    s->service = YMmDNSServiceCreate(s->type, s->name, (uint16_t)port);
    if ( ! s->service ) {
        ymerr("failed to create mdns service");
        goto rewind_fail;
    }
    *(asyncBool->okay) = YMmDNSServiceStart(s->service);
    if ( ! asyncBool->s ) {
        ymerr("failed to start mdns service");
        goto rewind_fail;
    }
    
    *(asyncBool->okay) = __YMSessionObserveNetworkInterfaceChanges(s,true);
    return;
    
rewind_fail:
    if ( socket >= 0 ) {
        YM_CLOSE_SOCKET(socket);
	}
    s->listenSocket = NULL_SOCKET;
    s->acceptExitFlag = true;

    if ( s->service ) {
        YMmDNSServiceStop(s->service);
        YMRelease(s->service);
        s->service = NULL;
    }
}

bool YMSessionStopAdvertising(YMSessionRef s_)
{
    YM_IO_BOILERPLATE
    
    __ym_session_t *s = (__ym_session_t *)s_;
    
    s->acceptExitFlag = true;
    bool okay = true;
    if ( s->listenSocket != NULL_SOCKET ) {
		YM_CLOSE_SOCKET(s->listenSocket);
        if ( result != 0 ) {
            ymerr("warning: failed to close listen socket: %d %s",error,errorStr);
            okay = false;
        }
        s->listenSocket = NULL_SOCKET;
    }
    
    if ( s->service ) {
        bool mdnsOK = YMmDNSServiceStop(s->service);
        YMRelease(s->service);
        s->service = NULL;
        if ( ! mdnsOK )
            okay = false;
    }
    
    __YMSessionObserveNetworkInterfaceChanges(s, false);
    
    return okay;
}

typedef struct __ym_connection_init
{
    __ym_session_t *s;
    int socket;
    struct sockaddr *addr;
    socklen_t addrLen; // redundant?
} __ym_connection_init;
typedef struct __ym_connection_init __ym_connection_init_t;

YM_ENTRY_POINT(__ym_session_accept_proc)
{
    __ym_session_t *s = context;
    
    while ( ! s->acceptExitFlag ) {        
        struct sockaddr_in6 *bigEnoughAddr = YMALLOC(sizeof(struct sockaddr_in6));
        socklen_t thisLength = s->ipv4 ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);
        
        ymerr("accepting on sf%d...",s->listenSocket);
        int aResult = accept(s->listenSocket, (struct sockaddr *)bigEnoughAddr, &thisLength);
        if ( aResult < 0 ) {
            ymerr("accept(sf%d) failed: %d (%s)",s->listenSocket,errno,strerror(errno));
            YMFREE(bigEnoughAddr);
            continue;
        }
        
        struct sockaddr_in6 bigEnoughSockName;
        socklen_t slen = sizeof(bigEnoughSockName);
        int result = getsockname(aResult, (struct sockaddr *)&bigEnoughSockName, &slen);
        if ( result == 0 ) {
            if ( bigEnoughAddr->sin6_family == AF_INET )
                ((struct sockaddr_in *)bigEnoughAddr)->sin_port = ntohs(((struct sockaddr_in *)&bigEnoughSockName)->sin_port);
            else if ( bigEnoughAddr->sin6_family == AF_INET6 )
                ((struct sockaddr_in6 *)bigEnoughAddr)->sin6_port = ntohs(bigEnoughSockName.sin6_port);
            else
                ymabort("accept: address family %d unsupported",bigEnoughAddr->sin6_family);
        }
        
        ymerr("accepted sf%d, dispatching connection init", aResult);
        
        if ( s->initializingFunc )
            s->initializingFunc(s,s->callbackContext);
        
        __ym_connection_init_t *initCtx = (__ym_connection_init_t *)YMALLOC(sizeof(__ym_connection_init_t));
        initCtx->s = (__ym_session_t *)YMRetain(s);
        initCtx->socket = aResult;
        initCtx->addr = (struct sockaddr *)bigEnoughAddr;
        initCtx->addrLen = thisLength;
        ym_dispatch_user_t dispatch = { __ym_session_init_incoming_connection_proc, initCtx, NULL, ym_dispatch_user_context_noop };
        YMDispatchAsync(YMDispatchGetGlobalQueue(), &dispatch);
    }
    
    YMRelease(s);
}

YM_ENTRY_POINT(__ym_session_init_incoming_connection_proc)
{
    __ym_connection_init_t *initCtx = (__ym_connection_init_t *)context;
    __ym_session_t *s = initCtx->s;
    int socket = initCtx->socket;
    struct sockaddr *addr = initCtx->addr;
    socklen_t addrLen = initCtx->addrLen;
    
    YMAddressRef peerAddress = NULL;
    YMPeerRef peer = NULL;
    YMConnectionRef newConnection = NULL;
    
    ymlog("__ym_session_init_connection entered: sf%d %d %s",socket,addrLen,s->ipv4?"ip4":"ip6");
    
    peerAddress = YMAddressCreate(addr,s->ipv4?((struct sockaddr_in *)addr)->sin_port:((struct sockaddr_in6 *)addr)->sin6_port);
    peer = _YMPeerCreateWithAddress(peerAddress);
    if ( ! s->shouldAcceptFunc(s, peer, s->callbackContext) ) {
        ymlog("client rejected peer %s",YMSTR(YMAddressGetDescription(peerAddress)));
        goto catch_release;
    }
    
    newConnection = YMConnectionCreateIncoming(socket, peerAddress, YMConnectionStream, true);
    if ( ! newConnection ) {
        ymlog("failed to create new connection");
		if ( s->connectFailedFunc )
			s->connectFailedFunc(s, peer, false, s->callbackContext);
        goto catch_release;
    }
    
    ymlog("new connection %s",YMSTR(YMAddressGetDescription(peerAddress)));
    
    __YMSessionAddConnection(s, newConnection);
    ymassert(s->defaultConnection,"connection init incoming");
    s->connectedFunc(s,newConnection,s->callbackContext);
    
catch_release:
    
    if ( peerAddress )
        YMRelease(peerAddress);
    if ( peer )
        YMRelease(peer);
    if ( newConnection )
        YMRelease(newConnection);
    YMRelease(s);
    YMFREE(addr);
    YMFREE(initCtx);
}

#pragma mark shared

YMConnectionRef YMSessionGetDefaultConnection(YMSessionRef s)
{
    return s->defaultConnection;
}

YMArrayRef YMAPI YMSessionCopyConnections(YMSessionRef s)
{
    if ( ! ( s->connectionsByAddress && YMDictionaryGetCount(s->connectionsByAddress) ) )
        return NULL;
    
    YMArrayRef copy = YMArrayCreate2(true);
    
    YMDictionaryEnumRef denum = YMDictionaryEnumeratorBegin(s->connectionsByAddress);
    while ( denum ) {
        YMConnectionRef aConnection = (YMConnectionRef)denum->value;
        YMArrayAdd(copy, aConnection);
        denum = YMDictionaryEnumeratorGetNext(denum);
    }
    
    return copy;
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

void __YMSessionAddConnection(__ym_session_t *s, YMConnectionRef connection)
{
    YMAddressRef address = YMConnectionGetAddress(connection);
    ymassert(address, "could not initialize addr for connection");
    
    YMSelfLock(s);
    {
        YMDictionaryKey key = (YMDictionaryKey)address;
        ymassert(!YMDictionaryContains(s->connectionsByAddress, key), "connections by address state");
        YMDictionaryAdd(s->connectionsByAddress, key, (void *)YMRetain(connection));
        ymlog("%p added connection %p with key %p",s,connection,key);
    }
    YMSelfUnlock(s);
    
    YMConnectionSetCallbacks(connection, __ym_session_new_stream_proc, s,
                             __ym_session_stream_closing_proc, s,
                             __ym_session_connection_interrupted_proc, s);
    
    bool isNewDefault = (s->defaultConnection == NULL);
    
    ymlog("adding %s connection for %s",isNewDefault?"default":"aux",YMSTR(YMAddressGetDescription(address)));
    
    if ( isNewDefault )
        s->defaultConnection = connection;
    
    ymassert(s->defaultConnection!= NULL,"no default connection");
}

bool __YMSessionInterrupt(__ym_session_t *s, YMConnectionRef floatConnection)
{
    bool first = false;
    YMSelfLock(s);
    {
        if ( ! s->interrupted ) {
            first = true;
            s->interrupted = true;
            s->defaultConnection = NULL;
    
            while ( YMDictionaryGetCount(s->connectionsByAddress) ) {
                YMDictionaryKey aKey = YMDictionaryGetRandomKey(s->connectionsByAddress);
                YMConnectionRef aConnection = (YMConnectionRef)YMDictionaryRemove(s->connectionsByAddress, aKey);
                ymassert(aConnection,"connection list state");
                ymerr("releasing %s",YMSTR(YMAddressGetDescription(YMConnectionGetAddress(aConnection))));
                if ( aConnection != floatConnection )
                    YMRelease(aConnection);
            }
        }
    }
    YMSelfUnlock(s);
    
    return first;
}

#pragma mark connection callbacks

void __ym_session_new_stream_proc(YMConnectionRef connection, YMStreamRef stream, void *context)
{
    __ym_session_t *s = context;
    
    YMAddressRef address = YMConnectionGetAddress(connection);
    YMPlexerStreamID streamID = YM_STREAM_INFO(stream)->streamID;
    ymdbg("new incoming stream %"PRIu64" on %s",streamID,YMSTR(YMAddressGetDescription(address)));
    
    if ( connection != s->defaultConnection )
        ymerr("warning: new stream on non-default connection");
    
    // is it weird that we don't report 'connection' here, despite user only being concerned with "active"?
    if ( s->newStreamFunc )
        s->newStreamFunc(s,connection,stream,s->callbackContext);
}

void __ym_session_stream_closing_proc(YMConnectionRef connection, YMStreamRef stream, void *context)
{
    __ym_session_t *s = context;
    
    YMAddressRef address = YMConnectionGetAddress(connection);
    YMPlexerStreamID streamID = YM_STREAM_INFO(stream)->streamID;
    ymdbg("remote stream %"PRIu64" closing on %s",streamID,YMSTR(YMAddressGetDescription(address)));
    
    if ( connection != s->defaultConnection ) {
        ymerr("warning: closing remote stream on non-default connection");
        return;
    }
    
    if ( s->streamClosingFunc )
        s->streamClosingFunc(s,connection,stream,s->callbackContext);
}

void __ym_session_connection_interrupted_proc(YMConnectionRef connection, void *context)
{
    __ym_session_t *s = context;
    
    YMConnectionRef savedDefault = s->defaultConnection;
    bool first = __YMSessionInterrupt(s, connection);
    if ( ! first )
        return;
    
    bool isDefault = ( connection == savedDefault );
    
    ymerr("connection interrupted: %s",YMSTR(YMAddressGetDescription(YMConnectionGetAddress(connection))));
    
    YMSelfLock(s);
    {
        YMDictionaryKey key = (YMDictionaryKey)YMConnectionGetAddress(connection);
        if ( YMDictionaryContains(s->connectionsByAddress, key) )
            YMDictionaryRemove(s->connectionsByAddress, key);
    }
    YMSelfUnlock(s);
    
    if ( isDefault ) {
        if ( s->interruptedFunc )
            s->interruptedFunc(s,s->callbackContext);
    }
    
    YMRelease(connection);
}

#pragma mark client mdns callbacks

void __ym_mdns_service_appeared_func(__unused YMmDNSBrowserRef browser, YMmDNSServiceRecord * service, void *context)
{
    __ym_session_t *s = context;
    ymlog("__ym_mdns_service_appeared_func: %s",YMSTR(service->name));
    
    YMPeerRef peer = _YMPeerCreate(service->name, NULL, NULL);
    YMSelfLock(s);
    YMDictionaryAdd(s->knownPeers, (YMDictionaryKey)peer, (void *)peer);
    YMSelfUnlock(s);
    
    s->addedFunc(s,peer,s->callbackContext);
}

void __ym_mdns_service_removed_func(__unused YMmDNSBrowserRef browser, YMStringRef name, void *context)
{
    __ym_session_t *s = context;
    
    ymlog("__ym_mdns_service_removed_func %s",YMSTR(name));
    
    YMSelfLock(s);
    {
        YMDictionaryKey mysteryKey = (YMDictionaryKey)MAX_OF(uint64_t);
        bool found = false;
        YMDictionaryEnumRef myEnum = YMDictionaryEnumeratorBegin(s->knownPeers);
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
            goto catch_return;
        }
        
        YMDictionaryRemove(s->knownPeers, mysteryKey);
    }
catch_return:
    YMSelfUnlock(s);
}

void __ym_mdns_service_updated_func(__unused YMmDNSBrowserRef browser, YMmDNSServiceRecord *service, void *context)
{
    __unused __ym_session_t *s = context;
    ymlog("__ym_mdns_service_updated_func %s",YMSTR(service->name));
}

void __ym_mdns_service_resolved_func(__unused YMmDNSBrowserRef browser, bool success, YMmDNSServiceRecord *service, void *context)
{
    __ym_session_t *s = context;
    
    ymlog("__ym_mdns_service_resolved_func %s",YMSTR(service->name));
    
    bool found = false;
    YMPeerRef peer = NULL;
    YMSelfLock(s);
    {
        YMDictionaryEnumRef myEnum = YMDictionaryEnumeratorBegin(s->knownPeers);
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
    YMSelfUnlock(s);
    
    if ( found ) {
        if ( success )
            s->resolvedFunc(s,peer,s->callbackContext);
        else
            s->resolveFailedFunc(s,peer,s->callbackContext);
    } else
        ymabort("notified of resolution of unknown peer: %s",YMSTR(service->name));
}

#pragma mark config change observing

void __YMSessionUpdateNetworkConfigDate(__ym_session_t *s)
{
	ymerr("network config changed on %p", s);
}

#if defined(YMAPPLE)

void __ym_SCDynamicStoreCallBack(__unused SCDynamicStoreRef store, __unused CFArrayRef changedKeys, void *info)
{
	__YMSessionUpdateNetworkConfigDate(info);
}

YM_ENTRY_POINT(__ym_session_macos_sc_runloop_proc)
{
    __ym_session_t *s = context;

    SCDynamicStoreContext storeCtx = { 0, s, NULL, NULL, NULL };
    
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
    
    while ( ! s->scObserverThreadExitFlag )
      CFRunLoopRunInMode(kCFRunLoopDefaultMode, .2, false);

    CFRunLoopRemoveSource(aRunloop, storeSource, kCFRunLoopDefaultMode);
    CFRelease(storeSource);
    CFRelease(scStore);

    ymerr("network interface change observer exiting");
    
#else
    // todo Reachability?
    storeCtx.version = 6969;
    ymerr("warning: network interface change observing unimplemented on ios");
#endif
}

bool __YMSessionObserveNetworkInterfaceChangesMacos(__ym_session_t *s, bool startStop)
{
	if (startStop) {
		if (s->scObserverThread)
			return true;
		
		s->scObserverThread = YMThreadCreate(NULL,__ym_session_macos_sc_runloop_proc,s);
		s->scObserverThreadExitFlag = false;
		return s->scObserverThread && YMThreadStart(s->scObserverThread);
	}
	else {
		if (!s->scObserverThread)
			return false;

		ymdbg("stopped observing network interface changes");
		s->scObserverThreadExitFlag = true;
		YMThreadJoin(s->scObserverThread);
		YMRelease(s->scObserverThread);
		s->scObserverThread = NULL;
	}

	return true;
}

#elif defined(YMLINUX)

YM_ENTRY_POINT(__ym_session_linux_proc_net_dev_scrape_proc)
{
	__ym_session_t *s = context;
	
	YMDictionaryRef thisIter = NULL, prevIter = NULL;
	// this /proc scraping method was cribbed from ifplugd. thanks ifplugd!
	// PF_NETLINK approach didn't work after not a whole lot of trying on raspbian
	while (!s->linuxPollThreadExitFlag) {
		FILE *f;
		char ln[256];

		if (!(f = fopen("/proc/net/dev", "r"))) {
			ymerr("failed to open /proc/net/dev: %d %s", errno, strerror(errno));
			return;
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
				return;
			}

			*e = '\0';

			int fd;
			if ((fd = socket(PF_INET, SOCK_DGRAM, 0)) < 0) {
				ymerr("failed to open /proc/net/dev: %d %s",errno,strerror(errno));
				fclose(f);
				return;
			}
			
	        interface_status_t st;
	        if ((st = interface_detect_beat_ethtool(fd, p)) == IFSTATUS_ERR)
	            if ((st = interface_detect_beat_mii(fd, p)) == IFSTATUS_ERR)
	                if ((st = interface_detect_beat_wlan(fd, p)) == IFSTATUS_ERR)
	                    st = interface_detect_beat_iff(fd, p);
	        close(fd);
	        
			bool somethingMoved = false;
	        if ( prevIter ) {
				bool found = false;
				YMDictionaryEnumRef dEnum = YMDictionaryEnumeratorBegin(prevIter);
				while ( dEnum ) {
					if ( strcmp((char *)dEnum->key,p) == 0 ) {
						found = true;
						interface_status_t prevStatus = (interface_status_t)dEnum->value;
						somethingMoved = prevStatus != st;
						if ( somethingMoved ) {
							ymerr("%s: changed status: %s",p,st==IFSTATUS_UP?"up":(st==IFSTATUS_DOWN?"down":"?"));
							__YMSessionUpdateNetworkConfigDate(s);
						}
					}
					dEnum = YMDictionaryEnumeratorGetNext(dEnum);
				}
				
				if ( ! found ) {
					ymerr("%s: new interface status: %s",p,st==IFSTATUS_UP?"up":(st==IFSTATUS_DOWN?"down":"?"));
					__YMSessionUpdateNetworkConfigDate(s);
				}
			}
	        char *ifCopy = strdup(p);
	        YMDictionaryAdd(thisIter,(YMDictionaryKey)ifCopy,(YMDictionaryValue)st);
	
	        switch(st) {
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

		if (s->linuxPollThreadExitFlag)
			break;
		sleep(1);
		
		while ( prevIter && ( YMDictionaryGetCount(prevIter) > 0 ) ) {
			YMDictionaryKey key = YMDictionaryGetRandomKey(prevIter);
			YMDictionaryRemove(prevIter,key);
			YMFREE((void *)key);
		}
		if ( prevIter ) YMRelease(prevIter);
		prevIter = thisIter;
	}
}

bool __YMSessionObserveNetworkInterfaceChangesLinux(__ym_session_t *s, bool startStop)
{
	if (!startStop) {
		if (!s->linuxPollThread)
			return false;

		s->linuxPollThreadExitFlag = true;
		YMThreadJoin(s->linuxPollThread);
		YMRelease(s->linuxPollThread);
		s->linuxPollThread = NULL;

		return true;
	}

	s->linuxPollThread = YMThreadCreate(NULL, __ym_session_linux_proc_net_dev_scrape_proc, s);
	s->linuxPollThreadExitFlag = false;
	bool okay = YMThreadStart(s->linuxPollThread);
	if (!okay) {
		YMRelease(s->linuxPollThread);
		s->linuxPollThread = NULL;
	}

    return okay;
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

bool __YMSessionObserveNetworkInterfaceChangesWin32(__ym_session_t *s, bool startStop)
{
	static BSTR gbNamespace = NULL;
	static BSTR gbWql = NULL;
	static BSTR gbQuery = NULL;

	if (!startStop) {
		if (s->gobbledygook) {
			s->gobbledygook->lpVtbl->CancelAsyncCall(s->gobbledygook, (IWbemObjectSink *)s->my_gobbledygook);
			s->gobbledygook->lpVtbl->Release(s->gobbledygook);
			s->gobbledygook = NULL;

			YMRelease(s->my_gobbledygook->that);
			s->my_gobbledygook->lpVtbl->Release((IWbemObjectSink *)s->my_gobbledygook);
			// these malloc'd pointers move on the first QueryInterface call
			// as if some standard com thing wraps them in something else.
			// presumably they still need to be free'd, wherever it is they went.
			// or maybe that's done for us in Release?
			//YMFREE(s->my_gobbledygook->lpVtbl);
			//YMFREE(s->my_gobbledygook);
			s->my_gobbledygook = NULL;

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
	hr = pLoc->lpVtbl->ConnectServer(pLoc, gbNamespace, NULL, NULL, NULL, 0, NULL, NULL, &s->gobbledygook);
	if (FAILED(hr)) {
		ymerr("ConnectServer failed");
		goto catch_release;
	}

	hr = CoSetProxyBlanket((IUnknown *)s->gobbledygook, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL, RPC_C_AUTHN_LEVEL_CALL,
		RPC_C_IMP_LEVEL_DELEGATE, NULL, EOAC_NONE);

	if (FAILED(hr)) {
		ymerr("CoSetProxyBlanket failed");
		goto catch_release;
	}

	hr = CoCreateInstance(&CLSID_UnsecuredApartment, NULL,
		CLSCTX_LOCAL_SERVER, &IID_IUnsecuredApartment,
		(void**)&pUnsecApp);

	s->my_gobbledygook = YMALLOC(sizeof(YM_IWbemObjectSink));
	s->my_gobbledygook->that = (void *)YMRetain(s);
	s->my_gobbledygook->lpVtbl = YMALLOC(sizeof(IWbemObjectSinkVtbl));
	s->my_gobbledygook->lpVtbl->AddRef = ymsink_AddRef;
	s->my_gobbledygook->lpVtbl->Indicate = ymsink_Indicate;
	s->my_gobbledygook->lpVtbl->QueryInterface = ymsink_QueryInterface;
	s->my_gobbledygook->lpVtbl->Release = ymsink_Release;
	s->my_gobbledygook->lpVtbl->SetStatus = ymsink_SetStatus;

	hr = pUnsecApp->lpVtbl->CreateObjectStub(pUnsecApp, (IUnknown *)s->my_gobbledygook, &pStubUnk);

	if (FAILED(hr)) {
		ymerr("CreateObjectStub failed");
		goto catch_release;
	}

	hr = pStubUnk->lpVtbl->QueryInterface(pStubUnk, &IID_IWbemObjectSink, (void **)&s->my_gobbledygook);

	if (FAILED(hr)) {
		ymerr("QueryInterface failed");
		goto catch_release;
	}

	hr = s->gobbledygook->lpVtbl->ExecNotificationQueryAsync(s->gobbledygook,
		gbWql,
		gbQuery,
		WBEM_FLAG_SEND_STATUS,
		NULL,
		(IWbemObjectSink *)s->my_gobbledygook);

	if (FAILED(hr)) {
		ymerr("ExecNotificationQueryAsync failed");
		goto catch_release;
	}

	ymdbg("started observing interface changes");

	return true;

catch_release:

	if (s->my_gobbledygook) {
		YMFREE(s->my_gobbledygook->lpVtbl);
		YMFREE(s->my_gobbledygook);
		s->my_gobbledygook = NULL;
	}
	if (s->gobbledygook) {
		s->gobbledygook->lpVtbl->Release(s->gobbledygook);
		s->gobbledygook = NULL;
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
