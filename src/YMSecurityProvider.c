//
//  YMSecurityProvider.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMSecurityProvider.h"
#include "YMSecurityProviderInternal.h"

#include "YMUtilities.h"

#define ymlog_type YMLogSecurity
#include "YMLog.h"

YM_EXTERN_C_PUSH

bool YMNoSecurityInit(__YMSecurityProviderRef provider);
bool YMNoSecurityRead(__YMSecurityProviderRef provider,uint8_t*,size_t);
bool YMNoSecurityWrite(__YMSecurityProviderRef provider,const uint8_t*,size_t);
bool YMNoSecurityClose(__YMSecurityProviderRef provider);

YMSecurityProviderRef YMSecurityProviderCreateWithSocket(YMSOCKET socket)
{
    __YMSecurityProviderRef provider = (__YMSecurityProviderRef)_YMAlloc(_YMSecurityProviderTypeID,sizeof(struct __ym_security_provider_t));
    
    provider->socket = socket;
    provider->initFunc = YMNoSecurityInit;
    provider->readFunc = YMNoSecurityRead;
    provider->writeFunc = YMNoSecurityWrite;
    provider->closeFunc = YMNoSecurityClose;
    
    return provider;
}

void _YMSecurityProviderFree(YMSecurityProviderRef provider_)
{
    __unused __YMSecurityProviderRef provider = (__YMSecurityProviderRef)provider_;
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
    YM_IO_BOILERPLATE
    YM_READ_SOCKET(provider->socket, buffer, bytes);
    return ( (size_t)aRead == bytes );
}

bool YMNoSecurityWrite(__YMSecurityProviderRef provider, const uint8_t *buffer, size_t bytes)
{
    YM_IO_BOILERPLATE
    YM_WRITE_SOCKET(provider->socket, buffer, bytes);
    return ( (size_t)aWrite == bytes );
}

bool YMNoSecurityClose(__unused __YMSecurityProviderRef provider_)
{
    return true;
}

YM_EXTERN_C_POP
