//
//  YMLocalSocketPair.c
//  yammer
//
//  Created by david on 11/11/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMLocalSocketPair.h"

#include "YMDispatch.h"
#include "YMSemaphore.h"
#include "YMUtilities.h"

#define ymlog_type YMLogDefault
#define ymlog_pre "local-socket[%s]: "
#define ymlog_args ( p->userName ? YMSTR(p->userName) : "&" )
#include "YMLog.h"

#if !defined(YMWIN32)
# include <sys/socket.h>
# include <sys/un.h>
# define LOCAL_SOCKET_PROTOCOL PF_UNSPEC
# define LOCAL_SOCKET_DOMAIN AF_LOCAL
#else
# include <winsock2.h>
# include <ws2tcpip.h>
# define LOCAL_SOCKET_PROTOCOL IPPROTO_TCP
# define LOCAL_SOCKET_DOMAIN AF_INET
# define LOCAL_SOCKET_ADDR 0x7f000001
# define LOCAL_SOCKET_PORT 6969 // fixme use YMReservePort
#endif

#include <stddef.h> // offsetof

#if defined(YMLINUX) // __USE_MISC /usr/include/arm-linux-gnueabihf/sys/un.h
# ifndef SUN_LEN
#  define SUN_LEN(ptr) ((size_t) (((struct sockaddr_un *) 0)->sun_path)	      \
		      + strlen ((ptr)->sun_path))
# endif
#endif

YM_EXTERN_C_PUSH

typedef struct __ym_local_socket_pair
{
    _YMType _common;
    
    YMStringRef socketName;
    YMStringRef userName;
    YMSOCKET socketA;
	YMSOCKET socketB;
    bool shutdown;
} __ym_local_socket_pair;
typedef struct __ym_local_socket_pair __ym_local_socket_pair_t;

int __YMLocalSocketPairCreateClient(void);
YM_ENTRY_POINT(__ym_local_socket_accept_proc);
YM_ONCE_DEF(__YMLocalSocketPairInitOnce);

static YM_ONCE_OBJ gYMLocalSocketOnce = YM_ONCE_INIT;
static const char *__YMLocalSocketPairNameBase = "ym-lsock";
static YMDispatchQueueRef gYMLocalSocketPairAcceptQueue = NULL;
static YMStringRef gYMLocalSocketPairName = NULL;
static YMSemaphoreRef gYMLocalSocketSemaphore = NULL; // thread ready & each 'accepted'
static bool gYMLocalSocketPairAcceptKeepListening = false;
static bool gYMLocalSocketThreadEnd = false; // sadly not quite the same as 'keep listening'
static YMSOCKET gYMLocalSocketListenSocket = NULL_SOCKET;
static YMSOCKET gYMLocalSocketPairAcceptedLast = NULL_SOCKET;

void YMLocalSocketPairStop(void)
{
	YM_IO_BOILERPLATE
	
    if ( gYMLocalSocketListenSocket != NULL_SOCKET ) {
        // flag & signal thread to exit
        gYMLocalSocketPairAcceptKeepListening = false;
        gYMLocalSocketThreadEnd = true;
        ymlogg("local-socket: closing %d",gYMLocalSocketListenSocket);
 #if defined(YMLINUX)
		result = shutdown(gYMLocalSocketListenSocket,SHUT_RDWR); // on raspian (and from what i read, 'linux') closing the socket will not signal an accept() call
		ymsoftassert(result==0,"warning: shutdown(%d) listen failed: %d (%s)",(int)gYMLocalSocketListenSocket,error,errorStr);
 #endif
        YM_CLOSE_SOCKET(gYMLocalSocketListenSocket);
        ymsoftassert(result==0,"warning: close(%d) listen failed: %d (%s)",(int)gYMLocalSocketListenSocket,error,errorStr);
        gYMLocalSocketListenSocket = NULL_SOCKET;

        YMDispatchQueueRelease(gYMLocalSocketPairAcceptQueue);
        gYMLocalSocketPairAcceptQueue = NULL;
        
        YMRelease(gYMLocalSocketSemaphore);
        gYMLocalSocketSemaphore = NULL;
        
#if defined(YMAPPLE) || defined(YMLINUX)
		char *prefix = "";
//#if defined (YMAPPLE) // it seems i was confused by XCTest setting cwd to /tmp
//		prefix = "/tmp/";
//#endif
        YMStringRef tmpPath = YMSTRCF("%s%s",prefix,YMSTR(gYMLocalSocketPairName));
        result = unlink(YMSTR(tmpPath));
        if ( result != 0 )
            ymerrg("failed to unlink socket file '%s': %d %s",YMSTR(gYMLocalSocketPairName),errno,strerror(errno));
        YMRelease(tmpPath);
#endif
        YMRelease(gYMLocalSocketPairName);
        gYMLocalSocketPairName = NULL;
        
        YM_ONCE_OBJ onceAgain = YM_ONCE_INIT;
        memcpy(&gYMLocalSocketOnce,&onceAgain,sizeof(onceAgain));
    }
}

