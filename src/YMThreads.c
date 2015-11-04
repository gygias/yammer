//
//  YMThreads.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMThreads.h"
#include "YMPrivate.h"

#include <pthread.h>

typedef struct __YMThread
{
    YMTypeID _typeID;
    
    ym_thread_entry entryPoint;
    void *context;
    pthread_t pthread;
} _YMThread;

void _YMThreadFree(YMTypeRef object)
{
    _YMThread *thread = (_YMThread *)object;
    // todo
    free(thread);
}

YMThreadRef YMThreadCreate(ym_thread_entry entryPoint, void *context)
{
    _YMThread *thread = (_YMThread *)calloc(1,sizeof(_YMThread));
    thread->_typeID = _YMThreadTypeID;
    
    thread->entryPoint = entryPoint;
    thread->context = context;
    thread->pthread = NULL;
    return (YMThreadRef)thread;
}

bool YMThreadStart(YMThreadRef thread)
{
    _YMThread *_thread = (_YMThread *)thread;
    pthread_t pthread;
    int result;
    if ( ( result = pthread_create(&pthread, NULL, _thread->entryPoint, _thread->context) ) )
    {
        YMLog("pthread_create failed: %d %s", result, strerror(result));
        return false;
    }
    
    _thread->pthread = pthread;
    return true;
}

bool YMThreadJoin(YMThreadRef thread)
{
    _YMThread *_thread = (_YMThread *)thread;
    int result;
    if ( ( result = pthread_join(_thread->pthread, NULL) ) )
    {
        YMLog("pthread_join failed: %d %s", result, strerror(result));
        return false;
    }
    
    return true;
}
