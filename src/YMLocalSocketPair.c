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

#include "YMLog.h"
#undef ymlog_type
#define ymlog_type YMLogIO
#if ( ymlog_type > ymlog_target )
#undef ymlog
#define ymlog(x,...) ;
#endif

#include <sys/socket.h>
#include <sys/un.h>
#include <stddef.h> // offsetof
#include <pthread.h>

typedef struct __ym_local_socket_pair
{
    _YMType _type;
    
    YMStringRef name;
    int socketA;
    int socketB;
} ___ym_local_socket_pair;
typedef struct __ym_local_socket_pair __YMLocalSocketPair;
typedef __YMLocalSocketPair *__YMLocalSocketPairRef;

int __YMLocalSocketPairCreateClient();
typedef struct __ym_local_socket_pair_thread_context_def
{
    YMSemaphoreRef semaphore;
} _ym_local_socket_pair_thread_context_def;
typedef struct __ym_local_socket_pair_thread_context_def *__ym_local_socket_pair_thread_context;
void __ym_local_socket_accept_proc(void *ctx);
void __YMLocalSocketPairInitOnce(void);

const char *__YMLocalSocketPairNameBase = "ym-local-socket";
static YMThreadRef gYMLocalSocketPairAcceptThread = NULL;
static pthread_once_t gYMLocalSocketPairAcceptThreadOnce = PTHREAD_ONCE_INIT;
static YMStringRef gYMLocalSocketPairName = NULL;
static YMSemaphoreRef gYMLocalSocketPairDidAcceptSemaphore = NULL;
static bool gYMLocalSocketPairAcceptStopFlag = false;
static int gYMLocalSocketPairAcceptLast = -1;

YMLocalSocketPairRef YMLocalSocketPairCreate(YMStringRef name)
{
    if ( ! name )
    {
        ymlog("local-socket[%s]: error: name is required",YMSTR(name));
        return NULL;
    }
    
    pthread_once(&gYMLocalSocketPairAcceptThreadOnce, __YMLocalSocketPairInitOnce);
    
    int clientSocket = __YMLocalSocketPairCreateClient(name);
    if ( clientSocket < 0 )
    {
        ymerr("local-socket[%s]: error: failed to create client socket",YMSTR(name));
        return NULL;
    }
    
    YMSemaphoreWait(gYMLocalSocketPairDidAcceptSemaphore);
    
    int serverSocket = gYMLocalSocketPairAcceptLast;
    if ( serverSocket < 0 )
    {
        ymerr("local-socket[%s]: error: failed to create server socket",YMSTR(name));
        return NULL;
    }
    
    __YMLocalSocketPairRef pair = (__YMLocalSocketPairRef)_YMAlloc(_YMLocalSocketPairTypeID,sizeof(__YMLocalSocketPair));
    
    YMStringRef myName = YMStringCreateWithFormat("ls:s%d<->c%d:%s",serverSocket,clientSocket,YMSTR(name),NULL);
    pair->name = myName;
    YMRelease(myName);
    
    pair->socketA = serverSocket;
    pair->socketB = clientSocket;
    
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
    YMRelease(pair->name);
    int result = close(pair->socketA);
    if ( result != 0 )
    {
        ymerr("local-socket[%s]: close failed (%d): %d (%s)",YMSTR(pair->name),result,errno,strerror(errno));
        abort();
    }
    result = close(pair->socketB);
    if ( result != 0 )
    {
        ymerr("local-socket[%s]: close failed (%d): %d (%s)",YMSTR(pair->name),result,errno,strerror(errno));
        abort();
    }
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
    
    /* Bind a name to the socket. */
    struct sockaddr_un sockName;
    sockName.sun_family = AF_LOCAL;
    strncpy (sockName.sun_path, gYMLocalSocketPairName, sizeof (sockName.sun_path));
    sockName.sun_path[sizeof (sockName.sun_path) - 1] = '\0';
    socklen_t size = (socklen_t)SUN_LEN(&sockName);
    
    int result = connect(sock, (struct sockaddr *) &sockName, size);
    if ( result != 0 )
    {
        ymerr("local-socket[new-client]: connect failed: %d: %d (%s)",result,errno,strerror(errno));
        close(sock);
        return -1;
    }
    
    ymlog("local-socket[new-client]: connected: %d",sock);
    
    return sock;
}

