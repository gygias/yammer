//
//  YMPlexer.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMPlexer_h
#define YMPlexer_h

#include "YMBase.h"

#include "YMSecurityProvider.h"
#include "YMStream.h"

typedef const struct __ym_plexer_t *YMPlexerRef; // CF defines const, how to shadow writeable struct?

typedef void (*ym_plexer_interrupted_func)      (YMPlexerRef,void*);
typedef void (*ym_plexer_new_upstream_func)     (YMPlexerRef,YMStreamRef,void*);
typedef void (*ym_plexer_stream_closing_func)   (YMPlexerRef,YMStreamRef,void*);

YMPlexerRef YMAPI YMPlexerCreate(YMStringRef name, YMSecurityProviderRef provider, bool master);

// init
void YMAPI YMPlexerSetInterruptedFunc(YMPlexerRef plexer, ym_plexer_interrupted_func func);
void YMAPI YMPlexerSetNewIncomingStreamFunc(YMPlexerRef plexer, ym_plexer_new_upstream_func func);
void YMAPI YMPlexerSetStreamClosingFunc(YMPlexerRef plexer, ym_plexer_stream_closing_func func);
void YMAPI YMPlexerSetCallbackContext(YMPlexerRef plexer, void *context);

bool YMAPI YMPlexerStart(YMPlexerRef plexer);
// closes the file (or files) associated with this plexer
bool YMAPI YMPlexerStop(YMPlexerRef plexer);

YMStreamRef YMAPI YMPlexerCreateStream(YMPlexerRef plexer, YMStringRef name);
void YMAPI YMPlexerCloseStream(YMPlexerRef plexer, YMStreamRef stream);

#endif /* YMPlexer_h */
