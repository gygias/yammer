//
//  YMCompression.h
//  yammer
//
//  Created by david on 4/1/16.
//  Copyright © 2016 combobulated. All rights reserved.
//

#ifndef YMCompression_h
#define YMCompression_h

typedef const struct __ym_compression_t *YMCompressionRef;

YMCompressionRef YMCompressionCreate(YMCompressionType, YMFILE);

bool YMAPI YMCompressionInit(YMCompressionRef c);
bool YMAPI YMCompressionRead(YMCompressionRef c, uint8_t *b, size_t l);
bool YMAPI YMCompressionWrite(YMCompressionRef c, const uint8_t *b, size_t l);
bool YMAPI YMCompressionClose(YMCompressionRef c);

YM_EXTERN_C_POP

#endif /* YMCompression_h */
