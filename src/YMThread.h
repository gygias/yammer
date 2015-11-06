//
//  YMThread.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMThread_h
#define YMThread_h

#include "YMBase.h"

typedef struct __YMThread *YMThreadRef;

typedef void *(*ym_thread_entry)(void *);
typedef void *(*ym_thread_dispatch_entry)(void *);

YMThreadRef YMThreadCreate(char *name, ym_thread_entry entryPoint, void *context);
YMThreadRef YMThreadDispatchCreate(char *name);

typedef struct
{
    ym_thread_dispatch_entry func;
    void *context;
    const char *description; // optional, for debugging
} YMThreadUserDispatch;
typedef YMThreadUserDispatch *YMThreadUserDispatchRef;

void YMThreadDispatchDispatch(YMThreadRef thread, YMThreadUserDispatchRef dispatch);

bool YMThreadStart(YMThreadRef thread);
bool YMThreadJoin(YMThreadRef thread);

#endif /* YMThread_h */
