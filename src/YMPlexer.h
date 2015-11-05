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
#include "YMStream.h"

typedef struct __YMPlexer *YMPlexerRef; // CF defines const, how to shadow writeable struct?

typedef void (*ym_plexer_interrupted_func)      (YMPlexerRef);
typedef void (*ym_plexer_new_upstream_func)     (YMPlexerRef,YMStreamRef);
typedef void (*ym_plexer_stream_closing_func)   (YMPlexerRef,YMStreamRef);

YMPlexerRef YMPlexerCreate(int inFd, int outFd);
YMPlexerRef YMPlexerCreateWithFullDuplexFile(int fd);

// init
void YMPlexerSetInterruptedFunc(YMPlexerRef plexer, ym_plexer_interrupted_func func);
void YMPlexerSetNewIncomingStreamFunc(YMPlexerRef plexer, ym_plexer_new_upstream_func func);
void YMPlexerSetStreamClosingFunc(YMPlexerRef plexer, ym_plexer_stream_closing_func func);

// set an initialized security provider to wrap the plexed data
void YMPlexerSetSecurityProvider(YMPlexerRef plexer, YMTypeRef provider); // unsure how to handle this poly in c (yet?)

bool YMPlexerStart(YMPlexerRef plexer, bool master);
void YMPlexerStop(YMPlexerRef plexer);

YMStreamRef YMPlexerNewStream(YMPlexerRef plexer, char *name, bool direct);
void YMPlexerCloseStream(YMPlexerRef plexer, YMStreamRef stream);

#endif /* YMPlexer_h */
