//
//  YMSecurityProviderVeryPriv.h
//  yammer
//
//  Created by david on 11/10/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMSecurityProviderVeryPriv_h
#define YMSecurityProviderVeryPriv_h

typedef struct __ym_security_provider __YMSecurityProvider;
typedef __YMSecurityProvider *__YMSecurityProviderRef;

typedef bool (*ym_security_init_func)(__YMSecurityProviderRef provider);
typedef bool (*ym_security_read_func)(__YMSecurityProviderRef provider,uint8_t*,size_t);
typedef bool (*ym_security_write_func)(__YMSecurityProviderRef provider,const uint8_t*,size_t);
typedef bool (*ym_security_close_func)(__YMSecurityProviderRef provider);

typedef struct __ym_security_provider
{
    _YMType _typeID;
    
    int readFile;
    int writeFile;
    ym_security_init_func   initFunc;
    ym_security_read_func   readFunc;
    ym_security_write_func  writeFunc;
    ym_security_close_func  closeFunc;
} ___ym_security_provider;

#endif /* YMSecurityProviderVeryPriv_h */
