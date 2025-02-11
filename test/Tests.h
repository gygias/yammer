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
#ifndef YMWIN32
# include <unistd.h>
#endif
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#ifndef YMWIN32
# include <dirent.h>
#endif
#include <errno.h>

#define ymlog_type YMLogDefault
#include "Yammer.h"
#include "YMLog.h"

YM_EXTERN_C_PUSH

YM_WPPUSH
#define testassert(x,y,...) { theTest->assert(theTest->context,(x),y"\n",##__VA_ARGS__); }
YM_WPOP

typedef void (*ym_test_assert_func)(const void *ctx, bool exp, const char *fmt, ...);
typedef bool (*ym_test_diff_func)(const void *ctx, const char *path1, const char *path2, bool recursive, YMDictionaryRef exceptions);

void RunAllTests();

YM_EXTERN_C_POP

#endif /* Tests_h */
