//
//  YMUtilities.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMUtilities.h"

#include "YMPrivate.h"

#include "YMLog.h"
#undef ymlog_type
#define ymlog_type YMLogIO // this file isn't very clearly purposed
#if ( ymlog_type > ymlog_target )
#undef ymlog
#define ymlog(x,...) ;
#endif

#include <stdarg.h>

YMIOResult __YMReadFull(int fd, uint8_t *buffer, size_t bytes, size_t *outRead);
YMIOResult __YMWriteFull(int fd, const uint8_t *buffer, size_t bytes, size_t *outWritten);

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

YMIOResult YMReadFull(int fd, uint8_t *buffer, size_t bytes)
{
    return __YMReadFull(fd, buffer, bytes, NULL);
}

YMIOResult YMWriteFull(int fd, const uint8_t *buffer, size_t bytes)
{
    return __YMWriteFull(fd, buffer, bytes, NULL);
}

YMIOResult __YMReadFull(int fd, uint8_t *buffer, size_t bytes, size_t *outRead)
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

YMIOResult __YMWriteFull(int fd, const uint8_t *buffer, size_t bytes, size_t *outWritten)
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
                break;
            case -1:
                result = YMIOError;
                break;
            default:
                result = YMIOEOF;
                break;
        }
        off += aWrite;
    }
    if ( outWritten )
        *outWritten = off;
    return result;
}

YMIOResult YMReadWriteFull(int inFile, int outFile, uint64_t *outBytes)
{
    uint64_t off = 0;
    
    bool lastIter = false;
    uint16_t bufferSize = 16384;
    void *buffer = YMALLOC(bufferSize);
    
    YMIOResult aResult;
    size_t aBytes;
    do
    {
        aResult = __YMReadFull(inFile, buffer, bufferSize, &aBytes);
        if ( aResult == YMIOError )
        {
            ymerr("read-write-full: error reading %llu-%llu from %d: %d (%s)",off,off+bufferSize,inFile,errno,strerror(errno));
            break;
        }
        else if ( aResult == YMIOEOF )
            lastIter = true;
        
        aResult = __YMWriteFull(outFile, buffer, aBytes, NULL);
        
        outBytes += aBytes;
    } while(!lastIter);
    
    free(buffer);
    
    if ( outBytes )
        *outBytes = off;
    return aResult;
}

char *YMStringCreateWithFormat(char *formatStr, ...)
{
    va_list testArgs,formatArgs;
    va_start(testArgs,formatStr);
    va_copy(formatArgs,testArgs);
    int length = vsnprintf(NULL, 0, formatStr, testArgs) + 1;
    
    char *newStr = NULL;
    if ( length == 0 )
        newStr = strdup("");
    else if ( length < 0 )
        ymerr("snprintf failed on format: %s", formatStr);
    else
    {
        newStr = (char *)YMALLOC(length);
        //va_start(formatArgs,formatStr);
        vsnprintf(newStr, length, formatStr, formatArgs);
        va_end(formatArgs);
    }
    
    va_end(testArgs);
    
    return newStr;
}

char *YMStringCreateByAppendString(char *baseStr, char *appendStr)
{
    size_t baseLen = strlen(baseStr);
    size_t appendLen = strlen(appendStr);
    size_t newStringLen = baseLen + appendLen + 1;
    char *newString = (char *)YMALLOC(newStringLen);
    memcpy(newString, baseStr, baseLen);
    memcpy(newString + baseLen, appendStr, appendLen);
    newString[newStringLen - 1] = '\0';
    
    return newString;
}
