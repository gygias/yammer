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

#define ymlog_type YMLogIO
#include "YMLog.h"
#define YM_LOG_PRE "local-socket[%s]"
#define YM_LOG_DSC YMSTR(pair->userName)
#define YM_LOG_DSCG "&"

#ifndef WIN32
# include <sys/socket.h>
# include <sys/un.h>
# include <pthread.h>
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
# define SUN_LEN(ptr) ((size_t) (((struct sockaddr_un *) 0)->sun_path)	      \
		      + strlen ((ptr)->sun_path))
#endif

YM_EXTERN_C_PUSH

typedef struct __ym_local_socket_pair_t
{
    _YMType _type;
    
    YMStringRef socketName;
    YMStringRef userName;
    YMSOCKET socketA;
	YMSOCKET socketB;
    bool shutdown;
} __ym_local_socket_pair_t;
typedef struct __ym_local_socket_pair_t *__YMLocalSocketPairRef;

int __YMLocalSocketPairCreateClient();
YM_THREAD_RETURN YM_CALLING_CONVENTION __ym_local_socket_accept_proc(YM_THREAD_PARAM);
YM_ONCE_DEF(__YMLocalSocketPairInitOnce);

static YM_ONCE_OBJ gYMLocalSocketOnce = YM_ONCE_INIT;
static const char *__YMLocalSocketPairNameBase = "ym-lsock";
static YMThreadRef gYMLocalSocketPairAcceptThread = NULL;
static YMStringRef gYMLocalSocketPairName = NULL;
static YMSemaphoreRef gYMLocalSocketSemaphore = NULL; // thread ready & each 'accepted'
static bool gYMLocalSocketPairAcceptKeepListening = false;
static bool gYMLocalSocketThreadEnd = false; // sadly not quite the same as 'keep listening'
static bool gYMLocalSocketThreadExited = false;
static YMSOCKET gYMLocalSocketListenSocket = NULL_SOCKET;
static YMSOCKET gYMLocalSocketPairAcceptedLast = NULL_SOCKET;

void YMLocalSocketPairStop()
{
	YM_IO_BOILERPLATE
	
    if ( gYMLocalSocketListenSocket != NULL_SOCKET )
    {
        // flag & signal thread to exit
        gYMLocalSocketPairAcceptKeepListening = false;
        gYMLocalSocketThreadEnd = true;
        ymlog("local-socket: closing %d",gYMLocalSocketListenSocket);
 #if defined(YMLINUX)
		result = shutdown(gYMLocalSocketListenSocket,SHUT_RDWR); // on raspian (and from what i read, 'linux') closing the socket will not signal an accept() call
		ymsoftassert(result==0,YM_LOG_PRE "warning: shutdown(%d) listen failed: %d (%s)",YM_LOG_DSCG,(int)gYMLocalSocketListenSocket,error,errorStr);
 #endif
        YM_CLOSE_SOCKET(gYMLocalSocketListenSocket);
        ymsoftassert(result==0,YM_LOG_PRE "warning: close(%d) listen failed: %d (%s)",YM_LOG_DSCG,(int)gYMLocalSocketListenSocket,error,errorStr);
        gYMLocalSocketListenSocket = NULL_SOCKET;

        while ( ! gYMLocalSocketThreadExited ) {}; // avoid free->create race where previous thread holds previous global sem
        
        YMRelease(gYMLocalSocketSemaphore);
        gYMLocalSocketSemaphore = NULL;

        YMRelease(gYMLocalSocketPairAcceptThread);
        gYMLocalSocketPairAcceptThread = NULL;
        
#if defined(YMMACOS) || defined(YMLINUX)
		char *prefix = "";
//#if defined (YMMACOS) // it seems i was confused by XCTest setting cwd to /tmp
//		prefix = "/tmp/";
//#endif
        YMStringRef tmpPath = YMSTRCF("%s%s",prefix,YMSTR(gYMLocalSocketPairName));
        result = unlink(YMSTR(tmpPath));
        if ( result != 0 )
            ymerr(YM_LOG_PRE "failed to unlink socket file '%s': %d %s",YM_LOG_DSCG,YMSTR(gYMLocalSocketPairName),errno,strerror(errno));
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
    gYMLocalSocketPairAcceptThread = YMThreadCreate(name, __ym_local_socket_accept_proc, NULL);
    YMRelease(name);
    
    ymassert(gYMLocalSocketPairAcceptThread&&gYMLocalSocketSemaphore,"local-socket: fatal: failed to spawn accept thread");
    
    gYMLocalSocketThreadEnd = false;
    gYMLocalSocketThreadExited = false;
    bool okay = YMThreadStart(gYMLocalSocketPairAcceptThread);
    ymassert(okay,YM_LOG_PRE "fatal: failed to start accept thread",YM_LOG_DSCG);
    
    YMSemaphoreWait(gYMLocalSocketSemaphore);
});

