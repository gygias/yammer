//
//  YMSession.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMSession.h"
#include "YMPrivate.h"

#include "YMmDNSService.h"
#include "YMmDNSBrowser.h"
#include "YMLock.h"
#include "YMThread.h"
#include "YMAddress.h"
#include "YMPeerPriv.h"
#include "YMStreamPriv.h"

#include "YMLog.h"
#undef ymlog_type
#define ymlog_type YMLogSession
#if ( ymlog_type > ymlog_target )
#undef ymlog
#define ymlog(x,...) ;
#endif

#include <netinet/in.h>
#include <netdb.h> // struct hostent

typedef struct __YMSession
{
    YMTypeID _type;
    
    // shared
    bool isServer;
    const char *type;
    YMDictionaryRef connectionsByAddress;
    YMLockRef connectionsByAddressLock;
    const char *logDescription;
    YMConnectionRef defaultConnection;
    
    // server
    const char *name;
    YMmDNSServiceRef service;
    int ipv4ListenSocket;
    int ipv6ListenSocket;
    YMThreadRef acceptThread;
    bool acceptThreadExitFlag;
    YMThreadRef initConnectionDispatchThread;
    
    // client
    YMmDNSBrowserRef browser;
    YMDictionaryRef availablePeers;
    YMLockRef availablePeersLock;
    
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
} _YMSession;

#pragma mark setup

YMSessionRef __YMSessionCreateShared(const char *type, bool isServer);
void __YMSessionAddConnection(YMSessionRef session, YMConnectionRef connection);
void __ym_session_accept_proc(void *);
void *__ym_session_init_incoming_connection_proc(ym_thread_dispatch_ref);
void *__ym_session_connect_async_proc(ym_thread_dispatch_ref);

void __ym_session_new_stream_proc(YMConnectionRef connection, YMStreamRef stream, void *context);
void __ym_session_stream_closing_proc(YMConnectionRef connection, YMStreamRef stream, void *context);
void __ym_session_connection_interrupted_proc(YMConnectionRef connection, void *context);

void __ym_mdns_service_appeared_func(YMmDNSBrowserRef browser, YMmDNSServiceRecord * service, void *context);
void __ym_mdns_service_removed_func(YMmDNSBrowserRef browser, const char *name, void *context);
void __ym_mdns_service_updated_func(YMmDNSBrowserRef browser, YMmDNSServiceRecord *service, void *context);
void __ym_mdns_service_resolved_func(YMmDNSBrowserRef browser, bool success, YMmDNSServiceRecord *service, void *context);

YMSessionRef YMSessionCreateClient(const char *type)
{
    YMSessionRef session = __YMSessionCreateShared(type,false);
    session->logDescription = YMStringCreateWithFormat("c:%s",type);
    session->availablePeers = YMDictionaryCreate();
    session->availablePeersLock = YMLockCreateWithOptionsAndName(YMLockDefault, "available-peers");
    return session;
}

YMSessionRef YMSessionCreateServer(const char *type, const char *name)
{
    YMSessionRef session = __YMSessionCreateShared(type,true);
    session->name = strdup(name);
    session->ipv4ListenSocket = -1;
    session->ipv6ListenSocket = -1;
    session->logDescription = YMStringCreateWithFormat("s:%s:%s",type,name);
    session->service = NULL;
    session->acceptThread = NULL;
    session->acceptThreadExitFlag = false;
    session->initConnectionDispatchThread = NULL;
    return session;
}

YMSessionRef __YMSessionCreateShared(const char *type, bool isServer)
{
    YMSessionRef session = (YMSessionRef)YMALLOC(sizeof(struct __YMSession));
    session->_type = _YMSessionTypeID;
    
    session->type = strdup(type);
    session->isServer = isServer;
    session->connectionsByAddress = YMDictionaryCreate();
    session->connectionsByAddressLock = YMLockCreate(YMLockDefault, "connections-by-address");
    return session;
}

void YMSessionSetClientCallbacks(YMSessionRef session, ym_session_added_peer_func added, ym_session_removed_peer_func removed,
                                 ym_session_resolve_failed_func rFailed, ym_session_resolved_peer_func resolved,
                                 ym_session_connect_failed_func cFailed, void *context)
{
    session->addedFunc = added;
    session->removedFunc = removed;
    session->resolveFailedFunc = rFailed;
    session->resolvedFunc = resolved;
    session->connectFailedFunc = cFailed;
    session->callbackContext = context;
}

