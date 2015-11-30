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

YMPlexerRef YMPlexerCreate(YMStringRef name, YMSecurityProviderRef provider, bool master);

// init
void YMPlexerSetInterruptedFunc(YMPlexerRef plexer, ym_plexer_interrupted_func func);
void YMPlexerSetNewIncomingStreamFunc(YMPlexerRef plexer, ym_plexer_new_upstream_func func);
void YMPlexerSetStreamClosingFunc(YMPlexerRef plexer, ym_plexer_stream_closing_func func);
void YMPlexerSetCallbackContext(YMPlexerRef plexer, void *context);

bool YMPlexerStart(YMPlexerRef plexer);
// closes the file (or files) associated with this plexer
bool YMPlexerStop(YMPlexerRef plexer);

YMStreamRef YMPlexerCreateStream(YMPlexerRef plexer, YMStringRef name);
void YMPlexerCloseStream(YMPlexerRef plexer, YMStreamRef stream);

#endif /* YMPlexer_h */
