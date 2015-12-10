//
//  main.c
//  yammer
//
//  Created by david on 12/9/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMThreadPriv.h"

#include "YMLog.h"

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
    
    ymlog("dispatch-main-test running on %s with %d reps",main?"main":"thread",gReps);
    
    if ( main )
    {
        YMThreadStart(YMThreadCreate(YMSTRC("dispatch-main-test-thread"), _ym_dispatch_main_test_proc, NULL));
        YMThreadDispatchSetGlobalMode(true);
        YMThreadDispatchMain();
    }
    else
    {
        _ym_dispatch_main_test_proc(NULL);
    }
    
    ymassert(false,"main() regained control");
    
    return 1;
}

void YM_CALLING_CONVENTION _ym_thread_dispatch_func(__unused ym_thread_dispatch_ref dispatch)
{
    bool mainThread = _YMThreadGetCurrentThreadNumber() ==
#if !defined(YMWIN32)
        (uint64_t)pthread_self();
#else
        (uint64_t)GetCurrentThread();
#endif
    
    ymassert(mainThread, "dispatch not on main thread");
    
    gCompleted++;
    
    if ( gCompleted == gReps )
    {
        ymlog("dispatch-main-test completed %d reps",gReps);
        exit(0);
    }
}

YM_THREAD_RETURN YM_CALLING_CONVENTION _ym_dispatch_main_test_proc(__unused YM_THREAD_PARAM ctx)
{
    for( int i = 0; i < gReps; i++ )
    {
        YMThreadRef globalQueue = YMThreadDispatchGetGlobal();
        struct ym_thread_dispatch_t aDispatch = { _ym_thread_dispatch_func, NULL, true, NULL, NULL };
        YMThreadDispatchDispatch(globalQueue, aDispatch);
    }
    
    sleep(999999);
	ymassert(false,"sleep returned");
    
    YM_THREAD_END
}
