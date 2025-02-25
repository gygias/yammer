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
# define ymlog_target ( YMLogDefault | YMLogSecurity | YMLogConnection | YMLogSession | YMLogmDNS )
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

#define ymlogg(x,...)   { if ( ymlog_type & ymlog_target ) __YMLogType(ymlog_type,(x),##__VA_ARGS__); }
#define ymlog(x,...)    { if ( ymlog_type & ymlog_target ) __YMLogType(ymlog_type,ymlog_pre x,ymlog_args,##__VA_ARGS__); }
#define ymdbg(x,...)    { if ( ymlog_type_debug & ymlog_target_debug ) __YMLogType(ymlog_type_debug,ymlog_pre x,ymlog_args,##__VA_ARGS__); }
#define ymerr(x,...)    __YMLogType(YMLogError,ymlog_pre x,ymlog_args,##__VA_ARGS__)
#define ymerrg(x,...)   __YMLogType(YMLogError,x,##__VA_ARGS__)
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
    YMLogDispatch = YMLogSecurity << 1,
    YMLogThread = YMLogDispatch << 1,
    YMLogDispatchForward = YMLogDispatch << 1,
    YMLogCombined2 = YMLogDispatchForward,
    YMLogPlexer = YMLogDispatchForward << 1,
    YMLogCombined3 = YMLogPlexer,
    YMLogThreadSync = YMLogPlexer << 1,
    YMLogStream = YMLogThreadSync << 1,
    YMLogCombined4 = YMLogStream,
    YMLogCompression = YMLogCombined4 << 1,
    YMLogIO = YMLogCompression << 1,
    YMLogCombined5 = YMLogIO,
    YMLogEverything = 0xFFFF
} YMLogLevel;

void YMAPI __YMLogType( int level, char* format, ... ) __printflike(2, 3);
void YMAPI __YMLogReturn( int level );

void YMLogFreeGlobals();

YM_EXTERN_C_POP

#endif /* YMLog_h */
