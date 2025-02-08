//
//  YMThread.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMThread_h
#define YMThread_h

#include "YMBase.h"
#include "YMString.h"
#include "YMStream.h"

YM_EXTERN_C_PUSH

typedef const struct __ym_thread * YMThreadRef;

typedef void (*ym_void_voidp_func)(void *);
typedef void *(*ym_voidp_voidp_func)(void *);

#if !defined(YMWIN32)
# define YM_THREAD_RETURN void *
# define YM_CALLING_CONVENTION
# define YM_THREAD_PARAM void *
# define YM_THREAD_END return NULL;
#else
//typedef DWORD(WINAPI *PTHREAD_START_ROUTINE)(
//	LPVOID lpThreadParameter
//	);
# define YM_THREAD_RETURN DWORD
# define YM_CALLING_CONVENTION WINAPI
# define YM_THREAD_PARAM LPVOID
# define YM_THREAD_END return 0;
#endif

typedef YM_THREAD_RETURN(YM_CALLING_CONVENTION *ym_thread_entry)(YM_THREAD_PARAM);

YMThreadRef YMAPI YMThreadCreate(YMStringRef name, ym_thread_entry entryPoint, void *context);

bool YMAPI YMThreadStart(YMThreadRef thread);
bool YMAPI YMThreadJoin(YMThreadRef thread);

YM_EXTERN_C_POP

#endif /* YMThread_h */
