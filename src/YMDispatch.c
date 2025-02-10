#include "YMDispatch.h"
#include "YMThreadPriv.h"
#include "YMDictionary.h"
#include "YMLock.h"
#include "YMSemaphore.h"
#include "YMArray.h"
#include "YMUtilities.h"

#define ymlog_type YMLogDispatch
#include "YMLog.h"

#include <time.h>
#include <math.h>
#include <signal.h>

#define YM_DISPATCH_LOG
//#define YM_DISPATCH_LOG_1

YM_EXTERN_C_PUSH

typedef struct __ym_dispatch_timer
{
    YMDispatchQueueRef queue;
    struct timespec *time;
    timer_t timer;
    ym_dispatch_user_t *dispatch;
} __ym_dispatch_timer;
typedef struct __ym_dispatch_timer __ym_dispatch_timer_t;

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

    bool exit;
} __ym_dispatch_queue;
typedef struct __ym_dispatch_queue __ym_dispatch_queue_t;

typedef struct __ym_dispatch
{
    __ym_dispatch_queue_t *mainQueue;
    __ym_dispatch_queue_t *globalQueue;
    YMArrayRef userQueues;
    YMLockRef lock;
    YMArrayRef timers; // excluding next
    __ym_dispatch_timer_t *nextTimer;
} __ym_dispatch;
typedef struct __ym_dispatch __ym_dispatch_t;
__ym_dispatch_t *gDispatch = NULL;

YM_ONCE_DEF(__YMDispatchInitOnce);
void YMDispatchInit()
{
    YM_ONCE_DO_LOCAL(__YMDispatchInitOnce);
}

typedef struct __ym_dispatch_queue_thread
{
    YMThreadRef thread; // weak
    bool busy;
} __ym_dispatch_queue_thread_t;
typedef struct __ym_dispatch_queue_thread __ym_dispatch_queue_thread_t;

YM_ENTRY_POINT(__ym_dispatch_service_loop);
YM_ENTRY_POINT(__ym_dispatch_user_service_loop);
__ym_dispatch_queue_t *__YMDispatchQueueInitCommon(YMStringRef name, YMDispatchQueueType type);

void __ym_dispatch_sigalarm(int);

YM_ONCE_OBJ(gDispatchGlobalInitOnce);
YM_ONCE_DEF(__YMDispatchInitOnce);
YM_ONCE_FUNC(__YMDispatchInitOnce,
{
    gDispatch = YMALLOC(sizeof(__ym_dispatch_t));

    YMStringRef name = YMSTRC("com.combobulated.dispatch.main");
    gDispatch->mainQueue = __YMDispatchQueueInitCommon(name, YMDispatchQueueMain);
    YMRelease(name);

    name = YMSTRC("com.combobulated.dispatch.global-0");
    gDispatch->globalQueue = __YMDispatchQueueInitCommon(name, YMDispatchQueueGlobal);
    YMRelease(name);
	gDispatch->userQueues = YMArrayCreate();
    gDispatch->lock = YMLockCreate(); // not a ymtype

    gDispatch->nextTimer = NULL;
    gDispatch->timers = YMArrayCreate();
    signal(SIGALRM, __ym_dispatch_sigalarm);
})

__ym_dispatch_queue_thread_t *__YMDispatchQueueThreadCreate(YMThreadRef thread)
{
    __ym_dispatch_queue_thread_t *qt = YMALLOC(sizeof(__ym_dispatch_queue_thread_t));
    qt->thread = thread;
    qt->busy = false;
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
    uint8_t nThreads = YMArrayGetCount(q->queueThreads);
    bool willExit = ! q->exit;
    YMSelfUnlock(q);

    if ( willExit ) {
        q->exit = true;
        while ( nThreads-- )
            YMSemaphoreSignal(q->queueSem);
    }

    YMRelease(q->name);
    YMRelease(q->queueStack);
    YMRelease(q->queueSem);
    YMRelease(q->queueThreads);
}

typedef struct __ym_dispatch_service_loop_context
{
    __ym_dispatch_queue_t *q;
    __ym_dispatch_queue_thread_t *qt;
} __ym_dispatch_service_loop_context;
typedef struct __ym_dispatch_service_loop_context __ym_dispatch_service_loop_context_t;

