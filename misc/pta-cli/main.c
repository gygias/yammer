//
//  main.c
//  pta-cli
//
//  Created by david on 4/10/16.
//  Copyright © 2016 combobulated. All rights reserved.
//

#include <stdio.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#ifndef YMWIN32
# include <unistd.h>
# include <pthread.h>
# if defined(YMLINUX)
#  include <signal.h>
# endif
# define myexit _Exit
#else
# define WIN32_LEAN_AND_MEAN
# include <windows.h>
# define myexit exit
#endif

#include <libyammer/Yammer.h>
#include "YMUtilities.h"
#include "misc/chat/defs.h"

YMSessionRef gYMSession = NULL;
bool gIsServer;
bool gExited = false;

#ifndef YMWIN32
void __sigint_handler (__unused int signum)
{
#else
void __CtrlHandler(DWORD cType)
{
    if ( cType != CTRL_C_EVENT )
        return;
#endif
    printf("caught sigint\n");
    myexit(1);
}

int main(int argc, const char * argv[]) {
    
    if ( argc < 1 || argc > 2 ) {
        printf("usage: pta [<mdns name>]\n");
        printf(" if name is not specified, the tool will act as a client.\n");
        myexit(1);
    }
    
#ifndef YMWIN32
    signal(SIGINT, __sigint_handler);
#else
    SetConsoleCtrlHandler((PHANDLER_ROUTINE)__CtrlHandler, TRUE);
#endif
    
    YMStringRef ymType = YMSTRC("_ympta._tcp");
    if ( argc == 2 ) {
        gIsServer = true;
        gYMSession = YMSessionCreate(ymType);
        YMSessionSetCommonCallbacks(gYMSession, NULL, _connected_func, _interrupted_func, _new_stream_func, _closing_func);
        YMSessionSetAdvertisingCallbacks(gYMSession, _should_accept_func, NULL);
        if ( ! YMSessionStartAdvertising(gYMSession, YMSTRC(argv[1])) )
            myexit(1);
    } else {
        gIsServer = false;
        gYMSession = YMSessionCreate(ymType);
        YMSessionSetCommonCallbacks(gYMSession, NULL, _connected_func, _interrupted_func, _new_stream_func, _closing_func);
        YMSessionSetBrowsingCallbacks(gYMSession, _added_peer_func, _removed_peer_func, _resolve_failed_func, _resolved_func, _connect_failed_func, NULL);
        if ( ! YMSessionStartBrowsing(gYMSession) )
            myexit(1);
        printf("looking for service...\n");
    }
    
    YMDispatchMain();
    
    return 0;
}

void thread(void (*func)(YMStreamRef), YMStreamRef context)
{
#ifndef YMWIN32
    pthread_t pthread;
    pthread_create(&pthread, NULL, (void *(*)(void *))func, (void *)context);
#else
    HANDLE thread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)func, (LPVOID)context, 0, NULL);
#endif
}

typedef struct print_stuff_context
{
    bool server;
    uint64_t *off;
    uint64_t *thenOff;
} print_stuff_context;

#define LogInterval 1

YM_ENTRY_POINT(print_stuff)
{
    if ( gExited )
        return;

    print_stuff_context *p = context;
    bool server = p->server;
    uint64_t this = *(p->off) - *(p->thenOff);
    *(p->thenOff) = *(p->off);

    printf("%s: wrote %lu: ",server?"s":"c",this);
    
    YMArrayRef connections = YMSessionCopyConnections(gYMSession);
    for( int i = 0; i < YMArrayGetCount(connections); i++ ) {
        YMConnectionRef aConnection = YMArrayGet(connections, i);
        const char *lDesc = YMInterfaceTypeDescription(YMConnectionGetLocalInterface(aConnection));
        const char *rDesc = YMInterfaceTypeDescription(YMConnectionGetRemoteInterface(aConnection));
        double sampleMBs = (double)YMConnectionGetSample(aConnection) / 1024. / 1024.;
        if ( YMSessionGetDefaultConnection(gYMSession) == aConnection ) {
            double tputMBs = (double)this / LogInterval / 1024. / 1024.;
            double ratio = tputMBs / sampleMBs;
            printf("* (%0.2f mb/s, %0.0f%%) ",tputMBs,ratio*100);
        }
        printf("%s <-> %s (%0.2f mb/s)%s", lDesc, rDesc, sampleMBs,i<YMArrayGetCount(connections)-1?", ":"");
    }
    YMRelease(connections);
    
    printf("\n");

    ym_dispatch_user_t u = { print_stuff, p, NULL, ym_dispatch_user_context_noop };
    YMDispatchAfter(YMDispatchGetGlobalQueue(), &u, 1);
}