YMLocalSocketPairRef YMLocalSocketPairCreate(YMStringRef name, bool moreComing)
{
    YMNetworkingInit();
    
    const char *name_str = name ? YMSTR(name) : "*";

    YM_ONCE_DO(gYMLocalSocketOnce,__YMLocalSocketPairInitOnce);
    
    // now that thread is going to accept [once more], flag this
    gYMLocalSocketPairAcceptKeepListening = moreComing;
    
    int clientSocket = __YMLocalSocketPairCreateClient();
    if ( clientSocket < 0 )
    {
        ymerr(YM_LOG_PRE "error: failed to create client socket",name_str);
        return NULL;
    }
    
    YMSemaphoreWait(gYMLocalSocketSemaphore); // wait for accept to happen and 'server' socket to get set
    
    int serverSocket = gYMLocalSocketPairAcceptedLast;
    if ( serverSocket < 0 )
    {
        ymerr(YM_LOG_PRE "error: failed to create server socket",name_str);
        return NULL;
    }
    
    __YMLocalSocketPairRef pair = (__YMLocalSocketPairRef)_YMAlloc(_YMLocalSocketPairTypeID,sizeof(struct __ym_local_socket_pair_t));
    
    pair->userName = YMSTRCF("ls:%s:sf%d<->cf%d",name_str,serverSocket,clientSocket);
    pair->socketName = YMRetain(gYMLocalSocketPairName);
    pair->socketA = serverSocket;
    pair->socketB = clientSocket;
    pair->shutdown = false;
    
    if ( ! moreComing )
        YMLocalSocketPairStop(); // eat your own dogfood
    
    return pair;
}

YMSOCKET YMLocalSocketPairGetA(YMLocalSocketPairRef pair_)
{
    __YMLocalSocketPairRef pair = (__YMLocalSocketPairRef)pair_;
    return pair->socketA;
}
YMSOCKET YMLocalSocketPairGetB(YMLocalSocketPairRef pair_)
{
    __YMLocalSocketPairRef pair = (__YMLocalSocketPairRef)pair_;
    return pair->socketB;
}

bool YMAPI YMLocalSocketPairShutdown(YMLocalSocketPairRef pair_)
{
    __YMLocalSocketPairRef pair = (__YMLocalSocketPairRef)pair_;
    
    bool okay = false;
    if ( ! pair->shutdown )
    {
        ymlog(YM_LOG_PRE "shutting down",YM_LOG_DSC);
        int result = shutdown(pair->socketA, SHUT_RDWR);
        okay = ( result == 0 );
        shutdown(pair->socketB, SHUT_RDWR);
        okay = okay && ( result == 0 );
    }
    return okay;
}

void _YMLocalSocketPairFree(YMTypeRef object)
{
    __YMLocalSocketPairRef pair = (__YMLocalSocketPairRef)object;
    
	int result, error; const char *errorStr;
    YM_CLOSE_SOCKET(pair->socketA);
    if ( result != 0 )
    {
        ymerr(YM_LOG_PRE "close failed (%d): %d (%s)",YM_LOG_DSC,result,errno,strerror(errno));
        abort();
    }
    YM_CLOSE_SOCKET(pair->socketB);
    if ( result != 0 )
    {
        ymerr(YM_LOG_PRE "close failed (%d): %d (%s)",YM_LOG_DSC,result,errno,strerror(errno));
        abort();
    }
    
    YMRelease(pair->userName);
    YMRelease(pair->socketName);
}

int __YMLocalSocketPairCreateClient()
{
    YMSOCKET sock = socket(LOCAL_SOCKET_DOMAIN, SOCK_STREAM, LOCAL_SOCKET_PROTOCOL/* IP, /etc/sockets man 5 protocols*/);
#ifndef WIN32
	ymassert(sock>=0,YM_LOG_PRE "socket failed: %d (%s)",YM_LOG_DSCG,errno,strerror(errno));
#else
	ymassert(sock!=INVALID_SOCKET,YM_LOG_PRE "socket failed: %x", YM_LOG_DSCG, GetLastError());
#endif
    
    int yes = 1;
    int result = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const void *)&yes, sizeof(yes));
    if ( result != 0 )
        ymerr(YM_LOG_PRE "warning: setsockopt failed on %d: %d: %d (%s)",YM_LOG_DSCG,sock,result,errno,strerror(errno));
    
    /* Bind a name to the socket. */
