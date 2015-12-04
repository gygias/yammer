//
//  YMConnectionPriv.h
//  yammer
//
//  Created by david on 11/15/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMConnectionPriv_h
#define YMConnectionPriv_h

// "private" vs internal to allow for connection-level unit tests
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

YMConnectionRef YMAPI YMConnectionCreate(YMAddressRef address, YMConnectionType type, YMConnectionSecurityType securityType);
YMConnectionRef YMAPI YMConnectionCreateIncoming(YMSOCKET socket, YMAddressRef address, YMConnectionType type, YMConnectionSecurityType securityType);
void YMAPI YMConnectionSetCallbacks(YMConnectionRef connection,
                                    ym_connection_new_stream_func newFunc, void *newFuncContext,
                                    ym_connection_stream_closing_func closingFunc, void *closingFuncContext,
                                    ym_connection_interrupted_func interruptedFunc, void *interruptedFuncContext);
bool YMAPI YMConnectionConnect(YMConnectionRef connection);

uint64_t YMAPI YMConnectionDoSample(YMConnectionRef connection);
bool YMAPI YMConnectionClose(YMConnectionRef connection);

#endif /* YMConnectionPriv_h */
