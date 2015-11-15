//
//  YMStream.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMStream_h
#define YMStream_h

#include "YMUtilities.h"

#include "YMSemaphore.h"

typedef YMTypeRef YMStreamRef;

void YMStreamWriteDown(YMStreamRef stream, const void *buffer, uint16_t length);
YMIOResult YMStreamReadUp(YMStreamRef stream_, void *buffer, uint16_t length, uint16_t *outLength);
YMIOResult YMStreamReadFromFile(YMStreamRef stream, int file, uint64_t *inBytes, uint64_t *outBytes);
YMIOResult YMStreamWriteToFile(YMStreamRef stream, int file, uint64_t *inBytes, uint64_t *outBytes);

#endif /* YMStream_h */
