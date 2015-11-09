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

#pragma message "when the other 5 things are put here, this should be defined by plexer not stream"
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

// exposed to plexer and user, in order to synchronize actually deallocating the object
// if remote is the last to write to a stream, we may get the close command before the user is done
// flushing the upstream. if the user is the last to write to the stream, we need to wait for the plexer
// to forward the user message.
void _YMStreamRemoteSetPlexerReleased(YMStreamRef stream);
void _YMStreamRemoteSetUserReleased(YMStreamRef stream);
YMLockRef _YMStreamGetRetainLock(YMStreamRef stream); // for coordinating outgoing closure and deallocation, should probably be used for, or converted to, the remote stream _Set*Released model

int _YMStreamGetDownwardWrite(YMStreamRef);
int _YMStreamGetDownwardRead(YMStreamRef);
int _YMStreamGetUpstreamWrite(YMStreamRef);
int _YMStreamGetUpstreamRead(YMStreamRef);
YMStreamUserInfoRef _YMStreamGetUserInfo(YMStreamRef);
void _YMStreamSetDataAvailableSemaphore(YMStreamRef stream, YMSemaphoreRef semaphore);

void _YMStreamSetLastServiceTimeNow(YMStreamRef stream);
struct timeval *_YMStreamGetLastServiceTime(YMStreamRef stream);

bool _YMStreamIsLocallyOriginated(YMStreamRef stream);

const char *_YMStreamGetName(YMStreamRef stream);

#endif /* YMStreamPriv_h */
