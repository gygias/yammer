//
//  YMStream.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMStream_h
#define YMStream_h

#include <libyammer/YMBase.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef YMTypeRef YMStreamRef;

YMAPI YMIOResult YMStreamWriteDown(YMStreamRef stream, const void *buffer, uint16_t length);
YMAPI YMIOResult YMStreamReadUp(YMStreamRef stream_, void *buffer, uint16_t length, uint16_t *outLength);
YMAPI YMIOResult YMStreamReadFromFile(YMStreamRef stream, int file, uint64_t *inBytes, uint64_t *outBytes);
YMAPI YMIOResult YMStreamWriteToFile(YMStreamRef stream, int file, uint64_t *inBytes, uint64_t *outBytes);

#ifdef __cplusplus
}
#endif

#endif /* YMStream_h */