void YMSessionSetServerCallbacks(YMSessionRef session,ym_session_should_accept_func should, void* context)
{
    session->shouldAcceptFunc = should;
    session->callbackContext = context;
}

void YMSessionSetSharedCallbacks(YMSessionRef session, ym_session_connected_func connected, ym_session_interrupted_func interrupted,
                                                        ym_session_new_stream_func new_, ym_session_stream_closing_func closing)
{
    session->connectedFunc = connected;
    session->interruptedFunc = interrupted;
    session->newStreamFunc = new_;
    session->streamClosingFunc = closing;
}

void _YMSessionFree(YMTypeRef object)
{
    YMSessionRef session = (YMSessionRef)object;
    if ( ! session->isServer )
        free((void *)session->name);
#pragma message "implement me"
    free(session);
}

#pragma mark client

ymbool YMSessionClientStart(YMSessionRef session)
{
    if ( session->browser )
        return false;
    
    session->browser = YMmDNSBrowserCreateWithCallbacks(session->type,
                                                        __ym_mdns_service_appeared_func,
                                                        __ym_mdns_service_updated_func,
                                                        __ym_mdns_service_resolved_func,
                                                        __ym_mdns_service_removed_func,
                                                        session);
    if ( ! session->browser )
    {
        ymerr("session[%s]: error: failed to create browser",session->logDescription);
        return false;
    }
    
    bool startOK = YMmDNSBrowserStart(session->browser);
    if ( ! startOK )
    {
        ymerr("session[%s]: error: failed to start browser",session->logDescription);
        return false;
    }
    
    return true;
}

ymbool YMSessionClientResolvePeer(YMSessionRef session, YMPeerRef peer)
{
    YMLockLock(session->availablePeersLock);
    const char *peerName = YMPeerGetName(peer);
    bool knownPeer = true;
    if ( ! YMDictionaryContains(session->availablePeers, (YMDictionaryKey)peer) )
    {
        ymerr("session[%s]: requested resolve of unknown peer: %s",session->logDescription,peerName);
        knownPeer = false;
    }
    YMLockUnlock(session->availablePeersLock);
    
    if ( ! knownPeer )
        return false;
    
    return YMmDNSBrowserResolve(session->browser, peerName);
}

typedef struct __ym_session_connect_async_context_def
{
    YMSessionRef session;
    YMPeerRef peer;
    YMConnectionRef connection;
} ___ym_session_connect_async_context_def;
typedef struct __ym_session_connect_async_context_def *__ym_session_connect_async_context;

ymbool YMSessionClientConnectToPeer(YMSessionRef session, YMPeerRef peer, ymbool sync)
{
    bool knownPeer = true;
    YMLockLock(session->availablePeersLock);
    const char *peerName = YMPeerGetName(peer);
    if ( ! YMDictionaryContains(session->availablePeers, (YMDictionaryKey)peer) )
    {
        ymerr("session[%s]: requested connect to unknown peer: %s",session->logDescription,peerName);
        knownPeer = false;
    }
    YMLockUnlock(session->availablePeersLock);
    
    if ( ! knownPeer )
        return false;
    
    YMDictionaryRef addresses = YMPeerGetAddresses(peer);
    YMDictionaryKey aKey = YMDictionaryRandomKey(addresses);
    YMAddressRef address = (YMAddressRef)YMDictionaryGetItem(addresses, aKey);
    YMConnectionRef newConnection = YMConnectionCreate(address, YMConnectionStream, YMTLS);
    
    __ym_session_connect_async_context context = (__ym_session_connect_async_context)YMALLOC(sizeof(struct __ym_session_connect_async_context_def));
    context->session = session;
    context->peer = peer;
    context->connection = newConnection;
    
    char *name = YMStringCreateWithFormat("session-async-connect-%s",YMAddressGetDescription(address));
    ym_thread_dispatch connectDispatch = {__ym_session_connect_async_proc, 0, 0, context, name};
    
    YMThreadRef dispatchThread = NULL;
    if ( ! sync )
    {
        dispatchThread = YMThreadDispatchCreate(name);
        free(name);
        if ( ! dispatchThread )
        {
            ymerr("session[%s]: error: failed to create async connect thread",session->logDescription);
            return false;
        }
        bool okay = YMThreadStart(dispatchThread);
        if ( ! okay )
        {
            ymerr("session[%s]: error: failed to start async connect thread",session->logDescription);
            return false;
        }
        
        YMThreadDispatchDispatch(dispatchThread, connectDispatch);
    }
    else
    {
        __ym_session_connect_async_proc(&connectDispatch);
        //free(context); free'd by proc, to have one code path with async
    }
    
    return true;
}

