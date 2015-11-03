//
//  YMSecurityProvider.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 Combobulated Software. All rights reserved.
//

#include "YMSecurityProvider.h"
#include "YMPrivate.h"

typedef struct __YMSecurityProvider
{
    YMTypeID type;
    
    int fd;
    ym_security_init_func   initFunc;
    ym_read_func            readFunc;
    ym_write_func           writeFunc;
} _YMSecurityProvider;

bool YMNoSecurityInit(int);
bool YMNoSecurityRead(int, uint8_t*, size_t);
bool YMNoSecurityWrite(int, uint8_t*, size_t);

YMSecurityProviderRef YMSecurityProviderCreate(int fd)
{
    _YMSecurityProvider *provider = (_YMSecurityProvider *)calloc(1,sizeof(_YMSecurityProvider));
    provider->type = _YMSecurityProviderType;
    provider->initFunc = YMNoSecurityInit;
    provider->readFunc = YMRead;
    provider->writeFunc = YMWrite;
    provider->fd = fd;
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

void YMSecurityProviderRead(YMSecurityProviderRef provider, const void *buffer, size_t bytes)
{
    provider->writeFunc(provider->fd,buffer,bytes);
}

void YMSecurityProviderWrite(YMSecurityProviderRef provider, const void *buffer, size_t bytes)
{
    provider->readFunc(provider->fd,buffer,bytes);
}

// passthrough

bool YMNoSecurityInit(int fd)
{
    return true;
}
