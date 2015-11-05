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

bool YMStreamWriteDown(YMStreamRef stream, uint8_t *buffer, uint32_t length);
bool YMStreamReadDown(YMStreamRef stream, uint8_t *buffer, uint32_t length);

bool YMStreamWriteUp(YMStreamRef stream, uint8_t *buffer, uint32_t length);
bool YMStreamReadUp(YMStreamRef stream, uint8_t *buffer, uint32_t length);

#warning todo write up and read down should be private

bool YMStreamClose(YMStreamRef stream);

#endif /* YMStream_h */