YM_ONCE_FUNC(__YMLocalSocketPairInitOnce,
{
    YMStringRef name = YMSTRC("local-socket-spawn");
    gYMLocalSocketSemaphore = YMSemaphoreCreateWithName(name, 0);
    gYMLocalSocketPairAcceptQueue = YMDispatchQueueCreate(name);
    YMRelease(name);
    
    gYMLocalSocketThreadEnd = false;
    ym_dispatch_user_t *user = YMALLOC(sizeof(ym_dispatch_user_t));
    user->dispatchProc = __ym_local_socket_accept_proc;
    user->context = NULL;
    user->onCompleteProc = NULL;
    user->mode = ym_dispatch_user_context_noop;
    YMDispatchAsync(gYMLocalSocketPairAcceptQueue,user);
    YMFREE(user);
    
    YMSemaphoreWait(gYMLocalSocketSemaphore);
})

YMLocalSocketPairRef YMLocalSocketPairCreate(YMStringRef name, bool moreComing)
{
    YMNetworkingInit();
    
    const char *name_str = name ? YMSTR(name) : "*";

    YM_ONCE_DO(gYMLocalSocketOnce,__YMLocalSocketPairInitOnce);
    
    // now that thread is going to accept [once more], flag this
    gYMLocalSocketPairAcceptKeepListening = moreComing;
    
    int clientSocket = __YMLocalSocketPairCreateClient();
    if ( clientSocket < 0 ) {
        ymerrg("failed to create client socket: %s",name_str);
        return NULL;
    }
    
    YMSemaphoreWait(gYMLocalSocketSemaphore); // wait for accept to happen and 'server' socket to get set
    
    int serverSocket = gYMLocalSocketPairAcceptedLast;
    if ( serverSocket < 0 ) {
        ymerrg("failed to create server socket: %s",name_str);
        return NULL;
    }
    
    __ym_local_socket_pair_t *p = (__ym_local_socket_pair_t *)_YMAlloc(_YMLocalSocketPairTypeID,sizeof(__ym_local_socket_pair_t));
    
    p->userName = YMSTRCF("ls:%s:sf%d<->cf%d",name_str,serverSocket,clientSocket);
    p->socketName = YMRetain(gYMLocalSocketPairName);
    p->socketA = serverSocket;
    p->socketB = clientSocket;
    p->shutdown = false;
    
    if ( ! moreComing )
        YMLocalSocketPairStop(); // eat your own dogfood
    
    return p;
}

YMSOCKET YMLocalSocketPairGetA(YMLocalSocketPairRef p)
{
    return p->socketA;
}
YMSOCKET YMLocalSocketPairGetB(YMLocalSocketPairRef p)
{
    return p->socketB;
}

bool YMLocalSocketPairShutdown(YMLocalSocketPairRef p)
{
    bool okay = false;
    if ( ! p->shutdown ) {
        ymlog("shutting down");
        int result = shutdown(p->socketA, SHUT_RDWR);
        okay = ( result == 0 );
        shutdown(p->socketB, SHUT_RDWR);
        okay = okay && ( result == 0 );
    }
    return okay;
}

void _YMLocalSocketPairFree(YMTypeRef o_)
{
    YM_IO_BOILERPLATE

    __ym_local_socket_pair_t *p = (__ym_local_socket_pair_t *)o_;
    
    YM_CLOSE_SOCKET(p->socketA);
    if ( result != 0 )
        ymabort("close failed (%zd): %d (%s)",result,errno,strerror(errno));

    YM_CLOSE_SOCKET(p->socketB);
    if ( result != 0 )
        ymabort("close failed (%zd): %d (%s)",result,errno,strerror(errno));
    
    YMRelease(p->userName);
    YMRelease(p->socketName);
}

int __YMLocalSocketPairCreateClient(void)
{
    YM_IO_BOILERPLATE

    YMSOCKET sock = socket(LOCAL_SOCKET_DOMAIN, SOCK_STREAM, LOCAL_SOCKET_PROTOCOL/* IP, /etc/sockets man 5 protocols*/);
#if !defined(YMWIN32)
	ymassert(sock>=0,"socket failed: %d (%s)",errno,strerror(errno));
#else
	ymassert(sock!=INVALID_SOCKET,"socket failed: %x", GetLastError());
#endif
    
    int yes = 1;
    result = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const void *)&yes, sizeof(yes));
    if ( result != 0 )
        ymerrg("setsockopt failed on %d: %zd: %d (%s)",sock,result,errno,strerror(errno));
    
    /* Bind a name to the socket. */
