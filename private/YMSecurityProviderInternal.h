//
//  YMSecurityProviderInternal.h
//  yammer
//
//  Created by david on 11/10/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMSecurityProviderInternal_h
#define YMSecurityProviderInternal_h

YM_EXTERN_C_PUSH

typedef struct __ym_security_provider __ym_security_provider_t;

typedef bool (*ym_security_init_func)(__ym_security_provider_t *);
typedef bool (*ym_security_read_func)(__ym_security_provider_t *,uint8_t*,size_t);
typedef bool (*ym_security_write_func)(__ym_security_provider_t *,const uint8_t*,size_t);
typedef bool (*ym_security_close_func)(__ym_security_provider_t *);

typedef struct __ym_security_provider
{
    _YMType _typeID;
    
	YMSOCKET socket;
    ym_security_init_func   initFunc;
    ym_security_read_func   readFunc;
    ym_security_write_func  writeFunc;
    ym_security_close_func  closeFunc;
} __ym_security_provider;

YM_EXTERN_C_POP

#endif /* YMSecurityProviderInternal_h */