void *__ym_session_connect_async_proc(ym_thread_dispatch_ref dispatch)
{
    __ym_session_connect_async_context context = (__ym_session_connect_async_context)dispatch->context;
    YMSessionRef session = context->session;
    YMPeerRef peer = context->peer;
    YMConnectionRef connection = context->connection;
    free(context);
    
    ymlog("session[%s]: __ym_session_connect_async_proc entered",session->logDescription);
    
    bool okay = YMConnectionConnect(connection);
    
    if ( okay )
    {
        __YMSessionAddConnection(session,connection);
        session->connectedFunc(session,connection,session->callbackContext);
    }
    else
        session->connectFailedFunc(session,peer,session->callbackContext);
    
    ymlog("session[%s]: __ym_session_connect_async_proc exiting: %s",session->logDescription,okay?"success":"fail");
    
    return NULL;
}

void __YMSessionAddConnection(YMSessionRef session, YMConnectionRef connection)
{
    
    YMLockLock(session->connectionsByAddressLock);
    YMDictionaryKey key = (YMDictionaryKey)connection;
    if ( YMDictionaryContains(session->connectionsByAddress, key) )
    {
        ymerr("session[%s]: error: connections list already contains %llu",session->logDescription,key);
        abort();
    }
    YMDictionaryAdd(session->connectionsByAddress, key, connection);
    YMLockUnlock(session->connectionsByAddressLock);
    
    YMConnectionSetCallbacks(connection, __ym_session_new_stream_proc, session,
                                         __ym_session_stream_closing_proc, session,
                                        __ym_session_connection_interrupted_proc, session);
    
    bool isNewDefault = (session->defaultConnection == NULL);
    YMAddressRef address = YMConnectionGetAddress(connection);
    if ( ! address )
    {
        ymerr("session[%s]: internal: connection has no address",session->logDescription);
        abort();
    }
    ymlog("session[%s]: adding %s connection for %s",session->logDescription,isNewDefault?"default":"aux",YMAddressGetDescription(address));
    
    if ( isNewDefault )
        session->defaultConnection = connection;
}

#pragma mark server

typedef struct __ym_session_accept_thread_context_def
{
    YMSessionRef session;
    bool ipv4;
} _ym_session_accept_thread_context_def;
typedef struct __ym_session_accept_thread_context_def *__ym_session_accept_thread_context;

ymbool YMSessionServerStartAdvertising(YMSessionRef session)
{
    if ( session->service )
        return false;
    
#pragma message "in case of v4 and v6, two services?"
    bool mDNSOK = false;
    int socket = -1;
    bool ipv4 = true;
    int32_t port = YMPortReserve(ipv4, &socket);
    if ( port == -1 || socket == -1 || socket > UINT16_MAX )
    {
        ymerr("session[%s]: error: failed to reserve port for server start",session->logDescription);
        return false;
    }
    
    int aResult = listen(socket, 1);
    if ( aResult != 0 )
    {
        ymerr("session[%s]: error: failed to listen for server start",session->logDescription);
        close(socket);
        goto rewind_fail;
    }
    
    if ( ipv4 )
        session->ipv4ListenSocket = socket;
    else
        session->ipv6ListenSocket = socket;
    
    char *name = YMStringCreateWithFormat("session-accept-%s",session->type);
    __ym_session_accept_thread_context ctx = YMALLOC(sizeof(struct __ym_session_accept_thread_context_def));
    ctx->session = session;
    ctx->ipv4 = ipv4;
    session->acceptThread = YMThreadCreate(name, __ym_session_accept_proc, ctx);
    free(name);
    if ( ! session->acceptThread )
    {
        ymerr("session[%s]: error: failed to create accept thread",session->logDescription);
        goto rewind_fail;
    }
    session->acceptThreadExitFlag = false;
    bool threadOK = YMThreadStart(session->acceptThread);
    if ( ! threadOK )
    {
        ymerr("session[%s]: error: failed to start accept thread",session->logDescription);
        goto rewind_fail;
    }
    
    name = YMStringCreateWithFormat("session-init-%s",session->type);
    session->initConnectionDispatchThread = YMThreadDispatchCreate(name);
    free(name);
    if ( ! session->initConnectionDispatchThread )
    {
        ymerr("session[%s]: error: failed to create connection init thread",session->logDescription);
        goto rewind_fail;
    }
    threadOK = YMThreadStart(session->initConnectionDispatchThread);
    if ( ! threadOK )
    {
        ymerr("session[%s]: error: failed to start start connection init thread",session->logDescription);
        goto rewind_fail;
    }
    
    session->service = YMmDNSServiceCreate(session->type, session->name, (uint16_t)port);
    if ( ! session->service )
    {
        ymerr("session[%s]: error: failed to create mdns service",session->logDescription);
        goto rewind_fail;
    }
    mDNSOK = YMmDNSServiceStart(session->service);
    if ( ! mDNSOK )
    {
        ymerr("session[%s]: error: failed to start mdns service",session->logDescription);
        goto rewind_fail;
    }
    
    return true;
    
rewind_fail:
    if ( socket >= 0 )
        close(socket);
    session->ipv4ListenSocket = -1;
    session->ipv6ListenSocket = -1;
    if ( session->acceptThread )
    {
        session->acceptThreadExitFlag = true;
        YMFree(session->acceptThread);
        session->acceptThread = NULL;
    }
    if ( session->service )
    {
        if ( mDNSOK )
            YMmDNSServiceStop(session->service, false);
        YMFree(session->service);
        session->service = NULL;
    }
    return false;
}

