#ifndef YMDispatch_h
#define YMDispatch_h

#include "YMBase.h"
#include "YMString.h"
#include "YMThread.h"

YM_EXTERN_C_PUSH

typedef const struct __ym_dispatch_queue * YMDispatchQueueRef;

YMDispatchQueueRef YMAPI YMDispatchQueueCreate(YMStringRef name);

typedef enum ym_dispatch_user_context_mode
{
    ym_dispatch_user_context_noop = 0,
    ym_dispatch_user_context_release = 1,
    ym_dispatch_user_context_free = 2
} ym_dispatch_user_context_mode;

typedef void(YM_CALLING_CONVENTION *ym_dispatch_user_func)(YM_THREAD_PARAM);
typedef struct ym_dispatch_user
{
    ym_dispatch_user_func dispatchProc;
    void *context; // supermultimodal weak (wow!)
    ym_dispatch_user_func onCompleteProc;
    ym_dispatch_user_context_mode mode;
} ym_dispatch_user;
typedef struct ym_dispatch_user ym_dispatch_user_t;


YMDispatchQueueRef YMAPI YMDispatchGetMainQueue();
YMDispatchQueueRef YMAPI YMDispatchGetGlobalQueue();

void YMAPI YMDispatchAsync(YMDispatchQueueRef queue, ym_dispatch_user_t *userDispatch);
void YMAPI YMDispatchSync(YMDispatchQueueRef queue, ym_dispatch_user_t *userDispatch);

_Noreturn void YMAPI YMDispatchMain();

YM_EXTERN_C_POP

#endif /* YMDispatch_h */
