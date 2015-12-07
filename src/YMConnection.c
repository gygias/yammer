//
//  YMConnection.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMConnection.h"
#include "YMConnectionPriv.h"

#include "YMPlexer.h"
#include "YMSecurityProvider.h"
#include "YMTLSProvider.h"
#include "YMUtilities.h"
#include "YMThread.h"

#define ymlog_type YMLogConnection
#include "YMLog.h"

#define YM_CON_DESC (connection->address ? YMSTR(YMAddressGetDescription(connection->address)) : "*")

#ifndef WIN32
#include <sys/socket.h>
#include <arpa/inet.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#define NOT_CONNECTED ( ( connection->socket == NULL_SOCKET ) && ! connection->isConnected )

YM_EXTERN_C_PUSH

typedef struct __ym_connection_t
{
    _YMType _type;
    
	YMSOCKET socket;
    bool isIncoming;
    YMAddressRef address;
    YMConnectionType type;
    YMConnectionSecurityType securityType;
    bool closeWhenDone;
    
    ym_connection_new_stream_func newFunc;
    void *newFuncContext;
    ym_connection_stream_closing_func closingFunc;
    void *closingFuncContext;
    ym_connection_interrupted_func interruptedFunc;
    void *interruptedFuncContext;
    
    // volatile
    bool isConnected;
    YMSecurityProviderRef security;
    YMPlexerRef plexer;
} __ym_connection_t;
typedef struct __ym_connection_t *__YMConnectionRef;

enum
{
    __YMConnectionTypeMin = YMConnectionStream,
    __YMConnectionTypeMax = YMConnectionStream
};

enum
{
    __YMConnectionSecurityTypeMin = YMInsecure,
    __YMConnectionSecurityTypeMax = YMTLS
};

void ym_connection_new_stream_proc(YMPlexerRef plexer, YMStreamRef stream, void *context);
void ym_connection_stream_closing_proc(YMPlexerRef plexer, YMStreamRef stream, void *context);
void ym_connection_interrupted_proc(YMPlexerRef plexer, void *context);

__YMConnectionRef __YMConnectionCreate(bool isIncoming, YMAddressRef address, YMConnectionType type, YMConnectionSecurityType securityType, bool closeWhenDone);
bool __YMConnectionDestroy(__YMConnectionRef connection, bool explicit);
bool __YMConnectionInitCommon(__YMConnectionRef connection, YMSOCKET newSocket, bool asServer);

bool __YMConnectionForward(YMConnectionRef connection, bool toFile, YMStreamRef stream, YMFILE file, const uint64_t *nBytesPtr, bool sync, ym_connection_forward_context_t*);
void _ym_connection_forward_callback_proc(void *context, YMIOResult result, uint64_t bytesForwarded);

YMConnectionRef YMConnectionCreate(YMAddressRef address, YMConnectionType type, YMConnectionSecurityType securityType, bool closeWhenDone)
{
    __YMConnectionRef c = __YMConnectionCreate(false, address, type, securityType, closeWhenDone);
    
    c->socket = NULL_SOCKET;
    
    return c;
}

YMConnectionRef YMConnectionCreateIncoming(YMSOCKET socket, YMAddressRef address, YMConnectionType type, YMConnectionSecurityType securityType, bool closeWhenDone)
{
    __YMConnectionRef connection = __YMConnectionCreate(true, address, type, securityType, closeWhenDone);
    bool commonInitOK = __YMConnectionInitCommon(connection, socket, true);
    if ( ! commonInitOK )
    {
        ymlog("connection[%s]: server init failed",YM_CON_DESC);
        YMRelease(connection);
        return NULL;
    }
    
    return connection;
}

