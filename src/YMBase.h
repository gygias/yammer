//
//  YMBase.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMBase_h
#define YMBase_h

#define YM_VARGS_SENTINEL_REQUIRED __attribute__((sentinel(0,1)))

// from what i read, a bool yn = ptr warning can't be a thing
// this is here to periodically check for bugs like one where we were assigning a pointer to a bool
#ifdef YM_TEST_BOOL_USE
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuninitialized"
#undef bool
#define bool char
#define ymbool char // this is for code that gets stdbool from 3rd party headers, like mDNS
#define true 1
#define false 0
#pragma GCC diagnostic pop
#else
#include <stdbool.h>
#define ymbool bool
#endif

#ifdef FIGURED_OUT_HOW_TO_GET_COMPILER_WARNINGS
typedef struct ym_not_a_void_star
{
    char notAThing[1];
} _ym_not_a_void_star;

typedef struct ym_not_a_void_star *YMTypeRef;
#else
typedef const void *YMTypeRef;
#endif
typedef char YMTypeID;

YMTypeRef YMRetain(YMTypeRef object);
YMTypeRef YMAutorelease(YMTypeRef object);
void YMRelease(YMTypeRef object);

#define YM_WPPUSH \
    _Pragma("GCC diagnostic push") \
    _Pragma("GCC diagnostic ignored \"-Wpedantic\"")
#define YM_WPOP \
    _Pragma("GCC diagnostic pop")

#define YMSTR(x) YMStringGetCString(x)
#define YMSTRC(x) YMAutorelease(YMStringCreateWithCString(x))
// Token pasting of ',' and __VA_ARGS__ is a GNU extension
YM_WPPUSH
#define YMSTRCF(x,...) YMAutorelease(YMStringCreateWithFormat((x),##__VA_ARGS__))
YM_WPOP
#define YMLEN(x) YMStringGetLength(x)

#endif /* YMBase_h */
