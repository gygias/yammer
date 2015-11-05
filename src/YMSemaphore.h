//
//  YMSemaphore.h
//  yammer
//
//  Created by david on 11/4/15.
//  Copyright © 2015 Combobulated Software. All rights reserved.
//

#ifndef YMSemaphore_h
#define YMSemaphore_h

#include "YMBase.h"

typedef struct __YMSemaphore *YMSemaphoreRef;

YMSemaphoreRef YMSemaphoreCreate();

void YMSemaphoreWait(YMSemaphoreRef semaphore);
void YMSemaphoreSignal(YMSemaphoreRef semaphore);

#endif /* YMSemaphore_h */