__YMConnectionRef __YMConnectionCreate(bool isIncoming, YMAddressRef address, YMConnectionType type, YMConnectionSecurityType securityType, bool closeWhenDone)
{
    if ( type < __YMConnectionTypeMin || type > __YMConnectionTypeMax )
        return NULL;
    if ( securityType < __YMConnectionSecurityTypeMin || securityType > __YMConnectionSecurityTypeMax )
        return NULL;

	YMNetworkingInit();
    
    __YMConnectionRef connection = (__YMConnectionRef)_YMAlloc(_YMConnectionTypeID,sizeof(struct __ym_connection_t));
    
    connection->isIncoming = isIncoming;
    connection->address = (YMAddressRef)YMRetain(address);
    connection->type = type;
    connection->securityType = securityType;
    connection->closeWhenDone = closeWhenDone;
    
    connection->newFunc = NULL;
    connection->newFuncContext = NULL;
    connection->closingFunc = NULL;
    connection->closingFuncContext = NULL;
    connection->interruptedFunc = NULL;
    connection->interruptedFuncContext = NULL;
    
    connection->isConnected = false;
    connection->plexer = NULL;
    
    return connection;
}

void _YMConnectionFree(YMTypeRef object)
{
    __YMConnectionRef connection = (__YMConnectionRef)object;
    __YMConnectionDestroy(connection, true); // frees security and plexer
    YMRelease(connection->address);
}

void YMConnectionSetCallbacks(YMConnectionRef connection_,
                              ym_connection_new_stream_func newFunc, void *newFuncContext,
                              ym_connection_stream_closing_func closingFunc, void *closingFuncContext,
                              ym_connection_interrupted_func interruptedFunc, void *interruptedFuncContext)
{
    __YMConnectionRef connection = (__YMConnectionRef)connection_;
    
    connection->newFunc = newFunc;
    connection->newFuncContext = newFuncContext;
    connection->closingFunc = closingFunc;
    connection->closingFuncContext = closingFuncContext;
    connection->interruptedFunc = interruptedFunc;
    connection->interruptedFuncContext = interruptedFuncContext;
}

bool YMConnectionConnect(YMConnectionRef connection_)
{
    __YMConnectionRef connection = (__YMConnectionRef)connection_;
    
    if ( connection->socket != NULL_SOCKET || connection->isIncoming )
    {
        ymerr("connection[%s]: connect called on connected socket",YM_CON_DESC);
        return false;
    }    
    
    int type;
    switch(connection->type)
    {
        case YMConnectionStream:
            type = SOCK_STREAM;
            break;
        default:
            return false;
    }
    
    int domain = YMAddressGetDomain(connection->address);
    int addressFamily = YMAddressGetAddressFamily(connection->address);
    int protocol = YMAddressGetDefaultProtocolForAddressFamily(addressFamily);
    //struct protoent *ppe = getprotobyname("tcp");
    
	YMSOCKET newSocket = socket(domain, type, protocol);
#ifndef WIN32
    ymassert(newSocket>=0,"connection[%s]: socket failed: %d (%s)",YM_CON_DESC,errno,strerror(errno));
#else
	ymassert(newSocket!=INVALID_SOCKET, "connection[%s]: socket failed: %x",YM_CON_DESC,GetLastError());
#endif
    
    int yes = 1;
    int result = setsockopt(newSocket, SOL_SOCKET, SO_REUSEADDR, (const void *)&yes, sizeof(yes));
    if (result != 0 )
        ymerr("connection[%s]: warning: setsockopt failed on f%d: %d: %d (%s)",YM_CON_DESC,newSocket,result,errno,strerror(errno));
    
    ymlog("connection[%s]: connecting...",YM_CON_DESC);
    
    struct sockaddr *addr = (struct sockaddr *)YMAddressGetAddressData(connection->address);
    socklen_t addrLen = YMAddressGetLength(connection->address);
    __unused struct sockaddr_in *addrAsIPV4 = (struct sockaddr_in *)addr;
    __unused struct sockaddr_in6 *addrAsIPV6 = (struct sockaddr_in6 *)addr;
    
<<<<<<< HEAD
//#if defined(RPI)
//	unsigned long rev = htonl(addrAsIPV4->sin_addr.s_addr);
//	memcpy(&addrAsIPV4->sin_addr.s_addr,&rev,sizeof(rev));
//#endif
    
=======
>>>>>>> 45f4df55d57067cbad2cd33d2681ccc1029fafb3
    result = connect(newSocket, addr, addrLen);
    if ( result != 0 )
    {
        ymerr("connection[%s]: error: connect(%s): %d (%s)",YM_CON_DESC,YMSTR(YMAddressGetDescription(connection->address)),errno,strerror(errno));
		int error; const char *errorStr;
        YM_CLOSE_SOCKET(newSocket);
        return false;
    }
    
    ymlog("connection[%s]: connected",YM_CON_DESC);
    
    bool commonInitOK = __YMConnectionInitCommon(connection, newSocket, false);
    if ( ! commonInitOK )
        return false;
    
    return true;
}

