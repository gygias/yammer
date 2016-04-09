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

typedef void (*ym_socket_disconnected)(YMSocketRef, const void*);

YMSocketRef YMSocketCreate(ym_socket_disconnected, const void*);

bool YMSocketSet(YMSocketRef, YMSOCKET);
void YMSocketSetPassthrough(YMSocketRef, bool);

YMFILE YMSocketGetInput(YMSocketRef);
YMFILE YMSocketGetOutput(YMSocketRef);

YM_EXTERN_C_POP

#endif /* YMSocket_h */
