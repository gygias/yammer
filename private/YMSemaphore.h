//
//  YMSemaphore.h
//  yammer
//
//  Created by david on 11/4/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMSemaphore_h
#define YMSemaphore_h

typedef struct __YMSemaphore *YMSemaphoreRef;

YMSemaphoreRef YMAPI YMSemaphoreCreate(int initialValue);
YMSemaphoreRef YMAPI YMSemaphoreCreateWithName(YMStringRef name, int initialValue);

void YMAPI YMSemaphoreWait(YMSemaphoreRef semaphore);
void YMAPI YMSemaphoreSignal(YMSemaphoreRef semaphore);

#endif /* YMSemaphore_h */
