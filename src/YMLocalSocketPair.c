//
//  YMLocalSocketPair.c
//  yammer
//
//  Created by david on 11/11/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMLocalSocketPair.h"

#include "YMThread.h"
#include "YMSemaphore.h"
#include "YMUtilities.h"

#include "YMLog.h"
#undef ymlog_type
#define ymlog_type YMLogIO
#if ( ymlog_type > ymlog_target )
#undef ymlog
#define ymlog(x,...) ;
#endif

#ifndef WIN32
#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#define PF_LOCAL PF_UNIX
#endif
#include <stddef.h> // offsetof

#if defined(RPI) // __USE_MISC /usr/include/arm-linux-gnueabihf/sys/un.h
# define SUN_LEN(ptr) ((size_t) (((struct sockaddr_un *) 0)->sun_path)	      \
		      + strlen ((ptr)->sun_path))
#endif

#ifndef WIN32 // todo? this is only used by the os x unit tests atm

typedef struct __ym_local_socket_pair
{
    _YMType _type;
    
    YMStringRef socketName;
    YMStringRef userName;
    int socketA;
    int socketB;
} ___ym_local_socket_pair;
typedef struct __ym_local_socket_pair __YMLocalSocketPair;
typedef __YMLocalSocketPair *__YMLocalSocketPairRef;

int __YMLocalSocketPairCreateClient();
void __ym_local_socket_accept_proc(void *);
void __YMLocalSocketPairInitOnce(void);

const char *__YMLocalSocketPairNameBase = "ym-lsock";
static YMThreadRef gYMLocalSocketPairAcceptThread = NULL;
static YMStringRef gYMLocalSocketPairName = NULL;
static YMSemaphoreRef gYMLocalSocketSemaphore = NULL; // thread ready & each 'accepted'
static bool gYMLocalSocketPairAcceptKeepListening = false;
static bool gYMLocalSocketThreadEnd = false; // sadly not quite the same as 'keep listening'
static int gYMLocalSocketListenSocket = -1;
static int gYMLocalSocketPairAcceptedLast = -1;

void YMLocalSocketPairStop()
{
    if ( gYMLocalSocketListenSocket >= 0 )
    {
        // flag & signal thread to exit
        gYMLocalSocketPairAcceptKeepListening = false;
        gYMLocalSocketThreadEnd = true;
        int result = close(gYMLocalSocketListenSocket);
        if ( result != 0 )
            ymerr("local-socket: warning: failed to close listen socket: %d (%s)",errno,strerror(errno));
        gYMLocalSocketListenSocket = -1;
        
        YMRelease(gYMLocalSocketSemaphore);
        gYMLocalSocketSemaphore = NULL;
        YMRelease(gYMLocalSocketPairAcceptThread);
        gYMLocalSocketPairAcceptThread = NULL;
        
#ifdef _MACOS
        YMStringRef tmpPath = YMSTRCF("/tmp/%s",YMSTR(gYMLocalSocketPairName));
        result = unlink(YMSTR(tmpPath));
        if ( result != 0 )
            ymerr("local-socket[%s]: failed to unlink socket file: %d %s",YMSTR(gYMLocalSocketPairName),errno,strerror(errno));
        YMRelease(tmpPath);
#endif
        YMRelease(gYMLocalSocketPairName);
        gYMLocalSocketPairName = NULL;
    }
}

void __YMLocalSocketPairInitOnce(void)
{
    YMStringRef name = YMSTRC("local-socket-spawn");
    gYMLocalSocketSemaphore = YMSemaphoreCreate(name, 0);
    gYMLocalSocketPairAcceptThread = YMThreadCreate(name, __ym_local_socket_accept_proc, NULL);
    YMRelease(name);
    
    if ( ! gYMLocalSocketPairAcceptThread || ! gYMLocalSocketSemaphore )
    {
        ymerr("local-socket: fatal: failed to spawn accept thread");
        abort();
    }
    
    gYMLocalSocketThreadEnd = false;
    bool okay = YMThreadStart(gYMLocalSocketPairAcceptThread);
    if ( ! okay )
    {
        ymerr("local-socket: fatal: failed to start accept thread");
        abort();
    }
    
    YMSemaphoreWait(gYMLocalSocketSemaphore);
}

