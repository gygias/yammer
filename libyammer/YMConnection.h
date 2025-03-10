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
#include <libyammer/YMCompression.h>
#include <libyammer/YMUtilities.h>

#if defined(YMWIN32)
# include <winsock2.h>
#endif

typedef const struct __ym_connection * YMConnectionRef;

// interface / speed stuff
YMStringRef YMAPI YMConnectionGetLocalInterfaceName(YMConnectionRef connection);
YMInterfaceType YMAPI YMConnectionGetLocalInterface(YMConnectionRef connection);
YMInterfaceType YMAPI YMConnectionGetRemoteInterface(YMConnectionRef connection);
int64_t YMAPI YMConnectionGetSample(YMConnectionRef connection);
YMAddressRef YMAPI YMConnectionGetAddress(YMConnectionRef connection);

YMStreamRef YMAPI YMConnectionCreateStream(YMConnectionRef connection, YMStringRef name, YMCompressionType compression);
void YMAPI YMConnectionCloseStream(YMConnectionRef connection, YMStreamRef stream);
    
typedef void (*ym_connection_forward_callback)(YMConnectionRef, YMStreamRef, YMIOResult, size_t, void *);
typedef struct ym_connection_forward_context_t
{
    ym_connection_forward_callback callback;
    void * context;
} ym_connection_forward_context_t;
    
bool YMAPI YMConnectionForwardFile(YMConnectionRef connection, YMFILE fromFile, YMStreamRef toStream, size_t *nBytesPtr, bool sync, ym_connection_forward_context_t *callbackInfo);
bool YMAPI YMConnectionForwardStream(YMConnectionRef connection, YMStreamRef fromStream, YMFILE toFile, size_t *nBytesPtr, bool sync, ym_connection_forward_context_t *callbackInfo);

YM_EXTERN_C_POP

#endif /* YMConnection_h */
