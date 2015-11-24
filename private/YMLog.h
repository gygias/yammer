//
//  YMLog.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef ymlog_type
#define ymlog_type YMLogDefault
#endif
#ifndef ymlog_target
#define ymlog_target ( YMLogError | YMLogStream | YMLogPlexer )
// Token pasting of ',' and __VA_ARGS__ is a GNU extension
YM_WPPUSH
#define ymlog(x,...) if ( ymlog_type & ymlog_target ) __YMLogType((x),##__VA_ARGS__)
// it might be nice if this postpended errno/strerror (or had a designated version for cases that errno is relevant)
#define ymerr(x,...) __YMLogType((x),##__VA_ARGS__)
YM_WPOP
#endif

#ifndef YMLog_h
#define YMLog_h

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
    YMLogPipe = 1 << 11,
    YMLogIO = 1 << 12,
    YMLogEverything = 0xFFFF
} YMLogLevel;

extern void __YMLogType( char* format, ... ) __printflike(1, 2);

#endif /* YMLog_h */
