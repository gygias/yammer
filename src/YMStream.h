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

typedef const struct _YMStream *YMStreamRef;

YMStreamRef YMStreamCreate(char *name);

bool YMStreamRead(YMStreamRef stream, void *buffer, size_t length);
bool YMStreamWrite(YMStreamRef stream, void *buffer, size_t length);

bool YMStreamClose(YMStreamRef stream);

#endif /* YMStream_h */
