//
//  YMUtilities.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMUtilities.h"

#include <fcntl.h>

#include "YMLock.h"
#include "YMAddress.h"
#include "YMArray.h"

#define ymlog_type YMLogIO // this file isn't very clearly purposed
#include "YMLog.h"

#if defined(YMAPPLE) || defined(YMLINUX)
# include <netinet/in.h>
# include <sys/ioctl.h>
# include <sys/types.h>
# include <sys/socket.h>
# include <ifaddrs.h>
# if defined(YMLINUX)
#  include <sys/resource.h>
#  define __USE_UNIX98
# endif
# include <pthread.h>
# if defined (YMAPPLE)
#  include <sys/time.h>
#  define YM_PORT_MAX IPPORT_HILASTAUTO
# else
#  define YM_PORT_MAX 65535
# endif
#elif defined(YMWIN32)
# define YM_PORT_MAX IPPORT_DYNAMIC_MAX
# include <Winsock2.h>
# include <Ws2tcpip.h>
# include <time.h>
# include <Winternl.h> // NtQuery
# include <Processthreadsapi.h> // GetCurrentProcessId
# include <VersionHelpers.h> // IsWindows*
#endif

YM_EXTERN_C_PUSH

const char *YMGetCurrentTimeString(char *buf, size_t bufLen)
{
    struct timeval epoch = {0,0};
    int result = gettimeofday(&epoch, NULL);
	if ( result != 0 )
		return NULL;
	const time_t secsSinceEpoch = epoch.tv_sec;
    struct tm *now = localtime(&secsSinceEpoch); // um, what? todo
    if ( ! now )
        return NULL;
    result = (int)strftime(buf, bufLen, "%Y-%m-%d %H:%M:%S", now);
    if ( result == 0 )
        return NULL;
    if ( result < (int)bufLen - 4 )
        snprintf(buf + result, bufLen - result, ".%03d",epoch.tv_usec/1000);
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
    
#ifdef YMAPPLE
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
    YM_IO_BOILERPLATE
    
    if ( buffer == NULL || bytes == 0 || fd < 0 )
        return YMIOError; // is this success or failure? :)
    
    size_t off = 0;
    YMIOResult ioResult = YMIOSuccess;
    while ( off < bytes )
    {
        YM_READ_FILE(fd, buffer + off, bytes - off);
        if ( aRead == 0 )
        {
            ymlog("    io: read(f%d, %p + %zu, %zu - %zu) EOF",fd, buffer, off, bytes, off);
            ioResult = YMIOEOF;
            break;
        }
        else if ( aRead == -1 )
        {
            ymerr("    io: read(f%d, %p + %zu, %zu - %zu) failed: %d (%s)",fd, buffer, off, bytes, off, error, errorStr);
            ioResult = YMIOError;
            break;
        }
        ymlog("    io: read(f%d, %p + %zu, %zu - %zu): %zd",fd, buffer, off, bytes, off, aRead);
        off += aRead;
    }
    
    if ( outRead )
        *outRead = off;
    
    return ioResult;
}

YMIOResult YMWriteFull(YMFILE fd, const uint8_t *buffer, size_t bytes, size_t *outWritten)
{
    YM_IO_BOILERPLATE
    
    if ( buffer == NULL || bytes == 0 || fd < 0 )
        return YMIOError;
    
    size_t off = 0;
    YMIOResult ioResult = YMIOSuccess;
    while ( off < bytes )
    {
        YM_WRITE_FILE(fd, buffer + off, bytes - off);
        switch(aWrite)
        {
            case 0:
                ymerr("    io: write(f%d, %p + %zu, %zu - %zu) failed 0?: %d (%s)",fd, buffer, off, bytes, off, error, errorStr);
                abort();
                //goto catch_fail;
            case -1:
                ymerr("    io: write(f%d, %p + %zu, %zu - %zu) failed: %d (%s)",fd, buffer, off, bytes, off, error, errorStr);
                ioResult = YMIOError;
                goto catch_fail;
            default:
                ymlog("    io: write(f%d, %p + %zu, %zu - %zu): %zd",fd, buffer, off, bytes, off, aWrite);
                break;
        }
        off += aWrite;
    }
    
    if ( outWritten )
        *outWritten = off;
    
catch_fail:
    return ioResult;
}
    
#if defined(YMWIN32)
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
#if defined(YMWIN32)
	YM_ONCE_DO_LOCAL(__YMNetworkingInit);
#endif
}

