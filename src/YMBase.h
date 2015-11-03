//
//  YMBase.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 Combobulated Software. All rights reserved.
//

#ifndef YMBase_h
#define YMBase_h

#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

#include "YMIO.h"
#include "YMLog.h"

typedef const void *YMTypeRef;
typedef unsigned long YMTypeID;

typedef ssize_t (*ym_read_func)(int,void*,size_t);
typedef ssize_t (*ym_write_func)(int,void*,size_t);

#endif /* YMBase_h */
