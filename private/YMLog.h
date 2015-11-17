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
#define ymlog_target YMLogmDNS
//#define ymlog_target YMLogEverything
// Token pasting of ',' and __VA_ARGS__ is a GNU extension
YM_WPPUSH
#define ymlog(x,...) if ( ymlog_type <= ymlog_target ) YMLogType(ymlog_type,(x),##__VA_ARGS__)
// it might be nice if this postpended errno/strerror (or had a designated version for cases that errno is relevant)
#define ymerr(x,...) YMLogType(YMLogError,(x),##__VA_ARGS__)
YM_WPOP
#endif
#define ymlog_stream_lifecycle false

#ifndef YMLog_h
#define YMLog_h

typedef enum
{
    YMLogNothing = 0,
    YMLogError,
    YMLogDefault,
    YMLogmDNS,
    YMLogSession,
    YMLogSecurity,
    YMLogConnection,
    YMLogThread,
    YMLogThreadDispatch, // todo: time to split out dispatch
    YMLogPlexer,
    YMLogStreamLifecycle,
    YMLogThreadSync,
    YMLogStream,
    YMLogPipe,
    YMLogIO,
    YMLogEverything
} YMLogLevel;

void YMLogType( YMLogLevel, char* format, ... ) __printflike(2, 3);

#endif /* YMLog_h */
