//
//  YMStreamPriv.h
//  yammer
//
//  Created by david on 11/5/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMStreamPriv_h
#define YMStreamPriv_h

#include "YMStream.h"

#include "YMLock.h" // for _GetRetainLock only, which should be refactored to work like incoming close/free coordination

typedef int32_t YMStreamChunkSize;

typedef enum
{
    YMStreamClose = -1
} YMStreamCommands;

typedef struct _YMStreamChunkHeader
{
    YMStreamCommands command;
} YMStreamCommand;

typedef struct _ym_stream_user_info_def
{
    YMStringRef name;
} __ym_stream_user_info_def;
typedef struct _ym_stream_user_info_def ym_stream_user_info;
typedef struct ym_stream_user_info *ym_stream_user_info_ref;

YMStreamRef _YMStreamCreate(YMStringRef name, ym_stream_user_info_ref userInfo);
typedef void (*_ym_stream_data_available_func)(void *);
void _YMStreamSetDataAvailableCallback(YMStreamRef stream, _ym_stream_data_available_func, void *ctx);

void _YMStreamDesignatedFree(YMStreamRef stream );

void _YMStreamReadDown(YMStreamRef stream, void *buffer, uint32_t length);
void _YMStreamWriteUp(YMStreamRef stream, const void *buffer, uint32_t length);
void _YMStreamClose(YMStreamRef stream);

// these should only be used by the plexer (!!) otherwise hilarity will ensue
int _YMStreamGetDownwardRead(YMStreamRef);
int _YMStreamGetUpstreamWrite(YMStreamRef);

ym_stream_user_info_ref _YMStreamGetUserInfo(YMStreamRef);
YMStringRef _YMStreamGetName(YMStreamRef stream);

#endif /* YMStreamPriv_h */
