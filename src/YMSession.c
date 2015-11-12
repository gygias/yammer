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

#include "YMLog.h"
#undef ymlog_type
#define ymlog_type YMLogSession
#if ( ymlog_type > ymlog_target )
#undef ymlog
#define ymlog(x,...) ;
#endif

#include <netinet/in.h>

typedef struct __YMSession
{
    YMTypeID _type;
    
    // shared
    bool isServer;
    const char *type;
    YMDictionaryRef connectionsByAddress;
    YMLockRef connectionsByAddressLock;
    const char *logDescription;
    
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
void *__ym_session_accept_proc(void *context);
void *__ym_session_init_connection(void *context);
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
    session->logDescription = YMStringCreateWithFormat("s:%s@%s",type,name);
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

#pragma mark server

typedef struct __YMSessionAcceptThreadCtx
{
    YMSessionRef session;
    bool ipv4;
} _YMSessionAcceptThreadCtx;

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
    struct __YMSessionAcceptThreadCtx *ctx = YMALLOC(sizeof(struct __YMSessionAcceptThreadCtx));
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
        ymerr("session[%s]: error: failed to start connection init thread",session->logDescription);
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

typedef struct __YMInitConnectionCtx
{
    YMSessionRef session;
    int socket;
    struct sockaddr *addr;
    socklen_t addrLen; // redundant?
    bool ipv4;
} _YMInitConnectionCtx;

void *__ym_session_accept_proc(void *context)
{
    struct __YMSessionAcceptThreadCtx *ctx = context;
    YMSessionRef session = ctx->session;
    bool ipv4 = ctx->ipv4;
    free(ctx);
    
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
        
        struct __YMInitConnectionCtx *initCtx = (struct __YMInitConnectionCtx *)YMALLOC(sizeof(struct __YMInitConnectionCtx));
        initCtx->session = session;
        initCtx->socket = socket;
        initCtx->addr = (struct sockaddr *)bigEnoughAddr;
        initCtx->addrLen = thisLength;
        initCtx->ipv4 = ipv4;
        YMThreadDispatchUserInfo userInfo = { __ym_session_init_connection, NULL, false, initCtx, "init-connection" };
        YMThreadDispatchDispatch(session->initConnectionDispatchThread, &userInfo);
    }
    
    session->acceptThreadExitFlag = false;
    
    return NULL;
}

void *__ym_session_init_connection(void *context)
{
    struct __YMInitConnectionCtx *initCtx = context;
    YMSessionRef session = initCtx->session;
    int socket = initCtx->socket;
    struct sockaddr *addr = initCtx->addr;
    socklen_t addrLen = initCtx->addrLen;
    __unused bool ipv4 = initCtx->ipv4;
    free(initCtx);
    
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
    
    newConnection = YMConnectionCreate(address, YMConnectionStream, YMTLS);
    if ( ! newConnection )
    {
        ymlog("session[%s]: failed to create new connection",session->logDescription);
        goto catch_return;
    }
    
    YMLockLock(session->connectionsByAddressLock);
    YMDictionaryAdd(session->connectionsByAddress, (YMDictionaryKey)socket, newConnection);
    YMLockUnlock(session->connectionsByAddressLock);
    
    ymlog("session[%s]: added connection for %s",session->logDescription,YMAddressGetAddressData(address));
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
