//
//  YMConnection.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMConnection_h
#define YMConnection_h

#include <yammer/YMBase.h>

#include <yammer/YMConnection.h>
#include <yammer/YMStream.h>
#include <yammer/YMAddress.h>

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

YMConnectionRef YMConnectionCreate(YMAddressRef address, YMConnectionType type, YMConnectionSecurityType securityType);
YMConnectionRef YMConnectionCreateIncoming(int socket, YMAddressRef address, YMConnectionType type, YMConnectionSecurityType securityType);
void YMConnectionSetCallbacks(YMConnectionRef connection,
                              ym_connection_new_stream_func newFunc, void *newFuncContext,
                              ym_connection_stream_closing_func closingFunc, void *closingFuncContext,
                              ym_connection_interrupted_func interruptedFunc, void *interruptedFuncContext);
bool YMConnectionConnect(YMConnectionRef connection);

uint64_t YMConnectionDoSample(YMConnectionRef connection);
YMAddressRef YMConnectionGetAddress(YMConnectionRef connection);

YMStreamRef YMConnectionCreateStream(YMConnectionRef connection, YMStringRef name);
void YMConnectionCloseStream(YMConnectionRef connection, YMStreamRef stream); // not thread safe

#endif /* YMConnection_h */
