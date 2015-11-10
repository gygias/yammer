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
#define ymlog_target YMLogConnection
#define ymlog(x,...) if ( ymlog_type <= ymlog_target ) YMLogType(ymlog_type,(x),##__VA_ARGS__)
// it might be nice if this postpended errno/strerror (or had a designated version for cases that errno is relevant)
#define ymerr(x,...) YMLogType(YMLogError,(x),##__VA_ARGS__)
#endif
#define ymlog_stream_lifecycle false // this might be hairy enough to warrant a special case

#ifndef YMLog_h
#define YMLog_h

typedef enum
{
    YMLogError = 0,
    YMLogDefault,
    YMLogSession,
    YMLogSecurity,
    YMLogmDNS,
    YMLogConnection,
    YMLogThread,
    YMLogLock,
    YMLogPlexer,
    YMLogStreamLifecycle,
    YMLogStream,
    YMLogPipe
} YMLogLevel;

void YMLogType( YMLogLevel, char* format, ... ) __printflike(2, 3);

#endif /* YMLog_h */
