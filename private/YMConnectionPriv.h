//
//  YMConnectionPriv.h
//  yammer
//
//  Created by david on 11/15/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMConnectionPriv_h
#define YMConnectionPriv_h

YM_EXTERN_C_PUSH

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

YMConnectionRef YMAPI YMConnectionCreate(YMAddressRef address, YMConnectionType type, YMConnectionSecurityType securityType, bool closeWhenDone);
YMConnectionRef YMAPI YMConnectionCreateIncoming(YMSOCKET socket, YMAddressRef address, YMConnectionType type, YMConnectionSecurityType securityType, bool closeWhenDone);
void YMAPI YMConnectionSetCallbacks(YMConnectionRef connection,
                                    ym_connection_new_stream_func newFunc, void *newFuncContext,
                                    ym_connection_stream_closing_func closingFunc, void *closingFuncContext,
                                    ym_connection_interrupted_func interruptedFunc, void *interruptedFuncContext);
bool YMAPI YMConnectionConnect(YMConnectionRef connection);
bool YMAPI YMConnectionInit(YMConnectionRef connection);

uint64_t YMAPI YMConnectionDoSample(YMConnectionRef connection);
bool YMAPI YMConnectionClose(YMConnectionRef connection);

YM_EXTERN_C_POP

#endif /* YMConnectionPriv_h */
