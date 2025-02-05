//
//  main.c
//  yammer
//
//  Created by david on 12/9/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMLog.h"
#include "YMThreadPriv.h"

#ifndef YMWIN32
# define myexit _Exit
#else
# define myexit exit
#endif

YM_THREAD_RETURN YM_CALLING_CONVENTION _ym_dispatch_main_test_proc(YM_THREAD_PARAM ctx);

void usage() { ymlog("usage: ym-dispatch-main-test [m|t] (nReps)"); exit(1); }

static int gReps = 0, gCompleted = 0;

int main(int argc, char *argv[])
{
    bool main = false,
        thread = false;
    if ( argc != 3 )
        usage();
    if ( strcmp(argv[1],"m") == 0 )
        main = true;
    else if ( strcmp(argv[1],"t") == 0 )
        thread = true;
    else
        usage();
    gReps = atoi(argv[2]);
    if ( gReps <= 0 )
        usage();
    
    ymlog("ym-dispatch-main-test running on %s with %d reps",main?"main":"thread",gReps);
    
    if ( main ) {
        YMThreadStart(YMThreadCreate(YMSTRC("dispatch-main-test-thread"), _ym_dispatch_main_test_proc, NULL));
        YMThreadDispatchSetGlobalMode(true);
        YMThreadDispatchMain();
    } else {
        _ym_dispatch_main_test_proc(NULL);
    }
    
    ymassert(false,"main() regained control");
    
    return 1;
}

void YM_CALLING_CONVENTION _ym_thread_dispatch_func(__unused YM_THREAD_PARAM context)
{
    bool mainThread = _YMThreadGetCurrentThreadNumber() ==
#if !defined(YMWIN32)
        (uint64_t)pthread_self();
#else
        (uint64_t)GetCurrentThread();
#endif
    
    ymassert(mainThread, "dispatch not on main thread");
    
    gCompleted++;
    
    if ( gCompleted == gReps ) {
        ymlog("dispatch-main-test completed %d reps",gReps);
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
    for( int i = 0; i < gReps; i++ ) {
        YMThreadRef globalQueue = YMThreadDispatchGetGlobal();
        
        ym_thread_dispatch_user_t aDispatch = { _ym_thread_dispatch_func, NULL, false, NULL, NULL };
        switch(arc4random_uniform(3)) {
            case 0:
                YMThreadDispatchDispatch(globalQueue, aDispatch);
                break;
            case 1:
                aDispatch.context = valgrind_hit_me();
                aDispatch.deallocProc = valgrind_take_me_away;
                YMThreadDispatchDispatch(globalQueue, aDispatch);
                break;
            default:
                aDispatch.context = valgrind_hit_me();
                aDispatch.freeContextWhenDone = true;
                YMThreadDispatchDispatch(globalQueue, aDispatch);
            break;
        }
    }
    
    sleep(999999);
	ymassert(false,"sleep returned");
    
    YM_THREAD_END
}
