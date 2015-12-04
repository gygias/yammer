//
//  YMConnection.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMConnection_h
#define YMConnection_h

YM_EXTERN_C_PUSH

#include <libyammer/YMBase.h>

#include <libyammer/YMStream.h>
#include <libyammer/YMAddress.h>

#ifdef WIN32
#include <winsock2.h>
#endif

typedef const struct __ym_connection_t *YMConnectionRef;

YMAddressRef YMAPI YMConnectionGetAddress(YMConnectionRef connection);
YMStreamRef YMAPI YMConnectionCreateStream(YMConnectionRef connection, YMStringRef name);
void YMAPI YMConnectionCloseStream(YMConnectionRef connection, YMStreamRef stream);
    
typedef void (*ym_connection_forward_callback)(YMConnectionRef, YMStreamRef, YMIOResult, uint64_t, void *);
typedef struct ym_connection_forward_context_t
{
    ym_connection_forward_callback callback;
    void * context;
} ym_connection_forward_context_t;
    
bool YMAPI YMConnectionForwardFile(YMConnectionRef connection, YMFILE fromFile, YMStreamRef toStream, const uint64_t *nBytesPtr, bool sync, ym_connection_forward_context_t *callbackInfo);
bool YMAPI YMConnectionForwardStream(YMConnectionRef connection, YMStreamRef fromStream, YMFILE toFile, const uint64_t *nBytesPtr, bool sync, ym_connection_forward_context_t *callbackInfo);

YM_EXTERN_C_POP

#endif /* YMConnection_h */