__ym_dispatch_queue_t *__YMDispatchQueueInitCommon(YMStringRef name, YMDispatchQueueType type)
{
    __ym_dispatch_queue_t *q = (__ym_dispatch_queue_t *)_YMAlloc(_YMDispatchQueueTypeID,sizeof(__ym_dispatch_queue_t));
    q->name = name ? YMRetain(name) : YMSTRC("*");
    q->type = type;
    q->queueStack = YMArrayCreate();
    q->queueSem = YMSemaphoreCreate(0);
    q->queueThreads = YMArrayCreate();
    q->exit = false;

    ym_entry_point entry = type == YMDispatchQueueUser ? __ym_dispatch_user_service_loop : __ym_dispatch_service_loop;
    if ( type != YMDispatchQueueMain ) {
        __ym_dispatch_service_loop_context_t *c = YMALLOC(sizeof(__ym_dispatch_service_loop_context_t));
        YMThreadRef first = YMThreadCreate(name, entry, c);
        c->q = (__ym_dispatch_queue_t *)YMRetain(q);
        __ym_dispatch_queue_thread_t *qt = __YMDispatchQueueThreadCreate(first);
        c->qt = qt;

        bool okay = YMThreadStart(first);
        if ( ! okay ) {
            printf("ymdispatch failed to start %s queue thread\n", YMSTR(name));
            abort();
        }

        YMArrayAdd(q->queueThreads,qt); // no sync, guarded by once


#ifdef YM_DISPATCH_LOG
        printf("started %s queue thread %p[%p,%p] '%s'\n", ( type == YMDispatchQueueGlobal ) ? "global" : "user", c, c->q, c->qt, YMSTR(name));
#endif
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
#ifdef YM_DISPATCH_LOG_1
    printf("signaling %s for %p[%p,%p]\n",YMSTR(q->name),user,user->dispatchProc,user->context);
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

    YMSelfLock(queue);
    {
        bool overflow = YMArrayGetCount(queue->queueThreads) >= YMGetDefaultThreadsForCores(YMGetNumberOfCoresAvailable());
        if ( YMArrayGetCount(queue->queueThreads) >= YMGetDefaultThreadsForCores(YMGetNumberOfCoresAvailable()) ) {
            bool busy = true;
            for(int i = 0; i < YMArrayGetCount(queue->queueThreads); i++) {
                __ym_dispatch_queue_thread_t *qt = (__ym_dispatch_queue_thread_t *)YMArrayGet(queue->queueThreads,i);
                if ( ! qt->busy ) {
                    busy = false;
                    break;
                }
            }

            if ( ! busy ) {
#ifdef YM_DISPATCH_LOG_1
                printf("threads are available on %s\n",YMSTR(queue->name));
#endif
                YMSelfUnlock(queue);
                return;
            }
        }

        name = YMStringCreateWithFormat("com.combobulated.dispatch.global-%ld",YMArrayGetCount(queue->queueThreads),NULL);
        __ym_dispatch_service_loop_context_t *c = YMALLOC(sizeof(__ym_dispatch_service_loop_context_t));
        YMThreadRef next = YMThreadCreate(name, __ym_dispatch_service_loop, c);
        __ym_dispatch_queue_thread_t *qt = __YMDispatchQueueThreadCreate(next);
        c->q = queue;
        c->qt = qt;

        YMArrayAdd(queue->queueThreads,qt);

        bool okay = YMThreadStart(next);
        ymassert(okay, "ymdispatch failed to start %s queue thread", YMSTR(name));

        YMRelease(name);
#ifdef YM_DISPATCH_LOG
        printf("added %sworker to busy global queue %s [%ld]\n",overflow?"overflow ":"",YMSTR(name),YMArrayGetCount(queue->queueThreads));
    #endif
    }
    YMSelfUnlock(queue);
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

void YMAPI YMDispatchAfter(YMDispatchQueueRef queue, ym_dispatch_user_t *userDispatch, double secondsAfter)
{
    struct timespec *timespec = YMALLOC(sizeof(struct timespec));
    int ret = clock_gettime(CLOCK_REALTIME,timespec);
    if ( ret != 0 ) {
        printf("clock_gettime failed: %d %s",errno,strerror(errno));
        abort();
    }

#define NSEC_PER_SEC 1000000000
    int seconds = floor(secondsAfter);
    time_t nSeconds = (secondsAfter - seconds) * NSEC_PER_SEC;
    if ( nSeconds + timespec->tv_nsec > NSEC_PER_SEC ) {
        seconds++;
        nSeconds -= NSEC_PER_SEC;
    }
    timespec->tv_nsec = nSeconds;
    timespec->tv_sec += seconds;

    __ym_dispatch_timer_t *timer = YMALLOC(sizeof(__ym_dispatch_timer_t));
    timer->time = timespec;
    timer->queue = YMRetain(queue);
    timer->dispatch = __YMUserDispatchCopy(userDispatch);

    YMLockLock(gDispatch->lock);
    {
        if ( ! gDispatch->nextTimer ) {
            gDispatch->nextTimer = timer;
        } else {
            __ym_dispatch_timer_t *prevNextTimer = gDispatch->nextTimer;
            for ( int i = 0; i < YMArrayGetCount(gDispatch->timers); i++ ) {
                __ym_dispatch_timer_t *aTimer = (__ym_dispatch_timer_t *)YMArrayGet(gDispatch->timers,i);
                if ( timespec->tv_sec < aTimer->time->tv_sec )
                    gDispatch->nextTimer = timer;
                else if ( timespec->tv_sec == aTimer->time->tv_sec ) {
                    if ( timespec->tv_nsec < aTimer->time->tv_nsec ) {
                        gDispatch->nextTimer = timer;
                    } else if ( timespec->tv_nsec == aTimer->time->tv_nsec ) {
                        if ( timespec->tv_nsec == NSEC_PER_SEC - 1 )
                            timespec->tv_sec++;
                        else
                            timespec->tv_nsec++;
                    }
                }
            }

            if ( timer == gDispatch->nextTimer ) {
                if ( prevNextTimer )
                    YMArrayAdd(gDispatch->timers,prevNextTimer);
            }
        }

        ret = timer_create(CLOCK_REALTIME,NULL,&(timer->timer));
        if ( ret != 0 ) {
            printf("timer_create failed: %d %s",errno,strerror(errno));
            abort();
        }

        struct itimerspec ispec = {0};
        ispec.it_value.tv_sec = timespec->tv_sec;
        ispec.it_value.tv_nsec = timespec->tv_nsec;
        ret = timer_settime(timer->timer,TIMER_ABSTIME,&ispec,NULL);
        if ( ret != 0 ) {
            printf("timer_settime failed: %d %s\n",errno,strerror(errno));
            abort();
        }
    }
    YMLockUnlock(gDispatch->lock);

#ifdef YM_DISPATCH_LOG
    printf("scheduled %p[%p,%p] after %f seconds on %p(%s)  \n",userDispatch,userDispatch->dispatchProc,userDispatch->context,secondsAfter,queue,YMSTR(((__ym_dispatch_queue_t *)queue)->name));
#endif
}

void __ym_dispatch_sigalarm(int signum)
{
#ifdef YM_DISPATCH_LOG
    printf("__ym_dispatch_sigalarm\n");
#endif
    if ( signum != SIGALRM ) {
        printf("__ym_dispatch_sigalarm: %d",signum);
        abort();
    } else if ( ! gDispatch->nextTimer ) {
        printf("__ym_dispatch_sigalarm: next timer not set\n");
        abort();
    }

    __ym_dispatch_timer_t *thisTimer = gDispatch->nextTimer;

    __ym_dispatch_timer_t *nextTimer = NULL;
    YMLockLock(gDispatch->lock);
    {
        for( int i = 0; i < YMArrayGetCount(gDispatch->timers); i++ ) {
            __ym_dispatch_timer_t *aTimer = (__ym_dispatch_timer_t *)YMArrayGet(gDispatch->timers,i);

            if ( ! nextTimer ) {
                nextTimer = aTimer;
                continue;
            }

            if ( aTimer->time->tv_sec < nextTimer->time->tv_sec ) {
                nextTimer = aTimer;
            } else if ( aTimer->time->tv_sec == nextTimer->time->tv_sec ) {
                if ( aTimer->time->tv_nsec < nextTimer->time->tv_nsec ) {
                    nextTimer = aTimer;
                }
            }
        }
        gDispatch->nextTimer = nextTimer;
        if ( nextTimer ) {
            YMArrayRemoveObject(gDispatch->timers,nextTimer);
        }
    }
    YMLockUnlock(gDispatch->lock);

#ifdef YM_DISPATCH_LOG
    printf("dispatching %p[%p,%p] on %p(%s)\n",thisTimer->dispatch,thisTimer->dispatch->dispatchProc,thisTimer->dispatch->context,thisTimer->queue,YMSTR((((__ym_dispatch_queue_t *)(thisTimer->queue))->name)));
#endif
    YMDispatchAsync(thisTimer->queue,thisTimer->dispatch);

    YMFREE(thisTimer->time);
    YMRelease(thisTimer->queue);
    YMFREE(thisTimer->dispatch);
    YMFREE(thisTimer);

    struct timespec now;

    int ret = clock_gettime(CLOCK_REALTIME,&now);
    if ( ret != 0 ) {
        printf("clock_gettime failed: %d %s",errno,strerror(errno));
        return;
    }

#ifdef YM_DISPATCH_LOG
    if ( nextTimer ) {
        printf("nextTimer is now %p[%p,%p] in %ld.%9ldf on %s\n",nextTimer->dispatch,nextTimer->dispatch->dispatchProc,nextTimer->dispatch->context,nextTimer->time->tv_sec-now.tv_sec,nextTimer->time->tv_nsec-now.tv_nsec,YMSTR(((__ym_dispatch_queue_t *)(nextTimer->queue))->name));
    } else
        printf("nextTimer is now (null)\n");
#endif
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

YM_ENTRY_POINT(__ym_dispatch_user_service_loop)
{
    __ym_dispatch_service_loop(context);
}

YM_ENTRY_POINT(__ym_dispatch_service_loop)
{
    __ym_dispatch_service_loop_context_t *c = context;
    __ym_dispatch_queue_t *q = c->q;
    __ym_dispatch_queue_thread_t *qt = c->qt;
    
    __ym_dispatch_dispatch_t *aDispatch = NULL;
#ifdef YM_DISPATCH_LOG
    printf("[%s:%08lx] entered\n", YMSTR(q->name), _YMThreadGetCurrentThreadNumber());
#endif

    while ( true ) {
#ifdef YM_DISPATCH_LOG_1
        printf("[%s:%08lx] begin service loop\n", YMSTR(q->name), _YMThreadGetCurrentThreadNumber());
#endif
        YMSemaphoreWait(q->queueSem);

        if(q->exit)
            goto catch_return;

#ifdef YM_DISPATCH_LOG_1
        printf("[%s:%08lx] woke for service\n", YMSTR(q->name), _YMThreadGetCurrentThreadNumber());
#endif
        int64_t count;
        YMSelfLock(q);
        {
            aDispatch = (__ym_dispatch_dispatch_t *)YMArrayGet(q->queueStack,0);
            YMArrayRemove(q->queueStack,0);
            count = YMArrayGetCount(q->queueStack);
        }
        YMSelfUnlock(q);
        
#ifdef YM_DISPATCH_LOG_1
        printf("[%s:%08lx:%ld] entering dispatch\n", YMSTR(q->name), _YMThreadGetCurrentThreadNumber(),count);
#endif
        if ( qt ) qt->busy = true;
        aDispatch->user->dispatchProc(aDispatch->user->context);
        if ( qt ) qt->busy = false;
#ifdef YM_DISPATCH_LOG_1
        printf("[%s:%08lx:%ld] finished dispatch\n", YMSTR(q->name), _YMThreadGetCurrentThreadNumber(),count);
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

    if ( c->qt ) YMFREE(c->qt);
    YMRelease(c->q);
    YMFREE(c);

#ifdef YM_DISPATCH_LOG
    printf("[%s:%08lx] exiting\n", YMSTR(q->name), _YMThreadGetCurrentThreadNumber());
#endif
}

void YMDispatchMain()
{
    YM_ONCE_DO(gDispatchGlobalInitOnce, __YMDispatchInitOnce);
    __ym_dispatch_service_loop_context_t *c = YMALLOC(sizeof(__ym_dispatch_service_loop_context_t));
    __ym_dispatch_queue_thread_t *qt = YMALLOC(sizeof(__ym_dispatch_queue_thread_t));
    qt->thread = NULL;
    qt->busy = false;
    c->q = gDispatch->mainQueue;
    c->qt = qt;
    __ym_dispatch_service_loop(c);
    printf("YMDispatchMain will return\n");
    abort();
}
