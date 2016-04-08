//
//  YMCompression.h
//  yammer
//
//  Created by david on 4/1/16.
//  Copyright © 2016 combobulated. All rights reserved.
//

#ifndef YMCompression_h
#define YMCompression_h

typedef const struct __ym_compression * YMCompressionRef;

YMCompressionRef YMAPI YMCompressionCreate(YMCompressionType, YMFILE, bool);

bool YMAPI YMCompressionInit(YMCompressionRef c);
YMIOResult YMAPI YMCompressionRead(YMCompressionRef c, uint8_t *b, size_t l, size_t *o);
YMIOResult YMAPI YMCompressionWrite(YMCompressionRef c, const uint8_t *b, size_t l, size_t *o);
bool YMAPI YMCompressionClose(YMCompressionRef c);

YM_EXTERN_C_POP

#endif /* YMCompression_h */
