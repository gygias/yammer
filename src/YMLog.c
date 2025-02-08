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
#include "YMDispatch.h"
#include "YMThreadPriv.h"

#include <stdarg.h>
#if !defined(YMWIN32)
# include <pthread.h>
#endif

YM_EXTERN_C_PUSH

void YMLog( char* format, ... ) __printflike(1, 2);

static YMDispatchQueueRef gYMLogQueue = NULL;
static char *gTimeFormatBuf = NULL;
#define gTimeFormatBufLen 128

YM_ONCE_FUNC(__YMLogInit,
{
    YMStringRef name = YMSTRC("com.combobulated.dispatch.ymlog");
    gYMLogQueue = YMDispatchQueueCreate(name);
    YMRelease(name);
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

typedef struct ___ym_log_t
{
    FILE *file;
    char *string;
} ___ym_log_t;

YM_ENTRY_POINT(___ym_log)
{
    ___ym_log_t *log = ctx;
    fprintf(log->file,log->string,NULL);
    fflush(log->file);
    YMFREE(log->string);
    YMFREE(log);
}

void __YMLogType( int level, char* format, ... )
{
	YM_ONCE_DO(gYMLogInitOnce,__YMLogInit);

    uint16_t max = 512, off = 0;
    char *line = YMALLOC(max*sizeof(char));
    
    const char *timeStr = YMGetCurrentTimeString(gTimeFormatBuf, gTimeFormatBufLen);
    uint64_t threadID = _YMThreadGetCurrentThreadNumber();
    uint64_t pid =
#if !defined(YMWIN32)
        getpid();
#else
        GetCurrentProcessId();
#endif

    if (timeStr)
        off += snprintf(line+off, max-off, "%s ", timeStr);
    off += snprintf(line+off,max-off,"yammer[%lu:%08lx]: ",pid,threadID);
    
    int indent = __YMLogIndent(level);
    while ( indent-- > 0 )
        off += snprintf(line+off,max-off," ");
    if ( level == YMLogError ) off += snprintf(line+off,max-off, "!: ");
    
    va_list args;
    va_start(args,format);
    off += vsnprintf(line+off,max-off,format, args);
    va_end(args);
    
    off += snprintf(line+off,max-off,"\n");

    ___ym_log_t *log = YMALLOC(sizeof(___ym_log_t));
    log->file = (level == YMLogError) ? stderr : stdout;
    log->string = line;
    ym_dispatch_user_t dispatch = { ___ym_log, log, NULL, ym_dispatch_user_context_noop };
    YMDispatchAsync(gYMLogQueue,&dispatch);
}

void YMLogFreeGlobals()
{
    if ( gYMLogQueue ) {
        YMRelease(gYMLogQueue);
        gYMLogQueue = NULL;
        
        YMFREE(gTimeFormatBuf);
        gTimeFormatBuf = NULL;
        
        YM_ONCE_OBJ onceAgain = YM_ONCE_INIT;
        memcpy(&gYMLogInitOnce,&onceAgain,sizeof(onceAgain));
    }
}

YM_EXTERN_C_POP
