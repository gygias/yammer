//
//  YMThread.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMThread_h
#define YMThread_h

typedef struct __YMThread *YMThreadRef;

// entry point definition for standard threads
typedef void *(*ym_thread_entry)(void *);

// function pointers for YMThreadDispatchUserInfo
#pragma message "todo: make parameter explicitly YMThreadDispatchUserInfoRef, i got confused thinkng it was userInfo->context"
typedef void *(*ym_thread_dispatch_entry)(void *);
typedef void *(*ym_thread_dispatch_dealloc)(void *);

YMThreadRef YMThreadCreate(char *name, ym_thread_entry entryPoint, void *context);
YMThreadRef YMThreadDispatchCreate(char *name);

void YMThreadSetContext(YMThreadRef thread, void *context);

// if context contains nested allocations, or doesn't use the YMALLOC allocator, use ym_thread_dispatch_dealloc
typedef struct
{
    ym_thread_dispatch_entry dispatchProc;
    ym_thread_dispatch_dealloc deallocProc; // optional // todo why is this this necessary? can't dispatchProc take care of opaque stuff before it finishes?
    bool freeContextWhenDone; // optional convenience for YMALLOC'd context pointers. will be free'd after deallocProc, if it is specified.
    void *context; // weak
    const char *description; // optional, copied, assigns a name that will be included in logging from YMThreadDispatch
} YMThreadDispatchUserInfo;
typedef YMThreadDispatchUserInfo *YMThreadDispatchUserInfoRef;

// description (and other non-opaque types) will be copied
// todo, make dispatch api less boilerplate-y
//void YMThreadDispatchDispatch(YMThreadRef thread,   ym_thread_dispatch_entry entryProc,
//                                                    ym_thread_dispatch_finally finallyProc,
//                                                    bool freeContextWhenDone, // convenience for YMALLOC contexts that don't nest other allocations. mutually exclusive with finallyProc.
//                                                    const char *description,
//                                                    void *context);
void YMThreadDispatchDispatch(YMThreadRef thread, YMThreadDispatchUserInfoRef dispatch);
bool YMThreadDispatchForwardFile(int fromFile, int toFile);

bool YMThreadStart(YMThreadRef thread);
bool YMThreadJoin(YMThreadRef thread);

#endif /* YMThread_h */
