//
//  YMSocket.h
//  yammer
//
//  Created by david on 4/8/16.
//  Copyright © 2016 combobulated. All rights reserved.
//

#ifndef YMSocket_h
#define YMSocket_h

// YMSocket intends to encapsulate the ability to replace the "underlying medium" (the network socket)
// out from underneath the various i/o layers (compression, security, plexer), without their awareness,
// or needing to reconcile in-flight state of, tls, compression dictionaries, user data caught in the middle,
// etc. etc. Data is opaque to the "socket" (which could be called something like "journaled" or "resumable")
// but I'm all hot for short names right now.

YM_EXTERN_C_PUSH

typedef const struct __ym_socket * YMSocketRef;

YMSocketRef YMSocketCreate();

bool YMSocketSet(YMSocketRef, YMSOCKET);

YMIOResult YMSocketWrite(YMSocketRef, const uint8_t *, size_t);
YMIOResult YMSocketRead(YMSocketRef, uint8_t *, size_t);

YM_EXTERN_C_POP

#endif /* YMSocket_h */
