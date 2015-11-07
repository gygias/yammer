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

#pragma message "instead of having YMLogType, add macros to each .c that expands a 'private' _YMLog into _YMLogType(#classname#Type,format,...)"
void YMLog( char* format, ... ) __printflike(1, 2);

#endif /* YMLog_h */
