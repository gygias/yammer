//
//  YMConnection.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMConnection_h
#define YMConnection_h

#ifdef __cplusplus
extern "C" {
#endif

#include <libyammer/YMBase.h>

#include <libyammer/YMStream.h>
#include <libyammer/YMAddress.h>

#ifdef WIN32
#include <winsock2.h>
#endif

typedef const struct __ym_connection_t *YMConnectionRef;

YMAPI YMAddressRef YMConnectionGetAddress(YMConnectionRef connection);
YMAPI YMStreamRef YMConnectionCreateStream(YMConnectionRef connection, YMStringRef name);
YMAPI void YMConnectionCloseStream(YMConnectionRef connection, YMStreamRef stream);
    
typedef void (*ym_connection_forward_callback)(YMConnectionRef, YMStreamRef, YMIOResult, uint64_t, void *);
typedef struct ym_connection_forward_context_t
{
    ym_connection_forward_callback callback;
    void * context;
} ym_connection_forward_context_t;
    
YMAPI bool YMConnectionForwardFile(YMConnectionRef connection, YMFILE fromFile, YMStreamRef toStream, const uint64_t *nBytesPtr, bool sync, ym_connection_forward_context_t *callbackInfo);
YMAPI bool YMConnectionForwardStream(YMConnectionRef connection, YMStreamRef fromStream, YMFILE toFile, const uint64_t *nBytesPtr, bool sync, ym_connection_forward_context_t *callbackInfo);

#ifdef __cplusplus
}
#endif

#endif /* YMConnection_h */
