#include "YMDispatch.h"
#include "YMThreadPriv.h"
#include "YMDictionary.h"
#include "YMLock.h"
#include "YMSemaphore.h"
#include "YMArray.h"

#define ymlog_type YMLogDispatch
#include "YMLog.h"

YM_EXTERN_C_PUSH

typedef struct __ym_dispatch
{
    YMDispatchQueueRef mainQueue;
    YMDispatchQueueRef globalQueue;
    YMArrayRef userQueues;
    YMLockRef lock;
} __ym_dispatch;
typedef struct __ym_dispatch __ym_dispatch_t;
__ym_dispatch_t *gDispatch = NULL;

YM_ONCE_DEF(__YMDispatchInitOnce);
void YMDispatchInit()
{
    YM_ONCE_DO_LOCAL(__YMDispatchInitOnce);
}

typedef enum
{
    YMDispatchQueueMain = 0,
    YMDispatchQueueGlobal = 1,
    YMDispatchQueueUser = 2
} YMDispatchQueueType;

typedef struct __ym_dispatch_queue
{
    _YMType _type;

    YMDispatchQueueType type;
    YMStringRef name;

    YMArrayRef queueStack;  // __ym_dispatch_dispatch_t
    YMSemaphoreRef queueSem;
    YMArrayRef queueThreads; // __ym_dispatch_queue_thread_t

    uint8_t nThreads;
    uint8_t maxThreads;

    bool exit;
} __ym_dispatch_queue;
typedef struct __ym_dispatch_queue __ym_dispatch_queue_t;

typedef struct __ym_dispatch_queue_thread
{
    YMThreadRef thread; // weak
} __ym_dispatch_queue_thread_t;
typedef struct __ym_dispatch_queue_thread __ym_dispatch_queue_thread_t;

YM_THREAD_RETURN YM_CALLING_CONVENTION __ym_dispatch_service_loop(YM_THREAD_PARAM ctx);
__ym_dispatch_queue_t *__YMDispatchQueueInitCommon(YMStringRef name, YMDispatchQueueType type);

YM_ONCE_OBJ(gDispatchGlobalInitOnce);
YM_ONCE_DEF(__YMDispatchInitOnce);
YM_ONCE_FUNC(__YMDispatchInitOnce,
{
    gDispatch = YMALLOC(sizeof(__ym_dispatch_t));

    YMStringRef name = YMSTRC("com.combobulated.dispatch.main");
    gDispatch->mainQueue = __YMDispatchQueueInitCommon(name, YMDispatchQueueMain);
    YMRelease(name);

    name = YMSTRC("com.combobulated.dispatch.global");
    gDispatch->globalQueue = __YMDispatchQueueInitCommon(name, YMDispatchQueueGlobal);
    YMRelease(name);
	gDispatch->userQueues = YMArrayCreate();
    gDispatch->lock = YMLockCreate(); // not a ymtype
})

__ym_dispatch_queue_thread_t *__YMDispatchQueueThreadCreate(YMThreadRef thread)
{
    __ym_dispatch_queue_thread_t *qt = YMALLOC(sizeof(__ym_dispatch_queue_thread_t));
    qt->thread = thread;
    return qt;
}

void __YM_DISPATCH_CATCH_MISUSE(__ym_dispatch_queue_t *q)
{
    ymassert(false,"fatal: something has released the %s dispatch queue!",q->type==YMDispatchQueueGlobal?"global":"main");
}

void _YMDispatchQueueFree(YMDispatchQueueRef p_)
{
    __ym_dispatch_queue_t *q = (__ym_dispatch_queue_t *)p_;
    if ( q->type == YMDispatchQueueGlobal || q->type == YMDispatchQueueMain )
        __YM_DISPATCH_CATCH_MISUSE(q);

    YMSelfLock(q);
    uint8_t nThreads = q->nThreads;
    bool willExit = ! q->exit;
    YMSelfUnlock(q);

    if ( willExit ) {
        q->exit = true;
        while ( nThreads-- )
            YMSemaphoreSignal(q->queueSem);
    }
}

uint8_t __YMDispatchMaxQueueThreads()
{
    // todo
    return 8;
}

