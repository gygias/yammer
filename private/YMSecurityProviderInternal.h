//
//  YMSecurityProviderInternal.h
//  yammer
//
//  Created by david on 11/10/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMSecurityProviderInternal_h
#define YMSecurityProviderInternal_h

typedef struct __ym_security_provider_t *__YMSecurityProviderRef;

typedef bool (*ym_security_init_func)(__YMSecurityProviderRef provider);
typedef bool (*ym_security_read_func)(__YMSecurityProviderRef provider,uint8_t*,size_t);
typedef bool (*ym_security_write_func)(__YMSecurityProviderRef provider,const uint8_t*,size_t);
typedef bool (*ym_security_close_func)(__YMSecurityProviderRef provider);

typedef struct __ym_security_provider_t
{
    _YMType _typeID;
    
	YMFILE readFile;
	YMFILE writeFile;
    ym_security_init_func   initFunc;
    ym_security_read_func   readFunc;
    ym_security_write_func  writeFunc;
    ym_security_close_func  closeFunc;
} __ym_security_provider_t;

#endif /* YMSecurityProviderInternal_h */
