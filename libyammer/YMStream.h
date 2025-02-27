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

typedef const struct __ym_stream * YMStreamRef;

YMAPI YMIOResult YMStreamWriteDown(YMStreamRef stream, const uint8_t *buffer, size_t length);
// out parameters are for unbounded (unpacked) stream support
YMAPI YMIOResult YMStreamReadUp(YMStreamRef stream_, uint8_t *buffer, size_t length, size_t *outLength);
YMAPI YMIOResult YMStreamReadFromFile(YMStreamRef stream, YMFILE file, size_t *inBytes, size_t *outBytes);
YMAPI YMIOResult YMStreamWriteToFile(YMStreamRef stream, YMFILE file, size_t *inBytes, size_t *outBytes);

YMAPI void YMStreamGetPerformance(YMStreamRef stream, uint64_t *oDownIn, uint64_t *oDownOut, uint64_t *oUpIn, uint64_t *oUpOut);

YM_EXTERN_C_POP

#endif /* YMStream_h */
