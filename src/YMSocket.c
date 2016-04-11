//
//  YMSocket.c
//  yammer
//
//  Created by david on 4/8/16.
//  Copyright © 2016 combobulated. All rights reserved.
//

#include "YMSocket.h"

#include "YMPipe.h"
#include "YMThread.h"

#include "YMLog.h"

YM_EXTERN_C_PUSH

typedef struct __ym_socket
{
    _YMType _common;
    
    YMSOCKET socket;
    bool passthrough;
    
    YMPipeRef inPipe;
    YMThreadRef inThread;
    
    YMPipeRef outPipe;
    YMThreadRef outThread;
    
    uint64_t iOff;
    uint64_t oOff;
    uint64_t iRolls;
    
    ym_socket_disconnected dFunc;
    const void *dCtx;
    bool dNotified;
} __ym_socket;
typedef struct __ym_socket __ym_socket_t;

// socket message
// { ack thru off 1234,
//   n bytes to follow,
//   user data... }

#define SocketChunkSize 16384

YM_THREAD_RETURN YM_CALLING_CONVENTION _ym_socket_out_proc(YM_THREAD_PARAM);
YM_THREAD_RETURN YM_CALLING_CONVENTION _ym_socket_in_proc(YM_THREAD_PARAM);

typedef struct ym_socketgram
{
    uint64_t iOff;
    uint8_t data[SocketChunkSize];
} ym_socketgram;
typedef struct ym_socketgram ym_socketgram_t;

YMSocketRef YMSocketCreate(ym_socket_disconnected f, const void *c)
{
    __ym_socket_t *s = (__ym_socket_t *)_YMAlloc(_YMSocketTypeID, sizeof(__ym_socket_t));
    
    s->socket = NULL_SOCKET;
    s->dFunc = f;
    s->dCtx = c;
    s->passthrough = true;
    
    s->inPipe = YMPipeCreate(NULL);
    s->inThread = YMThreadCreate(NULL, _ym_socket_in_proc, s);
    
    s->outPipe = YMPipeCreate(NULL);
    s->outThread = YMThreadCreate(NULL, _ym_socket_out_proc, s);
    
    return s;
}

void _YMSocketFree(YMTypeRef object)
{
    YMSocketRef s = object;
    
    YMRelease(s->inPipe);
    YMRelease(s->inThread);
    
    YMRelease(s->outPipe);
    YMRelease(s->outThread);
}

bool YMSocketSet(YMSocketRef s_, YMSOCKET socket)
{
    __ym_socket_t *s = (__ym_socket_t *)s_;
    ymassert(s->socket==NULL_SOCKET,"socket hot swap not implemented");
    
#if !defined(YMWIN32)
    struct stat statbuf;
    fstat(socket, &statbuf);
    if ( ! S_ISSOCK(statbuf.st_mode) ) {
        ymerr("file f%d is not a socket",socket);
        return false;
    }
#endif
    
    s->socket = socket;
    
    ymerr("socket[if%d->of%d -> %d -> if%d->of%d]: %p allocating",
          YMPipeGetInputFile(s->inPipe),
          YMPipeGetOutputFile(s->inPipe),
          s->socket,
          YMPipeGetInputFile(s->outPipe),
          YMPipeGetOutputFile(s->outPipe),
          s );
    
    bool okay = YMThreadStart(s->outThread);
    if ( okay ) {
        okay = YMThreadStart(s->inThread);
    }
    if ( ! okay )
        s->socket = NULL_SOCKET;
    
    s->dNotified = false;
    
    return okay;
}

YMFILE YMSocketGetInput(YMSocketRef s)
{
    return YMPipeGetInputFile(s->inPipe);
}

YMFILE YMSocketGetOutput(YMSocketRef s)
{
    return YMPipeGetOutputFile(s->outPipe);
}

// threads run for the lifetime of the socket object, across potentially many "underlying sockets"

