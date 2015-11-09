//
//  YMLog.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef ymlogType
#define ymlogType YMLogDefault
#endif
#ifndef ymlogTarget
#define ymlogTarget YMLogConnection
#define ymlog(x,...) if ( ymlogType <= ymlogTarget ) YMLogType(ymlogType,(x),##__VA_ARGS__)
#define ymerr(x,...) YMLogType(YMLogError,(x),##__VA_ARGS__)
#endif

#ifndef YMLog_h
#define YMLog_h

typedef enum
{
    YMLogError = 0,
    YMLogDefault,
    YMLogSession,
    YMLogmDNS,
    YMLogConnection,
    YMLogSecurity,
    YMLogThread,
    YMLogLock,
    YMLogPlexer,
    YMLogStream,
    YMLogPipe
} YMLogLevel;

void YMLogType( YMLogLevel, char* format, ... ) __printflike(2, 3);

#endif /* YMLog_h */
