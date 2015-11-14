//
//  YMLock.h
//  yammer
//
//  Created by david on 11/4/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMLock_h
#define YMLock_h

#include <pthread.h>

typedef enum
{
    YMLockDefault = 0,
    YMLockRecursive = 1 << 0,
    YMLockErrorCheck = 1 << 1
} YMLockOptions;

typedef YMTypeRef YMLockRef;

YMLockRef YMLockCreate();
YMLockRef YMLockCreateWithOptions(YMLockOptions options);
YMLockRef YMLockCreateWithOptionsAndName(YMLockOptions options, YMStringRef name);

// avoid 'try lock'
void YMLockLock(YMLockRef lock);
void YMLockUnlock(YMLockRef lock);

// for YMSemaphore
pthread_mutex_t _YMLockGetMutex(YMLockRef lock);

#endif /* YMLock_h */
