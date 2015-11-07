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

YMPlexerRef YMPlexerCreate(char *name, int inputFile, int outputFile, bool master);
YMPlexerRef YMPlexerCreateWithFullDuplexFile(char *name, int file, bool master);

// init
void YMPlexerSetInterruptedFunc(YMPlexerRef plexer, ym_plexer_interrupted_func func);
void YMPlexerSetNewIncomingStreamFunc(YMPlexerRef plexer, ym_plexer_new_upstream_func func);
void YMPlexerSetStreamClosingFunc(YMPlexerRef plexer, ym_plexer_stream_closing_func func);

// set an initialized security provider to wrap the plexed data
void YMPlexerSetSecurityProvider(YMPlexerRef plexer, YMTypeRef provider); // unsure how to handle this poly in c (yet?)

bool YMPlexerStart(YMPlexerRef plexer);
void YMPlexerStop(YMPlexerRef plexer);

YMStreamRef YMPlexerCreateNewStream(YMPlexerRef plexer, const char *name, bool direct);
void YMPlexerCloseStream(YMPlexerRef plexer, YMStreamRef stream);

#endif /* YMPlexer_h */
