#include "YMDispatch.h"
#include "YMThreadPriv.h"
#include "YMDictionary.h"
#include "YMLock.h"
#include "YMSemaphore.h"
#include "YMArray.h"
#include "YMPipe.h"
#include "YMUtilities.h"

#define ymlog_type YMLogDispatch
#include "YMLog.h"

#include <time.h>
#include <math.h>
#include <signal.h>
#if defined(YMLINUX)
# include <sys/select.h>
#else
# error implement me
#endif

//#define YM_DISPATCH_LOG
//#define YM_DISPATCH_LOG_1
//#define YM_SOURCE_LOG
//#define YM_SOURCE_LOG_1
//#define YM_SOURCE_LOG_2

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
    YMArrayRef timers; // excluding next
    __ym_dispatch_timer_t *nextTimer;

    // sources
    YMArrayRef sources; // __ym_dispatch_source_t
    YMLockRef sourcesLock;
    YMThreadRef selectThread;
    YMPipeRef selectSignalPipe;
    YMSemaphoreRef selectSignalSemaphore;
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

YM_ENTRY_POINT(__ym_dispatch_main_service_loop);
YM_ENTRY_POINT(__ym_dispatch_global_service_loop);
YM_ENTRY_POINT(__ym_dispatch_user_service_loop);
YM_ENTRY_POINT(__ym_dispatch_service_loop);

__ym_dispatch_queue_t *__YMDispatchQueueInitCommon(YMStringRef name, YMDispatchQueueType type, ym_entry_point entry);
void __YMDispatchQueueStop(__ym_dispatch_queue_t *q);

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
	gDispatch->userQueues = YMArrayCreate();
    gDispatch->lock = YMLockCreate(); // not a ymtype

    gDispatch->nextTimer = NULL;
    gDispatch->timers = YMArrayCreate();
    signal(SIGALRM, __ym_dispatch_sigalarm);

    // sources
    gDispatch->sources = NULL;
    gDispatch->sourcesLock = NULL;
    gDispatch->selectThread = NULL;
    gDispatch->selectSignalPipe = NULL;
    gDispatch->selectSignalSemaphore = NULL;
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
    //printf("%s %p\n",__FUNCTION__,p_);
    __ym_dispatch_queue_t *q = (__ym_dispatch_queue_t *)p_;
    if ( q->type == YMDispatchQueueGlobal || q->type == YMDispatchQueueMain )
        __YM_DISPATCH_CATCH_MISUSE(q);

    __YMDispatchQueueStop(q);
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
        __ym_dispatch_service_loop_context_t *c = YMALLOC(sizeof(__ym_dispatch_service_loop_context_t));
        YMThreadRef first = YMThreadCreate(name, entry, c);
        c->q = (__ym_dispatch_queue_t *)YMRetain(q); // matched on thread return
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

    __ym_dispatch_queue_t *q = __YMDispatchQueueInitCommon(name, YMDispatchQueueUser, __ym_dispatch_user_service_loop);

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
    gDispatch->selectSignalSemaphore = YMSemaphoreCreate(0);
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
        char signal[] = { '$' };
        YMFILE signalFile = YMPipeGetInputFile(gDispatch->selectSignalPipe);
        YM_WRITE_FILE(signalFile,signal,1);
        if ( result != 1 ) {
            printf("failed to $ignal $elect loop %d %d %s\n",signalFile,error,errorStr);
            abort();
        }

        YMSemaphoreSignal(gDispatch->selectSignalSemaphore);
#ifdef YM_SOURCE_LOG_1
        printf("$ignaled $elect loop '$' %d for %p\n",signalFile,source);
#endif
    }

    return source;
}

void __YMDispatchQueueStop(__ym_dispatch_queue_t *q)
{

    YMSelfLock(q);
    uint8_t nThreads = YMArrayGetCount(q->queueThreads);
    bool willExit = ! q->exit;
    YMSelfUnlock(q);

    if ( willExit ) {
        q->exit = true;
        while ( nThreads-- ) {
            YMSemaphoreSignal(q->queueSem);
            YMSemaphoreWait(q->queueExitSem);
        }
    }
}

