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

#ifndef WIN32
#define YM_PORT_MAX IPPORT_HILASTAUTO
#include <netinet/in.h>
#include <sys/time.h>
#else
#define YM_PORT_MAX IPPORT_DYNAMIC_MAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <time.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

const char *YMGetCurrentTimeString(char *buf, size_t bufLen)
{
    struct timeval epoch = {0,0};
    gettimeofday(&epoch, NULL);
    struct tm *now = localtime((const time_t *)&epoch.tv_sec);
    if ( ! now )
        return NULL;
    size_t result = strftime(buf, bufLen, "%Y-%m-%d %H:%M:%S", now);
    if ( result == 0 )
        return NULL;
    if ( result < bufLen - 3 )
        snprintf(buf, bufLen - result, "%s.%03d",buf,epoch.tv_usec%1000);
    return buf;
}

void YMGetTheBeginningOfPosixTimeForCurrentPlatform(struct timeval *time)
{
    // todo i'm not sure what 'extension used' or have any idea how this macro works
	YM_WPPUSH
	time->tv_sec = 0;
    time->tv_usec = 0;
	YM_WPOP
}
    
void YMGetTheEndOfPosixTimeForCurrentPlatform(struct timeval *time)
{
	YM_WPPUSH
    
#ifndef WIN32
    time->tv_sec = MAX_OF(typeof(time->tv_sec));
	time->tv_usec = MAX_OF(typeof(time->tv_usec));
#else
    time->tv_sec = LONG_MAX;
    time->tv_usec = LONG_MAX;
#endif
    
	YM_WPOP
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

YMIOResult YMReadFull(YMFILE fd, uint8_t *buffer, size_t bytes, size_t *outRead)
{
    if ( buffer == NULL || bytes == 0 || fd < 0 )
        return YMIOError; // is this success or failure? :)
    
    YMIOResult result = YMIOSuccess;
    
    size_t off = 0;
    while ( off < bytes )
    {
		ssize_t aRead;
#ifndef WIN32
        aRead = read(fd, buffer + off, bytes - off);
#else
		BOOL okay = ReadFile(fd, buffer + off, bytes - off, &aRead, NULL);
		if ( ! okay )
			aRead = -1;
#endif
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

YMIOResult YMWriteFull(YMFILE fd, const uint8_t *buffer, size_t bytes, size_t *outWritten)
{
    if ( buffer == NULL || bytes == 0 || fd < 0 )
        return YMIOError;
    
    YMIOResult result = YMIOSuccess;
    ssize_t aWrite;
    size_t off = 0;
    while ( off < bytes )
    {
#ifndef WIN32
        aWrite = write(fd, buffer + off, bytes - off);
#else
		BOOL okay = WriteFile(fd, buffer+ off, bytes - off, &aWrite, NULL);
		if ( ! okay ) aWrite = -1;
#endif
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
    
#ifdef WIN32
YM_ONCE_FUNC(__YMNetworkingInit,
{
	WSADATA wsa;
	int result = WSAStartup(MAKEWORD(2,2),&wsa);
	if (result != 0)
	{
		ymerr("fatal: WSAStartup failed: %x %x",result,GetLastError());
		exit(1);
	}
})
#endif

void YMNetworkingInit()
{
#ifdef WIN32
	YM_ONCE_DO_LOCAL(__YMNetworkingInit);
#endif
}

int32_t YMPortReserve(bool ipv4, int *outSocket)
{
    bool okay = false;
    uint16_t aPort = IPPORT_RESERVED;
    uint16_t thePort = aPort;
    int aSocket = -1;
    
    uint8_t length = ipv4 ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);
    struct sockaddr *addr = YMALLOC(length);
    addr->sa_family = ipv4 ? AF_INET : AF_INET6;
#if defined(_MACOS)
    addr->sa_len = length;
#endif
    if ( ipv4 )
        ((struct sockaddr_in *)addr)->sin_addr.s_addr = INADDR_ANY;
    else
        ((struct sockaddr_in6 *)addr)->sin6_addr = in6addr_any;
    
    while (aPort < YM_PORT_MAX)
    {
        thePort = aPort++;
        
        int domain = ipv4 ? PF_INET : PF_INET6;
        int aResult = socket(domain, SOCK_STREAM, IPPROTO_TCP);
        if ( aResult < 0 )
            goto catch_continue;
        
        aSocket = aResult;
        
        int yes = 1;
        aResult = setsockopt(aSocket, SOL_SOCKET, SO_REUSEADDR, (const void *)&yes, sizeof(yes));
        if ( aResult != 0 )
            goto catch_continue;
        
        if ( ipv4 )
            ((struct sockaddr_in *)addr)->sin_port = thePort;
        
        else
            ((struct sockaddr_in6 *)addr)->sin6_port = thePort;
        
        aResult = bind(aSocket, addr, length);
        if ( aResult != 0 )
            goto catch_continue;
        
        *outSocket = aSocket;
        okay = true;
        break;
        
    catch_continue:
        if ( aSocket > 0 )
		{
			int result, error; char *errorStr;
            YM_CLOSE_SOCKET(aSocket);
		}
    }
    
    free(addr);
    return okay ? (uint32_t)thePort : -1;
}

#ifndef WIN32
pthread_mutex_t *YMCreateMutexWithOptions(YMLockOptions options)
{
    pthread_mutex_t *outMutex = NULL;
    pthread_mutex_t mutex;
    pthread_mutexattr_t attributes;
    pthread_mutexattr_t *attributesPtr = &attributes;
    
    int result = pthread_mutexattr_init(attributesPtr);
    if ( result != 0 )
    {
        fprintf(stdout,"pthread_mutexattr_init failed: %d (%s)\n", result, strerror(result));
        goto catch_release;
    }
    
    if ( options )
    {
        int optionsList[4] = { YMLockRecursive, PTHREAD_MUTEX_RECURSIVE, YMLockErrorCheck, PTHREAD_MUTEX_ERRORCHECK };
        for(uint8_t i = 0; i < 4; i+=2 )
        {
            if ( options & optionsList[i] )
            {
                result = pthread_mutexattr_settype(attributesPtr, optionsList[i+1]);
                if ( result != 0 )
                {
                    fprintf(stdout,"pthread_mutexattr_settype failed: %d (%s)\n", result, strerror(result));
                    goto catch_release;
                }
            }
        }
    }
    
    
    result = pthread_mutex_init(&mutex, attributesPtr);
    if ( result != 0 )
    {
        fprintf(stdout,"pthread_mutex_init failed: %d (%s)", result, strerror(result));
        goto catch_release;
    }
    
    size_t sizeOfMutex = sizeof(pthread_mutex_t);
    outMutex = YMALLOC(sizeOfMutex);
    memcpy(outMutex, &mutex, sizeOfMutex);
    
catch_release:
    if ( attributesPtr )
        pthread_mutexattr_destroy(attributesPtr);
    return outMutex;
}

bool YMLockMutex(pthread_mutex_t *mutex)
{
    int result = pthread_mutex_lock(mutex);
    bool okay = true;
    switch(result)
    {
        case 0:
            break;
        case EDEADLK:
            ymerr("mutex: error: %p EDEADLK", mutex);
            okay = false;
            break;
        case EINVAL:
            ymerr("mutex: error: %p EINVAL", mutex);
            okay = false;
            break;
        default:
            ymerr("mutex: error: %p unknown error", mutex);
            break;
    }
    
    return okay;
}

bool YMUnlockMutex(pthread_mutex_t *mutex)
{
    int result = pthread_mutex_unlock(mutex);
    bool okay = true;
    switch(result)
    {
        case 0:
            break;
        case EPERM:
            ymerr("mutex: error: unlocking thread doesn't hold %p", mutex);
            okay = false;
            break;
        case EINVAL:
            ymerr("mutex: error: unlock EINVAL %p", mutex);
            okay = false;
            break;
        default:
            ymerr("mutex: error: unknown %p", mutex);
            break;
    }
    
    return okay;
}

bool YMDestroyMutex(pthread_mutex_t *mutex)
{
    int result = pthread_mutex_destroy(mutex);
    free(mutex);
    return ( result == 0 );
}

#else
bool YMLockMutex(HANDLE mutex)
{
	return ( WaitForSingleObject(mutex,INFINITE) == WAIT_OBJECT_0 );
}
bool YMUnlockMutex(HANDLE mutex)
{
	return ReleaseMutex(mutex);
}
bool YMDestroyMutex(HANDLE mutex)
{
	return CloseHandle(mutex);
}
HANDLE YMCreateMutexWithOptions(YMLockOptions options)
{
	return CreateMutex(NULL, false, NULL);
}
#endif

#if defined(WIN32) || defined(_YOLO_DONT_TELL_PROFESSOR)
int gettimeofday(struct timeval * tp, struct timezone * tzp)
{
	// Note: some broken versions only have 8 trailing zero's, the correct epoch has 9 trailing zero's
	static const uint64_t EPOCH = ((uint64_t)116444736000000000ULL);

	SYSTEMTIME  system_time;
	FILETIME    file_time;
	uint64_t    time;

	GetSystemTime(&system_time);
	SystemTimeToFileTime(&system_time, &file_time);
	time = ((uint64_t)file_time.dwLowDateTime);
	time += ((uint64_t)file_time.dwHighDateTime) << 32;

	tp->tv_sec = (long)((time - EPOCH) / 10000000L);
	tp->tv_usec = (long)(system_time.wMilliseconds * 1000);
	return 0;
}
#endif

#ifdef __cplusplus
}
#endif