YM_THREAD_RETURN YM_CALLING_CONVENTION _ym_socket_out_proc(YM_THREAD_PARAM ctx)
{
    __ym_socket_t *s = ctx;
    ymlog("_ym_socket_out_proc entered");
    
    YMFILE outputOfInput = YMPipeGetOutputFile(s->inPipe);
    
    YM_IO_BOILERPLATE
    
    ym_socketgram_t socketgram;
    
    do {
        
        // read one chunk
        ssize_t off = 0;
        ssize_t toForwardRaw = s->passthrough ? 1 : SocketChunkSize;
        ssize_t toForwardBoxed = s->passthrough ? 1 : sizeof(socketgram);
        while ( off < toForwardRaw ) {
            ymdbg("_ym_socket_out_proc reading socketgram from %d by 1: %zd of %zd",outputOfInput,off,toForwardRaw);
            YM_READ_FILE(outputOfInput, ((uint8_t *)&socketgram.data) + off, toForwardRaw - off);
            if ( result <= 0 ) {
                ymlog("socket input closed");
                YMSelfLock(s);
                if ( ! s->dNotified ) {
                    s->dNotified = true;
                    if ( s->dFunc )
                        s->dFunc(s,s->dCtx);
                }
                YMSelfUnlock(s);
                goto out_proc_exit;
            }
            off += result;
        }
        
        // send one chunk
        off = 0;
        if ( ! s->passthrough ) socketgram.iOff = s->iOff++;
        uint8_t *outBuf = s->passthrough ? (uint8_t *)&socketgram.data : (uint8_t *)&socketgram;
        while ( off < toForwardBoxed ) {
            ymdbg("_ym_socket_out_proc reading socketgram: %zd of %zd",off,toForwardBoxed);
            YM_WRITE_SOCKET(s->socket, outBuf + off, toForwardBoxed - off);
            if ( result <= 0 ) {
                ymlog("socket output");
                YMSelfLock(s);
                if ( ! s->dNotified ) {
                    s->dNotified = true;
                    if ( s->dFunc )
                        s->dFunc(s,s->dCtx);
                }
                YMSelfUnlock(s);
                goto out_proc_exit;
            }
            off += result;
        }
        
        
    } while (true);
out_proc_exit:
    ymlog("_ym_socket_out_proc exiting");
    
    YM_THREAD_END
}

YM_THREAD_RETURN YM_CALLING_CONVENTION _ym_socket_in_proc(YM_THREAD_PARAM ctx)
{
    __ym_socket_t *s = ctx;
    ymlog("_ym_socket_in_proc entered");
    
    YMFILE inputOfOutput = YMPipeGetInputFile(s->outPipe);
    
    YM_IO_BOILERPLATE
    
    ym_socketgram_t socketgram;
    
    do {
        
        // receive one chunk
        ssize_t off = 0;
        ssize_t toForwardRaw = s->passthrough ? 1 : SocketChunkSize;
        ssize_t toForwardBoxed = s->passthrough ? 1 : sizeof(socketgram);
        
        uint8_t *inBuf = s->passthrough ? (uint8_t *)&socketgram.data : (uint8_t *)&socketgram;
        while ( off < toForwardBoxed ) {
            ymdbg("_ym_socket_in_proc reading socketgram: %zd of %zd",off,toForwardBoxed);
            YM_READ_SOCKET(s->socket, inBuf + off, toForwardBoxed - off);
            if ( result <= 0 ) {
                ymlog("socket output closed");
                YMSelfLock(s);
                if ( ! s->dNotified ) {
                    s->dNotified = true;
                    if ( s->dFunc )
                        s->dFunc(s,s->dCtx);
                }
                YMSelfUnlock(s);
                goto in_proc_exit;
            }
            off += result;
        }
        
        // write one chunk
        off = 0;
        if ( ! s->passthrough ) s->oOff = socketgram.iOff;
        while ( off < toForwardRaw ) {
            ymdbg("_ym_socket_in_proc writing to %d: %zd of %zd",inputOfOutput,off,toForwardRaw);
            YM_WRITE_FILE(inputOfOutput, ((uint8_t *)&socketgram.data) + off, toForwardRaw - off);
            if ( result <= 0 ) {
                ymlog("socket input");
                YMSelfLock(s);
                if ( ! s->dNotified ) {
                    s->dNotified = true;
                    if ( s->dFunc )
                        s->dFunc(s,s->dCtx);
                }
                YMSelfUnlock(s);
                goto in_proc_exit;
            }
            off += result;
        }
    } while (true);
    
in_proc_exit:
    ymlog("_ym_socket_in_proc exiting");
    
    YM_THREAD_END
}

YM_EXTERN_C_POP