bool __YMConnectionInitCommon(__YMConnectionRef connection, YMSOCKET newSocket, bool asServer)
{
    YMSecurityProviderRef security = NULL;
    YMPlexerRef plexer = NULL;
    
    switch( connection->securityType )
    {
        case YMInsecure:
            security = YMSecurityProviderCreateWithSocket(newSocket);
            break;
        case YMTLS:
            security = (YMSecurityProviderRef)YMTLSProviderCreateWithSocket(newSocket, asServer);
            break;
        default:
            ymerr("connection[%s]: unknown security type",YM_CON_DESC);
            goto rewind_fail;
    }
    
    bool securityOK = YMSecurityProviderInit(security);
    if ( ! securityOK )
    {
        ymerr("connection[%s]: security type %d failed to initialize",YM_CON_DESC,connection->securityType);
        goto rewind_fail;
    }
    
    plexer = YMPlexerCreate(YMAddressGetDescription(connection->address), security, asServer);
	YMPlexerSetNewIncomingStreamFunc(plexer, ym_connection_new_stream_proc);
	YMPlexerSetInterruptedFunc(plexer, ym_connection_interrupted_proc);
	YMPlexerSetStreamClosingFunc(plexer, ym_connection_stream_closing_proc);
	YMPlexerSetCallbackContext(plexer, connection);

    bool plexerOK = YMPlexerStart(plexer);
    if ( ! plexerOK )
    {
        ymerr("connection[%s]: plexer failed to initialize",YM_CON_DESC);
        goto rewind_fail;
    }
    
    connection->plexer = plexer;
    connection->socket = newSocket;
    
    YMRelease(security);
    return true;
    
rewind_fail:
    if ( security )
        YMRelease(security);
    if ( plexer )
        YMRelease(plexer);
    return false;
}

bool YMConnectionClose(YMConnectionRef connection_)
{
    __YMConnectionRef connection = (__YMConnectionRef)connection_;
    return __YMConnectionDestroy(connection, true);
}

bool __YMConnectionDestroy(__YMConnectionRef connection, bool explicit)
{
    YM_IO_BOILERPLATE
    
    bool okay = true;
    if ( connection->plexer )
    {
        bool plexerOK = YMPlexerStop(connection->plexer);
        if ( ! plexerOK )
        {
            ymerr("connection[%s]: warning: failed to close plexer",YM_CON_DESC);
            okay = plexerOK;
        }
        
        YMRelease(connection->plexer);
        connection->plexer = NULL;
    }
    
    if ( explicit && connection->socket != NULL_SOCKET )
    {
        YM_CLOSE_SOCKET(connection->socket);
        ymassert(result==0,"connection explicit media close: %d: %d %s",connection->socket,error,errorStr);
    }
    
    connection->socket = NULL_SOCKET;
    
    return okay;
}

uint64_t YMConnectionDoSample(YMConnectionRef connection)
{
    return (uint64_t)connection; // todo
}

YMAddressRef YMConnectionGetAddress(YMConnectionRef connection_)
{
    __YMConnectionRef connection = (__YMConnectionRef)connection_;
    return connection->address;
}

YMStreamRef YMConnectionCreateStream(YMConnectionRef connection_, YMStringRef name)
{
    __YMConnectionRef connection = (__YMConnectionRef)connection_;
    
    if ( NOT_CONNECTED )
        return NULL;
    
    return YMPlexerCreateStream(connection->plexer, name);
}

