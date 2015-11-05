//
//  YMStream.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMStream_h
#define YMStream_h

#include "YMBase.h"

#include "YMSemaphore.h"

typedef struct __YMStream *YMStreamRef;

YMStreamRef YMStreamCreate(char *name);

bool YMStreamRead(YMStreamRef stream, void *buffer, size_t length);
bool YMStreamWrite(YMStreamRef stream, void *buffer, size_t length);

bool YMStreamClose(YMStreamRef stream);

// for plexer only, these are not owned by the stream
void _YMStreamSetUserInfo(YMStreamRef stream, const void *);
const void *_YMStreamGetUserInfo(YMStreamRef stream);
void _YMStreamSetDataAvailableSemaphore(YMStreamRef stream, YMSemaphoreRef semaphore);

#endif /* YMStream_h */
