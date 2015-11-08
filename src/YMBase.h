//
//  YMBase.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMBase_h
#define YMBase_h

// from what i read, a bool yn = ptr warning can't be a thing
#define YM_TEST_BOOL
#ifdef YM_TEST_BOOL

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

#include <stdio.h>
#include <unistd.h>

#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "YMUtilities.h"
#include "YMLog.h"

typedef const void *YMTypeRef;
typedef char YMTypeID;

void YMFree(YMTypeRef object);

typedef bool (*ym_read_func)(int,const uint8_t*,size_t);
typedef bool (*ym_write_func)(int,const uint8_t*,size_t);

#endif /* YMBase_h */
