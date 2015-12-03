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
#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>

#import "Yammer.h"


//#define     Logging 1
#ifdef      Logging
#define     NoisyTestLog(x,...) printf((x)"\n",##__VA_ARGS__)
#else
#define     NoisyTestLog(x,...) ;
#endif

#define YammerTests             A_YammerTests
#define SessionTests            I_SessionTests
#define CheckStateTest          Z_CheckStateTest

#include "YMLog.h"
#undef ymlog_type
#define ymlog_type YMLogDefault
#if ( ymlog_type > ymlog_target )
#undef ymlog
#define ymlog(x,...) ;
#endif

#define testassert(x,y,...) { theTest->assert(theTest->context,(x),y"\n",##__VA_ARGS__); }

typedef void (*ym_test_assert_func)(const void *ctx, bool exp, const char *fmt, ...);
typedef bool (*ym_test_diff_func)(const void *ctx, const char *path1, const char *path2, bool recursive, YMDictionaryRef exceptions);

char *YMRandomASCIIStringWithMaxLength(uint16_t maxLength, bool for_mDNSServiceName, bool for_txtKey);
uint8_t *YMRandomDataWithMaxLength(uint16_t length, uint16_t *outLength);

#endif /* Tests_h */
