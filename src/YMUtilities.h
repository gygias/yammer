//
//  YMUtilities.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMUtilities_h
#define YMUtilities_h

#define YMMIN(a,b) ( (a<b) ? (a) : (b) )
#define YMMAX(a,b) ( (a>b) ? (a) : (b) )

//// Glyph from http://stackoverflow.com/questions/2053843/min-and-max-value-of-data-type-in-c
//#define issigned(t) (((t)(-1)) < ((t) 0))
//#define umaxof(t) (((0x1ULL << ((sizeof(t) * 8ULL) - 1ULL)) - 1ULL) | \
//(0xFULL << ((sizeof(t) * 8ULL) - 4ULL)))
//#define smaxof(t) (((0x1ULL << ((sizeof(t) * 8ULL) - 1ULL)) - 1ULL) | \
//(0x7ULL << ((sizeof(t) * 8ULL) - 4ULL)))
//#define maxof(t) ((unsigned long long) (issigned(t) ? smaxof(t) : umaxof(t)))
#define MAX_OF(type) \
(((type)(~0LLU) > (type)((1LLU<<((sizeof(type)<<3)-1))-1LLU)) ? (long long unsigned int)(type)(~0LLU) : (long long unsigned int)(type)((1LLU<<((sizeof(type)<<3)-1))-1LLU))
#define MIN_OF(type) \
(((type)(1LLU<<((sizeof(type)<<3)-1)) < (type)1) ? (long long int)((~0LLU)-((1LLU<<((sizeof(type)<<3)-1))-1LLU)) : 0LL)

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

YMIOResult YMReadFull(int fd, uint8_t *buffer, size_t bytes, size_t *outRead);
YMIOResult YMWriteFull(int fd, const uint8_t *buffer, size_t bytes, size_t *outWritten);

int32_t YMPortReserve(bool ipv4, int *outSocket);

// in utilities for YMAlloc
#include "YMLock.h"
ymbool YMCreateMutexWithOptions(YMLockOptions options, pthread_mutex_t *outMutex);
ymbool YMLockMutex(pthread_mutex_t mutex);
ymbool YMUnlockMutex(pthread_mutex_t mutex);
ymbool YMDestroyMutex(pthread_mutex_t mutex);

#endif /* YMUtilities_h */
