//
//  YMSecurityProviderVeryPriv.h
//  yammer
//
//  Created by david on 11/10/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMSecurityProviderVeryPriv_h
#define YMSecurityProviderVeryPriv_h

typedef bool (*ym_security_init_func)(YMSecurityProviderRef provider);
typedef bool (*ym_security_read_func)(YMSecurityProviderRef provider,uint8_t*,size_t);
typedef bool (*ym_security_write_func)(YMSecurityProviderRef provider,const uint8_t*,size_t);
typedef bool (*ym_security_close_func)(YMSecurityProviderRef provider);

typedef struct __YMSecurityProvider
{
    YMTypeID _typeID;
    
    int readFile;
    int writeFile;
    ym_security_init_func   initFunc;
    ym_security_read_func   readFunc;
    ym_security_write_func  writeFunc;
    ym_security_close_func  closeFunc;
} _YMSecurityProvider;

#endif /* YMSecurityProviderVeryPriv_h */
