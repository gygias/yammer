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

#include "YMDispatchPriv.h"

#ifndef YMWIN32
# define myexit _Exit
#else
# define myexit exit
#endif

YM_ENTRY_POINT(_ym_dispatch_main_test_proc);
YM_ENTRY_POINT(_ym_dispatch_denial_of_service_test_proc);

void usage() { ymlog("usage: ym-dispatch-main-test [m|g[r]|u|dos] [#nReps]"); exit(1); }

static int gReps = 0, gIter = 0, gCompleted = 0;
static bool gRacey = false;
static int gType;
static YMLockRef gLock = NULL;
static YMDispatchQueueRef gUserQueue;

int main(int argc, char *argv[])
{
    if ( argc < 2 || argc > 3 )
        usage();
    if ( strcmp(argv[1],"m") == 0 )
        gType = 0;
    else if ( strcmp(argv[1],"g") == 0 || ( gRacey = ( strcmp(argv[1],"gr") == 0 ) ) )
        gType = 1;
    else if ( strcmp(argv[1],"u") == 0 )
        gType = 2;
    else if ( strcmp(argv[1],"dos") == 0 )
        gType = 4;
    else
        usage();
    
    if ( gType != 4 ) {
        gReps = atoi(argv[2]);

        if ( gReps <= 0 )
            usage();
    }
    
    if ( gType != 4 ) {
        ymlog("ym-dispatch-test targeting %s with %d %sreps",(gType==0)?"main":(gType==1)?"global":"user",gReps,gRacey?"*RACEY* ":"");

        ym_dispatch_user_t dispatch = { _ym_dispatch_main_test_proc, NULL, false, ym_dispatch_user_context_noop };
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
    } else {
        ymlog("ym-dispatch-test running denial-of-service test");

        ym_dispatch_user_t dispatch = { _ym_dispatch_denial_of_service_test_proc, NULL, false, ym_dispatch_user_context_noop };
        YMDispatchAsync(YMDispatchGetMainQueue(), &dispatch);
    }
    
    YMDispatchMain();

    ymassert(false,"main() regained control");
    return 1;
}

YM_ENTRY_POINT(_do_a_work)
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

YM_ENTRY_POINT(valgrind_take_me_away)
{
    free(context);
}

void * valgrind_hit_me()
{
    size_t relativelyBig = 1024;
    return malloc(relativelyBig);
}

YM_ENTRY_POINT(_ym_dispatch_main_test_proc)
{
    YMDispatchQueueRef queue = NULL;
    if ( gType == 0 )
        queue = YMDispatchGetMainQueue();
    else if ( gType == 1 )
        queue = YMDispatchGetGlobalQueue();
    else
        queue = gUserQueue;

    for( gIter = 0; gIter < gReps; gIter++ ) {
        ym_dispatch_user_t aDispatch = { _do_a_work, NULL, false, ym_dispatch_user_context_noop };
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
}

YM_ENTRY_POINT(_ym_dispatch_denial_of_service)
{
    ymlog("sorry, basement flooded again");
    sleep(99999999);
}

YM_ENTRY_POINT(_ym_dispatch_denial_of_service_check)
{
    ymlog("i will fix your bugs!");
    myexit(0);
}

YM_ENTRY_POINT(_ym_dispatch_denial_of_service_finally)
{
    ymlog("rush limbaugh");
    sleep(5);
    myexit(1);
}

YM_ENTRY_POINT(_ym_dispatch_denial_of_service_test_proc)
{
    int threadsPerGlobalQueue = _YMDispatchMaxQueueThreads();
    int idx = threadsPerGlobalQueue;
    for ( idx = threadsPerGlobalQueue; idx; idx-- ) {
        ym_dispatch_user_t user = { _ym_dispatch_denial_of_service, NULL, NULL, ym_dispatch_user_context_noop };
        YMDispatchAsync(YMDispatchGetGlobalQueue(),&user);
    }

    ym_dispatch_user_t check = { _ym_dispatch_denial_of_service_check, NULL, NULL, ym_dispatch_user_context_noop };
    YMDispatchAsync(YMDispatchGetGlobalQueue(),&check);

    ym_dispatch_user_t finally = { _ym_dispatch_denial_of_service_finally, NULL, NULL, ym_dispatch_user_context_noop };
    YMDispatchAfter(YMDispatchGetMainQueue(),&finally, 5);
}
