//
//  YMLog.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMLog.h"
#include "YMLock.h"

#include <stdarg.h>
#include <pthread.h>

void YMLog( char* format, ... ) __printflike(1, 2);

static pthread_once_t gYMLogLockOnce = PTHREAD_ONCE_INIT;
YMLockRef gYMLogLock = NULL;

void YMLogInitLock()
{
    YMStringRef name = YMSTRC("ymlog");
    gYMLogLock = YMLockCreate(YMLockDefault,name);
    YMRelease(name);
}

// inline
void __YMLogInit()
{
    pthread_once(&gYMLogLockOnce, YMLogInitLock);
}

void YMLogType( YMLogLevel level, char* format, ... )
{
    __YMLogInit();
    
    if ( level > ymlog_target
        && ( ymlog_stream_lifecycle && level != YMLogStreamLifecycle ) )
        abort();
    
    YMLockLock(gYMLogLock);
    {
        va_list args;
        va_start(args,format);
        vprintf(format, args);
        va_end(args);
        printf("\n");
        fflush(stdout);
    }
    YMLockUnlock(gYMLogLock);
}
