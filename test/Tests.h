//
//  Tests.h
//  yammer
//
//  Created by david on 12/2/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef Tests_h
#define Tests_h

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
#ifndef WIN32
#include <unistd.h>
#endif
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#ifndef WIN32
#include <dirent.h>
#endif
#include <errno.h>

#define ymlog_type YMLogDefault
#include "Yammer.h"
#include "YMLog.h"

//#define     Logging 1
#ifdef      Logging
#define     NoisyTestLog(x,...) printf((x)"\n",##__VA_ARGS__)
#else
#define     NoisyTestLog(x,...) ;
#endif

#define YammerTests             A_YammerTests
#define CheckStateTest          Z_CheckStateTest

YM_EXTERN_C_PUSH

#define testassert(x,y,...) { theTest->assert(theTest->context,(x),y"\n",##__VA_ARGS__); }

typedef void (*ym_test_assert_func)(const void *ctx, bool exp, const char *fmt, ...);
typedef bool (*ym_test_diff_func)(const void *ctx, const char *path1, const char *path2, bool recursive, YMDictionaryRef exceptions);

void RunAllTests();

char *YMRandomASCIIStringWithMaxLength(uint16_t maxLength, bool for_mDNSServiceName, bool for_txtKey);
uint8_t *YMRandomDataWithMaxLength(uint16_t length, uint16_t *outLength);

YM_EXTERN_C_POP

#endif /* Tests_h */
