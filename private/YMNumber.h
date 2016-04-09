//
//  YMNumber.h
//  yammer
//
//  Created by david on 4/9/16.
//  Copyright © 2016 combobulated. All rights reserved.
//

#ifndef YMNumber_h
#define YMNumber_h

YM_EXTERN_C_PUSH

// YMNumber was added for the few places where we (had) a mixture of YMTypes and unmanaged types in
// collections, to avoid having to write one-off "free mixed type collection" functions.
// Please don't use this if not for that purpose.

typedef const struct __ym_number * YMNumberRef;

YMNumberRef YMNumberCreateWithDouble(double);
YMNumberRef YMNumberCreateWithInt32(int32_t);
YMNumberRef YMNumberCreateWithUInt32(uint32_t);
YMNumberRef YMNumberCreateWithInt64(int64_t);
YMNumberRef YMNumberCreateWithUInt64(uint64_t);

double   YMNumberDoubleValue(YMNumberRef);
int32_t  YMNumberInt32Value(YMNumberRef);
uint32_t YMNumberUInt32Value(YMNumberRef);
int64_t  YMNumberInt64Value(YMNumberRef);
uint64_t YMNumberUInt64Value(YMNumberRef);

YM_EXTERN_C_POP


#endif /* YMNumber_h */
