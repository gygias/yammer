//
//  YMUtilities.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMUtilities_h
#define YMUtilities_h

#include "YMBase.h"

#define YMMIN(a,b) ( (a<b) ? (a) : (b) )
#define YMMAX(a,b) ( (a>b) ? (a) : (b) )

typedef enum
{
    LessThan = -1,
    EqualTo = 0,
    GreaterThan = 1
} ComparisonResult;
ComparisonResult YMTimevalCompare(struct timeval *a, struct timeval *b);
// ensure portability of the end of time for the watchtower platform
void YMGetTheBeginningOfPosixTimeForCurrentPlatform(struct timeval *time);
void YMGetTheEndOfPosixTimeForCurrentPlatform(struct timeval *time);

typedef enum
{
    YMIOSuccess = 1,
    YMIOEOF = 0,
    YMIOError = -1
} YMIOResult;

YMIOResult YMReadFull(int fd, uint8_t *buffer, size_t bytes);
YMIOResult YMWriteFull(int fd, const uint8_t *buffer, size_t bytes);

char *YMStringCreateWithFormat(char *formatStr, ...) __printflike(1, 2);
char *YMStringCreateByAppendingString(char *baseStr, char *appendStr);

#endif /* YMUtilities_h */