__ym_dispatch_queue_t *__YMDispatchQueueInitCommon(YMStringRef name, YMDispatchQueueType type)
{
    __ym_dispatch_queue_t *q = (__ym_dispatch_queue_t *)_YMAlloc(_YMDispatchQueueTypeID,sizeof(__ym_dispatch_queue_t));
    q->name = name ? YMRetain(name) : YMSTRC("*");
    q->type = type;
    q->queueStack = YMArrayCreate();
    q->queueSem = YMSemaphoreCreate(0);
    q->queueThreads = YMArrayCreate();

    q->nThreads = 0;
    q->maxThreads = __YMDispatchMaxQueueThreads();

    q->exit = false;

    if ( type != YMDispatchQueueMain ) {
        YMThreadRef first = YMThreadCreate(name, __ym_dispatch_service_loop, q);
        __ym_dispatch_queue_thread_t *qt = __YMDispatchQueueThreadCreate(first);
        bool okay = YMThreadStart(first);

        ymassert(okay, "ymdispatch failed to start %s queue thread", YMSTR(name));
        YMArrayAdd(q->queueThreads,qt); // no sync, guarded by once
//#define YM_DISPATCH_LOG
#ifdef YM_DISPATCH_LOG
        printf("started %s queue %p '%s'\n", ( type == YMDispatchQueueGlobal ) ? "global" : "user", q, YMSTR(name));
#endif
        q->nThreads++;
    }

    return q;
}

YMDispatchQueueRef YMDispatchQueueCreate(YMStringRef name)
{
    YM_ONCE_DO(gDispatchGlobalInitOnce, __YMDispatchInitOnce);

    __ym_dispatch_queue_t *q = __YMDispatchQueueInitCommon(name, YMDispatchQueueUser);

    YMLockLock(gDispatch->lock);
    YMArrayAdd(gDispatch->userQueues,q);
    YMLockUnlock(gDispatch->lock);

    return q;
}

YMDispatchQueueRef YMDispatchGetGlobalQueue()
{
    YM_ONCE_DO(gDispatchGlobalInitOnce, __YMDispatchInitOnce);
    return gDispatch->globalQueue;
}

YMDispatchQueueRef YMDispatchGetMainQueue()
{
    YM_ONCE_DO(gDispatchGlobalInitOnce, __YMDispatchInitOnce);
    return gDispatch->mainQueue;
}

void __YMDispatchUserFinalize(ym_dispatch_user_t *);

typedef struct __ym_dispatch_dispatch
{
    YMSemaphoreRef sem;
    ym_dispatch_user_t *user;
} __ym_dispatch_dispatch_t;

void __YMDispatchDispatch(YMDispatchQueueRef queue, ym_dispatch_user_t *user, YMSemaphoreRef sem)
{
    __ym_dispatch_queue_t *q = (__ym_dispatch_queue_t *)queue;

    __ym_dispatch_dispatch_t *d = YMALLOC(sizeof(__ym_dispatch_dispatch_t));
    d->sem = sem;
    d->user = user;

    YMSelfLock(queue);
    YMArrayAdd(q->queueStack,d);
    YMSelfUnlock(queue);
#ifdef YM_DISPATCH_LOG
    printf("signaling %s\n",YMSTR(q->name));
#endif
    YMSemaphoreSignal(q->queueSem);

    if ( sem ) {
        YMSemaphoreWait(sem);
        YMRelease(sem);
        YMFREE(user);
        YMFREE(d);
    }
}

void __YMDispatchCheckExpandGlobalQueue(__ym_dispatch_queue_t *queue)
{
    YMStringRef name = NULL;

    if ( queue->type != YMDispatchQueueGlobal )
        return;

    ymassert(queue->nThreads <= queue->maxThreads, "%s has too many threads!",YMSTR(queue->name));
    if ( queue->nThreads == queue->maxThreads ) {
        //printf("%s is at max threads (%u)",YMSTR(queue->name),queue->maxThreads);
        return;
    }

    bool busy = YMSemaphoreTest(queue->queueSem);
    if ( busy ) {
        name = YMSTRC("com.combobulated.dispatch.global");
        YMThreadRef next = YMThreadCreate(name, __ym_dispatch_service_loop, queue);
        __ym_dispatch_queue_thread_t *qt = __YMDispatchQueueThreadCreate(next);

        YMSelfLock(queue);
        {
            queue->nThreads++;
            YMArrayAdd(queue->queueThreads,qt);
        }
        YMSelfUnlock(queue);

        bool okay = YMThreadStart(next);
        ymassert(okay, "ymdispatch failed to start %s queue thread", YMSTR(name));

        YMRelease(name);
#ifdef YM_DISPATCH_LOG
        printf("added worker to busy global queue %s (%u:%u) [%ld]\n",YMSTR(name),queue->nThreads,queue->maxThreads,YMArrayGetCount(queue->queueThreads));
#endif
    }
}

