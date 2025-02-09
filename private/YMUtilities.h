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
#include "YMArray.h"
#include "YMAddress.h"

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

const char YMAPI * YMGetCurrentTimeString(char *buf, size_t bufLen);
ComparisonResult YMAPI YMTimevalCompare(struct timeval *a, struct timeval *b);
// ensure portability of the end of time for the watchtower platform
void YMAPI YMGetTheBeginningOfPosixTimeForCurrentPlatform(struct timeval *time);
void YMAPI YMGetTheEndOfPosixTimeForCurrentPlatform(struct timeval *time);

YMIOResult YMAPI YMReadFull(YMFILE fd, uint8_t *buffer, size_t bytes, size_t *outRead);
YMIOResult YMAPI YMWriteFull(YMFILE fd, const uint8_t *buffer, size_t bytes, size_t *outWritten);

int YMAPI YMGetNumberOfOpenFilesForCurrentProcess();

void YMAPI YMNetworkingInit();
int32_t YMAPI YMPortReserve(bool ipv4, int *outSocket);

// { en0 :
//      { "addresses" : ( 0x123456789, ... ),
//        "type" : YMInterfaceType } }
#define kYMIFMapAddressesKey ((YMDictionaryKey) 1) // YMArray of YMAddresses
#define kYMIFMapTypeKey ((YMDictionaryKey) 2) // YMNumber
YMDictionaryRef YMAPI YMInterfaceMapCreateLocal();
YMInterfaceType YMAPI YMInterfaceTypeForName(YMStringRef ifName);
const char YMAPI * YMInterfaceTypeDescription(YMInterfaceType type);

// filesystem stuff
bool YMAPI YMRecursiveDelete(YMStringRef path);

// in utilities for YMAlloc
#include "YMLock.h"

MUTEX_PTR_TYPE YMAPI YMCreateMutexWithOptions(YMLockOptions options);
bool YMAPI YMLockMutex(MUTEX_PTR_TYPE mutex);
bool YMAPI YMUnlockMutex(MUTEX_PTR_TYPE mutex);
bool YMAPI YMDestroyMutex(MUTEX_PTR_TYPE mutex);

bool YMAPI YMIsDebuggerAttached();

int YMAPI YMGetNumberOfCoresAvailable();
int YMAPI YMGetDefaultThreadsForCores(int cores);
int YMAPI YMGetNumberOfThreadsInCurrentProcess();

void YMAPI YMUtilitiesFreeGlobals();

#if defined(YMWIN32) || defined(_YOLO_DONT_TELL_PROFESSOR)
int YMAPI gettimeofday(struct timeval * tp, struct timezone * tzp);
#endif

YM_EXTERN_C_POP

#endif /* YMUtilities_h */
