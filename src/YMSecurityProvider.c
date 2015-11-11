//
//  YMSecurityProvider.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMSecurityProvider.h"
#include "YMSecurityProviderVeryPriv.h"
#include "YMPrivate.h"

#include "YMUtilities.h"

#include "YMLog.h"
#undef ymlog_type
#define ymlog_type YMLogSecurity
#if ( ymlog_type > ymlog_target )
#undef ymlog
#define ymlog(x,...) ;
#endif

bool YMNoSecurityInit(YMSecurityProviderRef provider);
bool YMNoSecurityRead(YMSecurityProviderRef provider,uint8_t*,size_t);
bool YMNoSecurityWrite(YMSecurityProviderRef provider,const uint8_t*,size_t);
bool YMNoSecurityClose(YMSecurityProviderRef provider);

YMSecurityProviderRef YMSecurityProviderCreateWithFullDuplexFile(int fd)
{
    return YMSecurityProviderCreate(fd, fd);
}

YMSecurityProviderRef YMSecurityProviderCreate(int readFile, int writeFile)
{    
    _YMSecurityProvider *provider = (_YMSecurityProvider *)calloc(1,sizeof(_YMSecurityProvider));
    provider->_typeID = _YMSecurityProviderTypeID;
    provider->initFunc = YMNoSecurityInit;
    provider->readFunc = YMNoSecurityRead;
    provider->writeFunc = YMNoSecurityWrite;
    provider->closeFunc = YMNoSecurityClose;
    provider->readFile = readFile;
    provider->writeFile = writeFile;
    return provider;
}

void _YMSecurityProviderFree(YMSecurityProviderRef provider)
{
    free(provider);
}

void YMSecurityProviderSetInitFunc(YMSecurityProviderRef provider, ym_security_init_func func)
{
    provider->initFunc = func;
}

bool YMSecurityProviderInit(YMSecurityProviderRef provider)
{
    return provider->initFunc(provider);
}

bool YMSecurityProviderRead(YMSecurityProviderRef provider, uint8_t *buffer, size_t bytes)
{
    return provider->readFunc(provider, buffer, bytes);
}

bool YMSecurityProviderWrite(YMSecurityProviderRef provider, const uint8_t *buffer, size_t bytes)
{
    return provider->writeFunc(provider, buffer, bytes);
}

bool YMSecurityProviderClose(YMSecurityProviderRef provider)
{
    return provider->closeFunc(provider);
}

// passthrough
bool YMNoSecurityInit(__unused YMSecurityProviderRef provider)
{
    return true;
}

bool YMNoSecurityRead(YMSecurityProviderRef provider, uint8_t *buffer, size_t bytes)
{
    return YMReadFull(provider->readFile, buffer, bytes);
}

bool YMNoSecurityWrite(YMSecurityProviderRef provider, const uint8_t *buffer, size_t bytes)
{
    return YMWriteFull(provider->writeFile, buffer, bytes);
}

bool YMNoSecurityClose(__unused YMSecurityProviderRef provider)
{
    return true;
}
