//
//  YMUtilities.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMUtilities.h"

#include "YMLock.h"

#include "YMLog.h"
#undef ymlog_type
#define ymlog_type YMLogIO // this file isn't very clearly purposed
#if ( ymlog_type > ymlog_target )
#undef ymlog
#define ymlog(x,...) ;
#endif

#include <netinet/in.h>

void YMGetTheBeginningOfPosixTimeForCurrentPlatform(struct timeval *time)
{
    time->tv_sec = MIN_OF(typeof(time->tv_sec));
    time->tv_usec = MIN_OF(typeof(time->tv_usec));
}

void YMGetTheEndOfPosixTimeForCurrentPlatform(struct timeval *time)
{
    time->tv_sec = MAX_OF(typeof(time->tv_sec));
    time->tv_usec = MAX_OF(typeof(time->tv_usec));
}

ComparisonResult YMTimevalCompare(struct timeval *a, struct timeval *b)
{
    if ( a->tv_sec < b->tv_sec )
        return LessThan;
    else if ( a->tv_sec > b->tv_sec )
        return GreaterThan;
    else
    {
        if ( a->tv_usec < b->tv_usec )
            return LessThan;
        else if ( a->tv_usec > b->tv_usec )
            return GreaterThan;
    }
    
    return EqualTo;
}

YMIOResult YMReadFull(int fd, uint8_t *buffer, size_t bytes, size_t *outRead)
{
    YMIOResult result = YMIOSuccess;
    
    size_t off = 0;
    while ( off < bytes )
    {
        ssize_t aRead = read(fd, (void *)buffer + off, bytes - off);
        if ( aRead == 0 )
        {
            result = YMIOEOF;
            break;
        }
        else if ( aRead == -1 )
        {
            result = YMIOError;
            break;
        }
        off += aRead;
    }
    if ( outRead )
        *outRead = off;
    return result;
}

YMIOResult YMWriteFull(int fd, const uint8_t *buffer, size_t bytes, size_t *outWritten)
{
    YMIOResult result = YMIOSuccess;
    ssize_t aWrite;
    size_t off = 0;
    while ( off < bytes )
    {
        aWrite = write(fd, buffer + off, bytes - off);
        switch(aWrite)
        {
            case 0:
                ymerr("YMWrite: aWrite=0?");
                abort();
                break;
            case -1:
                result = YMIOError;
                break;
            default:
                break;
        }
        off += aWrite;
    }
    if ( outWritten )
        *outWritten = off;
    return result;
}

int32_t YMPortReserve(bool ipv4, int *outSocket)
{
    uint16_t aPort = IPPORT_RESERVED;
    while (aPort < IPPORT_HILASTAUTO)
    {
        uint16_t tryPort = aPort++;
        int aSocket = -1;
        
        int domain = ipv4 ? PF_INET : PF_INET6;
        int aResult = socket(domain, SOCK_STREAM, 6);
        if ( aResult < 0 )
            goto catch_continue;
        
        aSocket = aResult;
        
        int yes = 1;
        aResult = setsockopt(aSocket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        if ( aResult != 0 )
            goto catch_continue;
        
        uint8_t length = ipv4 ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);
        struct sockaddr *addr = YMALLOC(length);
        addr->sa_family = ipv4 ? AF_INET : AF_INET6;
        addr->sa_len = length;
        if ( ipv4 )
        {
            ((struct sockaddr_in *)addr)->sin_addr.s_addr = INADDR_ANY;
            ((struct sockaddr_in *)addr)->sin_port = tryPort;
        }
        else
        {
            ((struct sockaddr_in6 *)addr)->sin6_addr = in6addr_any;
            ((struct sockaddr_in6 *)addr)->sin6_port = tryPort;
        }
        
        aResult = bind(aSocket, addr, length);
        if ( aResult != 0 )
            goto catch_continue;
        
        *outSocket = aSocket;
        return tryPort;
        
    catch_continue:
        if ( aSocket > 0 )
            close(aSocket);
    }
    
    return -1;
}

ymbool YMCreateMutexWithOptions(YMLockOptions options, pthread_mutex_t *outMutex)
{
    pthread_mutex_t mutex;
    pthread_mutexattr_t attributes;
    pthread_mutexattr_t *attributesPtr = NULL;
    int result;
    
    if ( options & YMLockRecursive )
    {
        attributesPtr = &attributes;
        result = pthread_mutexattr_init(attributesPtr);
        if ( result != 0 )
        {
            ymerr("pthread_mutexattr_init failed: %d (%s)", result, strerror(result));
            return false;
        }
        result = pthread_mutexattr_settype(attributesPtr, PTHREAD_MUTEX_RECURSIVE);
        if ( result != 0 )
        {
            ymerr("pthread_mutexattr_settype failed: %d (%s)", result, strerror(result));
            goto catch_release;
        }
    }
    
    result = pthread_mutex_init(&mutex, attributesPtr);
    if ( result != 0 )
    {
        ymerr("pthread_mutex_init failed: %d (%s)", result, strerror(result));
        goto catch_release;
    }
    
    *outMutex = mutex;
    
catch_release:
    if ( attributesPtr )
        pthread_mutexattr_destroy(attributesPtr);
    return ( result == 0 );
}

ymbool YMLockMutex(pthread_mutex_t mutex)
{
    int result = pthread_mutex_lock(&mutex);
    ymbool okay = true;
    switch(result)
    {
        case 0:
            break;
        case EDEADLK:
            ymerr("mutex: error: %p EDEADLK", &mutex);
            okay = false;
            break;
        case EINVAL:
            ymerr("mutex: error: %p EINVAL", &mutex);
            okay = false;
            break;
        default:
            ymerr("mutex: error: %p unknown error", &mutex);
            break;
    }
    
    return okay;
}

ymbool YMUnlockMutex(pthread_mutex_t mutex)
{
    int result = pthread_mutex_unlock(&mutex);
    ymbool okay = true;
    switch(result)
    {
        case 0:
            break;
        case EPERM:
            ymerr("mutex: error: unlocking thread doesn't hold %p", &mutex);
            okay = false;
            break;
        case EINVAL:
            ymerr("mutex: error: unlock EINVAL %p", &mutex);
            okay = false;
            break;
        default:
            ymerr("mutex: error: unknown %p", &mutex);
            break;
    }
    
    return okay;
}

ymbool YMDestroyMutex(pthread_mutex_t mutex)
{
    int result = pthread_mutex_destroy(&mutex);
    return ( result == 0 );
}
