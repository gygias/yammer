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
#endif

#define YM_TOKEN_STR(x) #x
#define YM_STR_TOKEN(x,y) x ## y

#define YM_CALL(func, type) { void func(type); YM_FUNC_NAME(func)(); }
#define YM_CALL_V(func, firstType, first, ...) func(first, first,__VA_ARGS__) // iso c requires 'first'
#define YM_CALL_DV(func, firstType, first, ...) { void func(firstType arg1,...); func(first,__VA_ARGS__); }

//#define STR(x) YM_TOKEN_STR(x)
//#pragma message STR(__FILE__) ": example: " STR(example)

#include <stdio.h>

#if defined(YMMACOS) || defined(YMLINUX)
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
# if defined (YMLINUX)
  typedef __ssize_t ssize_t;
# include <limits.h>
# include <sys/types.h>
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
#define SHUT_RDWR SD_BOTH
#endif

#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#if defined(WIN32)
#define ssize_t SSIZE_T
#define typeof decltype
#include <direct.h>
#define strdup _strdup
#define unlink _unlink
#define mkdir(p,m) _mkdir(p)
#define rmdir _rmdir // ( ( RemoveDirectory(x) == 0 ) ? -1 : 0 )
#endif

#if defined(WIN32) || defined(YMLINUX)
#define __unused
#endif

#define YM_VERSION 1

#include "YMBase.h"
#include "YMInternal.h"

#ifdef YMDEBUG
# ifndef WIN32
#  include <malloc/malloc.h>
# else
#  include <malloc.h>
#  ifdef WIN32
#   define malloc_size _msize
#  elif defined(YMLINUX)
#  define malloc_size malloc_usable_size
#  endif
# endif

#define YM_DEBUG_ASSERT_MALLOC(x) ymassert(( (x) != NULL )&&( malloc_size((void *)x) > 0 ),"debug: malloc didn't allocate this address: %p",(x))
#define YM_INSANE_CHUNK_SIZE 65535
#define YM_DEBUG_CHUNK_SIZE(x) ymassert(( (x) != 0 )&&( (x) <= YM_INSANE_CHUNK_SIZE ),"debug: chunk length not sane: %u",(x));
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

	#define YM_IO_BOILERPLATE		__unused int result = 0, __unused error = 0; __unused ssize_t aRead = 0, aWrite = 0; __unused const char *errorStr = NULL;
	
	#define YM_OPEN_FILE(p,f)				{ result = open(p,f,0644); if ( result != 0 ) { error = errno; errorStr = strerror(errno); } }
	#define YM_OPEN_FILE_2(p,f,m)			{ result = open(p,(f)|O_CREAT,m); if ( result != 0 ) { error = errno; errorStr = strerror(errno); } }
	#define YM_STOMP_FILE(p,f)				{ result = open(p,(f)|O_CREAT|O_TRUNC,0644); if ( result != 0 ) { error = errno; errorStr = strerror(errno); } }
	#define YM_REWIND_FILE(f)				{ result = (int)lseek(f,0,SEEK_SET); if ( result != 0 ) { error = errno; errorStr = strerror(errno); } }
	#define YM_SEEK_FILE(f,o,r)				{ result = lseek(f,o,,r); if ( result != 0 ) { error = errno; errorStr = strerror(errno); } }
    #define YM_READ_FILE(fd,addr,count)		{ aRead = read(fd, addr, count); if ( aRead == -1 ) { error = errno; errorStr = strerror(errno); } }
    #define YM_WRITE_FILE(fd,addr,count)	{ aWrite = write(fd,addr,count); if ( aRead == -1 ) { error = errno; errorStr = strerror(errno); } }
    #define YM_CLOSE_FILE(fd)				{ result = close(fd); if ( result != 0 ) { error = errno; errorStr = strerror(error); } }
    #define NULL_SOCKET (-1)
	#define YM_READ_SOCKET(s,b,l)	YM_READ_FILE(s,b,l)
	#define YM_WRITE_SOCKET(s,b,l)	YM_WRITE_FILE(s,b,l)
    #define YM_CLOSE_SOCKET(x)		YM_CLOSE_FILE(x)
    #define YM_WAIT_SEMAPHORE(s)	{ result = sem_wait(s); if ( result != 0 ) { error = errno; errorStr = strerror(error); } }
    #define YM_POST_SEMAPHORE(s)	{ result = sem_post(s); if ( result != 0 ) { error = errno; errorStr = strerror(error); } }
    #define YM_RETRY_SEMAPHORE		( error == EINTR )
    #define YM_CLOSE_SEMAPHORE(so)	{ result = sem_unlink(YMSTR(so->semName)); if ( result != 0 ) { error = errno; errorStr = strerror(error); } }
	#define YM_CREATE_PIPE(fds)		{ result = pipe(fds); if ( result != 0 ) { error = errno; errorStr = strerror(error); } }
