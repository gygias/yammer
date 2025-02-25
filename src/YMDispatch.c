#include "YMDispatch.h"
#include "YMThreadPriv.h"
#include "YMDictionary.h"
#include "YMLock.h"
#include "YMSemaphore.h"
#include "YMArray.h"
#include "YMPipe.h"
#include "YMUtilities.h"
#include "YMDispatchPriv.h"

#define ymlog_type YMLogDispatch
#include "YMLog.h"

#include <time.h>
#include <math.h>
#include <signal.h>
#if defined(YMLINUX) || defined(YMAPPLE)
# include <sys/select.h>
# include <fcntl.h>
#else
# error implement me
#endif

#if defined(YMAPPLE)
#include <dispatch/dispatch.h>
#endif

#define YM_DISPATCH_LOG
//#define YM_DISPATCH_LOG_1
#define YM_AFTER_LOG
#define YM_SOURCE_LOG
//#define YM_SOURCE_LOG_point_5
//#define YM_SOURCE_LOG_1
//#define YM_SOURCE_LOG_2

YM_EXTERN_C_PUSH

typedef struct __ym_dispatch_timer
{
    YMDispatchQueueRef queue;
    struct timespec time;
#if !defined(YMAPPLE)
    timer_t timer;
#endif
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
    YMSemaphoreRef queueExitSem;

    bool exit;
} __ym_dispatch_queue;
typedef struct __ym_dispatch_queue __ym_dispatch_queue_t;

typedef struct __ym_dispatch
{
    __ym_dispatch_queue_t *mainQueue;
    __ym_dispatch_queue_t *globalQueue;
    YMArrayRef userQueues;
    YMLockRef lock;
    YMArrayRef orderedTimers;

    // sources
    YMArrayRef sources; // __ym_dispatch_source_t
    YMLockRef sourcesLock;
    YMThreadRef selectThread;
    YMPipeRef selectSignalPipe;
} __ym_dispatch;
typedef struct __ym_dispatch __ym_dispatch_t;
__ym_dispatch_t *gDispatch = NULL;

YM_ONCE_DEF(__YMDispatchInitOnce);
void YMDispatchInit(void)
{
    YM_ONCE_DO_LOCAL(__YMDispatchInitOnce);
}

typedef struct __ym_dispatch_queue_thread
{
    YMThreadRef thread; // weak
    bool busy;
} __ym_dispatch_queue_thread;
typedef struct __ym_dispatch_queue_thread __ym_dispatch_queue_thread_t;

YM_ENTRY_POINT(__ym_dispatch_main_service_loop);
YM_ENTRY_POINT(__ym_dispatch_global_service_loop);
YM_ENTRY_POINT(__ym_dispatch_user_service_loop);
YM_ENTRY_POINT(__ym_dispatch_service_loop);

__ym_dispatch_queue_t *__YMDispatchQueueInitCommon(YMStringRef name, YMDispatchQueueType type, ym_entry_point entry);
void __YMDispatchQueueExitSync(__ym_dispatch_queue_t *q);

void __ym_dispatch_sigalarm(int);

