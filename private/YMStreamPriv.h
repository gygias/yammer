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

#include "YMCompression.h"

YM_EXTERN_C_PUSH

typedef int64_t _YMStreamCommandType;
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

typedef struct _ym_stream_user_info
{
    YMStringRef name;
} _ym_stream_user_info;
typedef struct _ym_stream_user_info ym_stream_user_info_t;
typedef void (*_ym_stream_free_user_info_func)(YMStreamRef);

YMStreamRef _YMStreamCreate(YMStringRef name, ym_stream_user_info_t *userInfo, YMFILE *downOut);
bool _YMStreamSetCompression(YMStreamRef stream, YMCompressionType compression);

// with dispatchify, only exposed to keep lower-level fds private
YM_IO_RESULT _YMStreamPlexReadDown(YMStreamRef stream, void *buffer, size_t length);
YMIOResult _YMStreamPlexWriteUp(YMStreamRef stream, const void *buffer, size_t length);

// bypasses compression for stream close
YMIOResult _YMStreamPlexWriteDown(YMStreamRef s, const uint8_t *buffer, size_t length);

void _YMStreamCloseWriteUp(YMStreamRef stream);

ym_stream_user_info_t * _YMStreamGetUserInfo(YMStreamRef);
YMStringRef _YMStreamGetName(YMStreamRef stream);

YM_EXTERN_C_POP

#endif /* YMStreamPriv_h */
