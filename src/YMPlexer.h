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

typedef YMTypeRef YMPlexerRef; // CF defines const, how to shadow writeable struct?

typedef void (*ym_plexer_interrupted_func)      (YMPlexerRef,void*);
typedef void (*ym_plexer_new_upstream_func)     (YMPlexerRef,YMStreamRef,void*);
typedef void (*ym_plexer_stream_closing_func)   (YMPlexerRef,YMStreamRef,void*);

YMPlexerRef YMPlexerCreate(YMStringRef name, int inputFile, int outputFile, bool master);
YMPlexerRef YMPlexerCreateWithFullDuplexFile(YMStringRef name, int file, bool master);

// init
void YMPlexerSetInterruptedFunc(YMPlexerRef plexer, ym_plexer_interrupted_func func);
void YMPlexerSetNewIncomingStreamFunc(YMPlexerRef plexer, ym_plexer_new_upstream_func func);
void YMPlexerSetStreamClosingFunc(YMPlexerRef plexer, ym_plexer_stream_closing_func func);
void YMPlexerSetCallbackContext(YMPlexerRef plexer, void *context);

// set an initialized security provider to wrap the plexed data
void YMPlexerSetSecurityProvider(YMPlexerRef plexer, YMTypeRef provider); // unsure how to handle this poly in c (yet?)

bool YMPlexerStart(YMPlexerRef plexer);
void YMPlexerStop(YMPlexerRef plexer);

YMStreamRef YMPlexerCreateNewStream(YMPlexerRef plexer, YMStringRef name, bool direct);
void YMPlexerCloseStream(YMPlexerRef plexer, YMStreamRef stream);

// if a stream originates remotely, the client must release the (_upstream _read), which might be consumed out-of-band with remote closure notification
// this has to go through the plexer, because notify_close happens asynchronously
//void YMPlexerRemoteStreamRelease(YMPlexerRef plexer, YMStreamRef stream);

#endif /* YMPlexer_h */