void __YMLocalSocketPairInitOnce(void)
{
    YMSemaphoreRef waitForThreadSemaphore = YMSemaphoreCreate("local-socket-listening", 0);
    gYMLocalSocketPairDidAcceptSemaphore = YMSemaphoreCreate("local-socket-did-accept", 0);
    struct __ym_local_socket_pair_thread_context_def context;
    context.semaphore = waitForThreadSemaphore;
    gYMLocalSocketPairAcceptThread = YMThreadCreate("local-socket-accept", __ym_local_socket_accept_proc, &context);
    if ( ! gYMLocalSocketPairAcceptThread )
    {
        ymerr("local-socket: fatal: failed to spawn accept thread");
        abort();
    }
    bool okay = YMThreadStart(gYMLocalSocketPairAcceptThread);
    if ( ! okay )
    {
        ymerr("local-socket: fatal: failed to start accept thread");
        abort();
    }
    
    YMSemaphoreWait(waitForThreadSemaphore);
    YMRelease(waitForThreadSemaphore);
}

void __ym_local_socket_accept_proc(void *ctx)
{
    ymlog("__ym_local_socket_accept_proc entered");
    __ym_local_socket_pair_thread_context context = (__ym_local_socket_pair_thread_context)ctx;
    
    uint16_t nameSuffixIter = 0;
close_retry:;
    if ( nameSuffixIter == UINT16_MAX )
    {
        ymerr("local-socket[spawn]: fatal: unable to choose available name");
        abort();
    }
    YMStringRef tryName = NULL;
    if ( tryName )
        YMRelease(tryName);
    tryName = YMStringCreateWithFormat("%s:%u",__YMLocalSocketPairNameBase,nameSuffixIter,NULL);
    
    int listenSocket = socket(PF_LOCAL, SOCK_STREAM, PF_UNSPEC /* /etc/sockets man 5 protocols*/);
    if ( listenSocket < 0 )
    {
        ymerr("local-socket[spawn]: fatal: socket failed (listen): %d (%s)",errno,strerror(errno));
        abort();
    }
    
    int yes = 1;
    int aResult = setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    if ( aResult != 0 )
    {
        ymerr("local-socket[spawn]: fatal: setsockopt failed: %d: %d (%s)",aResult,errno,strerror(errno));
        abort();
    }
    
    /* Bind a name to the socket. */
    struct sockaddr_un sockName;
    sockName.sun_family = AF_LOCAL;
    strncpy (sockName.sun_path, tryName, sizeof (sockName.sun_path));
    sockName.sun_path[sizeof (sockName.sun_path) - 1] = '\0'; // ensure null termination if name is longer than (104)
    socklen_t size = (socklen_t) SUN_LEN(&sockName);
    
    aResult = bind (listenSocket, (struct sockaddr *) &sockName, size);
    if ( aResult != 0 )
    {
        int bindErrno = errno;
        close(listenSocket);
        if ( bindErrno == EADDRINUSE )
        {
            ymerr("local-socket[spawn]: %s in use, retrying",YMSTR(tryName));
            nameSuffixIter++;
            goto close_retry;
        }
        ymerr("local-socket[spawn]: fatal: bind failed: %d (%s)",errno,strerror(errno));
        abort();
    }
    
    aResult = listen(listenSocket, 1);
    if ( aResult != 0 )
    {
        ymerr("local-socket[spawn]: listen failed: %d (%s)",errno,strerror(errno));
        close(listenSocket);
        abort();
    }
    
    gYMLocalSocketPairName = tryName;
    YMSemaphoreSignal(context->semaphore); // free'd by InitOnce
    
    ymlog("local-socket[spawn]: listening on %d",listenSocket);
    
    while ( ! gYMLocalSocketPairAcceptStopFlag )
    {
        ymlog("__ym_local_socket_accept_proc accepting...");
        
        int newClient = accept(listenSocket, (struct sockaddr *) &sockName, &size);
        if ( newClient < 0 )
        {
            ymerr("local-socket[spawn]: error: accept failed: %d: %d (%s)",newClient,errno,strerror(errno));
            gYMLocalSocketPairAcceptLast = -1;
        }
        else
            gYMLocalSocketPairAcceptLast = newClient;
        
        ymlog("local-socket[spawn]: accepted: %d",newClient);
        
        YMSemaphoreSignal(gYMLocalSocketPairDidAcceptSemaphore);
    }
    
    ymlog("__ym_local_socket_accept_proc exiting");
}
