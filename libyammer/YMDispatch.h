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

typedef struct ym_dispatch_user
{
    ym_entry_point dispatchProc;
    void *context; // supermultimodal weak (wow!)
    ym_entry_point onCompleteProc;
    ym_dispatch_user_context_mode mode;
} ym_dispatch_user;
typedef struct ym_dispatch_user ym_dispatch_user_t;

YMDispatchQueueRef YMAPI YMDispatchGetMainQueue();
YMDispatchQueueRef YMAPI YMDispatchGetGlobalQueue();

// userDispatch is copied and can be passed from the stack, to make up for some of the
// inconvenience of not having language support for blocks
void YMAPI YMDispatchAsync(YMDispatchQueueRef queue, ym_dispatch_user_t *userDispatch);
void YMAPI YMDispatchSync(YMDispatchQueueRef queue, ym_dispatch_user_t *userDispatch);
void YMAPI YMDispatchAfter(YMDispatchQueueRef queue, ym_dispatch_user_t *userDispatch, double seconds);

typedef enum ym_dispatch_source_type
{
    ym_dispatch_source_readable = 0,
    ym_dispatch_source_writeable = 1
} ym_dispatch_source_type;

typedef void * ym_dispatch_source_t;
ym_dispatch_source_t YMAPI YMDispatchSourceCreate(YMDispatchQueueRef queue, ym_dispatch_source_type type, uint64_t data, ym_dispatch_user_t *user);
void YMAPI YMDispatchSourceDestroy(ym_dispatch_source_t source);

_Noreturn void YMAPI YMDispatchMain();

YM_EXTERN_C_POP

#endif /* YMDispatch_h */
