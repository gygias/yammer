//
//  YMNumber.c
//  yammer
//
//  Created by david on 4/9/16.
//  Copyright © 2016 combobulated. All rights reserved.
//

#include "YMNumber.h"

YM_EXTERN_C_PUSH

typedef enum YMNumberType {
    YMNumberSigned,
    YMNumberUnsigned,
    YMNumberFloat
} YMNumberType;

typedef struct __ym_number
{
    _YMType _common;
    
    YMNumberType type;
    double d;
    bool s;
    uint64_t u;
} __ym_number;
typedef struct __ym_number __ym_number_t;

void _YMNumberFree(__unused YMNumberRef object)
{
}

YMNumberRef YMNumberCreateWithDouble(double d)
{
    __ym_number_t *n = (__ym_number_t *)_YMAlloc(_YMNumberTypeID, sizeof(__ym_number));
    n->type = YMNumberFloat;
    n->d = d;
    return n;
}

YMNumberRef YMNumberCreateWithInt32(int32_t i)
{
    __ym_number_t *n = (__ym_number_t *)_YMAlloc(_YMNumberTypeID, sizeof(__ym_number));
    n->type = YMNumberSigned;
    n->u = (uint64_t)i;
    n->s = ( i < 0 );
    return n;
}

YMNumberRef YMNumberCreateWithUInt32(uint32_t u)
{
    __ym_number_t *n = (__ym_number_t *)_YMAlloc(_YMNumberTypeID, sizeof(__ym_number));
    n->type = YMNumberUnsigned;
    n->u = (uint64_t)u;
    return n;
}

YMNumberRef YMNumberCreateWithInt64(int64_t i)
{
    __ym_number_t *n = (__ym_number_t *)_YMAlloc(_YMNumberTypeID, sizeof(__ym_number));
    n->type = YMNumberSigned;
    n->u = (uint64_t)i;
    n->s = ( i < 0 );
    return n;
}

YMNumberRef YMNumberCreateWithUInt64(uint64_t u)
{
    __ym_number_t *n = (__ym_number_t *)_YMAlloc(_YMNumberTypeID, sizeof(__ym_number));
    n->type = YMNumberUnsigned;
    n->u = (uint64_t)u;
    return n;
}

double   YMNumberDoubleValue(YMNumberRef n)
{
    if ( n->type == YMNumberFloat )
        return n->d;
    
    double outD = (double)n->u;
    if ( n->type == YMNumberSigned && n->s )
        outD *= -1;
    return outD;
}

int32_t  YMNumberInt32Value(YMNumberRef n)
{
    int32_t outI;
    
    if ( n->type == YMNumberFloat )
        outI = (int32_t)n->d;
    else {
        outI = (int32_t)n->u;
        if ( n->type == YMNumberSigned && n->s )
            outI *= -1;
    }
    
    return outI;
}

uint32_t YMNumberUInt32Value(YMNumberRef n)
{
    uint32_t outI;
    
    if ( n->type == YMNumberFloat )
        outI = (uint32_t)n->d;
    else
        outI = (uint32_t)n->u;
    
    return outI;
}

int64_t  YMNumberInt64Value(YMNumberRef n)
{
    int64_t outI;
    
    if ( n->type == YMNumberFloat )
        outI = (int64_t)n->d;
    else {
        outI = (int64_t)n->u;
        if ( n->type == YMNumberSigned && n->s )
            outI *= -1;
    }
    
    return outI;
}

uint64_t YMNumberUInt64Value(YMNumberRef n)
{
    uint64_t outI;
    
    if ( n->type == YMNumberFloat )
        outI = (uint64_t)n->d;
    else
        outI = (uint64_t)n->u;
    
    return outI;
}

YM_EXTERN_C_POP
