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

typedef const struct __ym_thread_t *YMThreadRef;

typedef void (*ym_void_voidp_func)(void *);
typedef void *(*ym_voidp_voidp_func)(void *);

typedef struct ym_thread_dispatch_def ym_thread_dispatch;
typedef struct ym_thread_dispatch_def *ym_thread_dispatch_ref;

#ifndef WIN32
#define YM_THREAD_RETURN void
#define YM_CALLING_CONVENTION
#define YM_THREAD_PARAM void *
#define YM_THREAD_END
#else
//typedef DWORD(WINAPI *PTHREAD_START_ROUTINE)(
//	LPVOID lpThreadParameter
//	);
#define YM_THREAD_RETURN DWORD
#define YM_CALLING_CONVENTION WINAPI
#define YM_THREAD_PARAM LPVOID
#define YM_THREAD_END return 0;
#endif

typedef void(YM_CALLING_CONVENTION *ym_thread_dispatch_func)(ym_thread_dispatch_ref);
typedef YM_THREAD_RETURN(YM_CALLING_CONVENTION *ym_thread_entry)(YM_THREAD_PARAM);

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

void YMThreadDispatchDispatch(YMThreadRef thread, ym_thread_dispatch dispatch);

typedef void (*ym_dispatch_forward_file_callback)(void *, YMIOResult, uint64_t);
typedef struct _ym_thread_forward_file_context_t
{
    ym_dispatch_forward_file_callback callback;
    void * context;
} _ym_thread_forward_file_context_t;
typedef struct _ym_thread_forward_file_context_t *_ym_thread_forward_file_context_ref;

// the 'forward file' problem
// user should be able to asynchronously forward a file, release ownership of stream and forget it
// plexer cannot send remote-close at the time user forwards and releases
// want to be able to leave stream open, forward file doesn't imply close after completion
// need event based way to either notify user when forward is complete, or 'close when done' in sync with plexer's list of streams
// should the client 'forward' api be on connection, so it can ConnectionCloseStream either on callback or after sync method?
bool YMThreadDispatchForwardFile(YMFILE fromFile, YMStreamRef toStream, const uint64_t *nBytesPtr, bool sync, _ym_thread_forward_file_context_ref callbackInfo);
bool YMThreadDispatchForwardStream(YMStreamRef fromStream, YMFILE toFile, const uint64_t *nBytesPtr, bool sync, _ym_thread_forward_file_context_ref callbackInfo);
void YMThreadDispatchJoin(YMThreadRef thread_);

bool YMThreadStart(YMThreadRef thread);
bool YMThreadJoin(YMThreadRef thread);

#endif /* YMThread_h */