YMLocalSocketPairRef YMLocalSocketPairCreate(YMStringRef name, bool moreComing)
{
    if ( ! name )
    {
        ymlog("local-socket[%s]: error: name is required",YMSTR(name));
        return NULL;
    }
    
    if ( ! gYMLocalSocketSemaphore )
        __YMLocalSocketPairInitOnce();

	YMNetworkingInit();
    
    // now that thread is going to accept [once more], flag this
    gYMLocalSocketPairAcceptKeepListening = moreComing;
    
    int clientSocket = __YMLocalSocketPairCreateClient();
    if ( clientSocket < 0 )
    {
        ymerr("local-socket[%s]: error: failed to create client socket",YMSTR(name));
        return NULL;
    }
    
    YMSemaphoreWait(gYMLocalSocketSemaphore); // wait for accept to happen and 'server' socket to get set
    
    int serverSocket = gYMLocalSocketPairAcceptedLast;
    if ( serverSocket < 0 )
    {
        ymerr("local-socket[%s]: error: failed to create server socket",YMSTR(name));
        return NULL;
    }
    
    __YMLocalSocketPairRef pair = (__YMLocalSocketPairRef)_YMAlloc(_YMLocalSocketPairTypeID,sizeof(__YMLocalSocketPair));
    
    pair->userName = YMStringCreateWithFormat("ls:%s:s%d<->c%d",YMSTR(name),serverSocket,clientSocket,NULL);
    pair->socketName = YMRetain(gYMLocalSocketPairName);
    pair->socketA = serverSocket;
    pair->socketB = clientSocket;
    
    if ( ! moreComing )
        YMLocalSocketPairStop(); // eat your own dogfood
    
    return pair;
}

int YMLocalSocketPairGetA(YMLocalSocketPairRef pair_)
{
    __YMLocalSocketPairRef pair = (__YMLocalSocketPairRef)pair_;
    return pair->socketA;
}
int YMLocalSocketPairGetB(YMLocalSocketPairRef pair_)
{
    __YMLocalSocketPairRef pair = (__YMLocalSocketPairRef)pair_;
    return pair->socketB;
}

void _YMLocalSocketPairFree(YMTypeRef object)
{
    __YMLocalSocketPairRef pair = (__YMLocalSocketPairRef)object;
    
    int result = close(pair->socketA);
    if ( result != 0 )
    {
        ymerr("local-socket[%s]: close failed (%d): %d (%s)",YMSTR(pair->userName),result,errno,strerror(errno));
        abort();
    }
    result = close(pair->socketB);
    if ( result != 0 )
    {
        ymerr("local-socket[%s]: close failed (%d): %d (%s)",YMSTR(pair->userName),result,errno,strerror(errno));
        abort();
    }
    
    YMRelease(pair->userName);
    YMRelease(pair->socketName);
}

// lifted from http://www.gnu.org/software/libc/manual/html_node/Local-Socket-Example.html
int __YMLocalSocketPairCreateClient()
{
    int sock = socket(PF_LOCAL, SOCK_STREAM, PF_UNSPEC/* IP, /etc/sockets man 5 protocols*/);
    if ( sock < 0 )
    {
        ymerr("local-socket[new-client]: socket failed: %d (%s)",errno,strerror(errno));
        return -1;
    }
    
    int yes = 1;
    int result = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    if ( result != 0 )
        ymerr("local-socket[new-client]: warning: setsockopt failed on %d: %d: %d (%s)",sock,result,errno,strerror(errno));
    
    /* Bind a name to the socket. */
    struct sockaddr_un sockName;
    sockName.sun_family = AF_LOCAL;
    strncpy (sockName.sun_path, YMSTR(gYMLocalSocketPairName), sizeof (sockName.sun_path));
    sockName.sun_path[sizeof (sockName.sun_path) - 1] = '\0';
    socklen_t size = (socklen_t)SUN_LEN(&sockName);
    
    result = connect(sock, (struct sockaddr *) &sockName, size);
    if ( result != 0 )
    {
        ymerr("local-socket[new-client]: connect failed: %d: %d (%s)",result,errno,strerror(errno));
        close(sock);
        return -1;
    }
    
    ymlog("local-socket[new-client]: connected: %d",sock);
    
    return sock;
}

