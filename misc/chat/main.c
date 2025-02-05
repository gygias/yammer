//
//  main.m
//  testprompt
//
//  Created by david on 11/16/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

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
#include "defs.h"

YMSessionRef gYMSession = NULL;
bool gIsServer;

#ifndef YMWIN32
void __sigint_handler (__unused int signum)
{
#else
void __CtrlHandler(DWORD cType)
{
    if ( cType != CTRL_C_EVENT )
        return;
#endif
    if ( gYMSession ) {
        YMSessionRef session = gYMSession;
        gYMSession = NULL;

        printf("caught sigint...\n");
        if ( gIsServer )
            YMSessionStopAdvertising(session);
        else
            YMSessionStopBrowsing(session);
        YMSessionCloseAllConnections(session);
        YMRelease(session);
    }
    myexit(1);
}

int main(int argc, const char * argv[]) {
        
    if ( argc < 2 || argc > 3 ) {
        printf("usage: testprompt <mdns type> [<mdns name>]\n");
        printf(" if name is not specified, the tool will act as a client.\n");
		myexit(1);
    }
        
#ifndef YMWIN32
    signal(SIGINT, __sigint_handler);
#else
    SetConsoleCtrlHandler((PHANDLER_ROUTINE)__CtrlHandler, TRUE);
#endif

    if ( argc == 3 ) {
        gIsServer = true;
        gYMSession = YMSessionCreate(YMSTRC(argv[1]));
        YMSessionSetCommonCallbacks(gYMSession, NULL, _connected_func, _interrupted_func, _new_stream_func, _closing_func);
        YMSessionSetAdvertisingCallbacks(gYMSession, _should_accept_func, NULL);
        if ( ! YMSessionStartAdvertising(gYMSession, YMSTRC(argv[2])) )
            myexit(1);
    } else {
        gIsServer = false;
        gYMSession = YMSessionCreate(YMSTRC(argv[1]));
        YMSessionSetCommonCallbacks(gYMSession, NULL, _connected_func, _interrupted_func, _new_stream_func, _closing_func);
        YMSessionSetBrowsingCallbacks(gYMSession, _added_peer_func, _removed_peer_func, _resolve_failed_func, _resolved_func, _connect_failed_func, NULL);
        if ( ! YMSessionStartBrowsing(gYMSession) )
            myexit(1);
		printf("looking for service...\n");
    }
    
    int longTime = 999999999;
    sleep(longTime);
    
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

typedef struct message_header
{
    uint16_t length;
} _message_header;

void run_chat(YMStreamRef stream)
{
    int aChar;
    while ( ( aChar = getc(stdin) ) != EOF ) {
        YMStreamWriteDown(stream, (uint8_t *)&aChar, sizeof(aChar));
   }
}

void print_incoming(YMStreamRef stream)
{
    char aChar;
    while ( true ) {
        YMIOResult result = YMStreamReadUp(stream, (uint8_t *)&aChar, sizeof(aChar), NULL);
        if ( result != YMIOSuccess ) {
            printf("peer left\n");
            myexit(1);
        }
        
        putc(aChar, stdout);
    }
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
    myexit(1);
}

void _resolve_failed_func(__unused YMSessionRef session, YMPeerRef peer, __unused void* context)
{
    printf("resolve failed %s\n", YMSTR(YMPeerGetName(peer)));
    myexit(1);
}

void _resolved_func(__unused YMSessionRef session, YMPeerRef peer, __unused void* context)
{
    printf("resolved %s...\n", YMSTR(YMPeerGetName(peer)));
    YMSessionConnectToPeer(session, peer, false);
}

void _connect_failed_func(__unused YMSessionRef session, YMPeerRef peer, bool moreComing, __unused void* context)
{
    printf("connect failed %s\n", YMSTR(YMPeerGetName(peer)));
    if ( ! moreComing )
        myexit(1);
}

// server
bool _should_accept_func(__unused YMSessionRef session, YMPeerRef peer, __unused void* context)
{
    printf("accepting %s...\n", YMSTR(YMPeerGetName(peer)));
    return true;
}

// connection
void _connected_func(__unused YMSessionRef session,YMConnectionRef connection, __unused void* context)
{
    printf("connected to %s\n", YMSTR(YMAddressGetDescription(YMConnectionGetAddress(connection))));
    
    if ( ! gIsServer ) {
        YMStreamRef stream = YMConnectionCreateStream(connection, YMSTRC("outgoing"), YMCompressionNone);
        YMStreamWriteDown(stream, (uint8_t *)"!", 1);
        thread(run_chat, stream);
        thread(print_incoming, stream);
    }
}

void _interrupted_func(__unused YMSessionRef session, __unused void* context)
{
    printf("session interrupted\n");
    myexit(1);
}

// streams
void _new_stream_func(__unused YMSessionRef session, __unused YMConnectionRef connection, YMStreamRef stream, __unused void* context)
{
    printf("stream arrived\n");
    char hello;
    YMStreamReadUp(stream, (uint8_t *)&hello, 1, NULL);
    if ( gIsServer ) {
        thread(run_chat, stream);
        thread(print_incoming, stream);
    }
}

void _closing_func(__unused YMSessionRef session, __unused YMConnectionRef connection, __unused YMStreamRef stream, __unused void* context)
{
    printf("stream closed\n");
    myexit(1);
}
