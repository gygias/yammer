// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once

#include "targetver.h"
#include <windows.h>

#define YM_TOKEN_STR(x) #x
#define YM_STR_TOKEN(x,y) x ## y

#define YM_CALL(func, type) { void func(type); YM_FUNC_NAME(func)(); }
#define YM_CALL_V(func, firstType, first, ...) func(first,__VA_ARGS__) // iso c requires 'first'
#define YM_CALL_DV(func, firstType, first, ...) { void func(firstType arg1,...); func(first,__VA_ARGS__); } // iso c requires 'first'

//#define STR(x) YM_TOKEN_STR(x)
//#pragma message STR(__FILE__) ": example: " STR(example)

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#ifndef WIN32
#include <stdbool.h>
#include <unistd.h>
#else
#define uint8_t unsigned char
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