void YMConnectionCloseStream(YMConnectionRef connection_, YMStreamRef stream)
{
    __YMConnectionRef connection = (__YMConnectionRef)connection_;
    YMPlexerCloseStream(connection->plexer, stream);
}

typedef struct __ym_connection_forward_callback_t
{
    YMConnectionRef connection;
    YMStreamRef stream;
    YMFILE file;
    bool fileToStream;
    bool bounded;
    ym_connection_forward_context_t *userContext;
} __ym_connection_forward_callback_t;

bool YMConnectionForwardFile(YMConnectionRef connection, YMFILE fromFile, YMStreamRef toStream, const uint64_t *nBytesPtr, bool sync, ym_connection_forward_context_t *callbackInfo)
{
    return __YMConnectionForward(connection, false, toStream, fromFile, nBytesPtr, sync, callbackInfo);
}

bool YMConnectionForwardStream(YMConnectionRef connection, YMStreamRef fromStream, YMFILE toFile, const uint64_t *nBytesPtr, bool sync, ym_connection_forward_context_t *callbackInfo)
{
    return __YMConnectionForward(connection, true, fromStream, toFile, nBytesPtr, sync, callbackInfo);
}

bool __YMConnectionForward(YMConnectionRef connection, bool toFile, YMStreamRef stream, YMFILE file, const uint64_t *nBytesPtr, bool sync, ym_connection_forward_context_t *callbackInfo)
{
    
    __ym_connection_forward_callback_t *myContext = NULL;
    _ym_thread_forward_file_context_ref threadContext = NULL;
    
    if ( ! sync )
    {
        myContext = YMALLOC(sizeof(struct __ym_connection_forward_callback_t));
        myContext->connection = YMRetain(connection);
        myContext->stream = YMRetain(stream);
        myContext->file = file;
        myContext->fileToStream = ! toFile;
        myContext->bounded = ( nBytesPtr != NULL );
        myContext->userContext = callbackInfo;
        
        threadContext = YMALLOC(sizeof(_ym_thread_forward_file_context_t));
        threadContext->callback = _ym_connection_forward_callback_proc;
        threadContext->context = myContext;
    }
    
    bool ret;
    if ( toFile )
        ret = YMThreadDispatchForwardStream(stream, file, nBytesPtr, sync, threadContext);
    else
        ret = YMThreadDispatchForwardFile(file, stream, nBytesPtr, sync, threadContext);
    
    return ret;
}

void _ym_connection_forward_callback_proc(void *context, YMIOResult result, uint64_t bytesForwarded)
{
    __ym_connection_forward_callback_t *myContext = (__ym_connection_forward_callback_t *)context;
    YMConnectionRef connection = myContext->connection;
    YMStreamRef stream = myContext->stream;
    
    // allow user to async-forward, if they don't specify callback info it implies "close stream for me when done"
    if ( ! myContext->userContext )
    {
        ymerr("connection[%s]: automatically closing stream %p after async forward",YM_CON_DESC,myContext->stream);
        YMConnectionCloseStream(connection, stream);
    }
    else
    {
        if ( myContext->userContext->callback )
        {
            myContext->userContext->callback(connection, stream, result, bytesForwarded, myContext->userContext->context);
        }
    }
    
    YMRelease(connection);
    YMRelease(stream);
    free(myContext->userContext);
    free(myContext);
}

void ym_connection_new_stream_proc(__unused YMPlexerRef plexer,YMStreamRef stream, void *context)
{
    __YMConnectionRef connection = (__YMConnectionRef)context;
    connection->newFunc(connection, stream, connection->newFuncContext);
}

void ym_connection_stream_closing_proc(__unused YMPlexerRef plexer, YMStreamRef stream, void *context)
{
    __YMConnectionRef connection = (__YMConnectionRef)context;
    connection->closingFunc(connection, stream, connection->closingFuncContext);
}

void ym_connection_interrupted_proc(__unused YMPlexerRef plexer, void *context)
{
    __YMConnectionRef connection = (__YMConnectionRef)context;
    connection->interruptedFunc(connection, connection->interruptedFuncContext);
}

YM_EXTERN_C_POP
