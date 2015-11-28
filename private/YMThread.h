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

typedef YMTypeRef YMThreadRef;

typedef void (*ym_void_voidp_func)(void *);
typedef void *(*ym_voidp_voidp_func)(void *);

typedef struct ym_thread_dispatch_def ym_thread_dispatch;
typedef struct ym_thread_dispatch_def *ym_thread_dispatch_ref;
typedef void (*ym_thread_dispatch_func)(ym_thread_dispatch_ref);

// entry point definition for standard threads

#ifndef WIN32
typedef ym_void_voidp_func ym_thread_entry;
#define YM_CALLBACK_DEF(x) void x(void)
//#define YM_CALLBACK_FUNC(x,y) void x(void) { y; }
#else
//typedef DWORD(WINAPI *PTHREAD_START_ROUTINE)(
//	LPVOID lpThreadParameter
//	);
typedef DWORD (WINAPI *ym_dword_lpvoid_func)(LPVOID);
typedef ym_dword_lpvoid_func ym_thread_entry;
#define YM_CALLBACK_DEF(x) DWORD WINAPI x(LPVOID)
//#define YM_CALLBACK_FUNC(x,y) DWORD WINAPI x(LPVOID ctx) { { y }; return 0; }
#endif

// function pointers for YMThreadDispatchUserInfo
typedef ym_thread_dispatch_func ym_thread_dispatch_dealloc;

YMThreadRef YMThreadCreate(YMStringRef name, ym_thread_entry entryPoint, const void *context);
YMThreadRef YMThreadDispatchCreate(YMStringRef name);

void YMThreadSetContext(YMThreadRef thread, void *context);

// if context contains nested allocations, or doesn't use the YMALLOC allocator, use ym_thread_dispatch_dealloc
typedef struct ym_thread_dispatch_def
{
    ym_thread_dispatch_func dispatchProc;
    ym_thread_dispatch_dealloc deallocProc; // optional // todo why is this this necessary? can't dispatchProc take care of opaque stuff before it finishes?
    bool freeContextWhenDone; // optional convenience for YMALLOC'd context pointers. will be free'd after deallocProc, if it is specified.
    void *context; // weak
    YMStringRef description; // optional, assigns a name that will be included in logging from YMThreadDispatch
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
bool YMThreadDispatchForwardFile(int fromFile, YMStreamRef toStream, const uint64_t *nBytesPtr, bool sync, ym_thread_dispatch_forward_file_context callbackInfo);
bool YMThreadDispatchForwardStream(YMStreamRef fromStream, int toFile, const uint64_t *nBytesPtr, bool sync, ym_thread_dispatch_forward_file_context callbackInfo);
void YMThreadDispatchJoin(YMThreadRef thread_);

bool YMThreadStart(YMThreadRef thread);
bool YMThreadJoin(YMThreadRef thread);

#endif /* YMThread_h */
