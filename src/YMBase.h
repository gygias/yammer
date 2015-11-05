//
//  YMBase.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMBase_h
#define YMBase_h

#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "YMUtilities.h"
#include "YMLog.h"

typedef const void *YMTypeRef;
typedef char YMTypeID;

void YMFree(YMTypeRef object);

typedef bool (*ym_read_func)(int,const uint8_t*,size_t);
typedef bool (*ym_write_func)(int,const uint8_t*,size_t);

#endif /* YMBase_h */