int32_t YMPortReserve(bool ipv4, int *outSocket)
{
    bool okay = false;
    uint16_t aPort = IPPORT_RESERVED;
    uint16_t thePort = aPort;
    YMSOCKET aSocket = NULL_SOCKET;
    
    uint8_t length = ipv4 ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);
    struct sockaddr *addr = YMALLOC(length);
    addr->sa_family = ipv4 ? AF_INET : AF_INET6;
#if defined(YMAPPLE)
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
        YMSOCKET aResult = socket(domain, SOCK_STREAM, IPPROTO_TCP);
#if !defined(YMWIN32)
        if ( aResult < 0 )
#else
		if ( aResult == INVALID_SOCKET )
#endif
            goto catch_continue;
        
        aSocket = aResult;
        
        int yes = 1;
        aResult = setsockopt(aSocket, SOL_SOCKET, SO_REUSEADDR, (const void *)&yes, sizeof(yes));
        if ( aResult != 0 )
            goto catch_continue;
        
        if ( ipv4 )
            ((struct sockaddr_in *)addr)->sin_port = htons(thePort);
        
        else
            ((struct sockaddr_in6 *)addr)->sin6_port = htons(thePort);
        
        aResult = bind(aSocket, addr, length);
        if ( aResult != 0 )
            goto catch_continue;
        
        *outSocket = aSocket;
        okay = true;
        break;
        
    catch_continue:
        if ( aSocket > 0 )
		{
			int result, error; const char *errorStr;
            YM_CLOSE_SOCKET(aSocket);
		}
    }
    
    free(addr);
    return okay ? (uint32_t)thePort : -1;
}

int YMGetNumberOfOpenFilesForCurrentProcess()
{
    int nFiles = 0;
#if !defined(YMWIN32)
    struct rlimit r_limit;
    int result = getrlimit(RLIMIT_NOFILE, &r_limit);
    ymsoftassert(result==0, "getrlimit: %d %s",errno,strerror(errno));
    
    for( rlim_t i = 0; i < r_limit.rlim_cur; i++ )
    {
        errno = 0;
        result = fcntl((int)i, F_GETFD);
        if ( result == 0 )
            nFiles++;
    }
#else // maybe there's a hidden "getrlimit" for win32? couldn't find it
	// cribbed from http://www.codeproject.com/Articles/18975/Listing-Used-Files
    // Get the list of all handles in the system
	typedef struct _SYSTEM_HANDLE
	{
		DWORD       dwProcessId;
		BYTE		bObjectType;
		BYTE		bFlags;
		WORD		wValue;
		PVOID       pAddress;
		DWORD GrantedAccess;
	}SYSTEM_HANDLE;

	typedef struct _SYSTEM_HANDLE_INFORMATION
	{
		DWORD         dwCount;
		SYSTEM_HANDLE Handles[1];
	} SYSTEM_HANDLE_INFORMATION, *PSYSTEM_HANDLE_INFORMATION, **PPSYSTEM_HANDLE_INFORMATION;

	typedef enum _SYSTEM_INFORMATION_CLASS {
		SystemHandleInformation = 0X10,
	} SYSTEM_INFORMATION_CLASS;

#define VISTA_FILETYPE  25
#define XP_FILETYPE 28
	int nFileType = IsWindowsVistaOrGreater() ? VISTA_FILETYPE : XP_FILETYPE;

	SYSTEM_HANDLE_INFORMATION *pSysHandleInformation = NULL;
	DWORD sysHandleInformationSize = 0;
    NTSTATUS status = NtQuerySystemInformation( SystemHandleInformation,
                                               NULL, 0, &sysHandleInformationSize);
	if ( NT_SUCCESS(status) || sysHandleInformationSize == 0 )
		return -1;

	sysHandleInformationSize = sysHandleInformationSize + 1024;
	pSysHandleInformation = malloc(sysHandleInformationSize);
    status = NtQuerySystemInformation( SystemHandleInformation,
                                        pSysHandleInformation, sysHandleInformationSize, &sysHandleInformationSize);
    if( !NT_SUCCESS(status))
	{
		free(pSysHandleInformation);
		return -1;
	}
    
	DWORD currentProcessID = GetCurrentProcessId();
	for ( DWORD i = 0; i < pSysHandleInformation->dwCount; i++ )
	{
		SYSTEM_HANDLE sh = pSysHandleInformation->Handles[i];
		if ( sh.dwProcessId != currentProcessID )
			continue;
		if( sh.bObjectType != nFileType )// Under windows XP file handle is of type 28
			continue;
		ymerr("open file: %d",sh.wValue);
		nFiles++;
	}

	free(pSysHandleInformation);
#endif
    
    ymlog("open files: %d",nFiles);
    return nFiles;
}

