//
//  YMLock.h
//  yammer
//
//  Created by david on 11/4/15.
//  Copyright © 2015 Combobulated Software. All rights reserved.
//

#ifndef YMLock_h
#define YMLock_h

#include "YMBase.h"

typedef enum
{
    YMLockDefault = 0,
    YMLockRecursive = 1 << 0
} YMLockOptions;

typedef struct _YMLock *YMLockRef;

YMLockRef YMLockCreate();
YMLockRef YMLockCreateWithOptions(YMLockOptions options);
YMLockRef YMLockCreateWithOptionsAndName(YMLockOptions options, char *name);

// avoid 'try lock'
void YMLockLock(YMLockRef lock);
void YMLockUnlock(YMLockRef lock);

#endif /* YMLock_h */