void YMAPI YMDispatchSourceDestroy(ym_dispatch_source_t source)
{
    YM_ONCE_DO(gDispatchGlobalInitSelect, __YMDispatchSelectOnce);

    __ym_dispatch_source_t *s = source;

#warning watchlist recycle user threads
    if ( s->queue->type == YMDispatchQueueUser ) {
        __YMDispatchQueueStop(s->queue);
        YMSemaphoreWait(s->servicingSem);
    }

    __YMDispatchUserFinalize(s->user);

    YMLockLock(gDispatch->sourcesLock);
    int64_t count = YMArrayGetCount(gDispatch->sources);
    const void *item = NULL;
    for ( int i = 0; i < count; i++ ) {
        const void *anItem = YMArrayGet(gDispatch->sources,i);
        if ( anItem == s ) {
#ifdef YM_SOURCE_LOG_1
            printf("removed source with index %d[%ld]\n",i,count);
#endif
            item = anItem;
            YMArrayRemove(gDispatch->sources,i);
            break;
        }
    }
    YMLockUnlock(gDispatch->sourcesLock);

    ymassert(item,"source %p not in list[%ld]",source,count);

    if ( s->queue->type == YMDispatchQueueUser )
        YMRelease(s->servicingSem);
        //YMRelease(s->queue);

    YMFREE(s->user);
    YMFREE(s);
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

#ifdef YM_DISPATCH_LOG_1
    printf("scheduled %p[%p,%p] after %f seconds on %p(%s)  \n",userDispatch,userDispatch->dispatchProc,userDispatch->context,secondsAfter,queue,YMSTR(((__ym_dispatch_queue_t *)queue)->name));
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
    } else if ( ! gDispatch->nextTimer ) {
        printf("__ym_dispatch_sigalarm: next timer not set %ld\n",YMArrayGetCount(gDispatch->timers));
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

#ifdef YM_DISPATCH_LOG_1
    printf("dispatching %p[%p,%p] on %p(%s)\n",thisTimer->dispatch,thisTimer->dispatch->dispatchProc,thisTimer->dispatch->context,thisTimer->queue,YMSTR((((__ym_dispatch_queue_t *)(thisTimer->queue))->name)));
#endif
    YMDispatchAsync(thisTimer->queue,thisTimer->dispatch);

    YMFREE(thisTimer->time);
    YMRelease(thisTimer->queue);
    YMFREE(thisTimer->dispatch);
    YMFREE(thisTimer);

#ifdef YM_DISPATCH_LOG_1
    if ( nextTimer ) {
        struct timespec now;
        int ret = clock_gettime(CLOCK_REALTIME,&now);
        if ( ret != 0 ) {
            printf("clock_gettime failed: %d %s",errno,strerror(errno));
            return;
        }
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
    __ym_dispatch_source_t *s = context;
#ifdef YM_SOURCE_LOG_1
    printf("%s: %d is servicing\n",__FUNCTION__,s->data);
#endif

    s->user->dispatchProc(s->user->context);

#ifdef YM_SOURCE_LOG_1
    printf("%s: %d is no longer servicing\n",__FUNCTION__,s->data);
#endif
    YMSemaphoreSignal(s->servicingSem);
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

    uint64_t idx = 0;
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

        //FD_SET(signalFd, &readFds);
#ifdef YM_SOURCE_LOG_2
        printf("dispatch source select loop $%d\n",signalFd);
#endif
        //YMArrayRef selectSources = YMArrayCreate();
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
                    //YMArrayAdd(selectSources,source);
#ifdef YM_SOURCE_LOG_2
                    printf(" r%d",anFd);
#endif
                } else if ( source->type == ym_dispatch_source_writeable ) {
                    if ( anFd > maxFd )
                        maxFd = anFd;
                    FD_SET(anFd, &writeFds);
                    nfds++;
#ifdef YM_SOURCE_LOG_2
                    //YMArrayAdd(selectSources,source);
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

        int result = select(maxFd + 1, &readFds, &writeFds, NULL,
#ifdef debug_timeout
//#define debug_timeout_once
#ifdef debug_timeout_once
                                     (idx > 0) ? NULL : &tv
#else
                                                    &tv
#endif
#else
                            NULL
#endif
                                );
#ifdef YM_SOURCE_LOG_1
        printf(">>>source select: %d<<<\n",result);
#endif
        if (result > 0) {
            YMLockLock(gDispatch->sourcesLock);
            {
                if ( FD_ISSET(signalFd,&readFds) ) {
                    int i = 0;
                    while ( YMSemaphoreTest(gDispatch->selectSignalSemaphore, false) ) {
                        char buf[1];
                        YM_READ_FILE(signalFd,buf,1);
                        i++;
                    }
#ifdef YM_SOURCE_LOG_1
                    printf(">>> %d select loop %cignal%s consumed <<<\n",i,buf,i>1?"s":"");
#endif
                }

                for ( int i = 0; i < YMArrayGetCount(gDispatch->sources); i++ ) {
                    __ym_dispatch_source_t *source = (__ym_dispatch_source_t *)YMArrayGet(gDispatch->sources,i);
                    YMFILE anFd = (YMFILE)source->data;
                    if ( source->type == ym_dispatch_source_readable ) {
                        if ( FD_ISSET(anFd, &readFds) ) {
                            bool serviceable = YMSemaphoreTest(source->servicingSem, false);
#ifdef YM_SOURCE_LOG_1
                            printf(">>>%sreadable fd %d %s %p %p %p %p<<<\n",serviceable?"":"BUSY",anFd,YMSTR(source->queue->name),source,source->user,source->user->dispatchProc,source->user->context);
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
                        }
                    }
                }
            }
            YMLockUnlock(gDispatch->sourcesLock);
        } else if (result == 0) {
            // timeout elapsed but no fd-s were signalled.
        } else {

            #warning add YM_SELECT
            int error;
            char *errorStr;
#if defined(YMWIN32)
#error implement me
			//error = WSAGetLastError();
			//errorStr = "*";
#else
			error = errno;
			errorStr = strerror(errno);
#endif
            printf("select failed from n%d fds: %d: %d (%s)\n",nfds,result,error,errorStr);
            //keepGoing = false;
        }
        idx++;
    }
#ifdef YM_SOURCE_LOG
    printf("dispatch source select loop exiting\n");
#endif
}

void YMDispatchMain()
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
