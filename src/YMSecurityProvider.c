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

YMSecurityProviderRef YMSecurityProviderCreateWithSocket(YMSOCKET fd, bool closeWhenDone)
{
    return YMSecurityProviderCreate((YMFILE)fd, (YMFILE)fd, closeWhenDone);
}

YMSecurityProviderRef YMSecurityProviderCreate(YMFILE readFile, YMFILE writeFile, bool closeWhenDone)
{    
    __YMSecurityProviderRef provider = (__YMSecurityProviderRef)_YMAlloc(_YMSecurityProviderTypeID,sizeof(struct __ym_security_provider_t));
    provider->initFunc = YMNoSecurityInit;
    
    provider->readFunc = YMNoSecurityRead;
    provider->writeFunc = YMNoSecurityWrite;
    provider->closeFunc = YMNoSecurityClose;
    provider->readFile = readFile;
    provider->writeFile = writeFile;
    provider->closeWhenDone = closeWhenDone;
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
    return ( YMIOSuccess == YMReadFull(provider->readFile, buffer, bytes, NULL) );
}

bool YMNoSecurityWrite(__YMSecurityProviderRef provider, const uint8_t *buffer, size_t bytes)
{
    return ( YMIOSuccess == YMWriteFull(provider->writeFile, buffer, bytes, NULL) );
}

bool YMNoSecurityClose(__YMSecurityProviderRef provider_)
{
    __YMSecurityProviderRef provider = (__YMSecurityProviderRef)provider_;
    int result = 0, error = 0;
    const char *errorStr = NULL;
    if ( provider->closeWhenDone )
    {
        if ( provider->readFile != NULL_FILE )
            YM_CLOSE_FILE(provider->readFile);
        if ( provider->writeFile != NULL_FILE && provider->writeFile != provider->readFile )
            YM_CLOSE_FILE(provider->writeFile);
    }
    return ( result == 0 );
}

YM_EXTERN_C_POP