void run_client_loop(YMStreamRef stream)
{
    YMIOResult ymResult;
    uint64_t off = 0, thenOff = 0;

    print_stuff_context c = {0};
    c.server = false;
    c.off = &off;
    c.thenOff = &thenOff;
    ym_dispatch_user_t u = { print_stuff, &c, NULL, ym_dispatch_user_context_noop };
    YMDispatchAfter(YMDispatchGetGlobalQueue(), &u, 1);

    do {
#define bufSize UINT16_MAX
        uint8_t buf[bufSize];
        YMRandomDataWithLength(buf,bufSize);
        ymResult = YMStreamWriteDown(stream, buf, bufSize);
        if ( ymResult != YMIOSuccess ) {
            printf("%d = YMStreamWriteDown(%p, %p, %d)\n",ymResult,stream,buf,bufSize);
            break;
        }
        off += bufSize;
    } while ( ! gExited && ymResult == YMIOSuccess );

    gExited = true;
}

void run_server_loop(YMStreamRef stream)
{
    YMIOResult ymResult;
    uint64_t off = 0, thenOff = 0;

    print_stuff_context c = {0};
    c.server = true;
    c.off = &off;
    c.thenOff = &thenOff;
    ym_dispatch_user_t u = { print_stuff, &c, NULL, ym_dispatch_user_context_noop };
    YMDispatchAfter(YMDispatchGetGlobalQueue(), &u, 1);
    
    do {
        uint8_t buf[bufSize];

        uint16_t o = 0;
        ymResult = YMStreamReadUp(stream, buf, bufSize, &o);
        if ( ymResult != YMIOSuccess ) {
            printf("%d = YMStreamReadUp(%p, %p, %d, &%u)\n",ymResult,stream,buf,bufSize,o);
            break;
        } else if ( o != bufSize ) {
            printf("server read incomplete\n");
            break;
        }
        off += o;
    } while ( ! gExited &&  ymResult == YMIOSuccess );

    gExited = true;
}
    
// client, discover->connect
void _added_peer_func(YMSessionRef session, YMPeerRef peer, __unused void* context)
{
    printf("resolving %s...\n", YMSTR(YMPeerGetName(peer)));
    YMSessionResolvePeer(session, peer);
}

void _removed_peer_func(__unused YMSessionRef session, YMPeerRef peer, __unused void* context)
{
    printf("lost peer %s\n", YMSTR(YMPeerGetName(peer)));
    //myexit(1);
}

void _resolve_failed_func(__unused YMSessionRef session, YMPeerRef peer, __unused void* context)
{
    printf("resolve failed %s\n", YMSTR(YMPeerGetName(peer)));
    //myexit(1);
}

void _resolved_func(__unused YMSessionRef session, YMPeerRef peer, __unused void* context)
{
    printf("resolved %s...\n", YMSTR(YMPeerGetName(peer)));
    YMSessionConnectToPeer(session, peer, false);
}

void _connect_failed_func(__unused YMSessionRef session, YMPeerRef peer, bool moreComing, __unused void* context)
{
    printf("connect failed %s\n", YMSTR(YMPeerGetName(peer)));
    if ( ! moreComing ) {
        //myexit(1);
    }
}

// server
bool _should_accept_func(__unused YMSessionRef session, YMPeerRef peer, __unused void* context)
{
    printf("accepting %s...\n", YMSTR(YMPeerGetName(peer)));
    return true;
}

// connection
void _connected_func(YMSessionRef session, YMConnectionRef connection, __unused void* context)
{
    printf("connected to %s\n", YMSTR(YMAddressGetDescription(YMConnectionGetAddress(connection))));
    
    if ( ! gIsServer ) {
        if ( YMSessionGetDefaultConnection(session) == connection ) {
            YMStreamRef stream = YMConnectionCreateStream(connection, YMSTRC("outgoing"), YMCompressionNone);
            thread(run_client_loop, stream);
        }
    }
}

void _interrupted_func(__unused YMSessionRef session, __unused void* context)
{
    gExited = true;
    printf("session interrupted\n");
    //myexit(1);
}

// streams
void _new_stream_func(__unused YMSessionRef session, __unused YMConnectionRef connection, YMStreamRef stream, __unused void* context)
{
    printf("stream arrived\n");
    char hello;
    YMStreamReadUp(stream, (uint8_t *)&hello, 1, NULL);
    if ( gIsServer ) {
        thread(run_server_loop,stream);
    }
}
    
void _closing_func(__unused YMSessionRef session, __unused YMConnectionRef connection, __unused YMStreamRef stream, __unused void* context)
{
    printf("stream closed\n");
    //myexit(1);
}