#ifndef WIN32
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
    if ( result != 0 )
    {
        ymerr(YM_LOG_PRE "connect failed: %d: %d (%s)",YM_LOG_DSCG,result,errno,strerror(errno));
		int error; const char *errorStr;
        YM_CLOSE_SOCKET(sock);
        return -1;
    }
    
    ymlog(YM_LOG_PRE "connected: sf%d",YM_LOG_DSCG,sock);
    
    return sock;
}

YM_THREAD_RETURN YM_CALLING_CONVENTION __ym_local_socket_accept_proc(__unused YM_THREAD_PARAM ctx)
{
    ymlog(YM_LOG_PRE "__ym_local_socket_accept_proc entered", YM_LOG_DSCG);
    
    YMSOCKET listenSocket = -1;
    YMStringRef socketName = NULL;
    
    uint16_t nameSuffixIter = 0;
        
    name_retry:;
        
    ymassert(nameSuffixIter<UINT16_MAX,YM_LOG_PRE "fatal: unable to choose available name",YM_LOG_DSCG);
    
    if ( socketName )
        YMRelease(socketName);
    
    socketName = YMStringCreateWithFormat("%s:%u",__YMLocalSocketPairNameBase,nameSuffixIter,NULL);
    
    listenSocket = socket(LOCAL_SOCKET_DOMAIN, SOCK_STREAM, LOCAL_SOCKET_PROTOCOL /* /etc/sockets man 5 protocols*/);
#ifndef WIN32
    ymassert(listenSocket>=0,YM_LOG_PRE "fatal: socket failed (listen): %d (%s)",YM_LOG_DSCG,errno,strerror(errno));
#else
	ymassert(listenSocket!=INVALID_SOCKET,YM_LOG_PRE "fatal: socket failed (listen): %x",YM_LOG_DSCG,GetLastError());
#endif
    
    int yes = 1;
    int result = setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, (const void *)&yes, sizeof(yes));
    if (result != 0 )
        ymerr(YM_LOG_PRE "warning: setsockopt failed on sf%d: %d: %d (%s)",YM_LOG_DSCG,listenSocket,result,errno,strerror(errno));
    
    /* Bind a name to the socket. */
#ifndef WIN32
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
    if (result != 0 )
    {
        int bindErrno = errno;
		int error; const char *errorStr;
        YM_CLOSE_SOCKET(listenSocket);
        if ( bindErrno == EADDRINUSE )
        {
            ymerr(YM_LOG_PRE "%s in use, retrying",YM_LOG_DSCG,YMSTR(socketName));
            nameSuffixIter++;
            goto name_retry;
        }
        ymabort(YM_LOG_PRE "fatal: bind failed: %d (%s)",YM_LOG_DSCG,errno,strerror(errno));
    }
    
	result = listen(listenSocket, 1);
    if (result != 0 )
		ymabort(YM_LOG_PRE "listen failed: %d (%s)", YM_LOG_DSCG, errno, strerror(errno));
    
    gYMLocalSocketListenSocket = listenSocket;
    gYMLocalSocketPairName = socketName;
    ymlog(YM_LOG_PRE "listening on %s:%d",YM_LOG_DSCG,YMSTR(socketName),listenSocket);
    YMSemaphoreSignal(gYMLocalSocketSemaphore); // signal InitOnce
    
    do
    {
        ymlog(YM_LOG_PRE "__ym_local_socket_accept_proc accepting...",YM_LOG_DSCG);
        
        int newClient = accept(listenSocket, (struct sockaddr *) &sockName, &size);
        if ( newClient < 0 )
        {
            if ( ! gYMLocalSocketThreadEnd )
                ymerr("__ym_local_socket_accept_proc: error: accept failed: %d: %d (%s)",newClient,errno,strerror(errno));
            gYMLocalSocketPairAcceptedLast = NULL_SOCKET;
            break;
        }
        else
            gYMLocalSocketPairAcceptedLast = newClient;
        
        ymlog(YM_LOG_PRE "__ym_local_socket_accept_proc: accepted: sf%d",YM_LOG_DSCG,newClient);
        
        YMSemaphoreSignal(gYMLocalSocketSemaphore);
        
        // check flag again in case 'global stop' thing got called on a persistent accept thread
    } while ( gYMLocalSocketPairAcceptKeepListening );
    
    ymlog(YM_LOG_PRE "__ym_local_socket_accept_proc exiting",YM_LOG_DSCG);
    
    gYMLocalSocketThreadExited = true;

	YM_THREAD_END
}

YM_EXTERN_C_POP
