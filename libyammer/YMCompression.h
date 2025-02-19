//
//  YMCompression.h
//  yammer
//
//  Created by david on 4/1/16.
//  Copyright © 2016 combobulated. All rights reserved.
//

#ifndef YMCompression_h
#define YMCompression_h

typedef enum YMCompressionType {
    YMCompressionNone = 0,
#if !defined(YMWIN32)
#if YM_IMPLEMENTED
    YMCompressionGZ = 100,
    YMCompressionBZ2 = 200,
    YMCompressionLZ = 300,
#endif
    YMCompressionLZ4 = 400,
#endif
    YMCompressionMax = UINT16_MAX // wow!
} YMCompressionType;

typedef const struct __ym_compression * YMCompressionRef;

#define YM_COMPRESSION_LENGTH size_t // todo

YMCompressionRef YMAPI YMCompressionCreate(YMCompressionType, YMFILE, bool);

bool YMAPI YMCompressionInit(YMCompressionRef c);
YMIOResult YMAPI YMCompressionRead(YMCompressionRef c, uint8_t *b, YM_COMPRESSION_LENGTH l, YM_COMPRESSION_LENGTH *o);
YMIOResult YMAPI YMCompressionWrite(YMCompressionRef c, const uint8_t *b, YM_COMPRESSION_LENGTH l, YM_COMPRESSION_LENGTH *o, YM_COMPRESSION_LENGTH *oh);
bool YMAPI YMCompressionClose(YMCompressionRef c);

void YMCompressionGetPerformance(YMCompressionRef c, uint64_t *oIn, uint64_t *oOut);

YM_EXTERN_C_POP

#endif /* YMCompression_h */
