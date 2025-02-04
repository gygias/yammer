//
//  YMLog.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMLog_h
#define YMLog_h

#ifndef ymlog_type
# define ymlog_type YMLogDefault
#endif

#ifndef ymlog_target
# define ymlog_target ( YMLogDefault | YMLogSecurity | YMLogConnection | YMLogSession )
#endif

#ifndef ymlog_type_debug
# define ymlog_type_debug YMLogNothing
#endif

#ifndef ymlog_target_debug
# define ymlog_target_debug YMLogNothing
#endif

YM_WPPUSH // Token pasting of ',' and __VA_ARGS__ is a GNU extension
#ifndef ymlog_pre
# define ymlog_pre "%s"
#endif
#ifndef ymlog_args
# define ymlog_args ""
#endif

#define ymlogg(x,...)   { if ( ymlog_type & ymlog_target ) __YMLogType(ymlog_type,true,(x),##__VA_ARGS__); }
#define ymlog(x,...)    { if ( ymlog_type & ymlog_target ) __YMLogType(ymlog_type,true,ymlog_pre x,ymlog_args,##__VA_ARGS__); }
#define ymlogi(x,...)   { if ( ymlog_type & ymlog_target ) __YMLogType(ymlog_type,false,x,##__VA_ARGS__); }
#define ymlogr()        { if ( ymlog_type & ymlog_target ) __YMLogReturn(ymlog_type); }
#define ymdbg(x,...)    { if ( ymlog_type_debug & ymlog_target_debug ) __YMLogType(ymlog_type_debug,true,ymlog_pre x,ymlog_args,##__VA_ARGS__); }
#define ymerr(x,...)    __YMLogType(YMLogError,true,ymlog_pre x,ymlog_args,##__VA_ARGS__)
#define ymerrg(x,...)   __YMLogType(YMLogError,true,x,##__VA_ARGS__)
YM_WPOP

YM_EXTERN_C_PUSH

typedef enum
{
    YMLogNothing = 0,
    YMLogError = 1,
    YMLogDefault = YMLogError << 1,
    YMLogSession = YMLogDefault << 1,
    YMLogConnection = YMLogSession << 1,
    YMLogCombined1 = YMLogConnection,
    YMLogmDNS = YMLogCombined1 << 1,
    YMLogSecurity = YMLogmDNS << 1,
    YMLogThread = YMLogSecurity << 1,
    YMLogThreadDebug = YMLogThread << 1,
    YMLogThreadDispatch = YMLogThreadDebug << 1, // todo: time to split out dispatch
    YMLogCombined2 = YMLogThreadDispatch,
    YMLogPlexer = YMLogThreadDispatch << 1,
    YMLogCombined3 = YMLogPlexer,
    YMLogThreadSync = YMLogPlexer << 1,
    YMLogStream = YMLogThreadSync << 1,
    YMLogCombined4 = YMLogStream,
    YMLogCompression = YMLogCombined4 << 1,
    YMLogIO = YMLogCompression << 1,
    YMLogCombined5 = YMLogIO,
    YMLogEverything = 0xFFFF
} YMLogLevel;

void YMAPI __YMLogType( int level, bool newline, char* format, ... ) __printflike(3, 4);
void YMAPI __YMLogReturn( int level );

void YMLogFreeGlobals();

YM_EXTERN_C_POP

#endif /* YMLog_h */
