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

typedef int32_t _YMStreamCommandType;
typedef uint64_t _YMStreamBoundType;

typedef enum _YMStreamCommandType
{
    YMStreamClose = -1,
    YMStreamForwardFileBounded = -2,
    YMStreamForwardFileUnbounded = -3,
    YMStreamForwardFileEnd = -4
} _YMStreamCommands;

typedef struct _ym_stream_command_t
{
    _YMStreamCommandType command;
} __ym_stream_command_t;
typedef struct _ym_stream_command_t _YMStreamCommand;

//typedef struct _ym_stream_bounded_command_t
//{
//    _YMStreamCommand command;
//    _YMStreamBoundType length; // if zero, the file size is unbounded (not known), a forward-file-end command will follow
//} __ym_stream_bounded_command_t;
//typedef struct _ym_stream_bounded_command_t _YMStreamBoundedCommand;

typedef struct _ym_stream_user_info_t
{
    YMStringRef name;
} _ym_stream_user_info_t;
typedef struct _ym_stream_user_info_t *ym_stream_user_info_ref;
typedef void (*_ym_stream_free_user_info_func)(YMStreamRef);

YMStreamRef _YMStreamCreate(YMStringRef name, ym_stream_user_info_ref userInfo, _ym_stream_free_user_info_func callback);
typedef void (*_ym_stream_data_available_func)(YMStreamRef,uint32_t,void *);
void _YMStreamSetDataAvailableCallback(YMStreamRef stream, _ym_stream_data_available_func, void *ctx);

YMIOResult _YMStreamReadDown(YMStreamRef stream, void *buffer, uint32_t length);
YMIOResult _YMStreamWriteUp(YMStreamRef stream, const void *buffer, uint32_t length);

void _YMStreamCloseWriteUp(YMStreamRef stream);
void _YMStreamSendClose(YMStreamRef stream);

ym_stream_user_info_ref _YMStreamGetUserInfo(YMStreamRef);
YMStringRef _YMStreamGetName(YMStreamRef stream);

#endif /* YMStreamPriv_h */