YMDictionaryRef YMCreateLocalInterfaceMap()
{
    YMDictionaryRef map = YMDictionaryCreate2(true,true);
    
#if defined(YMAPPLE) || defined(YMLINUX) // on mac now, guessing about linux
    struct ifaddrs *ifaddrsList = NULL, *ifaddrsIter;
    if ( getifaddrs(&ifaddrsList) != 0 ) {
        ymerr("getifaddrs failed: %d %s",errno,strerror(errno));
        goto catch_return;
    }
    ifaddrsIter = ifaddrsList;
    while ( ifaddrsIter ) {
        // IFF_UP // defined in ifconfig.c, anywhere else?
        //ioctl(<#int#>, <#unsigned long, ...#>);
        if ( ifaddrsIter->ifa_name && ifaddrsIter->ifa_addr ) {
            YMAddressRef address = YMAddressCreate(ifaddrsIter->ifa_addr, 0);
            if ( address ) {
                YMStringRef name = YMSTRC(ifaddrsIter->ifa_name);
                if ( ! YMDictionaryContains(map, (YMDictionaryKey)name) ) {
                    YMArrayRef addresses = YMArrayCreate(true);
                    YMDictionaryAdd(map, (YMDictionaryKey)name, (void *)addresses);
                }
                YMArrayAdd(YMDictionaryGetItem(map, (YMDictionaryKey)name), address);
                YMRelease(name);
            }
        }
        ifaddrsIter = ifaddrsIter->ifa_next;
    }
    freeifaddrs(ifaddrsList);
#else
#error todo
#endif
    
    ymlog("current interface map:");
    YMDictionaryEnumRef denum = YMDictionaryEnumeratorBegin(map);
    while ( denum ) {
        ymlogi(" %s:",YMSTR((void *)denum->key))
        YMArrayRef addresses = (YMArrayRef)denum->value;
        for ( int i = 0; i < YMArrayGetCount(addresses); i++ ) {
            ymlogi(" %s",YMSTR(YMAddressGetDescription((YMAddressRef)YMArrayGet(addresses, i))));
        }
        ymlogr();
        denum = YMDictionaryEnumeratorGetNext(denum);
    }
    YMDictionaryEnumeratorEnd(denum);
    
catch_return:
    return map;
}

#if !defined(YMWIN32)
pthread_mutex_t *YMCreateMutexWithOptions(YMLockOptions options)
{
    pthread_mutex_t *mutex = NULL;
    pthread_mutexattr_t attributes;
    pthread_mutexattr_t *attributesPtr = NULL;
    
    int result = pthread_mutexattr_init(&attributes);
    if ( result != 0 )
    {
        fprintf(stderr,"pthread_mutexattr_init failed: %d (%s)\n", result, strerror(result));
        goto catch_release;
    }
    
    attributesPtr = &attributes;
    
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
                    fprintf(stderr,"pthread_mutexattr_settype failed: %d (%s)\n", result, strerror(result));
                    goto catch_release;
                }
            }
        }
    }
    
    mutex = YMALLOC(sizeof(pthread_mutex_t));
    result = pthread_mutex_init(mutex, attributesPtr);
    if ( result != 0 )
    {
        fprintf(stderr,"pthread_mutex_init failed: %d (%s)\n", result, strerror(result));
        free(mutex);
        mutex = NULL;
    }
    
catch_release:
    if ( attributesPtr )
        pthread_mutexattr_destroy(attributesPtr);
    return mutex;
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
            fprintf(stderr,"mutex: error: %p EDEADLK\n", mutex);
            okay = false;
            break;
        case EINVAL:
            fprintf(stderr,"mutex: error: %p EINVAL\n", mutex);
            okay = false;
            break;
        default:
            fprintf(stderr,"mutex: error: %p unknown error\n", mutex);
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
            fprintf(stderr,"mutex: error: unlocking thread doesn't hold %p\n", mutex);
            okay = false;
            break;
        case EINVAL:
            fprintf(stderr,"mutex: error: unlock EINVAL %p\n", mutex);
            okay = false;
            break;
        default:
            fprintf(stderr,"mutex: error: unknown %p\n", mutex);
            break;
    }
    
    return okay;
}

bool YMDestroyMutex(pthread_mutex_t *mutex)
{
    int result = pthread_mutex_destroy(mutex);
    if ( result != 0 ) fprintf(stderr,"mutex: failed to destroy mutex: %d %s\n",errno,strerror(errno));
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

void YMUtilitiesFreeGlobals()
{
#if defined(YMWIN32) && defined(YM_FEELING_THOROUGH)
	WSACleanup();
#endif
}

#if defined(YMWIN32) || defined(_YOLO_DONT_TELL_PROFESSOR)
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

YM_EXTERN_C_POP
