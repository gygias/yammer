//
//  YMStreamPriv.h
//  yammer
//
//  Created by david on 11/5/15.
//  Copyright © 2015 Combobulated Software. All rights reserved.
//

#ifndef YMStreamPriv_h
#define YMStreamPriv_h

#include "YMStream.h"

typedef uint32_t YMStreamID;
#define YMStreamIDMax UINT32_MAX

typedef int32_t YMStreamChunkSize;

typedef enum
{
    YMStreamClose = -1
} YMStreamCommands;

typedef struct _YMStreamChunkHeader
{
    YMStreamCommands command;
} YMStreamCommand;

typedef struct __YMStreamUserInfo
{
    YMStreamID streamID;
} _YMStreamUserInfo;

// for plexer only, these are not owned by the stream
typedef struct __YMStreamUserInfo *YMStreamUserInfoRef;

YMStreamRef YMStreamCreate(const char *name, bool isLocal, YMStreamUserInfoRef userInfo);

void _YMStreamReadDown(YMStreamRef stream, void *buffer, uint32_t length);
void _YMStreamWriteUp(YMStreamRef stream, const void *buffer, uint32_t length);
void _YMStreamClose(YMStreamRef stream);
void _YMStreamCloseUp(YMStreamRef stream);
bool _YMStreamIsClosed(YMStreamRef stream);

int _YMStreamGetDownwardWrite(YMStreamRef);
int _YMStreamGetDownwardRead(YMStreamRef);
int _YMStreamGetUpstreamWrite(YMStreamRef);
int _YMStreamGetUpstreamRead(YMStreamRef);
YMStreamUserInfoRef _YMStreamGetUserInfo(YMStreamRef);
void _YMStreamSetDataAvailableSemaphore(YMStreamRef stream, YMSemaphoreRef semaphore);

void _YMStreamSetLastServiceTimeNow(YMStreamRef stream);
struct timeval *_YMStreamGetLastServiceTime(YMStreamRef stream);

#endif /* YMStreamPriv_h */
