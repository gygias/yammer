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
#include "YMThreadPriv.h"

#include <stdarg.h>
#ifndef _WINDOWS
#include <pthread.h>
#endif

void YMLog( char* format, ... ) __printflike(1, 2);

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

#ifndef _WINDOWS
	static pthread_once_t gYMLogLockOnce = PTHREAD_ONCE_INIT;
	pthread_once(&gYMLogLockOnce, __YMLogInit);
#else
	static INIT_ONCE gDispatchInitOnce = INIT_ONCE_STATIC_INIT;
	InitOnceExecuteOnce(&gDispatchInitOnce, __YMLogInit, NULL, NULL);
#endif
    
    YMLockLock(gYMLogLock);
    {
        const char *timeStr = YMGetCurrentTimeString(gTimeFormatBuf, gTimeFormatBufLen);
		uint64_t threadID = _YMThreadGetCurrentThreadNumber();

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
