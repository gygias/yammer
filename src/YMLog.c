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

int __YMLogIndent( int level )
{
    if ( level >= YMLogCombined5 )
        return 5;
    else if ( level >= YMLogCombined4 )
        return 4;
    else if ( level >= YMLogCombined3 )
        return 3;
    else if ( level >= YMLogCombined2 )
        return 2;
    else if ( level >= YMLogCombined1 )
        return 1;
    return 0;
}

static YM_ONCE_OBJ gYMLogInitOnce = YM_ONCE_INIT;

void __YMLogType( int level, bool newline, char* format, ... )
{
	YM_ONCE_DO(gYMLogInitOnce,__YMLogInit);
    
    FILE *file = (level == YMLogError) ? stderr : stdout;
    
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
        fprintf(file,"yammer[%lu:%lu]: ",pid,threadID);
        
        int indent = __YMLogIndent(level);
        while ( indent-- > 0 )
            fprintf(file," ");
        if ( level == YMLogError ) fprintf(file, "!: ");
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

void YMAPI __YMLogReturn( int level )
{
    FILE *file = (level == YMLogError) ? stderr : stdout;
    fprintf(file,"\n");
    fflush(file);
    gIntraline = false;
}

void _YMLogLock() { YM_ONCE_DO(gYMLogInitOnce,__YMLogInit); /*YMLockLock(gYMLogLock);*/ }
void _YMLogUnlock() { YM_ONCE_DO(gYMLogInitOnce,__YMLogInit); /*YMLockUnlock(gYMLogLock);*/ }

void YMLogFreeGlobals()
{
    if ( gYMLogLock ) {
        YMRelease(gYMLogLock);
        gYMLogLock = NULL;
        
        free(gTimeFormatBuf);
        gTimeFormatBuf = NULL;
        
        YM_ONCE_OBJ onceAgain = YM_ONCE_INIT;
        memcpy(&gYMLogInitOnce,&onceAgain,sizeof(onceAgain));
    }
}

YM_EXTERN_C_POP
