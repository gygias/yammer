//
//  YMThreads.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMThreads_h
#define YMThreads_h

#include "YMBase.h"

typedef struct _YMThread *YMThreadRef;

typedef void *(*ym_thread_entry)(void *);

YMThreadRef YMThreadCreate(ym_thread_entry entryPoint, void *context);

bool YMThreadStart(YMThreadRef thread);
bool YMThreadJoin(YMThreadRef thread);

#endif /* YMThreads_h */
