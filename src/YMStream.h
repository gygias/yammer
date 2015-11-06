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

bool YMStreamWriteDown(YMStreamRef stream, const void *buffer, uint32_t length);

bool YMStreamReadUp(YMStreamRef stream, void *buffer, uint32_t length);

#pragma message "todo write up and read down should be private"


#endif /* YMStream_h */
