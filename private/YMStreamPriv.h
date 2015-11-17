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

typedef int32_t YMStreamCommand;

typedef enum
{
    YMStreamClose = -1
} YMStreamCommands;

typedef struct _YMStreamHeader
{
    YMStreamCommand command;
} YMStreamHeader;

typedef struct _ym_stream_user_info_def
{
    YMStringRef name;
} __ym_stream_user_info_def;
typedef struct _ym_stream_user_info_def ym_stream_user_info;
typedef struct ym_stream_user_info *ym_stream_user_info_ref;

YMStreamRef _YMStreamCreate(YMStringRef name, ym_stream_user_info_ref userInfo);
typedef void (*_ym_stream_data_available_func)(YMStreamRef,uint32_t,void *);
void _YMStreamSetDataAvailableCallback(YMStreamRef stream, _ym_stream_data_available_func, void *ctx);

YMIOResult _YMStreamReadDown(YMStreamRef stream, void *buffer, uint32_t length);
YMIOResult _YMStreamWriteUp(YMStreamRef stream, const void *buffer, uint32_t length);

void _YMStreamCloseReadUpFile(YMStreamRef stream);
void _YMStreamSendClose(YMStreamRef stream);

ym_stream_user_info_ref _YMStreamGetUserInfo(YMStreamRef);
YMStringRef _YMStreamGetName(YMStreamRef stream);

#endif /* YMStreamPriv_h */
