//
//  YMString.c
//  yammer
//
//  Created by david on 11/13/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMString.h"

#include <stdarg.h>

#include "YMLog.h"
#undef ymlog_type
#define ymlog_type YMLogDefault

typedef struct __ym_string
{
    _YMType _type;
    
    const char *cString;
    // lazy
    size_t length;
} ___ym_string;
typedef struct __ym_string __YMString;
typedef __YMString *__YMStringRef;

// this torturous thing is so that we have one implementation, as far as i know you can't 'forward' vargs to another function call
#define __YMStringFormatLocal \
                        va_list formatArgs2,formatArgs; \
                        va_start(formatArgs2,format); \
                        va_copy(formatArgs,formatArgs2); \
                        int length = vsnprintf(NULL, 0, format, formatArgs2) + 1; \
                        \
                        char *newStr = NULL; \
                        if ( length == 0 ) \
                            newStr = ""; \
                        else if ( length < 0 ) \
                            ymerr("snprintf failed on format: %s", format); \
                        else \
                        { \
                            newStr = (char *)YMALLOC(length); \
                            vsnprintf(newStr, length, format, formatArgs); \
                            va_end(formatArgs); \
                        } \
                         \
                        va_end(formatArgs2); \

YMStringRef YMStringCreate()
{
    return YMStringCreateWithCString("");
}

YMStringRef YMStringCreateWithCString(const char *cString)
{
    size_t length = strlen(cString);
    __YMStringRef string = (__YMStringRef)_YMAlloc(_YMStringTypeID,sizeof(__YMString));
    
    string->cString = YMALLOC(length + 1);
    strncpy((char *)string->cString, cString, length + 1);
    string->length = length;
    
    return string;
}

YMStringRef YMStringCreateWithFormat(const char *format,...)
{
    __YMStringFormatLocal;
    return YMStringCreateWithCString(newStr);
}

YMStringRef _YMStringCreateForYMAlloc(const char *format,...)
{
    __YMStringFormatLocal
    __YMStringRef string = (__YMStringRef)YMALLOC(sizeof(__YMString));
    string->_type.__type = _YMStringTypeID;
    string->cString = newStr;
    return string;
}

