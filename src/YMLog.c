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

pthread_once_t gYMLogLockOnce = PTHREAD_ONCE_INIT;
YMLockRef gYMLogLock = NULL;

void YMLogInitLock()
{
    gYMLogLock = YMLockCreate();
}

// inline
void __YMLogInit()
{
    pthread_once(&gYMLogLockOnce, YMLogInitLock);
}

void YMLog( char* format, ... )
{
    __YMLogInit();
    
    if ( ! strstr(format,"plexer[") )
        return;
    
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

void YMLogType( YMLogLevel level, char* format, ... )
{
    __YMLogInit();
    
    switch(level)
    {
        case YMLogSession:
        case YMLogConnection:
        case YMLogPlexer:
            break;
        case YMLogStream:
        case YMLogPipe:
        default:
            return;
    }
    
#pragma message "copied code, forward vargs"
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
