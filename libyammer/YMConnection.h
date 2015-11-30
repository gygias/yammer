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

#ifdef __cplusplus
}
#endif

#endif /* YMConnection_h */
