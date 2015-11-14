//
//  YMSecurityProvider.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMSecurityProvider.h"
#include "YMSecurityProviderVeryPriv.h"

#include "YMUtilities.h"

#include "YMLog.h"
#undef ymlog_type
#define ymlog_type YMLogSecurity
#if ( ymlog_type > ymlog_target )
#undef ymlog
#define ymlog(x,...) ;
#endif

bool YMNoSecurityInit(__YMSecurityProviderRef provider);
bool YMNoSecurityRead(__YMSecurityProviderRef provider,uint8_t*,size_t);
bool YMNoSecurityWrite(__YMSecurityProviderRef provider,const uint8_t*,size_t);
bool YMNoSecurityClose(__YMSecurityProviderRef provider);

YMSecurityProviderRef YMSecurityProviderCreateWithFullDuplexFile(int fd)
{
    return YMSecurityProviderCreate(fd, fd);
}

YMSecurityProviderRef YMSecurityProviderCreate(int readFile, int writeFile)
{    
    __YMSecurityProviderRef provider = (__YMSecurityProviderRef)_YMAlloc(_YMSecurityProviderTypeID,sizeof(__YMSecurityProvider));
    provider->initFunc = YMNoSecurityInit;
    
    provider->readFunc = YMNoSecurityRead;
    provider->writeFunc = YMNoSecurityWrite;
    provider->closeFunc = YMNoSecurityClose;
    provider->readFile = readFile;
    provider->writeFile = writeFile;
    return provider;
}

void _YMSecurityProviderFree(YMSecurityProviderRef provider_)
{
    __YMSecurityProviderRef provider = (__YMSecurityProviderRef)provider_;
    free(provider);
}

void YMSecurityProviderSetInitFunc(YMSecurityProviderRef provider_, ym_security_init_func func)
{
    __YMSecurityProviderRef provider = (__YMSecurityProviderRef)provider_;
    provider->initFunc = func;
}

bool YMSecurityProviderInit(YMSecurityProviderRef provider_)
{
    __YMSecurityProviderRef provider = (__YMSecurityProviderRef)provider_;
    return provider->initFunc(provider);
}

bool YMSecurityProviderRead(YMSecurityProviderRef provider_, uint8_t *buffer, size_t bytes)
{
    __YMSecurityProviderRef provider = (__YMSecurityProviderRef)provider_;
    return provider->readFunc(provider, buffer, bytes);
}

bool YMSecurityProviderWrite(YMSecurityProviderRef provider_, const uint8_t *buffer, size_t bytes)
{
    __YMSecurityProviderRef provider = (__YMSecurityProviderRef)provider_;
    return provider->writeFunc(provider, buffer, bytes);
}

bool YMSecurityProviderClose(YMSecurityProviderRef provider_)
{
    __YMSecurityProviderRef provider = (__YMSecurityProviderRef)provider_;
    return provider->closeFunc(provider);
}

// passthrough
bool YMNoSecurityInit(__unused __YMSecurityProviderRef provider)
{
    return true;
}

bool YMNoSecurityRead(__YMSecurityProviderRef provider, uint8_t *buffer, size_t bytes)
{
    return YMReadFull(provider->readFile, buffer, bytes, NULL);
}

bool YMNoSecurityWrite(__YMSecurityProviderRef provider, const uint8_t *buffer, size_t bytes)
{
    return YMWriteFull(provider->writeFile, buffer, bytes, NULL);
}

bool YMNoSecurityClose(__unused __YMSecurityProviderRef provider)
{
    return true;
}
