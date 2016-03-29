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
#if !defined(YMWIN32)
# include <pthread.h>
#endif

YM_EXTERN_C_PUSH

void YMLog( char* format, ... ) __printflike(1, 2);

static YMLockRef gYMLogLock = NULL;
static char *gTimeFormatBuf = NULL;
static bool gIntraline = false;
#define gTimeFormatBufLen 128

YM_ONCE_FUNC(__YMLogInit,
{
    gYMLogLock = YMLockCreate();
    gTimeFormatBuf = YMALLOC(gTimeFormatBufLen);
})

void __YMLogType( int level, bool newline, char* format, ... )
{
	YM_ONCE_DO_LOCAL(__YMLogInit);
    
    FILE *file = (level == YMLogError) ? stderr : stdout;
    
    YMLockLock(gYMLogLock);
    {
        if ( newline || ! gIntraline ) {
            const char *timeStr = YMGetCurrentTimeString(gTimeFormatBuf, gTimeFormatBufLen);
            uint64_t threadID = _YMThreadGetCurrentThreadNumber();
            uint64_t pid =
    #if !defined(YMWIN32)
                getpid();
    #else
                GetCurrentProcessId();
    #endif

            if (timeStr)
                fprintf(file, "%s ", timeStr);
            fprintf(file,"yammer[%llu:%llu]: " ymlog_pre,pid,threadID,ymlog_args);
        }
        
        if ( ! newline )
            gIntraline = true;
        else
            gIntraline = false;
        
        va_list args;
        va_start(args,format);
        vfprintf(file,format, args);
        va_end(args);
        
        if ( newline )
            fprintf(file,"\n");
        fflush(file);
    }
    YMLockUnlock(gYMLogLock);
}

void YMAPI __YMLogReturn( int level )
{
    YMLockLock(gYMLogLock);
    {
        FILE *file = (level == YMLogError) ? stderr : stdout;
        fprintf(file,"\n");
        fflush(file);
        gIntraline = false;
    }
    YMLockUnlock(gYMLogLock);
}

void _YMLogLock() { YM_ONCE_DO_LOCAL(__YMLogInit); YMLockLock(gYMLogLock); }
void _YMLogUnlock() { YM_ONCE_DO_LOCAL(__YMLogInit); YMLockUnlock(gYMLogLock); }

YM_EXTERN_C_POP
