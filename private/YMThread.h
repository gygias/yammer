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

YM_EXTERN_C_PUSH

typedef const struct __ym_thread * YMThreadRef;

typedef void (*ym_void_voidp_func)(void *);
typedef void *(*ym_voidp_voidp_func)(void *);

#if !defined(YMWIN32)
# define YM_THREAD_RETURN void *
# define YM_CALLING_CONVENTION
# define YM_THREAD_PARAM void *
# define YM_THREAD_END return NULL;
#else
//typedef DWORD(WINAPI *PTHREAD_START_ROUTINE)(
//	LPVOID lpThreadParameter
//	);
# define YM_THREAD_RETURN DWORD
# define YM_CALLING_CONVENTION WINAPI
# define YM_THREAD_PARAM LPVOID
# define YM_THREAD_END return 0;
#endif

typedef void(YM_CALLING_CONVENTION *ym_dispatch_user_func)(YM_THREAD_PARAM);
typedef YM_THREAD_RETURN(YM_CALLING_CONVENTION *ym_thread_entry)(YM_THREAD_PARAM);

YMThreadRef YMAPI YMThreadCreate(YMStringRef name, ym_thread_entry entryPoint, void *context);
YMThreadRef YMAPI YMThreadDispatchCreate(YMStringRef name);

void YMThreadSetContext(YMThreadRef thread, void *context);

// if context contains nested allocations, or doesn't use the YMALLOC allocator, use ym_thread_dispatch_dealloc
typedef struct ym_thread_dispatch_user
{
    ym_dispatch_user_func dispatchProc;
    ym_dispatch_user_func deallocProc; // optional // todo why is this necessary - can't dispatchProc take care of opaque stuff before it finishes?
    bool freeContextWhenDone; // optional convenience for YMALLOC'd context pointers. will be free'd after deallocProc, if it is specified.
    void *context; // weak
    YMStringRef description; // optional, assigns a name that will be included in logging from YMThreadDispatch
} ym_thread_dispatch_user;
typedef struct ym_thread_dispatch_user ym_thread_dispatch_user_t;

void YMAPI YMThreadDispatchDispatch(YMThreadRef thread, ym_thread_dispatch_user_t userDispatch);

typedef void (*ym_forward_file_callback)(void *, YMIOResult, uint64_t);
typedef struct ym_forward_file
{
    ym_forward_file_callback callback;
    void * context;
} ym_forward_file;
typedef struct ym_forward_file ym_forward_file_t;

void YMAPI YMThreadDispatchSetGlobalMode(bool);
void YMAPI YMThreadDispatchMain();
YMThreadRef YMAPI YMThreadDispatchGetGlobal();

// the 'forward file' problem
// user should be able to asynchronously forward a file, release ownership of stream and forget it
// plexer cannot send remote-close at the time user forwards and releases
// want to be able to leave stream open, forward file doesn't imply close after completion
// need event based way to either notify user when forward is complete, or 'close when done' in sync with plexer's list of streams
// should the client 'forward' api be on connection, so it can ConnectionCloseStream either on callback or after sync method?
bool YMAPI YMThreadDispatchForwardFile(YMFILE fromFile, YMStreamRef toStream, const uint64_t *nBytesPtr, bool sync, ym_forward_file_t *callbackInfo);
bool YMAPI YMThreadDispatchForwardStream(YMStreamRef fromStream, YMFILE toFile, const uint64_t *nBytesPtr, bool sync, ym_forward_file_t *callbackInfo);
void YMAPI YMThreadDispatchJoin(YMThreadRef thread_);

bool YMAPI YMThreadStart(YMThreadRef thread);
bool YMAPI YMThreadJoin(YMThreadRef thread);

YM_EXTERN_C_POP

#endif /* YMThread_h */
