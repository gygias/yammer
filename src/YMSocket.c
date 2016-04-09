//
//  YMSocket.c
//  yammer
//
//  Created by david on 4/8/16.
//  Copyright © 2016 combobulated. All rights reserved.
//

#include "YMSocket.h"

#include "YMLog.h"

YM_EXTERN_C_PUSH

typedef struct __ym_socket
{
    _YMType _type;
    
    YMSOCKET socket;
    
    uint64_t oOff;
    uint64_t iOff;
} __ym_socket;
typedef struct __ym_socket __ym_socket_t;

// socket message
// { ack thru off 1234,
//   n bytes to follow,
//   user data... }
typedef struct ym_socket_message
{
    uint64_t iOff;
    uint16_t nBytes;
} ym_socket_message;

YMSocketRef YMSocketCreate()
{
    __ym_socket_t *s = (__ym_socket_t *)_YMAlloc(_YMSocketTypeID, sizeof(__ym_socket_t));
    s->socket = NULL_SOCKET;
    return s;
}

void _YMSocketFree(__unused YMTypeRef object)
{
    
}

bool YMSocketSet(YMSocketRef s_, YMSOCKET socket)
{
    __ym_socket_t *s = (__ym_socket_t *)s_;
    s->socket = socket;
    
    return true;
}

YMIOResult YMSocketWrite(YMSocketRef s, const uint8_t *b, size_t l)
{
    YM_IO_BOILERPLATE
    
    ymassert(s->socket!=NULL_SOCKET,"underlying socket not set");
    
    YMIOResult ymResult = YMIOSuccess;
    size_t off = 0;
    while ( off < l ) {
        YM_WRITE_SOCKET(s->socket, b + off, l - off);
        if ( result <= 0 ) {
            ymResult = YMIOError;
            break;
        }
        off += result;
    }
    
    return ymResult;
}

YMIOResult YMSocketRead(YMSocketRef s, uint8_t *b, size_t l)
{
    YM_IO_BOILERPLATE
    
    ymassert(s->socket!=NULL_SOCKET,"underlying socket not set");
    
    YMIOResult ymResult = YMIOSuccess;
    size_t off = 0;
    while ( off < l ) {
        YM_READ_SOCKET(s->socket, b + off, l - off);
        if ( result == 0 ) {
            ymResult = YMIOEOF;
            break;
        } else if ( result == -1 ) {
            ymResult = YMIOError;
            break;
        }
        off += result;
    }
    
    return ymResult;
}

YM_EXTERN_C_POP