ymbool YMSessionServerStopAdvertising(YMSessionRef session)
{
    return YMmDNSServiceStop(session->service, false);
}

typedef struct __ym_connection_init_context_def
{
    YMSessionRef session;
    int socket;
    struct sockaddr *addr;
    socklen_t addrLen; // redundant?
    bool ipv4;
} ___ym_connection_init_context_def;
typedef struct __ym_connection_init_context_def *__ym_connection_init_context;

void __ym_session_accept_proc(void * ctx)
{
    __ym_session_accept_thread_context context = (__ym_session_accept_thread_context)ctx;
    YMSessionRef session = context->session;
    bool ipv4 = context->ipv4;
    free(context);
    
    while ( ! session->acceptThreadExitFlag )
    {
        ymlog("session[%s]: accepting...",session->logDescription);
        int socket = ipv4 ? session->ipv4ListenSocket : session->ipv6ListenSocket;
        
        struct sockaddr_in6 *bigEnoughAddr = calloc(1,sizeof(struct sockaddr_in6));
        socklen_t thisLength = ipv4 ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);
        
        int aResult = accept(socket, (struct sockaddr *)bigEnoughAddr, &thisLength);
        if ( aResult < 0 )
        {
            ymlog("session[%s]: accept(%d) failed: %d (%s)",session->logDescription,socket,errno,strerror(errno));
            continue;
        }
        
        ymlog("session[%s]: accepted %d, dispatching connection init",session->logDescription, aResult);
        
        __ym_connection_init_context initCtx = (__ym_connection_init_context)YMALLOC(sizeof(struct __ym_connection_init_context_def));
        initCtx->session = session;
        initCtx->socket = aResult;
        initCtx->addr = (struct sockaddr *)bigEnoughAddr;
        initCtx->addrLen = thisLength;
        initCtx->ipv4 = ipv4;
        ym_thread_dispatch dispatch = { __ym_session_init_incoming_connection_proc, NULL, false, initCtx, "init-connection" };
        YMThreadDispatchDispatch(session->initConnectionDispatchThread, dispatch);
    }
    
    session->acceptThreadExitFlag = false;
}

void *__ym_session_init_incoming_connection_proc(ym_thread_dispatch_ref dispatch)
{
    __ym_connection_init_context initCtx = dispatch->context;
    YMSessionRef session = initCtx->session;
    int socket = initCtx->socket;
    struct sockaddr *addr = initCtx->addr;
    socklen_t addrLen = initCtx->addrLen;
    __unused bool ipv4 = initCtx->ipv4;
    free(initCtx);
    
    ymlog("session[%s]: __ym_session_init_connection entered: %d %d %d",session->logDescription,socket,addrLen,ipv4);
    
    YMAddressRef address = NULL;
    YMPeerRef peer = NULL;
    YMConnectionRef newConnection = NULL;
    
    address = YMAddressCreate(addr, addrLen);
    peer = _YMPeerCreateWithAddress(address);
    if ( ! session->shouldAcceptFunc(session,peer,session->callbackContext) )
    {
        ymlog("session[%s]: client rejected peer %s",session->logDescription,YMAddressGetDescription(address));
        goto catch_return;
    }
    
    newConnection = YMConnectionCreateIncoming(socket, address, YMConnectionStream, YMTLS);
    if ( ! newConnection )
    {
        ymlog("session[%s]: failed to create new connection",session->logDescription);
        goto catch_return;
    }
    
    __YMSessionAddConnection(session, newConnection);
    session->connectedFunc(session,newConnection,session->callbackContext);
    
    free(addr);
    
    return NULL;
    
catch_return:
    close(socket);
    free(addr);
    if ( address )
        YMFree(address);
    if ( peer )
        YMFree(peer);
    if ( newConnection )
        YMFree(newConnection);
    return NULL;
}