#ifdef internal_function
const char *___YMStringFormat(const char *format,...)
{
#ifndef YMTypeRefToken
#else
    char *currentTokenPtr = strstr(format, "%");
    if ( ! currentTokenPtr )
    {
        ymerr("string; warning: format function used but no tokens found in '%s",format);
        return YMStringCreateWithCString(format);
    }
    
    size_t formatLen = strlen(format);
    size_t currentTokenIdx = currentTokenPtr - format;
    
    va_list vargs;
    va_start(vargs, format);
    
    while ( currentTokenPtr )
    {
        int aInt[2];
        char *aCharPtr = NULL;
        int *aIntPtr = NULL;
        unsigned *aUnsignedPtr = NULL;
        double *aDoublePtr = NULL;
        char *aStringPtr = NULL;
        unsigned long *aUnsignedLongPtr = NULL;
        unsigned long long *aUnsignedLongLongPtr = NULL;
        unsigned int *aUnsignedIntPtr = NULL;
        long *aLongIntPtr = NULL;
        long long *aLongLongPtr;
        
        bool expandingL = false;
        bool expandingZ = false;
        bool expandingLL = false;
        
        size_t specifierIdx = currentTokenIdx + 1;
        
        // test for single char tokens
        if ( specifierIdx < formatLen - 1 )
        {
            aInt[0] = va_arg(vargs,int);
            switch(format[specifierIdx])
            {
                case 'c':
                    aCharPtr = (char *)aInt;
                    break;
                case 'd':
                    aIntPtr = &aInt[0];
                    break;
                case 'u':
                    aUnsignedPtr = (unsigned *)aInt;
                    break;
                case 'f':
                    aDoublePtr = (double *)aInt; // todo
                    ymerr("string: warning: floating point specifier in '%s'",format);
                    break;
                case 's':
                    aStringPtr = (char *)aInt;
                    break;
                case 'l':
                    expandingL = true;
                    break;
                case 'z':
                    expandingZ = true;
                    break;
            }
        }
        else
        {
            ymerr("string: warning: invalid specifier ending '%s'",format);
            goto catch_fail;
        }
        size_t specifierIdx2 = specifierIdx + 1;
        if ( ( expandingZ || expandingL ) && ( specifierIdx < formatLen - 2 ) )
        {
            if ( expandingL )
            {
                switch(format[specifierIdx2])
                {
                    case 'd':
                        aLongIntPtr = (long *)aInt;
                        break;
                    case 'u':
                        aUnsignedLongPtr = (unsigned long *)aInt;
                        break;
                    case 'l':
                        expandingLL = true;
                        break;
                        
                }
            }
            else if ( expandingZ )
            {
                switch(format[specifierIdx2])
                {
                    case 'u':
#if __LP64__
                        aInt[1] = va_arg(vargs, int);
                        aUnsignedLongPtr = (unsigned long *)aInt;
#else
#error platform not supported
#endif
                        break;
                    case 'd':
#if __LP64__
                        aInt[1] = va_arg(vargs, int);
                        aUnsignedIntPtr = (unsigned int *)aInt;
#else
#error platform not supported
#endif
//#if __LP64__ || (TARGET_OS_EMBEDDED && !TARGET_OS_IPHONE) || TARGET_OS_WIN32 || NS_BUILD_32_LIKE_64
//                        typedef long NSInteger;
//                        typedef unsigned long NSUInteger;
//#else
//                        typedef int NSInteger;
//                        typedef unsigned int NSUInteger;
//#endif
                    default:
                        ymerr("string: warning: invalid z identifier in '%s'",format);
                        goto catch_fail;
                }
            }
        }
        else
        {
            ymerr("string: warning: incomplete 2-identifier in '%s'",format);
            goto catch_fail;
        }
        
        if ( specifierIdx2 < formatLen - 2 )
        {
            size_t specifierIdx3 = specifierIdx2 + 1;
            switch(format[specifierIdx3])
            {
                case 'd':
                    aLongLongPtr = (long long *)&aInt[0];
                    break;
                case 'u':
                    aUnsignedLongLongPtr = (unsigned long long *)&aInt[0];
                    break;
                default:
                    ymerr("string: warning: invalid 3-identifier in '%s'",format);
                    goto catch_fail;
            }
        }
        else
        {
            ymerr("string: warning: incomplete 3-identifier in '%s'",format);
            goto catch_fail;
        }
        
        currentTokenPtr = strstr(currentTokenPtr+1, "%");
    } // while token iter
    
    va_end(vargs);
    
catch_fail:
    va_end(vargs);
    return NULL;
    
#endif
}
#endif

YMStringRef YMStringCreateByAppendingString(YMStringRef base, YMStringRef append)
{
    const char* cBase = YMStringGetCString(base);
    const char *cAppend = YMStringGetCString(append);
    
    size_t baseLen = strlen(cBase);
    size_t appendLen = strlen(cAppend);
    size_t newStringLen = baseLen + appendLen + 1;
    char *newString = (char *)YMALLOC(newStringLen);
    memcpy(newString, cBase, baseLen);
    memcpy(newString + baseLen, cAppend, appendLen);
    newString[newStringLen - 1] = '\0';
    
    return YMStringCreateWithCString(newString);
}

void _YMStringFree(YMTypeRef object)
{
    __YMStringRef string = (__YMStringRef)object;
    free((void *)string->cString);
    return;
}

size_t YMStringGetLength(YMStringRef string_)
{
    __YMStringRef string = (__YMStringRef)string_;
    return string->length;
}

const char *YMStringGetCString(YMStringRef string_)
{
    __YMStringRef string = (__YMStringRef)string_;
    return string->cString;
}

bool YMStringEquals(YMStringRef stringA, YMStringRef stringB)
{
    return ( 0 == strcmp(YMSTR(stringA),YMSTR(stringB)) );
}
