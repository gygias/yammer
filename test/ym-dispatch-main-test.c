//
//  main.c
//  yammer
//
//  Created by david on 12/9/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMLog.h"
#include "YMDispatch.h"
#include "YMLock.h"

#ifndef YMWIN32
# define myexit _Exit
#else
# define myexit exit
#endif

YM_THREAD_RETURN YM_CALLING_CONVENTION _ym_dispatch_main_test_proc(YM_THREAD_PARAM ctx);

void usage() { ymlog("usage: ym-dispatch-main-test [m|g[r]|u] (nReps)"); exit(1); }

static int gReps = 0, gIter = 0, gCompleted = 0;
static bool gRacey = false;
static int gType;
static YMLockRef gLock = NULL;
static YMDispatchQueueRef gUserQueue;

int main(int argc, char *argv[])
{
    if ( argc != 3 )
        usage();
    if ( strcmp(argv[1],"m") == 0 )
        gType = 0;
    else if ( strcmp(argv[1],"g") == 0 || ( gRacey = ( strcmp(argv[1],"gr") == 0 ) ) )
        gType = 1;
    else if ( strcmp(argv[1],"u") == 0 )
        gType = 2;
    else
        usage();
    gReps = atoi(argv[2]);
    if ( gReps <= 0 )
        usage();
    
    ymlog("ym-dispatch-main-test targeting %s with %d %sreps",(gType==0)?"main":(gType==1)?"global":"user",gReps,gRacey?"*RACEY* ":"");
    
    ym_dispatch_user_t dispatch = { (void (*)(void *))_ym_dispatch_main_test_proc, NULL, false, ym_dispatch_user_context_noop };
    if ( gType == 0 ) {
        YMDispatchAsync(YMDispatchGetGlobalQueue(),&dispatch);
    } else if ( gType == 1 ) {
        if ( ! gRacey )
            gLock = YMLockCreate();
        YMDispatchAsync(YMDispatchGetMainQueue(), &dispatch);
    } else {
        gUserQueue = YMDispatchQueueCreate(YMSTRC("ym-dispatch-main-test-queue"));
        YMDispatchAsync(YMDispatchGetGlobalQueue(), &dispatch);
    }
    
    YMDispatchMain();

    ymassert(false,"main() regained control");
    return 1;
}

void YM_CALLING_CONVENTION _ym_do_a_work(__unused YM_THREAD_PARAM context)
{
    bool lock = gLock != NULL;
    if ( lock ) YMLockLock(gLock);
    gCompleted++;
    if ( lock ) YMLockUnlock(gLock);
    
    if ( gCompleted == gReps ) {
        ymlog("ym-dispatch-main-test completed %d reps",gReps);
        myexit(0);
    }
}

void valgrind_take_me_away(void *ptr)
{
    free(ptr);
}

void * valgrind_hit_me()
{
    size_t relativelyBig = 1024;
    return malloc(relativelyBig);
}

YM_THREAD_RETURN YM_CALLING_CONVENTION _ym_dispatch_main_test_proc(__unused YM_THREAD_PARAM ctx)
{
    YMDispatchQueueRef queue = NULL;
    if ( gType == 0 )
        queue = YMDispatchGetMainQueue();
    else if ( gType == 1 )
        queue = YMDispatchGetGlobalQueue();
    else
        queue = gUserQueue;

    for( gIter = 0; gIter < gReps; gIter++ ) {
        ym_dispatch_user_t aDispatch = { _ym_do_a_work, NULL, false, ym_dispatch_user_context_noop };
        switch(arc4random_uniform(3)) {
            case 0:
                YMDispatchAsync(queue, &aDispatch);
                break;
            case 1:
                aDispatch.context = valgrind_hit_me();
                aDispatch.onCompleteProc = valgrind_take_me_away;
                YMDispatchAsync(queue, &aDispatch);
                break;
            default:
                aDispatch.context = valgrind_hit_me();
                aDispatch.mode = ym_dispatch_user_context_free;
                YMDispatchAsync(queue, &aDispatch);
                break;
        }
    }

    if ( gType == 1 && gRacey ) {
        sleep(10); // could perhaps check % cpu
        printf("ym-dispatch-main-test assumes races have finished at (%d / %d)\n",gCompleted,gReps);
        myexit(1);
    }
    
    YM_THREAD_END
}
