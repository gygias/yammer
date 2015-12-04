//
//  YMStream.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMStream_h
#define YMStream_h

YM_EXTERN_C_PUSH

#include <libyammer/YMBase.h>

typedef const struct __ym_stream_t *YMStreamRef;

YMAPI YMIOResult YMStreamWriteDown(YMStreamRef stream, const void *buffer, uint16_t length);
YMAPI YMIOResult YMStreamReadUp(YMStreamRef stream_, void *buffer, uint16_t length, uint16_t *outLength);
YMAPI YMIOResult YMStreamReadFromFile(YMStreamRef stream, YMFILE file, uint64_t *inBytes, uint64_t *outBytes);
YMAPI YMIOResult YMStreamWriteToFile(YMStreamRef stream, YMFILE file, uint64_t *inBytes, uint64_t *outBytes);

YM_EXTERN_C_POP

#endif /* YMStream_h */
