//
//  yammerpch.h
//  yammer
//
//  Created by david on 11/9/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef PrefixHeader_pch
#define PrefixHeader_pch

#if defined(DEBUG) || defined(_DEBUG)
#define YMDEBUG 1
#else
#undef YMDEBUG
#endif

#define YM_TOKEN_STR(x) #x
#define YM_STR_TOKEN(x,y) x ## y

#define YM_CALL(func, type) { void func(type); YM_FUNC_NAME(func)(); }
#define YM_CALL_V(func, firstType, first, ...) func(first,__VA_ARGS__) // iso c requires 'first'
#define YM_CALL_DV(func, firstType, first, ...) { void func(firstType arg1,...); func(first,__VA_ARGS__); } // iso c requires 'first'

//#define STR(x) YM_TOKEN_STR(x)
//#pragma message STR(__FILE__) ": example: " STR(example)

#include <stdio.h>

#if defined(_MACOS) || defined(RPI)
#include <unistd.h>
# if defined (RPI)
  typedef __ssize_t ssize_t;
# include <limits.h>
# include <sys/types.h>
# include <sys/time.h>
# include "arc4random.h"
# define __USE_BSD
# endif
#elif defined(WIN32)
#define _WINSOCKAPI_
#include <windows.h>
#define _WINSOCK_DEPRECATED_NO_WARNINGS // todo, gethostbyname
#include <winsock2.h>
#include <ws2tcpip.h>
#include "arc4random.h"
#define bzero ZeroMemory
#endif

#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#if defined(_MACOS) || defined(RPI)
#define YM_VARGS_SENTINEL_REQUIRED __attribute__((sentinel(0)))
#endif

#if defined(WIN32)
#define ssize_t SSIZE_T
#define typeof decltype
#include <direct.h>
#define strdup _strdup
#define unlink _unlink
#define mkdir(p,m) _mkdir(p)
#define rmdir _rmdir // ( ( RemoveDirectory(x) == 0 ) ? -1 : 0 )
#endif

#if defined(WIN32) || defined(RPI)
#define __unused
#endif

#if defined(RPI)
#define __printflike(x,y) __attribute__ ((format (printf, x, y)))
#elif defined(WIN32)
#define YM_VARGS_SENTINEL_REQUIRED
#define __printflike(x,y)
#endif

#define YM_VERSION 1

#include "YMBase.h"
#include "YMInternal.h"

#ifdef DEBUG
#include <malloc/malloc.h>
#define YM_DEBUG_ASSERT_MALLOC(x) { if ( ( x != NULL) && ( malloc_size(x) <= 0 ) ) { ymerr("debug: malloc didn't allocate this address: %p",(x)); abort(); } }
#define YM_INSANE_CHUNK_SIZE 65535
#define YM_DEBUG_CHUNK_SIZE(x) { if ( ( (x) == 0 ) || ( (x) > YM_INSANE_CHUNK_SIZE ) ) { ymerr("debug: chunk length not sane: %u",(x)); abort(); } }
#else
#define YM_DEBUG_ASSERT_MALLOC(x) ;
#define YM_DEBUG_CHUNK_SIZE(x) ;
#endif



#ifndef WIN32
	#define YMSOCKET YMFILE
    #define NULL_FILE (-1)

	#define READ_FLAG O_RDONLY
	#define WRITE_FLAG O_WRONLY
	#define READ_WRITE_FLAG O_RDWR
	
	#define YM_OPEN_FILE(p,f)				open(p,f,0644)
	#define YM_OPEN_FILE_2(p,f,m)			open(p,(f)|O_CREAT,m)
	#define YM_STOMP_FILE(p,f)				open(p,(f)|O_CREAT|O_TRUNC,0644)
	#define YM_REWIND_FILE(f)				lseek(f,0,SEEK_SET)
	#define YM_SEEK_FILE(f,o,r)				SetFilePointer(f,o,NULL,r)
    #define YM_READ_FILE(fd,addr,count)		aRead = read(fd, addr, count);
    #define YM_WRITE_FILE(fd,addr,count)	aWrite = write(fd,addr,count);
    #define YM_CLOSE_FILE(fd)				{ result = close(fd); \
												if ( result != 0 ) { error = errno; errorStr = strerror(error); } \
												else { error = 0; errorStr = NULL; } }
    #define NULL_SOCKET (-1)
	#define YM_READ_SOCKET(s,b,l)	YM_READ_FILE(s,b,l)
	#define YM_WRITE_SOCKET(s,b,l)	YM_WRITE_FILE(s,b,l)
    #define YM_CLOSE_SOCKET(x)		YM_CLOSE_FILE(x)
    #define YM_WAIT_SEMAPHORE(s)	{ result = sem_wait(s->sem); \
										if ( result != 0 ) { error = errno; errorStr = strerror(error); } \
										else { error = 0; errorStr = NULL; } }
    #define YM_POST_SEMAPHORE(s)	{ result = sem_post(semaphore->sem); \
										if ( result != 0 ) { error = errno; errorStr = strerror(error); } \
										else { error = 0; errorStr = NULL; } }
    #define YM_RETRY_SEMAPHORE		( error == EINTR )
    #define YM_CLOSE_SEMAPHORE(s)	{ result = sem_unlink(YMSTR(s->semName)); \
										if ( result != 0 ) { error = errno; errorStr = strerror(error); } \
										else { error = 0; errorStr = NULL; } }
	#define YM_CREATE_PIPE(fds)		( 0 == pipe(fds) )
