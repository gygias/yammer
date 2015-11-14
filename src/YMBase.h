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
#else
#include <stdbool.h>
#define ymbool bool
#endif

typedef const void *YMTypeRef;
typedef char YMTypeID;

YMTypeRef YMRetain(YMTypeRef object);
void YMRelease(YMTypeRef object);

#define YMSTR(x) YMStringGetCString(x)
#define YMSTRC(x) YMStringCreateWithCString(x)
#define YMLEN(x) YMStringGetLength(x)

#endif /* YMBase_h */
