//
//  YMStreamPriv.h
//  yammer
//
//  Created by david on 11/5/15.
//  Copyright © 2015 Combobulated Software. All rights reserved.
//

#ifndef YMStreamPriv_h
#define YMStreamPriv_h


#endif /* YMStreamPriv_h */

typedef uint32_t YMStreamID;
#define YMStreamIDMax UINT32_MAX

typedef uint32_t YMStreamChunkSize;

typedef struct _YMStreamChunkHeader
{
    YMStreamChunkSize length;
} YMStreamChunkHeader;

typedef struct __YMStreamUserInfo
{
    YMStreamID streamID;
} _YMStreamUserInfo;

YMStreamRef YMStreamCreate(const char *name, bool isLocal);


bool _YMStreamReadDown(YMStreamRef stream, void *buffer, uint32_t length);
bool _YMStreamWriteUp(YMStreamRef stream, const void *buffer, uint32_t length);
bool _YMStreamClose(YMStreamRef stream);

int _YMStreamGetDownwardWrite(YMStreamRef);
int _YMStreamGetDownwardRead(YMStreamRef);
int _YMStreamGetUpstreamWrite(YMStreamRef);
int _YMStreamGetUpstreamRead(YMStreamRef);

// for plexer only, these are not owned by the stream
typedef struct __YMStreamUserInfo *YMStreamUserInfoRef;
void _YMStreamSetUserInfo(YMStreamRef, YMStreamUserInfoRef);
YMStreamUserInfoRef _YMStreamGetUserInfo(YMStreamRef);
void _YMStreamSetDataAvailableSemaphore(YMStreamRef stream, YMSemaphoreRef semaphore);

void _YMStreamSetLastServiceTimeNow(YMStreamRef stream);
struct timeval *_YMStreamGetLastServiceTime(YMStreamRef stream);