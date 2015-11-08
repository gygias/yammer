//
//  YMLog.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMLog_h
#define YMLog_h

#include "YMBase.h"

typedef enum
{
    YMLogDefault = 0,
    YMLogSession,
    YMLogTypemDNS,
    YMLogConnection,
    YMLogThread,
    YMLogLock,
    YMLogPlexer,
    YMLogStream,
    YMLogPipe
} YMLogLevel;


#define ymLogType YMLogDefault
#define ymlog(x,...) YMLogType(ymLogType,(x),##__VA_ARGS__)

void YMLogType( YMLogLevel, char* format, ... ) __printflike(2, 3);

#endif /* YMLog_h */
