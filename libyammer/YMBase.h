//
//  YMBase.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMBase_h
#define YMBase_h

#ifdef __cplusplus
extern "C" {
#endif

typedef const void *YMTypeRef;
typedef char YMTypeID;

YMAPI YMTypeRef YMRetain(YMTypeRef object);
YMAPI YMTypeRef YMAutorelease(YMTypeRef object);
#ifdef DEBUG
#define YM_RELEASE_RETURN_TYPE bool
#else
#define YM_RELEASE_RETURN_TYPE void
#endif
YMAPI YM_RELEASE_RETURN_TYPE YMRelease(YMTypeRef object);

YMAPI void YMSelfLock(YMTypeRef object);
YMAPI void YMSelfUnlock(YMTypeRef object);

#ifndef WIN32
    #define YMFILE int
    #define NULL_FILE (-1)
    #define YM_READ_FILE(fd,addr,count) aRead = read(fd, addr, count);
    #define YM_WRITE_FILE(fd,addr,count) aWrite = write(fd,addr,count);
    #define YM_CLOSE_FILE(fd) { result = close(fd); \
                                if ( result != 0 ) { error = errno; errorStr = strerror(error); } \
                                else { error = 0; errorStr = NULL; } }
    #define YMSOCKET YMFILE
    #define NULL_SOCKET (-1)
    #define YM_CLOSE_SOCKET(socket) YM_CLOSE_FILE(socket)
    #define YM_WAIT_SEMAPHORE(s) { result = sem_wait(s->sem); \
                                    if ( result != 0 ) { error = errno; errorStr = strerror(error); } \
                                    else { error = 0; errorStr = NULL; } }
    #define YM_POST_SEMAPHORE(s) { result = sem_post(semaphore->sem); \
                                    if ( result != 0 ) { error = errno; errorStr = strerror(error); } \
                                    else { error = 0; errorStr = NULL; } }
    #define YM_RETRY_SEMAPHORE ( error == EINTR )
    #define YM_CLOSE_SEMAPHORE(s) { result = sem_unlink(YMSTR(s->semName)); \
                                    if ( result != 0 ) { error = errno; errorStr = strerror(error); } \
                                    else { error = 0; errorStr = NULL; } }
#else
    #define YMFILE HANDLE
    #define NULL_FILE ((HANDLE)NULL)
    #define YM_READ_FILE(fd,addr,count) { BOOL okay = ReadFile(fd, buffer + off, bytes - off, &aRead, NULL); if ( ! okay ) aRead = -1; }
    #define YM_WRITE_FILE(fd,addr,count) { BOOL okay = WriteFile(fd, buffer+ off, bytes - off, &aWrite, NULL); if ( ! okay ) aWrite = -1; }
    #define YM_CLOSE_FILE(fd) { BOOL okay = CloseHandle(fd); if ( ! okay ) result = -1; else result = 0; }
    #define YMSOCKET SOCKET
    #define NULL_SOCKET ((SOCKET)NULL)
    #define YM_CLOSE_SOCKET(socket) { result = closesocket(socket); \
                                      if ( result == SOCKET_ERROR ) { result = -1; error = GetLastError(); errorStr = "windows error"; } \
                                      else { result = 0; error = 0; errorStr = NULL; } }
    #define YM_WAIT_SEMAPHORE(s) { DWORD dResult = WaitForSingleObject(s->sem, INFINITE); \
                                   if ( dResult != WAIT_OBJECT_0 ) { \
                                        if ( dResult == WAIT_FAILED ) error = (int)GetLastError(); \
                                        else error = (int)dResult; \
                                        result = -1; errorStr = "windows error"; } \
                                    else { result = 0; error = 0; errorStr = NULL; } }
    #define YM_POST_SEMAPHORE(s) { BOOL okay = ReleaseSemaphore(s->sem, 1, NULL); \
                                    if ( ! okay ) { result = -1; error = GetLastError(); errorStr = "windows error"; } \
                                    else { result = 0; error = 0; errorStr = NULL; } }
    #define YM_RETRY_SEMAPHORE false
    #define YM_CLOSE_SEMAPHORE(s) { BOOL okay = CloseHandle(s->sem); \
                                    if ( ! okay ) { result = -1; error = (int)GetLastError(); errorStr = "windows error"; } \
                                    else { result = 0; error = 0; errorStr = NULL; } }
#endif

typedef enum
{
    YMIOSuccess = 1,
    YMIOEOF = 0,
    YMIOError = -1
} YMIOResult;

YMAPI void YMFreeGlobalResources();

#if defined(_MACOS)
#define YM_VARGS_SENTINEL_REQUIRED __attribute__((sentinel(0,1)))
#define YM_WPPUSH \
_Pragma("GCC diagnostic push") \
_Pragma("GCC diagnostic ignored \"-Wpedantic\"")
#define YM_WPOP \
_Pragma("GCC diagnostic pop")
#elif defined(WIN32) || defined(RPI)
/*#define bool unsigned char
 #define false 0
 #define true 1
 typedef __int32 int32_t;
 typedef unsigned __int32 uint32_t;
 typedef unsigned __int16 uint16_t;
 typedef unsigned __int8 uint8_t;
 typedef unsigned int size_t;*/
#define ssize_t SSIZE_T
#define YM_VARGS_SENTINEL_REQUIRED
#define __printflike(x,y)
#define YM_WPPUSH
#define YM_WPOP
#define __unused
#define typeof decltype
#endif

#ifdef __cplusplus
}
#endif

#endif /* YMBase_h */
