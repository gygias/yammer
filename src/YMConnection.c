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

#include "YMStreamPriv.h"

#define ymlog_type YMLogConnection
#define ymlog_pre "connection[%s]: "
#define ymlog_args (connection->address ? YMSTR(YMAddressGetDescription(connection->address)) : "*")
#include "YMLog.h"

#if !defined(YMWIN32)
# include <sys/socket.h>
# include <arpa/inet.h>
#else
# include <winsock2.h>
# include <ws2tcpip.h>
# include <time.h>
#endif

#define NOT_CONNECTED ( ( connection->socket == NULL_SOCKET ) && ! connection->isConnected )

typedef enum {
    YMIFAP_IPV4 = 1,
    YMIFAP_IPV6 = 2,
    YMIFAP_Unknown = -1
} YMIFAPType;

typedef enum {
    YMConnectionCommandIFExchange = -1,
    YMConnectionCommandSample = -2,
    YMConnectionCommandInit = INT32_MIN
} __YMConnectionCommand;

typedef struct __ym_connection_command {
    __YMConnectionCommand command;
    uint32_t userInfo;
} __ym_connection_command;

YM_EXTERN_C_PUSH

typedef struct __ym_connection_t
{
    _YMType _type;
    
	YMSOCKET socket;
    bool isIncoming;
    YMIFAPType apType;
    YMStringRef localIFName;
    YMInterfaceType localIFType;
    YMStringRef remoteIFName;
    YMInterfaceType remoteIFType;
    const void *remoteAddr;
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
    int64_t sample;
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

__YMConnectionRef __YMConnectionCreate(bool isIncoming, YMAddressRef peerAddress, YMConnectionType type, YMConnectionSecurityType securityType, bool closeWhenDone);
bool __YMConnectionDestroy(__YMConnectionRef connection, bool explicit);
int64_t __YMConnectionDoSample(__YMConnectionRef connection, YMSOCKET socket, uint32_t length, bool asServer);
bool __YMConnectionDoIFExchange(__unused __YMConnectionRef connection, YMSOCKET socket, bool asServer);
bool __YMConnectionInitCommon(__YMConnectionRef connection, YMSOCKET newSocket, bool asServer);

bool __YMConnectionForward(YMConnectionRef connection, bool toFile, YMStreamRef stream, YMFILE file, const uint64_t *nBytesPtr, bool sync, ym_connection_forward_context_t*);
void _ym_connection_forward_callback_proc(void *context, YMIOResult result, uint64_t bytesForwarded);

YMConnectionRef YMConnectionCreate(YMAddressRef peerAddress, YMConnectionType type, YMConnectionSecurityType securityType, bool closeWhenDone)
{
    __YMConnectionRef c = __YMConnectionCreate(false, peerAddress, type, securityType, closeWhenDone);
    c->socket = NULL_SOCKET;
    return c;
}

YMConnectionRef YMConnectionCreateIncoming(YMSOCKET socket, YMAddressRef peerAddress, YMConnectionType type, YMConnectionSecurityType securityType, bool closeWhenDone)
{
    __YMConnectionRef connection = __YMConnectionCreate(true, peerAddress, type, securityType, closeWhenDone);
    bool commonInitOK = __YMConnectionInitCommon(connection, socket, true);
    if ( ! commonInitOK ) {
        ymlog("server init failed");
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
    
    // remote gets set during 'initialization' later
    connection->remoteIFType = YMInterfaceUnknown;
    connection->remoteIFName = NULL;
    connection->remoteAddr = NULL;
    
    return connection;
}

void _YMConnectionFree(YMTypeRef object)
{
    __YMConnectionRef connection = (__YMConnectionRef)object;
    __YMConnectionDestroy(connection, true); // frees security and plexer
    YMRelease(connection->address);
    if ( connection->localIFName )
        YMRelease(connection->localIFName);
    if ( connection->remoteIFName )
        YMRelease(connection->remoteIFName);
    if ( connection->remoteAddr )
        free((void *)connection->remoteAddr);
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
    
    YM_IO_BOILERPLATE
    
    if ( connection->socket != NULL_SOCKET || connection->isIncoming ) {
        ymerr("connect called on connected socket");
        return false;
    }    
    
    int type;
    switch(connection->type) {
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
#if !defined(YMWIN32)
    ymassert(newSocket>=0,"socket failed: %d (%s)",errno,strerror(errno));
#else
	ymassert(newSocket!=INVALID_SOCKET,"socket failed: %x",GetLastError());
#endif
    
    int yes = 1;
    result = setsockopt(newSocket, SOL_SOCKET, SO_REUSEADDR, (const void *)&yes, sizeof(yes));
    if ( result != 0 ) ymerr("warning: setsockopt(reuse) failed on f%d: %d: %d (%s)",newSocket,result,errno,strerror(errno));
    result = setsockopt(newSocket, SOL_SOCKET, SO_DONTROUTE, (const void *)&yes, sizeof(yes));
    if ( result != 0 ) ymerr("warning: setsockopt(dontroute) failed on f%d: %d: %d (%s)",newSocket,result,errno,strerror(errno));
    
    ymlog("connecting...");
    
    struct sockaddr *addr = (struct sockaddr *)YMAddressGetAddressData(connection->address);
    socklen_t addrLen = YMAddressGetLength(connection->address);
    __unused struct sockaddr_in *addrAsIPV4 = (struct sockaddr_in *)addr;
    __unused struct sockaddr_in6 *addrAsIPV6 = (struct sockaddr_in6 *)addr;
    
    result = connect(newSocket, addr, addrLen);
    if ( result != 0 ) {
        ymerr("connect(%s): %d (%s)",YMSTR(YMAddressGetDescription(connection->address)),errno,strerror(errno));
        YM_CLOSE_SOCKET(newSocket);
        return false;
    }
    
    ymlog("connected");
    connection->socket = newSocket;
    
    return true;
}

bool YMAPI YMConnectionInit(YMConnectionRef connection)
{
    return __YMConnectionInitCommon((__YMConnectionRef)connection, connection->socket, false);
}

#define YMIFExNameMax 64 // 36-char windows 'guid' representation longest known platform 'interface name'
typedef struct YMIFExchangePrefix {
    YMIFAPType apType;
    YMInterfaceType ifType;
    char ifName[YMIFExNameMax];
} YMIFExchangePrefix;

bool __YMConnectionDoIFExchange(__YMConnectionRef connection, YMSOCKET socket, bool asServer)
{
    YM_IO_BOILERPLATE
    
    const char *whyFailed = NULL;
    YMIFExchangePrefix theirPrefix = { YMIFAP_Unknown, YMInterfaceUnknown, {0} };
    YMIFExchangePrefix myPrefix = { YMIFAP_Unknown, connection->localIFType, {0} };
    strncat((char *)&(myPrefix.ifName),YMSTR(connection->localIFName),YMIFExNameMax);
    
    const struct sockaddr *mySockaddr = YMAddressGetAddressData(connection->address);
    if ( mySockaddr->sa_family == AF_INET )
        myPrefix.apType = YMIFAP_IPV4;
    else if ( mySockaddr->sa_family == AF_INET6 )
        myPrefix.apType = YMIFAP_IPV6;
    
    bool okay = true;
    
    for( int i = 0; i < 2; i++ ) {
        if ( (( i == 0 ) && asServer) || (( i == 1 ) && ! asServer) ) {
            okay = YMWriteFull(socket, (uint8_t *)&myPrefix, sizeof(myPrefix), NULL);
            if ( ! okay ) { whyFailed = "write prefix"; goto catch_return; }
            
            // todo implemented localhost<->localhost does endianness matter streaming this?
            if ( mySockaddr->sa_family == AF_INET ) {
                okay = YMWriteFull(socket,(uint8_t *)&((struct sockaddr_in *)mySockaddr)->sin_addr.s_addr, sizeof(in_addr_t), NULL);
                if ( ! okay ) { whyFailed = "write ipv4 addr"; goto catch_return; }
                okay = YMWriteFull(socket,(uint8_t *)&((struct sockaddr_in *)mySockaddr)->sin_port, sizeof(in_port_t), NULL);
                if ( ! okay ) { whyFailed = "write ipv6 port"; goto catch_return; }
            } else if ( mySockaddr->sa_family == AF_INET6 ) {
                okay = YMWriteFull(socket,(uint8_t *)&((struct sockaddr_in6 *)mySockaddr)->sin6_addr,sizeof(struct in6_addr), NULL);
                if ( ! okay ) { whyFailed = "write ipv6 addr"; goto catch_return; }
                okay = YMWriteFull(socket,(uint8_t *)&((struct sockaddr_in6 *)mySockaddr)->sin6_port,sizeof(in_port_t), NULL);
                if ( ! okay ) { whyFailed = "write ipv6 port"; goto catch_return; }
            }
        }
        
        if ( (( i == 1 ) && asServer) || (( i == 0 ) && ! asServer) ) {
            okay = YMReadFull(socket, (uint8_t *)&theirPrefix, sizeof(theirPrefix), NULL);
            if ( ! okay )  { whyFailed = "read prefix"; goto catch_return; }
            
            int32_t addrLen = 0;
            switch(theirPrefix.apType) {
                case YMIFAP_IPV4:
                    addrLen = 4 + 2;
                    break;
                case YMIFAP_IPV6:
                    addrLen = 16 + 2;
                    break;
                default:
                    okay = false;
                    whyFailed = "unknown ap type on if exchange";
                    goto catch_return;
            }
            
            connection->apType = theirPrefix.apType;
            connection->remoteAddr = YMALLOC(addrLen);
            okay = YMReadFull(socket, (void *)connection->remoteAddr, addrLen, NULL);
            if ( ! okay ) { whyFailed = "read addr"; goto catch_return; }
        }
        
    }
    
    connection->remoteIFName = YMStringCreateWithCString(theirPrefix.ifName);
    connection->remoteIFType = theirPrefix.ifType;
    
catch_return:;
    
    if ( ! okay )
        ymerr("if exchange: %s", whyFailed?whyFailed:"?");
        
    return okay;
}

int64_t __YMConnectionDoSample(__unused __YMConnectionRef connection, YMSOCKET socket, uint32_t length, bool asServer)
{
    YM_IO_BOILERPLATE
    
    uint32_t sample = -1;
    
    int remain = length % 4;
    if ( remain != 0 ) {
        ymerr("warning: sample length not word-aligned, snapping to next word length: %u",length);
        length += remain;
    }
    
    time_t startTime = time(NULL);
    
    uint32_t halfLength = length / 2;
    for( int i = 0; i < 2; i++ ) {
        bool writing = ( i == 0 ) ^ !asServer;
        uint32_t sentReceived = 0;
        
        while ( sentReceived < halfLength ) {
            uint32_t random;
            ssize_t toReadWrite = ( sizeof(random) + sentReceived > halfLength ) ? ( halfLength - sentReceived ) : sizeof(random);
            if ( writing ) {
                random = arc4random();
                YM_WRITE_SOCKET(socket, (const char *)&random, (size_t)toReadWrite);
                if ( aWrite != toReadWrite ) return sample;
                sentReceived += aWrite;
            } else {
                YM_READ_SOCKET(socket, (char *)&random, (size_t)toReadWrite);
                if ( aRead != toReadWrite ) return sample;
                sentReceived += aRead;
            }
        }
        ymlog("%s back sample",writing?"reading":"writing");
    }
    
    sample = (uint32_t)(length / ( time(NULL) - startTime ));
    ymlog("approximated sample to %ub/s",sample);
    
    return sample;
}

bool __YMConnectionInitCommon(__YMConnectionRef connection, YMSOCKET newSocket, bool asServer)
{
    YM_IO_BOILERPLATE
    
    YMSecurityProviderRef security = NULL;
    YMPlexerRef plexer = NULL;
    struct __ym_connection_command command;
    bool conCmdOkay = true;
    
    // determine local interface
#if defined(YMAPPLE) || defined(YMWIN32)
    struct sockaddr_in6 saddr;
    socklen_t slen = sizeof(saddr);
    bool matched = false;
    result = getsockname(newSocket, (struct sockaddr *)&saddr, &slen);
    YMAddressRef localAddr = YMAddressCreate(&saddr, 0);
    if ( result == 0 ) {
        YMDictionaryRef localIFMap = YMInterfaceMapCreateLocal();
        if ( localIFMap ) {
            YMDictionaryEnumRef denum = YMDictionaryEnumeratorBegin(localIFMap);
            while ( denum ) {
                YMStringRef ifName = (YMStringRef)denum->key;
                YMDictionaryRef ifInfo = (YMDictionaryRef)denum->value;
                YMArrayRef ifAddrs = (YMArrayRef)YMDictionaryGetItem(ifInfo, kYMIFMapAddressesKey);
                for ( int i = 0; i < YMArrayGetCount(ifAddrs); i++ ) {
                    YMAddressRef aLocalAddress = (YMAddressRef)YMArrayGet(ifAddrs, i);
                    ymdbg("%s: %s ?= %s",YMSTR(ifName),YMSTR(YMAddressGetDescription(localAddr)),YMSTR(YMAddressGetDescription(aLocalAddress)));
                    if ( YMAddressIsEqualIncludingPort(localAddr, aLocalAddress, false) ) {
                        connection->localIFName = YMRetain(ifName);
                        connection->localIFType = YMInterfaceTypeForName(ifName);
                        ymlog("allocated %s (%s)",YMSTR(ifName),YMInterfaceTypeDescription(connection->localIFType));
                        matched = true;
                        break;
                    }
                }
                if ( matched )
                    break;
                denum = YMDictionaryEnumeratorGetNext(denum);
            }
            YMDictionaryEnumeratorEnd(denum);
            
            YMRelease(localIFMap);
        }
    }
    if ( result == -1 || ! matched ) {
        connection->localIFName = YMSTRC("?");
        connection->localIFType = YMInterfaceUnknown;
    }
#else
#error implement me, getsockname is a BSD thing
#endif
    
    if ( asServer ) {
        // if ( clientWantsSamplingFastestEtc )
#define THIRTY_TWO_MEGABYTES 33554432
#define SIXTEEN_MEGABYTES ( THIRTY_TWO_MEGABYTES / 2 )
        for ( int i = 0; i < 3; i++ ) {
            uint32_t sampleSize = SIXTEEN_MEGABYTES;
            if      ( i == 0 )  { command.command = YMConnectionCommandIFExchange; command.userInfo = 0; }
            else if ( i == 1 )  { command.command = YMConnectionCommandSample; command.userInfo = sampleSize; }
            else                { command.command = YMConnectionCommandInit; command.userInfo = 0; }
            
            YM_WRITE_SOCKET(newSocket, (const char *)&command, sizeof(command));
            if ( aWrite != sizeof(command) ) {
                ymerr("connection failed to initialize: %d %d %s",i,error,errorStr);
                YM_CLOSE_SOCKET(newSocket);
                return false;
            }
            
            if ( command.command == YMConnectionCommandIFExchange ) {
                ymlog("performing ifinfo exchange");
                conCmdOkay = __YMConnectionDoIFExchange(connection, newSocket, true);
            } else if ( command.command == YMConnectionCommandSample ) {
                ymlog("performing sample of length %ub",sampleSize);
                int64_t sample = __YMConnectionDoSample(connection, newSocket, sampleSize, true);
                if ( sample >= 0 ) {
                    conCmdOkay = true;
                    connection->sample = sample;
                    YM_DEBUG_SAMPLE
                }
                YM_DEBUG_SAMPLE
            }
            
            if ( ! conCmdOkay ) {
                ymerr("connection command failed");
                YM_CLOSE_SOCKET(newSocket);
                return false;
            }
        }
        
        
    } else {
        while(1) {
            YM_READ_SOCKET(newSocket, (char *)&command, sizeof(command));
            if ( aRead != sizeof(command) ) {
                ymerr("connection failed to initialize: %d %s",error,errorStr);
                YM_CLOSE_SOCKET(newSocket);
                return false;
            }
            
            if ( command.command == YMConnectionCommandSample ) {
                ymlog("performing sample of length %ub",command.userInfo);
                int64_t sample = __YMConnectionDoSample(connection, newSocket, command.userInfo, false);
                if ( sample >= 0 ) {
                    conCmdOkay = true;
                    connection->sample = sample;
                    YM_DEBUG_SAMPLE
                }
            } else if ( command.command == YMConnectionCommandIFExchange ) {
                ymlog("performing ifinfo exchange");
                conCmdOkay = __YMConnectionDoIFExchange(connection, newSocket, false);
            } else if ( command.command == YMConnectionCommandInit ) {
                ymlog("init command received, proceeding");
                break;
            } else {
                ymerr("unknown initialization command: %d",command.command);
                YM_CLOSE_SOCKET(newSocket);
                return false;
            }
            
            if ( ! conCmdOkay ) {
                ymerr("connection command failed");
                YM_CLOSE_SOCKET(newSocket);
                return false;
            }
        }
    }
    
    switch( connection->securityType )
    {
        case YMInsecure:
            security = YMSecurityProviderCreateWithSocket(newSocket);
            break;
        case YMTLS:
            security = (YMSecurityProviderRef)YMTLSProviderCreateWithSocket(newSocket, asServer);
            break;
        default:
            ymerr("unknown security type");
            goto rewind_fail;
    }
    
    bool securityOK = YMSecurityProviderInit(security);
    if ( ! securityOK ) {
        ymerr("security type %d failed to initialize",connection->securityType);
        goto rewind_fail;
    }
    
    plexer = YMPlexerCreate(YMAddressGetDescription(connection->address), security, asServer);
	YMPlexerSetNewIncomingStreamFunc(plexer, ym_connection_new_stream_proc);
	YMPlexerSetInterruptedFunc(plexer, ym_connection_interrupted_proc);
	YMPlexerSetStreamClosingFunc(plexer, ym_connection_stream_closing_proc);
	YMPlexerSetCallbackContext(plexer, connection);

    bool plexerOK = YMPlexerStart(plexer);
    if ( ! plexerOK ) {
        ymerr("plexer failed to initialize");
        goto rewind_fail;
    }
    
    ymlog("new connection: %s (%s) <-> %s (%s)",YMSTR(connection->localIFName),
          YMInterfaceTypeDescription(connection->localIFType),
          YMSTR(connection->remoteIFName),
          YMInterfaceTypeDescription(connection->remoteIFType));
    
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
    if ( connection->plexer ) {
        bool plexerOK = YMPlexerStop(connection->plexer);
        if ( ! plexerOK ) {
            ymerr("warning: failed to close plexer");
            okay = plexerOK;
        }
        
        YMRelease(connection->plexer);
        connection->plexer = NULL;
    }
    
    if ( explicit && connection->socket != NULL_SOCKET ) {
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

YMStringRef YMAPI YMConnectionGetLocalInterfaceName(YMConnectionRef connection_)
{
    __YMConnectionRef connection = (__YMConnectionRef)connection_;
    return connection->localIFName;
}

YMInterfaceType YMAPI YMConnectionGetLocalInterface(YMConnectionRef connection_)
{
    __YMConnectionRef connection = (__YMConnectionRef)connection_;
    return connection->localIFType;
}

YMInterfaceType YMAPI YMConnectionGetRemoteInterface(YMConnectionRef connection_)
{
    __YMConnectionRef connection = (__YMConnectionRef)connection_;
    return connection->remoteIFType;
}

int64_t YMAPI YMConnectionGetSample(YMConnectionRef connection_)
{
    __YMConnectionRef connection = (__YMConnectionRef)connection_;
    return connection->sample;
}

YMAddressRef YMConnectionGetAddress(YMConnectionRef connection_)
{
    __YMConnectionRef connection = (__YMConnectionRef)connection_;
    return connection->address;
}

#define YMStreamInitBuiltinVersion 1
typedef struct _ymconnection_stream_init
{
    uint16_t version;
    YMCompressionType compressionType;
} _ymconnection_stream_init;
typedef struct _ymconnection_stream_init _ymconnection_stream_init_t;

YMStreamRef YMAPI YMConnectionCreateStream(YMConnectionRef connection_, YMStringRef name, YMCompressionType compression)
{
    __YMConnectionRef connection = (__YMConnectionRef)connection_;
    
    if ( NOT_CONNECTED )
        return NULL;
    
    YMStreamRef stream = YMPlexerCreateStream(connection->plexer, name);
    bool okay = _YMStreamSetCompression(stream,compression);
    ymassert(okay,"set compression for outgoing stream %s",YMSTR(name));
    
    _ymconnection_stream_init_t init = { YMStreamInitBuiltinVersion, compression };
    YMIOResult ymResult = YMStreamWriteDown(stream, (const uint8_t *)&init, sizeof(init));
    if ( ymResult != YMIOSuccess )
        ymerr("outgoing stream init failed");
    
    return stream;
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
    
    if ( ! sync ) {
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
    if ( ! myContext->userContext ) {
        ymerr("automatically closing stream %p after async forward",myContext->stream);
        YMConnectionCloseStream(connection, stream);
    }
    else if ( myContext->userContext->callback )
        myContext->userContext->callback(connection, stream, result, bytesForwarded, myContext->userContext->context);
    
    YMRelease(connection);
    YMRelease(stream);
    free(myContext->userContext);
    free(myContext);
}

void ym_connection_new_stream_proc(__unused YMPlexerRef plexer,YMStreamRef stream, void *context)
{
    __YMConnectionRef connection = (__YMConnectionRef)context;
    
    _ymconnection_stream_init_t init = { 0, 0 };
    uint16_t outLen = 0;
    YMIOResult ymResult = YMStreamReadUp(stream, (uint8_t *)&init, sizeof(init), &outLen);
    if ( ymResult != YMIOSuccess || outLen != sizeof(init) )
        ymerr("incoming stream init failed");
    ymassert(init.version == YMStreamInitBuiltinVersion, "the installed version of yammer doesn't support stream init %u",init.version);
    bool okay = _YMStreamSetCompression(stream, init.compressionType);
    ymassert(okay,"set compression for outgoing stream");
    
    if ( connection->newFunc )
        connection->newFunc(connection, stream, connection->newFuncContext);
}

void ym_connection_stream_closing_proc(__unused YMPlexerRef plexer, YMStreamRef stream, void *context)
{
    __YMConnectionRef connection = (__YMConnectionRef)context;
    if ( connection->closingFunc )
        connection->closingFunc(connection, stream, connection->closingFuncContext);
}

void ym_connection_interrupted_proc(__unused YMPlexerRef plexer, void *context)
{
    __YMConnectionRef connection = (__YMConnectionRef)context;
    if ( connection->interruptedFunc )
        connection->interruptedFunc(connection, connection->interruptedFuncContext);
}

YM_EXTERN_C_POP
