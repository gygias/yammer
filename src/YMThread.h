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

// entry point definition for standard threads
typedef void *(*ym_thread_entry)(void *);

// function pointers for YMThreadDispatchUserInfo
typedef void *(*ym_thread_dispatch_entry)(void *);
typedef void *(*ym_thread_dispatch_dealloc)(void *);

YMThreadRef YMThreadCreate(char *name, ym_thread_entry entryPoint, void *context);
YMThreadRef YMThreadDispatchCreate(char *name);

// if context contains nested allocations, or doesn't use the YMMALLOC allocator, use ym_thread_dispatch_dealloc
typedef struct
{
    ym_thread_dispatch_entry dispatchProc;
    void *context; // weak
    bool freeContextWhenDone; // optional, convenience for contexts pointers using YMMALLOC allocator and not nesting other allocations
    ym_thread_dispatch_dealloc deallocProc; // optional
    const char *description; // copied, optional, for debugging
} YMThreadDispatchUserInfo;
typedef YMThreadDispatchUserInfo *YMThreadDispatchUserInfoRef;

// description (and other non-opaque types) will be copied
void YMThreadDispatchDispatch(YMThreadRef thread, YMThreadDispatchUserInfoRef dispatch);

bool YMThreadStart(YMThreadRef thread);
bool YMThreadJoin(YMThreadRef thread);

#endif /* YMThread_h */
