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
    YMLogSession = 0,
    YMLogConnection,
    YMLogPlexer,
    YMLogStream,
    YMLogPipe
} YMLogLevel;

void YMLog( char* format, ... ) __printflike(1, 2);
void YMLogType( YMLogLevel, char* format, ... ) __printflike(2, 3);

#endif /* YMLog_h */
