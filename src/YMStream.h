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

typedef struct __YMStream *YMStreamRef;

YMStreamRef YMStreamCreate(char *name, void *userInfo);

bool YMStreamRead(YMStreamRef stream, void *buffer, size_t length);
bool YMStreamWrite(YMStreamRef stream, void *buffer, size_t length);

bool YMStreamClose(YMStreamRef stream);

// for plexer only
void _YMStreamSetUserInfo(YMStreamRef stream, const void *);
const void *_YMStreamGetUserInfo(YMStreamRef stream);

#endif /* YMStream_h */
