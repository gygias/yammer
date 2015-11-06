//
//  YMUtilities.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMUtilities.h"

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

void YMSetTheBeginningOfPosixTimeForCurrentPlatform(struct timeval *time)
{
    time->tv_sec = MIN_OF(typeof(time->tv_sec));
    time->tv_usec = MIN_OF(typeof(time->tv_usec));
}

void YMSetTheEndOfPosixTimeForCurrentPlatform(struct timeval *time)
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

bool YMReadFull(int fd, uint8_t *buffer, size_t bytes)
{
    size_t off = 0;
    while ( off < bytes )
    {
        ssize_t aRead = read(fd, (void *)buffer + off, bytes - off);
        if ( aRead == 0 )
            return false;
        else if ( aRead == -1 )
            return false;
        off += aRead;
    }
    return true;
}

bool YMWriteFull(int fd, const uint8_t *buffer, size_t bytes)
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
                return false;
                break;
            default:
                break;
        }
        off += aWrite;
    }
    return true;
}

// todo inline these?
char *YMStringCreateWithFormat(char *formatStr, ...)
{
    va_list args;
    va_start(args,formatStr);
    
    int length = snprintf(NULL, 0, formatStr, args);
    
    char *newStr = NULL;
    if ( length == 0 )
        newStr = strdup("");
    else if ( length < 0 )
        YMLog("snprintf failed on format: %s", formatStr);
    else
    {
        newStr = (char *)malloc(length);
        snprintf(newStr, length, formatStr, args);
    }
    
    va_end(args);
    
    return newStr;
}

char *YMStringCreateByAppendString(char *baseStr, char *appendStr)
{
    size_t baseLen = strlen(baseStr);
    size_t appendLen = strlen(appendStr);
    size_t newStringLen = baseLen + appendLen + 1;
    char *newString = (char *)malloc(newStringLen);
    memcpy(newString, baseStr, baseLen);
    memcpy(newString + baseLen, appendStr, appendLen);
    newString[newStringLen - 1] = '\0';
    
    return newString;
}