#else
	#define YMSOCKET SOCKET
    #define NULL_FILE ((HANDLE)NULL)

	#define READ_FLAG FILE_GENERIC_READ
	#define WRITE_FLAG FILE_GENERIC_WRITE
	#define READ_WRITE_FLAG ( READ_FLAG | WRITE_FLAG )

	#ifndef SEEK_CUR
	# define SEEK_CUR FILE_CURRENT
	# define SEEK_END FILE_END
	#endif

	#define YM_IO_BOILERPLATE				__unused int result = 0, __unused error = 0; __unused ssize_t aRead = 0, aWrite = 0; __unused const char *errorStr = NULL;
	
	#define YM_OPEN_FILE(p,f)				{ HANDLE __cfa = CreateFile(p,f,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL); \
												if ( __cfa == INVALID_HANDLE_VALUE ) { result = -1; error = (int)GetLastError(); errorStr = GENERIC_WERROR_STR; } else result = (int)__cfa; }
	#define YM_OPEN_FILE_2(p,f,m)			YM_OPEN_FILE(p,f)
	#define YM_STOMP_FILE(p,f)				{ HANDLE __cfa = CreateFileA(p,f,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL); \
												if ( __cfa == INVALID_HANDLE_VALUE ) { result = -1; error = (int)GetLastError(); errorStr = GENERIC_WERROR_STR; } else result = (int)__cfa; }
	#define YM_REWIND_FILE(f)				YM_SEEK_FILE(f,0,FILE_BEGIN)
	#define YM_SEEK_FILE(f,o,r)				{ DWORD __sfp = SetFilePointer(f,o,NULL,r); if ( __sfp == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR ) result = -1; else result = 0; }
	#define YM_READ_FILE(fd,addr,count)		{ BOOL __rf = ReadFile(fd, addr, count, &aRead, NULL); \
												if ( ! __rf ) { \
													error = GetLastError(); \
													if ( error == ERROR_HANDLE_EOF ) result = 0; \
													else result = -1;  } \
												else if ( aRead == 0 ) result = 0; }
	#define YM_WRITE_FILE(fd,addr,count)	{ BOOL __wf = WriteFile(fd, addr, count, &aWrite, NULL); if ( ! __wf ) { aWrite = -1; error = GetLastError(); } }
    #define YM_CLOSE_FILE(fd)				{ BOOL __ch = CloseHandle(fd); if ( ! __ch ) result = -1; else result = 0; }
    #define NULL_SOCKET ((SOCKET)NULL)
	#define GENERIC_WERROR_STR "windows error" // unfortunately the strerror equivalent FormatMessage needs the caller to take ownership, which we don't want to mess with.
												// we could have our own method with static strings. P.S. ERROR_ARENA_TRASHED
	#define YM_READ_SOCKET(s,b,l)	aRead = recv(s,b,l,0)
	#define YM_WRITE_SOCKET(s,b,l)	aWrite = send(s,b,l,0)
    #define YM_CLOSE_SOCKET(socket) { result = closesocket(socket); if ( result == SOCKET_ERROR ) { result = -1; error = GetLastError(); errorStr = GENERIC_WERROR_STR; } }
    #define YM_WAIT_SEMAPHORE(s)	{ DWORD __wfso = WaitForSingleObject(s, INFINITE); \
										if ( __wfso != WAIT_OBJECT_0 ) { \
											if ( __wfso == WAIT_FAILED ) error = (int)GetLastError(); \
											else error = (int)__wfso; \
											result = -1; errorStr = GENERIC_WERROR_STR; } \
										else result = 0; }
    #define YM_POST_SEMAPHORE(s)	{ BOOL __rs = ReleaseSemaphore(s, 1, NULL); if ( ! __rs ) { result = -1; error = GetLastError(); errorStr = GENERIC_WERROR_STR; } else result = 0; }
    #define YM_RETRY_SEMAPHORE		false
    #define YM_CLOSE_SEMAPHORE(s)	{ BOOL __ch = CloseHandle(s->sem); if ( ! __ch ) { result = -1; error = (int)GetLastError(); errorStr = GENERIC_WERROR_STR; } else result = 0; }
	#define YM_CREATE_PIPE(fds)		{ BOOL __cp = CreatePipe(&fds[0],&fds[1],NULL,UINT16_MAX); if ( ! __cp) { result = -1; error = (int)GetLastError(); errorStr = GENERIC_WERROR_STR; } else result = 0; }
	#define sleep(x) Sleep(((DWORD)x)*1000)
	#define usleep(x) Sleep((DWORD)(x)/1000)
	#define signal(x,y) ymerr("*** sigpipe win32 placeholder ***")
	#define strerror(x) "strerror win32"
#endif

#endif /* PrefixHeader_pch */
