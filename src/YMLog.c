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
# include <pthread.h>
#endif

YM_EXTERN_C_PUSH

void YMLog( char* format, ... ) __printflike(1, 2);

static YMLockRef gYMLogLock = NULL;
static char *gTimeFormatBuf = NULL;
#define gTimeFormatBufLen 128

YM_ONCE_FUNC(__YMLogInit,
{
    gYMLogLock = YMLockCreate();
    gTimeFormatBuf = YMALLOC(gTimeFormatBufLen);
})

void __YMLogType( int level, char* format, ... )
{
	YM_ONCE_DO_LOCAL(__YMLogInit);
    
    YMLockLock(gYMLogLock);
    {
        const char *timeStr = YMGetCurrentTimeString(gTimeFormatBuf, gTimeFormatBufLen);
		uint64_t threadID = _YMThreadGetCurrentThreadNumber();
		uint64_t pid =
#if !defined(WIN32)
			getpid();
#else
			GetCurrentProcessId();
#endif

		FILE *file = (level == YMLogError) ? stderr : stdout;

		if (timeStr)
			fprintf(file, "%s ", timeStr);
		fprintf(file,"yammer[%llu:%llu]: ",pid,threadID);
        
        va_list args;
        va_start(args,format);
        vfprintf(file,format, args);
        va_end(args);
        
        
        fprintf(file,"\n");
        fflush(file);
    }
    YMLockUnlock(gYMLogLock);
}

void _YMLogLock() { YMLockLock(gYMLogLock); }
void _YMLogUnlock() { YMLockUnlock(gYMLogLock); }

YM_EXTERN_C_POP