#pragma mark connected shared
YMConnectionRef YMSessionGetDefaultConnection(YMSessionRef session)
{
    return session->defaultConnection;
}

YMDictionaryRef YMSessionGetConnections(YMSessionRef session)
{
    session = NULL;
    return *(YMDictionaryRef *)session; // todo, sync
}

#pragma mark connection callbacks

void __ym_session_new_stream_proc(YMConnectionRef connection, YMStreamRef stream, void *context)
{
    YMSessionRef session = context;
    
    YMAddressRef address = YMConnectionGetAddress(connection);
    YMStreamID streamID = _YMStreamGetUserInfo(stream)->streamID;
    ymlog("session[%s]: new incoming stream %u on %s",session->logDescription,streamID,YMAddressGetDescription(address));
    
    if ( connection != session->defaultConnection )
        ymerr("session[%s]: warning: new stream on non-default connection",session->logDescription);
    
    // is it weird that we don't report 'connection' here, despite user only being concerned with "active"?
    if ( session->newStreamFunc )
        session->newStreamFunc(session,stream,session->callbackContext);
}

void __ym_session_stream_closing_proc(YMConnectionRef connection, YMStreamRef stream, void *context)
{
    YMSessionRef session = context;
    
    YMAddressRef address = YMConnectionGetAddress(connection);
    YMStreamID streamID = _YMStreamGetUserInfo(stream)->streamID;
    ymlog("session[%s]: stream %u closing on %s",session->logDescription,streamID,YMAddressGetDescription(address));
    
    if ( connection != session->defaultConnection )
    {
        ymerr("session[%s]: warning: closing stream on non-default connection",session->logDescription);
        return;
    }
    
    if ( session->streamClosingFunc )
        session->streamClosingFunc(session,stream,session->callbackContext);
}

void __ym_session_connection_interrupted_proc(YMConnectionRef connection, void *context)
{
    YMSessionRef session = context;
    
    bool isDefault = false;
    YMAddressRef address = YMConnectionGetAddress(connection);
    if ( connection == session->defaultConnection )
    {
        ymerr("session[%s]: default connection interrupted: %s",session->logDescription,YMAddressGetDescription(address));
        session->defaultConnection = NULL;
        isDefault = true;
    }
    else
        ymerr("session[%s]: aux connection interrupted: %s",session->logDescription,YMAddressGetDescription(address));
    
    YMLockLock(session->connectionsByAddressLock);
    YMDictionaryValue removedValue = YMDictionaryRemove(session->connectionsByAddress, (YMDictionaryKey)connection);
    if ( ! removedValue || ( removedValue != connection ) )
    {
        ymerr("session[%s]: sanity check dictionary: %p v %p",session->logDescription, removedValue, connection);
        abort();
    }
    
    // weird that new stream / stream close don't report connection but this does, should be consistent
    if ( isDefault && session->interruptedFunc )
        session->interruptedFunc(session,session->callbackContext);
}

#pragma mark client mdns callbacks

void __ym_mdns_service_appeared_func(__unused YMmDNSBrowserRef browser, YMmDNSServiceRecord * service, void *context)
{
    YMSessionRef session = (YMSessionRef)context;
    ymlog("session[%s]: __ym_mdns_service_appeared_func: %s",session->logDescription,service->name);
    
    const char *name = service->name;
    YMPeerRef peer = _YMPeerCreate(name, NULL, NULL);
    YMLockLock(session->availablePeersLock);
#pragma message "need to key dictionary off something better now.."
    YMDictionaryAdd(session->availablePeers, (YMDictionaryKey)peer, peer);
    YMLockUnlock(session->availablePeersLock);
    
    session->addedFunc(session,peer,session->callbackContext);
}

