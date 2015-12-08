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
#define ymlog_type YMLogDefault
#endif
#ifndef ymlog_target
#define ymlog_target ( YMLogDefault | YMLogSecurity | YMLogConnection | YMLogSession | YMLogmDNS )
#endif
#ifndef ymlog_type_debug
#define ymlog_type_debug YMLogNothing
#endif
#ifndef ymlog_target_debug
#define ymlog_target_debug YMLogNothing
#endif

YM_WPPUSH // Token pasting of ',' and __VA_ARGS__ is a GNU extension
#define ymlog(x,...) if ( ymlog_type & ymlog_target ) __YMLogType(ymlog_target,(x),##__VA_ARGS__)
#define ymdbg(x,...) if ( ymlog_type_debug & ymlog_target_debug ) __YMLogType(ymlog_type_debug,(x),##__VA_ARGS__)
#define ymerr(x,...) __YMLogType(YMLogError,(x),##__VA_ARGS__)
YM_WPOP

YM_EXTERN_C_PUSH

typedef enum
{
    YMLogNothing = 0,
    YMLogError = 1,
    YMLogDefault = YMLogError << 1,
    YMLogmDNS = YMLogDefault << 1,
    YMLogSession = YMLogmDNS << 1,
    YMLogSecurity = YMLogSession << 1,
    YMLogConnection = YMLogSecurity << 1,
    YMLogThread = YMLogConnection << 1,
    YMLogThreadDebug = YMLogThread << 1,
    YMLogThreadDispatch = YMLogThreadDebug << 1, // todo: time to split out dispatch
    YMLogPlexer = YMLogThreadDispatch << 1,
    YMLogThreadSync = YMLogPlexer << 1,
    YMLogStream = YMLogThreadSync << 1,
    YMLogIO = YMLogStream << 1,
    YMLogEverything = 0xFFFF
} YMLogLevel;

void YMAPI __YMLogType( int level, char* format, ... ) __printflike(2, 3);

void _YMLogLock();
void _YMLogUnlock();

YM_EXTERN_C_POP

#endif /* YMLog_h */
