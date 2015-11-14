//
//  YMSemaphore.h
//  yammer
//
//  Created by david on 11/4/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMSemaphore_h
#define YMSemaphore_h

typedef YMTypeRef YMSemaphoreRef;

YMSemaphoreRef YMSemaphoreCreate(YMStringRef name, int initialValue);

void YMSemaphoreWait(YMSemaphoreRef semaphore);
void YMSemaphoreSignal(YMSemaphoreRef semaphore);

#endif /* YMSemaphore_h */
