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

void YMLogInitLock()
{
    YMStringRef name = YMSTRC("ymlog");
    gYMLogLock = YMLockCreateWithOptionsAndName(YMInternalLockType,name);
    YMRelease(name);
}

// inline
void __YMLogInit()
{
    pthread_once(&gYMLogLockOnce, YMLogInitLock);
    gTimeFormatBuf = YMALLOC(gTimeFormatBufLen);
}

void YMLogType( __unused YMLogLevel level, char* format, ... )
{
    __YMLogInit();
    
    YMLockLock(gYMLogLock);
    {
        const char *timeStr = YMGetCurrentTimeString(gTimeFormatBuf, gTimeFormatBufLen);
        if ( timeStr )
            printf("%s yammer: ",timeStr);
        
        va_list args;
        va_start(args,format);
        vprintf(format, args);
        va_end(args);
        
        
        printf("\n");
        fflush(stdout);
    }
    YMLockUnlock(gYMLogLock);
}