// major convenience for clients
ym_dispatch_user_t *__YMUserDispatchCopy(ym_dispatch_user_t *userDispatch)
{
    ym_dispatch_user_t *copy = YMALLOC(sizeof(ym_dispatch_user_t));
    copy->dispatchProc = userDispatch->dispatchProc;
    copy->context = userDispatch->context;
    copy->onCompleteProc = userDispatch->onCompleteProc;
    copy->mode = userDispatch->mode;
    
    return copy;
}

void YMAPI YMDispatchAsync(YMDispatchQueueRef queue, ym_dispatch_user_t *userDispatch)
{
    __YMDispatchCheckExpandGlobalQueue((__ym_dispatch_queue_t *)queue);
    ym_dispatch_user_t *copy = __YMUserDispatchCopy(userDispatch);
    __YMDispatchDispatch(queue, copy, NULL);
}
void YMAPI YMDispatchSync(YMDispatchQueueRef queue, ym_dispatch_user_t *userDispatch)
{
    __YMDispatchCheckExpandGlobalQueue((__ym_dispatch_queue_t *)queue);
    ym_dispatch_user_t *copy = __YMUserDispatchCopy(userDispatch);
    __YMDispatchDispatch(queue, copy, YMSemaphoreCreate(0));
}

void __YMDispatchUserFinalize(ym_dispatch_user_t *user)
{
    if ( user->onCompleteProc )
        user->onCompleteProc(user->context);
    switch(user->mode) {
        case ym_dispatch_user_context_release:
            YMRelease(user->context);
            break;
        case ym_dispatch_user_context_free:
            free(user->context);
            break;
        default:
            ymerr("invalid user dispatch mode ignored (%d,%p,%p)",user->mode,user->dispatchProc,user->context);
        case ym_dispatch_user_context_noop:
            break;
    }
}

YM_THREAD_RETURN YM_CALLING_CONVENTION __ym_dispatch_service_loop(YM_THREAD_PARAM ctx)
{
    __ym_dispatch_queue_t *q = ctx;
    
    __ym_dispatch_dispatch_t *aDispatch = NULL;
#ifdef YM_DISPATCH_LOG
    printf("[%s:%08lx] entered\n", YMSTR(q->name), _YMThreadGetCurrentThreadNumber());
#endif

    while ( true ) {
#ifdef YM_DISPATCH_LOG
        printf("[%s:%08lx] begin service loop\n", YMSTR(q->name), _YMThreadGetCurrentThreadNumber());
#endif
        YMSemaphoreWait(q->queueSem);

        if(q->exit)
            goto catch_return;

#ifdef YM_DISPATCH_LOG
        printf("[%s:%08lx] woke for service\n", YMSTR(q->name), _YMThreadGetCurrentThreadNumber());
#endif
        
        YMSelfLock(q);
        {
            aDispatch = (__ym_dispatch_dispatch_t *)YMArrayGet(q->queueStack,0);
            YMArrayRemove(q->queueStack,0);
        }
        YMSelfUnlock(q);
        
#ifdef YM_DISPATCH_LOG
        //printf("[%s:%08lx] entering dispatch\n", YMSTR(q->name), _YMThreadGetCurrentThreadNumber());
#endif
        aDispatch->user->dispatchProc(aDispatch->user->context);
#ifdef YM_DISPATCH_LOG
        //printf("[%s:%08lx] finished dispatch\n", YMSTR(q->name), _YMThreadGetCurrentThreadNumber());
#endif
        
        __YMDispatchUserFinalize(aDispatch->user);

        if ( aDispatch->sem )
            YMSemaphoreSignal(aDispatch->sem);
        else {
            YMFREE(aDispatch->user);
            YMFREE(aDispatch);
        }
    }
    
catch_return:
#ifdef YM_DISPATCH_LOG
    printf("[%s:%08lx] exiting\n", YMSTR(q->name), _YMThreadGetCurrentThreadNumber());
#endif
	
	YM_THREAD_END
}

void YMDispatchMain()
{
    YM_ONCE_DO(gDispatchGlobalInitOnce, __YMDispatchInitOnce);
    __ym_dispatch_service_loop((void *)gDispatch->mainQueue);
    ymassert(false,"YMDispatchMain will return");
}
