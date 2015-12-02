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

#import "Yammer.h"

#include "YMLog.h"
#undef ymlog_type
#define ymlog_type YMLogDefault
#if ( ymlog_type > ymlog_target )
#undef ymlog
#define ymlog(x,...) ;
#endif

#define testassert(x,y,...) theTest->assertFunc(theTest->funcContext,(x),y,##__VA_ARGS__);

typedef void (*ym_test_assert_func)(const void *ctx, bool exp, const char *fmt, ...);

const char *YMRandomASCIIStringWithMaxLength(uint16_t maxLength, bool for_mDNSServiceName);
const uint8_t *YMRandomDataWithMaxLength(uint16_t length, uint16_t *outLength);

#endif /* Tests_h */