YM_ONCE_OBJ(gDispatchGlobalInitOnce);
YM_ONCE_DEF(__YMDispatchInitOnce);
YM_ONCE_FUNC(__YMDispatchInitOnce,
{
    gDispatch = YMALLOC(sizeof(__ym_dispatch_t));

    YMStringRef name = YMSTRC("com.combobulated.dispatch.main");
    gDispatch->mainQueue = __YMDispatchQueueInitCommon(name, YMDispatchQueueMain, __ym_dispatch_main_service_loop);
    YMRelease(name);

    name = YMSTRC("com.combobulated.dispatch.global");
    gDispatch->globalQueue = __YMDispatchQueueInitCommon(name, YMDispatchQueueGlobal, __ym_dispatch_global_service_loop);
    YMRelease(name);
	gDispatch->userQueues = YMArrayCreate2(true);
    gDispatch->lock = YMLockCreate(); // not a ymtype

    gDispatch->orderedTimers = YMArrayCreate();
    signal(SIGALRM, __ym_dispatch_sigalarm);

    // sources
    gDispatch->sources = NULL;
    gDispatch->sourcesLock = NULL;
    gDispatch->selectThread = NULL;
    gDispatch->selectSignalPipe = NULL;
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
#ifdef YM_DISPATCH_LOG
    printf("%s %p\n",__FUNCTION__,(void *)p_);
#endif
    __ym_dispatch_queue_t *q = (__ym_dispatch_queue_t *)p_;
    if ( q->type == YMDispatchQueueGlobal || q->type == YMDispatchQueueMain )
        __YM_DISPATCH_CATCH_MISUSE(q);

    YMRelease(q->name);
    YMRelease(q->queueStack);
    YMRelease(q->queueSem);
    YMRelease(q->queueThreads);
    YMRelease(q->queueExitSem);
}

typedef struct __ym_dispatch_service_loop_context
{
    __ym_dispatch_queue_t *q;
    __ym_dispatch_queue_thread_t *qt;
} __ym_dispatch_service_loop_context;
typedef struct __ym_dispatch_service_loop_context __ym_dispatch_service_loop_context_t;

__ym_dispatch_queue_t *__YMDispatchQueueInitCommon(YMStringRef name, YMDispatchQueueType type, ym_entry_point entry)
{
    __ym_dispatch_queue_t *q = (__ym_dispatch_queue_t *)_YMAlloc(_YMDispatchQueueTypeID,sizeof(__ym_dispatch_queue_t));
    q->name = name ? YMRetain(name) : YMSTRC("*");
    q->type = type;
    q->queueStack = YMArrayCreate();
    q->queueSem = YMSemaphoreCreate(0);
    q->queueThreads = YMArrayCreate();
    q->queueExitSem = YMSemaphoreCreate(0);
    q->exit = false;

    if ( type != YMDispatchQueueMain ) {
#warning watchlist todo recycle user threads
        int nThreads = ( type == YMDispatchQueueUser ) ? 1 : YMGetDefaultThreadsForCores(YMGetNumberOfCoresAvailable());
        while ( nThreads-- ) {
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
            printf("started %s queue thread %p[%p,%p] '%s'\n", ( type == YMDispatchQueueGlobal ) ? "global" : "user", (void *)c, (void *)c->q, (void *)c->qt, YMSTR(name));
#endif
        }
    }

    return q;
}

YMDispatchQueueRef YMDispatchQueueCreate(YMStringRef name)
{
    YM_ONCE_DO(gDispatchGlobalInitOnce, __YMDispatchInitOnce);

    __ym_dispatch_queue_t *q = __YMDispatchQueueInitCommon(name, YMDispatchQueueUser, __ym_dispatch_user_service_loop);

    YMLockLock(gDispatch->lock);
    YMArrayAdd(gDispatch->userQueues,q);
    YMLockUnlock(gDispatch->lock);

    return q;
}

void YMAPI YMDispatchQueueRelease(YMDispatchQueueRef queue)
{
    YMLockLock(gDispatch->lock);
    int64_t before = YMArrayGetCount(gDispatch->userQueues);
    YMArrayRemoveObject(gDispatch->userQueues,queue);
    int64_t after = YMArrayGetCount(gDispatch->userQueues);
    if ( before == after ) { printf("YMArrayRemoveObject: %ld\n",before); abort(); }
    YMLockUnlock(gDispatch->lock);

    __YMDispatchQueueExitSync((__ym_dispatch_queue_t *)queue);

    YMRelease(queue);
}

YMDispatchQueueRef YMDispatchGetGlobalQueue(void)
{
    YM_ONCE_DO(gDispatchGlobalInitOnce, __YMDispatchInitOnce);
    return gDispatch->globalQueue;
}

YMDispatchQueueRef YMDispatchGetMainQueue(void)
{
    YM_ONCE_DO(gDispatchGlobalInitOnce, __YMDispatchInitOnce);
    return gDispatch->mainQueue;
}

void __YMDispatchUserFinalize(ym_dispatch_user_t *);

typedef struct __ym_dispatch_dispatch
{
    YMSemaphoreRef sem;
    ym_dispatch_user_t *user;
    bool isSource;
} __ym_dispatch_dispatch_t;

void __YMDispatchDispatch(YMDispatchQueueRef queue, ym_dispatch_user_t *user, YMSemaphoreRef sem, bool isSource)
{
    __ym_dispatch_queue_t *q = (__ym_dispatch_queue_t *)queue;

    __ym_dispatch_dispatch_t *d = YMALLOC(sizeof(__ym_dispatch_dispatch_t));
    d->sem = sem;
    d->user = user;
    d->isSource = isSource;

    YMSelfLock(queue);
    YMArrayAdd(q->queueStack,d);
    YMSelfUnlock(queue);
#ifdef YM_DISPATCH_LOG_1
    printf("signaling %s for %p[%p,%p]\n",YMSTR(q->name),user,user->dispatchProc,user->context);
#endif
    YMSemaphoreSignal(q->queueSem);

    if ( sem ) {
        YMSemaphoreWait(sem);
        if ( ! isSource ) {
            YMRelease(sem);
            YMFREE(user);
        }
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
        if ( overflow ) {
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
        YMThreadRef next = YMThreadCreate(name, __ym_dispatch_global_service_loop, c);
        __ym_dispatch_queue_thread_t *qt = __YMDispatchQueueThreadCreate(next);
        c->q = (__ym_dispatch_queue_t *)YMRetain(queue);
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

[[clang::optnone]]
void YMAPI YMDispatchAsync(YMDispatchQueueRef queue, ym_dispatch_user_t *userDispatch)
{
    __YMDispatchCheckExpandGlobalQueue((__ym_dispatch_queue_t *)queue);
    ym_dispatch_user_t *copy = __YMUserDispatchCopy(userDispatch);
    __YMDispatchDispatch(queue, copy, NULL, false);
}

[[clang::optnone]]
void YMAPI YMDispatchSync(YMDispatchQueueRef queue, ym_dispatch_user_t *userDispatch)
{
    __YMDispatchCheckExpandGlobalQueue((__ym_dispatch_queue_t *)queue);
    ym_dispatch_user_t *copy = __YMUserDispatchCopy(userDispatch);
    __YMDispatchDispatch(queue, copy, YMSemaphoreCreate(0),false);
}

YM_ENTRY_POINT(__ym_dispatch_source_select_loop);

YM_ONCE_OBJ(gDispatchGlobalInitSelect);
YM_ONCE_DEF(__YMDispatchSelectOnce);
YM_ONCE_FUNC(__YMDispatchSelectOnce,
{
    YM_IO_BOILERPLATE

    gDispatch->sources = YMArrayCreate();
    gDispatch->sourcesLock = YMLockCreateWithOptions(YMLockRecursive);

    gDispatch->selectSignalPipe = YMPipeCreate(NULL);
    YMStringRef name = YMSTRC("com.combobulated.dispatch.select");
    gDispatch->selectThread = YMThreadCreate(name,__ym_dispatch_source_select_loop,NULL);
    YMThreadStart(gDispatch->selectThread);

    char buf[1];
    int fd = YMPipeGetOutputFile(gDispatch->selectSignalPipe);
    YM_READ_FILE(fd,buf,1);
    if ( result != 1 || buf[0] != '#' ) {
        printf("failed to wait for select loop %c %d %s\n",buf[0],error,errorStr);
        abort();
    }
#ifdef YM_SOURCE_LOG
    else
        printf("%d->#\n",fd);
#endif
})

typedef struct __ym_dispatch_source
{
    __ym_dispatch_queue_t *queue;
    ym_dispatch_source_type type;
    uint64_t data;
    ym_dispatch_user_t *user;

    YMSemaphoreRef servicingSem;
} __ym_dispatch_source;
typedef struct __ym_dispatch_source __ym_dispatch_source_t;

ym_dispatch_source_t YMAPI YMDispatchSourceCreate(YMDispatchQueueRef queue, ym_dispatch_source_type type, uint64_t data, ym_dispatch_user_t *user)
{
    YM_IO_BOILERPLATE

    YM_ONCE_DO(gDispatchGlobalInitOnce, __YMDispatchInitOnce);
    YM_ONCE_DO(gDispatchGlobalInitSelect, __YMDispatchSelectOnce);

    __ym_dispatch_source_t *source = YMALLOC(sizeof(__ym_dispatch_source_t));
    source->queue = (__ym_dispatch_queue_t *)YMRetain(queue);
    source->type = type;
    source->data = data;
    source->servicingSem = YMSemaphoreCreate(1);

    ym_dispatch_user_t *userCopy = YMALLOC(sizeof(ym_dispatch_user_t));
    userCopy->dispatchProc = user->dispatchProc;
    userCopy->context = user->context;
    userCopy->onCompleteProc = user->onCompleteProc;
    userCopy->mode = user->mode;
    source->user = userCopy;

    YMLockLock(gDispatch->sourcesLock);
    YMArrayAdd(gDispatch->sources,source);
    YMLockUnlock(gDispatch->sourcesLock);

    if ( type == ym_dispatch_source_readable || type == ym_dispatch_source_writeable ) {
        char signalBuf[] = { '$' };
        YMFILE signalFile = YMPipeGetInputFile(gDispatch->selectSignalPipe);
        YM_WRITE_FILE(signalFile,signalBuf,1);
        if ( result != 1 ) {
            printf("failed to $ignal $elect loop %d %d %s\n",signalFile,error,errorStr);
            abort();
        }
#ifdef YM_SOURCE_LOG_1
        else
            printf("$->%d for %p\n",signalFile,source);
#endif
    }

    return source;
}

void __YMDispatchQueueExitSync(__ym_dispatch_queue_t *q)
{
    ymerr("%s: %s!",__FUNCTION__,YMSTR(q->name));
    YMSelfLock(q);
    q->exit = true;
    for( int i = 0; i < YMArrayGetCount(q->queueThreads); ) {
        YMSemaphoreSignal(q->queueSem);
        YMSemaphoreWait(q->queueExitSem);
        YMArrayRemove(q->queueThreads,i);
    }
    YMSelfUnlock(q);
}

void YMAPI YMDispatchSourceDestroy(ym_dispatch_source_t source)
{
    YM_ONCE_DO(gDispatchGlobalInitSelect, __YMDispatchSelectOnce);

    __ym_dispatch_source_t *s = source;

    YMLockLock(gDispatch->sourcesLock);
    int64_t count = YMArrayGetCount(gDispatch->sources);
    const void *item = NULL;
    for ( int i = 0; i < count; i++ ) {
        const void *anItem = YMArrayGet(gDispatch->sources,i);
        if ( anItem == s ) {
#ifdef YM_SOURCE_LOG_1
            printf("removed source %ld with index %d[%ld]\n",s->data,i,count);
#endif
            item = anItem;
            YMArrayRemove(gDispatch->sources,i);
            break;
        }
    }
    YMLockUnlock(gDispatch->sourcesLock);

    if ( ! item ) {
        printf("source %p not in list[%ld], presuming select loop has recently reset\n",source,count);
    }

#warning watchlist recycle user threads
    if ( s->queue->type == YMDispatchQueueUser ) {
        //__YMDispatchQueueExitSync(s->queue);
        YMSemaphoreWait(s->servicingSem);
    }

    __YMDispatchUserFinalize(s->user);

    YMRelease(s->servicingSem);
    YMRelease(s->queue);
    YMFREE(s->user);
    YMFREE(s);
}

void YMAPI YMDispatchAfter(YMDispatchQueueRef queue, ym_dispatch_user_t *userDispatch, double secondsAfter)
{
#if !defined(YMAPPLE)
    struct timespec timespec;
    int ret = clock_gettime(CLOCK_REALTIME,&timespec);
    if ( ret != 0 ) {
        printf("clock_gettime failed: %d %s",errno,strerror(errno));
        abort();
    }

    struct timespec doubleToStruct = YMTimespecFromDouble(secondsAfter);
    timespec.tv_sec += doubleToStruct.tv_sec;
    timespec.tv_nsec += doubleToStruct.tv_nsec;
    timespec = YMTimespecNormalize(timespec);

    __ym_dispatch_timer_t *timer = YMALLOC(sizeof(__ym_dispatch_timer_t));
    memcpy(&timer->time,&timespec,sizeof(timespec));
    timer->queue = YMRetain(queue);
    timer->dispatch = __YMUserDispatchCopy(userDispatch);

    YMLockLock(gDispatch->lock);
    {
        bool added = false;
        for ( int i = 0; i < YMArrayGetCount(gDispatch->orderedTimers); i++ ) {
            __ym_dispatch_timer_t *aTimer = (__ym_dispatch_timer_t *)YMArrayGet(gDispatch->orderedTimers,i);
            ComparisonResult result = YMTimespecCompare(timer->time, aTimer->time);
            if ( result == LessThan ) {
                YMArrayInsert(gDispatch->orderedTimers,i,timer);
                added = true;
                break;
            } else if ( result == EqualTo ) {
                timer->time.tv_nsec++;
                YMArrayInsert(gDispatch->orderedTimers,i+1,timer);
                added = true;
                break;
            }
        }

        if ( ! added ) {
            YMArrayAdd(gDispatch->orderedTimers,timer);
        }

#ifdef YM_AFTER_LOG
        struct timespec now;
        ret = clock_gettime(CLOCK_REALTIME,&now);
        if ( ret != 0 ) {
            printf("clock_gettime failed: %d %s",errno,strerror(errno));
            return;
        }
        double inSecs = YMTimespecSince(timer->time,now);
        printf("new timer scheduled in position %ld in %0.9f on %s (%p q%p dp%p dctx%p)\n",YMArrayIndexOf(gDispatch->orderedTimers,timer),inSecs,YMSTR((((__ym_dispatch_queue_t *)(timer->queue))->name)),timer,timer->queue,timer->dispatch->dispatchProc,timer->dispatch->context);
#endif

        ret = timer_create(CLOCK_REALTIME,NULL,&(timer->timer));
        if ( ret != 0 ) {
            printf("timer_create failed: %d %s",errno,strerror(errno));
            abort();
        }

        struct itimerspec ispec = {0};
        ispec.it_value.tv_sec = timespec.tv_sec;
        ispec.it_value.tv_nsec = timespec.tv_nsec;
        ret = timer_settime(timer->timer,TIMER_ABSTIME,&ispec,NULL);
        if ( ret != 0 ) {
            struct timespec now;
            int ret = clock_gettime(CLOCK_REALTIME,&now);
            if ( ret != 0 ) {
                printf("clock_gettime failed: %d %s",errno,strerror(errno));
                return;
            }
            double since = YMTimespecSince(timespec,now);
            printf("timer_settime failed: %lds %ldns (%0.9f) %d %s\n",timespec.tv_sec,timespec.tv_nsec,since,errno,strerror(errno));
            abort();
        }
    }
    YMLockUnlock(gDispatch->lock);
#else
    YMRetain(queue);
    ym_dispatch_user_t *userCopy = __YMUserDispatchCopy(userDispatch);
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,secondsAfter*NSEC_PER_SEC), dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT,0), ^{
        YMDispatchAsync(queue,userCopy);
        YMFREE(userCopy);
        YMRelease(queue);
    });
#endif
}

void __ym_dispatch_sigalarm(int signum)
{
#ifdef YM_DISPATCH_LOG_1
    printf("__ym_dispatch_sigalarm\n");
#endif
    if ( signum != SIGALRM ) {
        printf("__ym_dispatch_sigalarm: %d",signum);
        abort();
    }

    __ym_dispatch_timer_t *nextTimer = NULL;
    __ym_dispatch_timer_t *nextNextTimer = NULL;
    YMLockLock(gDispatch->lock);
    {
        if ( ! YMArrayGetCount(gDispatch->orderedTimers) ) {
            printf("__ym_dispatch_sigalarm: next timer not set %ld\n",YMArrayGetCount(gDispatch->orderedTimers));
            abort();
        }

        nextTimer = (__ym_dispatch_timer_t *)YMArrayGet(gDispatch->orderedTimers,0);
        YMArrayRemove(gDispatch->orderedTimers,0);

        if ( YMArrayGetCount(gDispatch->orderedTimers) )
            nextNextTimer = (__ym_dispatch_timer_t *)YMArrayGet(gDispatch->orderedTimers,0);
    }
    YMLockUnlock(gDispatch->lock);

    if ( nextTimer ) {
#ifdef YM_AFTER_LOG
        printf("dispatching %p[%p,%p] on %p(%s)\n",nextTimer->dispatch,nextTimer->dispatch->dispatchProc,nextTimer->dispatch->context,nextTimer->queue,YMSTR((((__ym_dispatch_queue_t *)(nextTimer->queue))->name)));
#endif
        YMDispatchAsync(nextTimer->queue,nextTimer->dispatch);

        YMRelease(nextTimer->queue);
        YMFREE(nextTimer->dispatch);
        YMFREE(nextTimer);
    }

#ifdef YM_AFTER_LOG
    if ( nextNextTimer ) {
        struct timespec now;
        int ret = clock_gettime(CLOCK_REALTIME,&now);
        if ( ret != 0 ) {
            printf("clock_gettime failed: %d %s",errno,strerror(errno));
            return;
        }
        double inSecs = YMTimespecSince(nextNextTimer->time,now);
        printf("next timer is now %p[%p,%p] in %0.9f on %s\n",nextNextTimer->dispatch,nextNextTimer->dispatch->dispatchProc,nextNextTimer->dispatch->context,inSecs,YMSTR(((__ym_dispatch_queue_t *)(nextNextTimer->queue))->name));
    } else
        printf("next timer is now (null)\n");
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

[[clang::optnone]]
YM_ENTRY_POINT(__ym_dispatch_user_service_loop)
{
    __ym_dispatch_service_loop(context);
}

[[clang::optnone]]
YM_ENTRY_POINT(__ym_dispatch_global_service_loop)
{
    __ym_dispatch_service_loop(context);
}

[[clang::optnone]]
YM_ENTRY_POINT(__ym_dispatch_main_service_loop)
{
    __ym_dispatch_service_loop(context);
}

[[clang::optnone]]
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
        int64_t count;
#endif
        YMSelfLock(q);
        {
            aDispatch = (__ym_dispatch_dispatch_t *)YMArrayGet(q->queueStack,0);
            YMArrayRemove(q->queueStack,0);
#ifdef YM_DISPATCH_LOG_1
            count = YMArrayGetCount(q->queueStack);
#endif
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
        if ( ! aDispatch->isSource ) {
            __YMDispatchUserFinalize(aDispatch->user);
        }

        if ( aDispatch->sem )
            YMSemaphoreSignal(aDispatch->sem);
        else {
            if ( ! aDispatch->isSource )
                YMFREE(aDispatch->user);
            YMFREE(aDispatch);
        }
    }
    
catch_return:

#ifdef YM_DISPATCH_LOG
    printf("[%s:%08lx] exiting\n", YMSTR(q->name), _YMThreadGetCurrentThreadNumber());
#endif

    YMSemaphoreSignal(q->queueExitSem);

    if ( c->qt ) YMFREE(c->qt);
    YMRelease(c->q);
    YMFREE(c);
}

YM_ENTRY_POINT(__ym_source_select_wrapper)
{
    YM_IO_BOILERPLATE

    __ym_dispatch_source_t *s = context;
#ifdef YM_SOURCE_LOG_1
    printf("%s: %ld is servicing %p(%p)\n",__FUNCTION__,s->data,s->user->dispatchProc,s->user->context); fflush(stdout);
#endif

    s->user->dispatchProc(s->user->context);

#ifdef YM_SOURCE_LOG_1
    printf("%s: %ld is no longer servicing %p(%p)\n",__FUNCTION__,s->data,s->user->dispatchProc,s->user->context);
#endif
    YMSemaphoreSignal(s->servicingSem);

    char buf[] = {'$'};
    int syncFd = YMPipeGetInputFile(gDispatch->selectSignalPipe);
    YM_WRITE_FILE(syncFd,buf,1);
    if ( result != 1 ) {
        printf("failed to alert signal loop init %d %s\n",error,errorStr);
        abort();
    }
#ifdef YM_SOURCE_LOG_1
    else
        printf("$->%d\n",syncFd);
#endif
}

YM_ENTRY_POINT(__ym_dispatch_source_select_loop)
{
    YM_IO_BOILERPLATE

#ifdef YM_SOURCE_LOG
    printf("dispatch source select loop entered\n");
#endif
    // potentially factor this out of here and dnsbrowser, make dnsbrowser handle indefinite wait
    bool keepGoing = true;
    YMFILE signalFd = YMPipeGetOutputFile(gDispatch->selectSignalPipe);
//#define source_dispatch_direct
#ifdef source_dispatch_sync
    YMSemaphoreRef signalThreadSem = YMSemaphoreCreate(0);
#endif

    char buf[] = {'#'};
    int syncFd = YMPipeGetInputFile(gDispatch->selectSignalPipe);
    YM_WRITE_FILE(syncFd,buf,1);
    if ( result != 1 ) {
        printf("failed to alert signal loop init %d %s\n",error,errorStr);
        abort();
    }
#ifdef YM_SOURCE_LOG
    else
        printf("#->%d\n",syncFd);
#endif

    int signalFlags = fcntl(signalFd, F_GETFL, 0);
    ymassert(signalFlags != -1,"%s fcntl(syncFd,F_GETFL,0): %d %d %s",__FUNCTION__,signalFlags,errno,strerror(errno));
    signalFlags = signalFlags | O_NONBLOCK;
    int rFcntl = fcntl(signalFd, F_SETFL, signalFlags);
    ymassert(rFcntl == 0,"%s fcntl(signalFd,F_SETFL,%0x): %d %d %s",__FUNCTION__,signalFlags,rFcntl,errno,strerror(errno));

    uint64_t nServiced = 0, nIterations = 0, nTimeouts = 0, nSignals = 0, nBusySignals = 0, consecutiveFailures = 0;
    while ( keepGoing ) {
		int nfds = 1;
        int maxFd = -1; // unused on win32, parameter only exists for compatibility
        fd_set readFds,writeFds;
#define debug_timeout
#ifdef debug_timeout
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 10000;
#endif

        FD_ZERO(&readFds);
        FD_ZERO(&writeFds);

        uint64_t nServicedThis = 0;

        FD_SET(signalFd, &readFds);
#ifdef YM_SOURCE_LOG_2
        printf("dispatch source select loop $%d\n",signalFd);
#endif
        YMLockLock(gDispatch->sourcesLock);
        {
            for ( int i = 0; i < YMArrayGetCount(gDispatch->sources); i++ ) {
                __ym_dispatch_source_t *source = (__ym_dispatch_source_t *)YMArrayGet(gDispatch->sources, i);
                YMFILE anFd = (YMFILE)source->data;
                bool servicing = ! YMSemaphoreTest(source->servicingSem,true);
#ifdef YM_SOURCE_LOG_2
                printf("\t%s%p[%d]%p,%p,%p,%p:",servicing?"(servicing) ":"",source,source->type,source->queue,source->user,source->user->dispatchProc,source->user->context);
#endif
                if ( servicing ) {
#ifdef YM_SOURCE_LOG_2
                    printf(" %c%d\n",source->type == ym_dispatch_source_readable?'r':'w',anFd);
#endif
                    continue;
                }

                if ( source->type == ym_dispatch_source_readable ) {
                    if ( anFd > maxFd )
                        maxFd = anFd;
                    FD_SET(anFd, &readFds);
                    nfds++;
#ifdef YM_SOURCE_LOG_2
                    printf(" r%d",anFd);
#endif
                } else if ( source->type == ym_dispatch_source_writeable ) {
                    if ( anFd > maxFd )
                        maxFd = anFd;
                    FD_SET(anFd, &writeFds);
                    nfds++;
#ifdef YM_SOURCE_LOG_2
                    printf(" w%d",anFd);
#endif
                }
#ifdef YM_SOURCE_LOG_2
                printf("\n");
#endif
            }
        }
        YMLockUnlock(gDispatch->sourcesLock);
        #warning WARNING: select() can monitor only file descriptors  numbers  that  are \
       less  than  FD_SETSIZE (1024)—an unreasonably low limit for many modern \
       applications—and this limitation will not change.  All modern  applica‐ \
       tions  should instead use poll(2) or epoll(7), which do not suffer this \
       limitation.

        result = select(maxFd + 1, &readFds, &writeFds, NULL, &tv);
#ifdef YM_SOURCE_LOG_point_5
        if ( nIterations % 1000 == 0 )
            printf(">>> source select: %zd (%lu serviced, %lu loops (%0.2f%%) %lu timeouts (%0.2f%%), %lu $ignals (%0.2f%% busy))<<<\n",result,
                nServiced,nIterations,nIterations>0?100*((double)nServiced/(double)nIterations):0.0,
                nTimeouts,nIterations>0?100*((double)nTimeouts/(double)nServiced):0.0,
                nSignals,nSignals>0?100*((double)nBusySignals/(double)nSignals):0.0);
#endif
        if (result > 0) {
            YMLockLock(gDispatch->sourcesLock);
            {
                for ( int i = 0; i < YMArrayGetCount(gDispatch->sources); i++ ) {
                    __ym_dispatch_source_t *source = (__ym_dispatch_source_t *)YMArrayGet(gDispatch->sources,i);
                    YMFILE anFd = (YMFILE)source->data;
                    if ( source->type == ym_dispatch_source_readable ) {
                        if ( FD_ISSET(anFd, &readFds) ) {
                            bool serviceable = YMSemaphoreTest(source->servicingSem, false);
#ifdef YM_SOURCE_LOG_1
                            printf(">>> %sreadable fd %d %s %p %p %p %p<<<\n",serviceable?"":"BUSY ",anFd,YMSTR(source->queue->name),source,source->user,source->user->dispatchProc,source->user->context);
#endif
//#define source_dispatch_sync
#ifndef source_dispatch_direct
                            if ( ! serviceable )
                                continue;
                            ym_dispatch_user_t *wrapper = YMALLOC(sizeof(ym_dispatch_user_t));
                            wrapper->dispatchProc = __ym_source_select_wrapper;
                            wrapper->context = source;
                            wrapper->mode = ym_dispatch_user_context_free;
                            __YMDispatchDispatch(source->queue,wrapper,
#ifdef source_dispatch_sync
                                                    signalThreadSem,
#else
                                                    NULL,
#endif
                                                    true);
#else
                            source->user->dispatchProc(source->user->context);
#endif
                            nServicedThis++;
                        }
                    } else if ( source->type == ym_dispatch_source_writeable ) {
                        if( FD_ISSET(anFd, &writeFds) ) {
                            bool serviceable = YMSemaphoreTest(source->servicingSem, false);
#ifdef YM_SOURCE_LOG_1
                            printf(">>>%swriteable fd %d %s %p %p %p %p<<<\n",serviceable?"":"BUSY ",anFd,YMSTR(source->queue->name),source,source->user,source->user->dispatchProc,source->user->context);
#endif
#ifndef source_dispatch_direct
                            if ( ! serviceable )
                                continue;

                            ym_dispatch_user_t *wrapper = YMALLOC(sizeof(ym_dispatch_user_t));
                            wrapper->dispatchProc = __ym_source_select_wrapper;
                            wrapper->context = source;
                            wrapper->mode = ym_dispatch_user_context_free;
                            __YMDispatchDispatch(source->queue,wrapper,
#ifdef source_dispatch_sync
                                                    signalThreadSem,
#else
                                                    NULL,
#endif
                                                    true);
#else
                            source->user->dispatchProc(source->user->context);
#endif
                            nServicedThis++;
                        }
                    }
                }

                if ( FD_ISSET(signalFd,&readFds) ) {
                    int i = 0;
                    char flushBuf[1];
                    while(1) {
                        YM_READ_FILE(signalFd,flushBuf,1);
                        if ( result == -1 && errno == EAGAIN )
                            break;
                        ymassert(result==1,"%s YM_READ_FILE(signalFd,,)[%d] %d %d %s",__FUNCTION__,i,result,errno,strerror(errno));

                        if ( flushBuf[0] == 'x' ) {
                            printf("select thread signaled to xit\n");
                            keepGoing = false;
                            break;
                        }

                        i++;
                        nSignals++;
                    }

#ifdef YM_SOURCE_LOG_2
                    printf(">>> %d select %cignal%s consumed <<<\n",i,flushBuf[0],i>1?"s":"");
#endif
                    if ( i > 0 && nServicedThis == 0 ) nBusySignals++;
                }

                nServiced += nServicedThis;
            }
            YMLockUnlock(gDispatch->sourcesLock);
            consecutiveFailures = 0;
        } else if (result == 0) {
            // timeout elapsed but no fd-s were signalled.
            consecutiveFailures = 0;
            nTimeouts++;
        } else {

#if defined(YMWIN32)
#error implement me
			//error = WSAGetLastError();
			//errorStr = "*";
#else
			error = errno;
			errorStr = strerror(errno);
#endif
            consecutiveFailures++;
            if ( consecutiveFailures > 2 ) {
                printf("select failed from n%d fds: %zd: %d (%s)\n",nfds,result,error,errorStr);
            }
            if ( consecutiveFailures > 10 ) {
                YMLockLock(gDispatch->sourcesLock);
                int64_t count = YMArrayGetCount(gDispatch->sources);
                _YMArrayRemoveAll(gDispatch->sources, false, false);
                printf("select thread resetting %ld -> %ld!!\n",count,YMArrayGetCount(gDispatch->sources));
                YMLockUnlock(gDispatch->sourcesLock);
                //keepGoing = false;
            }
        }
        nIterations++;
    }

    printf("dispatch select exiting: %lu serviced, %lu loops (%0.2f%%) %lu timeouts (%0.2f%%), %lu $ignals (%0.2f%% busy)\n",
            nServiced,nIterations,nIterations>0?100*((double)nServiced/(double)nIterations):0.0,
            nTimeouts,nIterations>0?100*((double)nTimeouts/(double)nServiced):0.0,
            nSignals,nSignals>0?100*((double)nBusySignals/(double)nSignals):0.0);
    fflush(stdout);
}

void YMDispatchMain(void)
{
    YM_ONCE_DO(gDispatchGlobalInitOnce, __YMDispatchInitOnce);
    __ym_dispatch_service_loop_context_t *c = YMALLOC(sizeof(__ym_dispatch_service_loop_context_t));
    __ym_dispatch_queue_thread_t *qt = YMALLOC(sizeof(__ym_dispatch_queue_thread_t));
    qt->thread = NULL;
    qt->busy = false;
    c->q = (__ym_dispatch_queue_t *)YMRetain(gDispatch->mainQueue);
    c->qt = qt;
    __ym_dispatch_main_service_loop(c);
    printf("YMDispatchMain will return\n");
    abort();
}
