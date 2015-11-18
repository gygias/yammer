//
//  YMLog.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMLog.h"
#include "YMLock.h"
#include "YMUtilities.h"

#include <stdarg.h>
#include <pthread.h>

void YMLog( char* format, ... ) __printflike(1, 2);

static pthread_once_t gYMLogLockOnce = PTHREAD_ONCE_INIT;
static YMLockRef gYMLogLock = NULL;
static char *gTimeFormatBuf = NULL;
#define gTimeFormatBufLen 128

void __YMLogInit()
{
    YMStringRef name = YMSTRC("ymlog");
    gYMLogLock = YMLockCreateWithOptionsAndName(YMLockRecursive,name);
    YMRelease(name);
    gTimeFormatBuf = YMALLOC(gTimeFormatBufLen);
}

void __YMLogType( char* format, ... )
{
    pthread_once(&gYMLogLockOnce, __YMLogInit);
    
    YMLockLock(gYMLogLock);
    {
        const char *timeStr = YMGetCurrentTimeString(gTimeFormatBuf, gTimeFormatBufLen);
        uint64_t threadID = UINT64_MAX;
        pthread_threadid_np(pthread_self(), &threadID);
        if ( timeStr )
            fprintf(stdout,"%s yammer[%llu]: ",timeStr,threadID);
        
        va_list args;
        va_start(args,format);
        vprintf(format, args);
        va_end(args);
        
        
        fprintf(stdout,"\n");
        fflush(stdout);
    }
    YMLockUnlock(gYMLogLock);
}
