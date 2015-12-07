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
#define ymlog_target ( YMLogSecurity | YMLogConnection | YMLogmDNS )
#endif

YM_WPPUSH // Token pasting of ',' and __VA_ARGS__ is a GNU extension
#define ymlog(x,...) if ( ymlog_type & ymlog_target ) __YMLogType(ymlog_target,(x),##__VA_ARGS__)
#define ymerr(x,...) __YMLogType(YMLogError,(x),##__VA_ARGS__)
YM_WPOP

YM_EXTERN_C_PUSH

typedef enum
{
    YMLogError = 1 << 0,
    YMLogDefault = 1 << 1,
    YMLogmDNS = 1 << 2,
    YMLogSession = 1 << 3,
    YMLogSecurity = 1 << 4,
    YMLogConnection = 1 << 5,
    YMLogThread = 1 << 6,
    YMLogThreadDispatch = 1 << 7, // todo: time to split out dispatch
    YMLogPlexer = 1 << 8,
    YMLogThreadSync = 1 << 9,
    YMLogStream = 1 << 10,
    YMLogIO = 1 << 11,
    YMLogEverything = 0xFFFF
} YMLogLevel;

void YMAPI __YMLogType( int level, char* format, ... ) __printflike(2, 3);

YM_EXTERN_C_POP

#endif /* YMLog_h */
