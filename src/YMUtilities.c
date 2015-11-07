//
//  YMUtilities.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMUtilities.h"

#include "YMPrivate.h"

#include <stdarg.h>

//// Glyph from http://stackoverflow.com/questions/2053843/min-and-max-value-of-data-type-in-c
//#define issigned(t) (((t)(-1)) < ((t) 0))
//#define umaxof(t) (((0x1ULL << ((sizeof(t) * 8ULL) - 1ULL)) - 1ULL) | \
//(0xFULL << ((sizeof(t) * 8ULL) - 4ULL)))
//#define smaxof(t) (((0x1ULL << ((sizeof(t) * 8ULL) - 1ULL)) - 1ULL) | \
//(0x7ULL << ((sizeof(t) * 8ULL) - 4ULL)))
//#define maxof(t) ((unsigned long long) (issigned(t) ? smaxof(t) : umaxof(t)))
#define MAX_OF(type) \
(((type)(~0LLU) > (type)((1LLU<<((sizeof(type)<<3)-1))-1LLU)) ? (long long unsigned int)(type)(~0LLU) : (long long unsigned int)(type)((1LLU<<((sizeof(type)<<3)-1))-1LLU))
#define MIN_OF(type) \
(((type)(1LLU<<((sizeof(type)<<3)-1)) < (type)1) ? (long long int)((~0LLU)-((1LLU<<((sizeof(type)<<3)-1))-1LLU)) : 0LL)

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
    size_t off = 0;
    while ( off < bytes )
    {
        ssize_t aRead = read(fd, (void *)buffer + off, bytes - off);
        if ( aRead == 0 )
            return YMIOEOF;
        else if ( aRead == -1 )
            return YMIOError;
        off += aRead;
    }
    return YMIOSuccess;
}

YMIOResult YMWriteFull(int fd, const uint8_t *buffer, size_t bytes)
{
    size_t off = 0;
    while ( off < bytes )
    {
        ssize_t aWrite = write(fd, buffer + off, bytes - off);
        switch(aWrite)
        {
            case 0:
                printf("YMWrite: aWrite=0?");
            case -1:
                return YMIOError;
                break;
            default:
                break;
        }
        off += aWrite;
    }
    return YMIOSuccess;
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
        YMLog("snprintf failed on format: %s", formatStr);
    else
    {
        newStr = (char *)YMMALLOC(length);
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
    char *newString = (char *)YMMALLOC(newStringLen);
    memcpy(newString, baseStr, baseLen);
    memcpy(newString + baseLen, appendStr, appendLen);
    newString[newStringLen - 1] = '\0';
    
    return newString;
}