void __ym_local_socket_accept_proc(__unused void *ctx)
{
    ymlog("__ym_local_socket_accept_proc entered");
    
    int listenSocket = -1;
    YMStringRef socketName = NULL;
    
    uint16_t nameSuffixIter = 0;
        
    name_retry:;
        
    if ( nameSuffixIter == UINT16_MAX )
    {
        ymerr("local-socket[spawn-server]: fatal: unable to choose available name");
        abort();
    }
    
    if ( socketName )
        YMRelease(socketName);
    
    socketName = YMStringCreateWithFormat("%s:%u",__YMLocalSocketPairNameBase,nameSuffixIter,NULL);
    
    listenSocket = socket(PF_LOCAL, SOCK_STREAM, PF_UNSPEC /* /etc/sockets man 5 protocols*/);
    if ( listenSocket < 0 )
    {
        ymerr("local-socket[spawn-server]: fatal: socket failed (listen): %d (%s)",errno,strerror(errno));
        abort();
    }
    
    int yes = 1;
    int aResult = setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    if ( aResult != 0 )
        ymerr("local-socket[spawn-server]: warning: setsockopt failed on %d: %d: %d (%s)",listenSocket,aResult,errno,strerror(errno));
    
    /* Bind a name to the socket. */
    struct sockaddr_un sockName;
    sockName.sun_family = AF_LOCAL;
    strncpy (sockName.sun_path, YMSTR(socketName), sizeof (sockName.sun_path));
    sockName.sun_path[sizeof (sockName.sun_path) - 1] = '\0'; // ensure null termination if name is longer than (104)
    socklen_t size = (socklen_t) SUN_LEN(&sockName);
    
    aResult = bind (listenSocket, (struct sockaddr *) &sockName, size);
    if ( aResult != 0 )
    {
        int bindErrno = errno;
        close(listenSocket);
        if ( bindErrno == EADDRINUSE )
        {
            ymerr("local-socket[spawn-server]: %s in use, retrying",YMSTR(socketName));
            nameSuffixIter++;
            goto name_retry;
        }
        ymerr("local-socket[spawn-server]: fatal: bind failed: %d (%s)",errno,strerror(errno));
        abort();
    }
    
    aResult = listen(listenSocket, 1);
    if ( aResult != 0 )
    {
        ymerr("local-socket[spawn-server]: listen failed: %d (%s)",errno,strerror(errno));
        close(listenSocket);
        abort();
    }
    
    gYMLocalSocketListenSocket = listenSocket;
    gYMLocalSocketPairName = socketName;
    ymlog("local-socket[spawn-server]: listening on %s:%d",YMSTR(socketName),listenSocket);
    YMSemaphoreSignal(gYMLocalSocketSemaphore); // signal InitOnce
    
    do
    {
        ymlog("__ym_local_socket_accept_proc accepting...");
        
        int newClient = accept(listenSocket, (struct sockaddr *) &sockName, &size);
        if ( newClient < 0 )
        {
            if ( ! gYMLocalSocketThreadEnd )
                ymerr("local-socket[spawn]: error: accept failed: %d: %d (%s)",newClient,errno,strerror(errno));
            gYMLocalSocketPairAcceptedLast = -1;
            break;
        }
        else
            gYMLocalSocketPairAcceptedLast = newClient;
        
        ymlog("local-socket[spawn-server]: accepted: %d",newClient);
        
        YMSemaphoreSignal(gYMLocalSocketSemaphore);
        
        // check flag again in case 'global stop' thing got called on a persistent accept thread
    } while ( gYMLocalSocketPairAcceptKeepListening );
    
    ymlog("__ym_local_socket_accept_proc exiting");
}

#else // not WIN32
void _YMLocalSocketPairFree(YMTypeRef object) {}
#endif
