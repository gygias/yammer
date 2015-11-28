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

#include <libyammer/YMConnection.h>
#include <libyammer/YMStream.h>
#include <libyammer/YMAddress.h>

#ifdef WIN32
#include <winsock2.h>
#endif

typedef const struct __ym_connection *YMConnectionRef;

typedef enum
{
    YMConnectionStream = 0
} YMConnectionType;

typedef enum
{
    YMInsecure = 0,
    YMTLS
} YMConnectionSecurityType;

typedef void (*ym_connection_new_stream_func)(YMConnectionRef,YMStreamRef,void*);
typedef void (*ym_connection_stream_closing_func)(YMConnectionRef,YMStreamRef,void*);
typedef void (*ym_connection_interrupted_func)(YMConnectionRef,void*);

YMAPI YMConnectionRef YMConnectionCreate(YMAddressRef address, YMConnectionType type, YMConnectionSecurityType securityType);
YMAPI YMConnectionRef YMConnectionCreateIncoming(YMSOCKET socket, YMAddressRef address, YMConnectionType type, YMConnectionSecurityType securityType);
YMAPI void YMConnectionSetCallbacks(YMConnectionRef connection,
                              ym_connection_new_stream_func newFunc, void *newFuncContext,
                              ym_connection_stream_closing_func closingFunc, void *closingFuncContext,
                              ym_connection_interrupted_func interruptedFunc, void *interruptedFuncContext);
YMAPI bool YMConnectionConnect(YMConnectionRef connection);

YMAPI uint64_t YMConnectionDoSample(YMConnectionRef connection);
YMAPI YMAddressRef YMConnectionGetAddress(YMConnectionRef connection);

YMAPI YMStreamRef YMConnectionCreateStream(YMConnectionRef connection, YMStringRef name);
YMAPI void YMConnectionCloseStream(YMConnectionRef connection, YMStreamRef stream);

#ifdef __cplusplus
}
#endif

#endif /* YMConnection_h */
