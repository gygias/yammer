//
//  YMThread.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMThread_h
#define YMThread_h

#include "YMStream.h"

typedef struct __YMThread *YMThreadRef;

typedef void (*ym_void_voidp_func)(void *);
typedef void *(*ym_voidp_voidp_func)(void *);

typedef struct ym_thread_dispatch_def ym_thread_dispatch;
typedef struct ym_thread_dispatch_def *ym_thread_dispatch_ref;
typedef void *(*ym_thread_dispatch_func)(ym_thread_dispatch_ref);

// entry point definition for standard threads

typedef ym_void_voidp_func ym_thread_entry;

// function pointers for YMThreadDispatchUserInfo
#pragma message "todo: make parameter explicitly YMThreadDispatchUserInfoRef, i got confused thinkng it was userInfo->context"
typedef ym_thread_dispatch_func ym_thread_dispatch_dealloc;

YMThreadRef YMThreadCreate(char *name, ym_thread_entry entryPoint, void *context);
YMThreadRef YMThreadDispatchCreate(char *name);

void YMThreadSetContext(YMThreadRef thread, void *context);

// if context contains nested allocations, or doesn't use the YMALLOC allocator, use ym_thread_dispatch_dealloc
typedef struct ym_thread_dispatch_def
{
    ym_thread_dispatch_func dispatchProc;
    ym_thread_dispatch_dealloc deallocProc; // optional // todo why is this this necessary? can't dispatchProc take care of opaque stuff before it finishes?
    bool freeContextWhenDone; // optional convenience for YMALLOC'd context pointers. will be free'd after deallocProc, if it is specified.
    void *context; // weak
    const char *description; // optional, copied, assigns a name that will be included in logging from YMThreadDispatch
} _ym_thread_dispatch_def;

// description (and other non-opaque types) will be copied
// todo, make dispatch api less boilerplate-y
//void YMThreadDispatchDispatch(YMThreadRef thread,   ym_thread_dispatch_entry entryProc,
//                                                    ym_thread_dispatch_finally finallyProc,
//                                                    bool freeContextWhenDone, // convenience for YMALLOC contexts that don't nest other allocations. mutually exclusive with finallyProc.
//                                                    const char *description,
//                                                    void *context);
void YMThreadDispatchDispatch(YMThreadRef thread, ym_thread_dispatch dispatch);

typedef void (*ym_dispatch_forward_file_callback)(void *, uint64_t);
typedef struct ym_thread_dispatch_forward_file_context_def
{
    ym_dispatch_forward_file_callback callback;
    void * context;
} _ym_thread_dispatch_forward_file_context_def;
typedef struct ym_thread_dispatch_forward_file_context_def ym_thread_dispatch_forward_file_context;
typedef ym_thread_dispatch_forward_file_context *ym_thread_dispatch_forward_file_context_ref;

//
bool YMThreadDispatchForwardFile(int fromFile, YMStreamRef toStream, bool limited, uint64_t byteLimit, bool sync, ym_thread_dispatch_forward_file_context callbackInfo);
bool YMThreadDispatchForwardStream(YMStreamRef fromStream, int toFile, bool limited, uint64_t byteLimit, bool sync, ym_thread_dispatch_forward_file_context callbackInfo);

bool YMThreadStart(YMThreadRef thread);
bool YMThreadJoin(YMThreadRef thread);

#endif /* YMThread_h */
