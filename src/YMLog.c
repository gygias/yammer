//
//  YMLog.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 Combobulated Software. All rights reserved.
//

#include "YMLog.h"

#include <stdarg.h>

const unsigned long ym_log_line_max = 256;

void YMLog( char* format, ... )
{
    char str[ym_log_line_max];
    va_list vargs;
    va_start(vargs,format);
    snprintf(str, ym_log_line_max, format, vargs);
    va_end(vargs);
    
    printf("%s",str);
}