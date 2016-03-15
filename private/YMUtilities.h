//
//  YMUtilities.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMUtilities_h
#define YMUtilities_h

#include "YMDictionary.h"

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

YM_EXTERN_C_PUSH

typedef enum
{
    LessThan = -1,
    EqualTo = 0,
    GreaterThan = 1
} ComparisonResult;

const char *YMGetCurrentTimeString(char *buf, size_t bufLen);
ComparisonResult YMTimevalCompare(struct timeval *a, struct timeval *b);
// ensure portability of the end of time for the watchtower platform
void YMGetTheBeginningOfPosixTimeForCurrentPlatform(struct timeval *time);
void YMGetTheEndOfPosixTimeForCurrentPlatform(struct timeval *time);

YMIOResult YMReadFull(YMFILE fd, uint8_t *buffer, size_t bytes, size_t *outRead);
YMIOResult YMWriteFull(YMFILE fd, const uint8_t *buffer, size_t bytes, size_t *outWritten);

int YMGetNumberOfOpenFilesForCurrentProcess();

typedef enum
{
    YMInterfaceUnknown = 0,
    YMInterfaceLoopback = 1,
    YMInterfaceWirelessEthernet = 100,
    YMInterfaceBluetooth = 200,
    YMInterfaceWiredEthernet = 300,
    YMInterfaceFirewire400 = 400,
    YMInterfaceFirewire800 = 401,
    YMInterfaceFirewire1600 = 402,
    YMInterfaceFirewire3200 = 403,
    YMInterfaceThunderbolt = 500
} YMInterfaceType;

void YMNetworkingInit();
int32_t YMPortReserve(bool ipv4, int *outSocket);
YMDictionaryRef YMCreateLocalInterfaceMap();
YMInterfaceType YMInterfaceTypeForName(YMStringRef ifName);
const char *YMInterfaceTypeDescription(YMInterfaceType type);

// in utilities for YMAlloc
#include "YMLock.h"

MUTEX_PTR_TYPE YMCreateMutexWithOptions(YMLockOptions options);
bool YMLockMutex(MUTEX_PTR_TYPE mutex);
bool YMUnlockMutex(MUTEX_PTR_TYPE mutex);
bool YMDestroyMutex(MUTEX_PTR_TYPE mutex);

void YMUtilitiesFreeGlobals();

#if defined(YMWIN32) || defined(_YOLO_DONT_TELL_PROFESSOR)
int gettimeofday(struct timeval * tp, struct timezone * tzp);
#endif

YM_EXTERN_C_POP

#endif /* YMUtilities_h */