#else
	#define YMSOCKET SOCKET
    #define NULL_FILE ((HANDLE)NULL)

	#define READ_FLAG GENERIC_READ
	#define WRITE_FLAG GENERIC_WRITE
	#define READ_WRITE_FLAG ( GENERIC_READ | GENERIC_WRITE )

	#ifndef SEEK_CUR
	# define SEEK_CUR FILE_CURRENT
	# define SEEK_END FILE_END
	#endif
	
	#define YM_OPEN_FILE(p,f)				CreateFileA(p,f,FILE_SHARE_READ,NULL,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL)
	#define YM_OPEN_FILE_2(p,f,m)			YM_OPEN_FILE(p,f)
	#define YM_STOMP_FILE(p,f)				CreateFileA(p,f,FILE_SHARE_READ,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL)
	#define YM_REWIND_FILE(f)				YM_SEEK_FILE(f,0,FILE_BEGIN)
	#define YM_SEEK_FILE(f,o,r)				{ DWORD __sfp = SetFilePointer(f,o,NULL,r); if ( __sfp == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR ) result = -1; else result = 0; }
    #define YM_READ_FILE(fd,addr,count)		{ BOOL __rf = ReadFile(fd, addr, count, &aRead, NULL); if ( ! __rf ) aRead = -1; }
    #define YM_WRITE_FILE(fd,addr,count)	{ BOOL __wf = WriteFile(fd, addr, count, &aWrite, NULL); if ( ! __wf ) aWrite = -1; }
    #define YM_CLOSE_FILE(fd)				{ BOOL __ch = CloseHandle(fd); if ( ! __ch ) result = -1; else result = 0; }
    #define NULL_SOCKET ((SOCKET)NULL)
	#define GENERIC_WERROR_STR "windows error" // unfortunately the strerror equivalent FormatMessage needs the caller to take ownership, which we don't want to mess with.
												// we could have our own method with static strings. P.S. ERROR_ARENA_TRASHED
	#define YM_READ_SOCKET(s,b,l)	aRead = recv(s,b,l,0)
	#define YM_WRITE_SOCKET(s,b,l)	aWrite = send(s,b,l,0)
    #define YM_CLOSE_SOCKET(socket) { result = closesocket(socket); \
                                      if ( result == SOCKET_ERROR ) { result = -1; error = GetLastError(); errorStr = GENERIC_WERROR_STR; } \
                                      else { result = 0; error = 0; errorStr = NULL; } }
    #define YM_WAIT_SEMAPHORE(s)	{ DWORD __wfso = WaitForSingleObject(s->sem, INFINITE); \
									if ( __wfso != WAIT_OBJECT_0 ) { \
                                        if ( __wfso == WAIT_FAILED ) error = (int)GetLastError(); \
                                        else error = (int)__wfso; \
                                        result = -1; errorStr = GENERIC_WERROR_STR; } \
                                    else { result = 0; error = 0; errorStr = NULL; } }
    #define YM_POST_SEMAPHORE(s)	{ BOOL __rs = ReleaseSemaphore(s->sem, 1, NULL); \
                                    if ( ! __rs ) { result = -1; error = GetLastError(); errorStr = GENERIC_WERROR_STR; } \
                                    else { result = 0; error = 0; errorStr = NULL; } }
    #define YM_RETRY_SEMAPHORE		false
    #define YM_CLOSE_SEMAPHORE(s)	{ BOOL __ch = CloseHandle(s->sem); \
                                    if ( ! __ch ) { result = -1; error = (int)GetLastError(); errorStr = GENERIC_WERROR_STR; } \
                                    else { result = 0; error = 0; errorStr = NULL; } }
	#define YM_CREATE_PIPE(fds)		( 0 != CreatePipe(&fds[0],&fds[1],NULL,0) )
	#define sleep(x) Sleep(((DWORD)x)/1000)
	#define usleep(x) Sleep((DWORD)(x)*1000)
	#define signal(x,y) ymerr("*** sigpipe win32 ***")
	#define strerror(x) "(strerror win32)"
#endif

#endif /* PrefixHeader_pch */
