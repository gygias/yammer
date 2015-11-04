//
//  YMLog.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMLog.h"

#include <stdarg.h>

void YMLog( char* format, ... )
{
    va_list args;
    va_start(args,format);
    vprintf(format, args);
    va_end(args);
    printf("\n");
}