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
#ifndef WIN32
#include <pthread.h>
#endif

void YMLog( char* format, ... ) __printflike(1, 2);

static YMLockRef gYMLogLock = NULL;
static char *gTimeFormatBuf = NULL;
#define gTimeFormatBufLen 128

YM_ONCE_FUNC(__YMLogInit,
{
    YMStringRef name = YMSTRC("ymlog");
    gYMLogLock = YMLockCreateWithOptionsAndName(YMLockRecursive,name);
    YMRelease(name);
    gTimeFormatBuf = YMALLOC(gTimeFormatBufLen);
})

void __YMLogType( char* format, ... )
{
	YM_ONCE_DO_LOCAL(__YMLogInit);
    
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
