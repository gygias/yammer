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

void YMStreamWriteDown(YMStreamRef stream, const void *buffer, uint16_t length);
void YMStreamReadUp(YMStreamRef stream, void *buffer, uint16_t length);

#endif /* YMStream_h */