#if !defined(YMWIN32)
    struct sockaddr_un sockName;
    sockName.sun_family = AF_LOCAL;
    strncpy (sockName.sun_path, YMSTR(gYMLocalSocketPairName), sizeof (sockName.sun_path));
    sockName.sun_path[sizeof (sockName.sun_path) - 1] = '\0';
    socklen_t size = (socklen_t)SUN_LEN(&sockName);
#else
	struct sockaddr_in sockName = { AF_INET, htons(LOCAL_SOCKET_PORT), {0}, {0}};
	result = inet_pton(AF_INET, "127.0.0.1",&sockName.sin_addr.s_addr);
	socklen_t size = sizeof(struct sockaddr_in);
#endif
    
    result = connect(sock, (struct sockaddr *) &sockName, size);
    if ( result != 0 ) {
        ymerrg("connect failed: %zd: %d (%s)",result,errno,strerror(errno));
        YM_CLOSE_SOCKET(sock);
        return -1;
    }
    
    ymlogg("connected: sf%d",sock);
    
    return sock;
}

YM_ENTRY_POINT(__ym_local_socket_accept_proc)
{
    YM_IO_BOILERPLATE

    ymlogg("__ym_local_socket_accept_proc entered");
    
    YMSOCKET listenSocket = -1;
    YMStringRef socketName = NULL;
    
    uint16_t nameSuffixIter = 0;
        
    name_retry:;
        
    ymassert(nameSuffixIter<UINT16_MAX,"fatal: unable to choose available name");
    
    if ( socketName )
        YMRelease(socketName);
    
    socketName = YMStringCreateWithFormat("%s:%u",__YMLocalSocketPairNameBase,nameSuffixIter,NULL);
    
    listenSocket = socket(LOCAL_SOCKET_DOMAIN, SOCK_STREAM, LOCAL_SOCKET_PROTOCOL /* /etc/sockets man 5 protocols*/);
#if !defined(YMWIN32)
    ymassert(listenSocket>=0,"fatal: socket failed (listen): %d (%s)",errno,strerror(errno));
#else
	ymassert(listenSocket!=INVALID_SOCKET,"fatal: socket failed (listen): %d",WSAGetLastError());
#endif
    
    int yes = 1;
    result = setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, (const void *)&yes, sizeof(yes));
    if ( result != 0 )
        ymerrg("setsockopt failed on sf%d: %zd: %d (%s)",listenSocket,result,errno,strerror(errno));
    
    /* Bind a name to the socket. */
#if !defined(YMWIN32)
    struct sockaddr_un sockName;
    sockName.sun_family = AF_LOCAL;
    strncpy (sockName.sun_path, YMSTR(socketName), sizeof (sockName.sun_path));
    sockName.sun_path[sizeof (sockName.sun_path) - 1] = '\0'; // ensure null termination if name is longer than (104)
    socklen_t size = (socklen_t) SUN_LEN(&sockName);
#else
	struct sockaddr_in sockName = { AF_INET, htons(LOCAL_SOCKET_PORT), {0} , {0} };
	result = inet_pton(AF_INET, "127.0.0.1", &sockName.sin_addr.s_addr);
	socklen_t size = sizeof(sockName);
#endif
    
	result = bind (listenSocket, (struct sockaddr *) &sockName, size);
    if ( result != 0 ) {
        int bindErrno = errno;
        YM_CLOSE_SOCKET(listenSocket);
        if ( bindErrno == EADDRINUSE ) {
            ymerrg("%s in use, retrying",YMSTR(socketName));
            nameSuffixIter++;
            goto name_retry;
        }
        ymabort("fatal: bind failed: %d (%s)",errno,strerror(errno));
    }
    
	result = listen(listenSocket, 1);
    if (result != 0 )
		ymabort("listen failed: %d (%s)",errno,strerror(errno));
    
    gYMLocalSocketListenSocket = listenSocket;
    gYMLocalSocketPairName = socketName;
    ymlogg("listening on %s:%d",YMSTR(socketName),listenSocket);
    YMSemaphoreSignal(gYMLocalSocketSemaphore); // signal InitOnce
    
    do {
        ymlogg("__ym_local_socket_accept_proc accepting...");
        
        int newClient = accept(listenSocket, (struct sockaddr *) &sockName, &size);
        if ( newClient < 0 ) {
            if ( ! gYMLocalSocketThreadEnd )
                ymerrg("__ym_local_socket_accept_proc: accept failed: %d: %d (%s)",newClient,errno,strerror(errno));
            gYMLocalSocketPairAcceptedLast = NULL_SOCKET;
            break;
        } else
            gYMLocalSocketPairAcceptedLast = newClient;
        
        ymlogg("__ym_local_socket_accept_proc: accepted: sf%d",newClient);
        
        YMSemaphoreSignal(gYMLocalSocketSemaphore);
        
        // check flag again in case 'global stop' thing got called on a persistent accept thread
    } while ( gYMLocalSocketPairAcceptKeepListening );
    
    ymlogg("__ym_local_socket_accept_proc exiting");
}

YM_EXTERN_C_POP