void __ym_mdns_service_removed_func(__unused YMmDNSBrowserRef browser, const char *name, void *context)
{
    YMSessionRef session = (YMSessionRef)context;
    ymlog("session[%s]: __ym_mdns_service_removed_func %s",session->logDescription,name);
    
    YMLockLock(session->availablePeersLock);
    {
        YMDictionaryKey mysteryKey = MAX_OF(YMDictionaryKey);
        bool found = false;
        YMDictionaryEnumRef myEnum = YMDictionaryEnumeratorBegin(session->availablePeers);
        while ( myEnum )
        {
            YMPeerRef peer = (YMPeerRef)myEnum->value;
            if ( strcmp(YMPeerGetName(peer),name) == 0 )
            {
                found = true;
                mysteryKey = myEnum->key;
            }
            myEnum = YMDictionaryEnumeratorGetNext(myEnum);
        }
        YMDictionaryEnumeratorEnd(myEnum);
        
        if ( found )
            YMDictionaryRemove(session->availablePeers, mysteryKey);
        else
        {
            ymerr("session[%s]: notified of removal of unknown peer: %s",session->logDescription,name);
            abort();
        }
    }
    YMLockUnlock(session->availablePeersLock);
}

void __ym_mdns_service_updated_func(__unused YMmDNSBrowserRef browser, YMmDNSServiceRecord *service, void *context)
{
    YMSessionRef session = (YMSessionRef)context;
    ymlog("session[%s]: __ym_mdns_service_updated_func %s",session->logDescription,service->name);
}

void __ym_mdns_service_resolved_func(__unused YMmDNSBrowserRef browser, bool success, YMmDNSServiceRecord *service, void *context)
{
    YMSessionRef session = (YMSessionRef)context;
    ymlog("session[%s]: __ym_mdns_service_resolved_func %s",session->logDescription,service->name);
    
    bool found = false;
    YMPeerRef peer = NULL;
    YMLockLock(session->availablePeersLock);
    {
        YMDictionaryEnumRef myEnum = YMDictionaryEnumeratorBegin(session->availablePeers);
        while ( myEnum )
        {
            peer = (YMPeerRef)myEnum->value;
            if ( strcmp(YMPeerGetName(peer),service->name) == 0 )
            {
                found = true;
                peer = (YMPeerRef)myEnum->value;
            }
            
            myEnum = YMDictionaryEnumeratorGetNext(myEnum);
        }
        YMDictionaryEnumeratorEnd(myEnum);
    }
    
    YMDictionaryRef addresses = YMDictionaryCreate();
    
    char *portString = YMStringCreateWithFormat("%u",service->port);
    struct addrinfo *outAddrInfo;
    int result = getaddrinfo(service->hostNames->h_name, portString, NULL, &outAddrInfo);
    free(portString);
    
    if ( result != 0 )
    {
        ymerr("session[%s]: error: failed to resolve addresses for '%s'",session->logDescription,service->hostNames->h_name);
    }
    else
    {
#pragma message "move this into YMAddress"
        struct addrinfo *addrInfoIter = outAddrInfo;
        while ( addrInfoIter )
        {
#pragma message "revisit"
            if ( addrInfoIter->ai_family == AF_INET /*|| addrInfoIter->ai_family == AF_INET6*/ )
            {
                YMAddressRef address = YMAddressCreate(addrInfoIter->ai_addr, addrInfoIter->ai_addrlen);
                if ( address )
                    YMDictionaryAdd(addresses, (YMDictionaryKey)address, address);
                ymlog("session[%s]: %s address with family %d proto %d length %d canon %s: %s",session->logDescription,address?"parsed":"failed to parse",
                      addrInfoIter->ai_family,addrInfoIter->ai_protocol,addrInfoIter->ai_addrlen,addrInfoIter->ai_canonname,address?YMAddressGetDescription(address):"(null");
            }
            else
                ymlog("session[%s]: ignoring address with family %d proto %d length %d canon %s:",session->logDescription,
                      addrInfoIter->ai_family,addrInfoIter->ai_protocol,addrInfoIter->ai_addrlen,addrInfoIter->ai_canonname);
            addrInfoIter = addrInfoIter->ai_next;
        }
        _YMPeerSetAddresses(peer, addresses);
        _YMPeerSetPort(peer, service->port);
    }
    
    YMLockUnlock(session->availablePeersLock);
    
    if ( found )
    {
        if ( success )
            session->resolvedFunc(session,peer,session->callbackContext);
        else
            session->resolveFailedFunc(session,peer,session->callbackContext);
    }
    else
    {
        ymerr("session[%s]: notified of resolution of unknown peer: %s",session->logDescription,service->name);
        abort();
    }
}
