//
//  YMSecurityProvider.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMSecurityProvider.h"
#include "YMPrivate.h"

typedef bool (*ym_security_init_func)(YMSecurityProviderRef);
typedef bool (*ym_security_read_func)(int,const uint8_t*,size_t);
typedef bool (*ym_security_write_func)(int,const uint8_t*,size_t);
typedef bool (*ym_security_close_func)(YMSecurityProviderRef);

typedef struct __YMSecurityProvider
{
    YMTypeID _typeID;
    
    int fd;
    ym_security_init_func   initFunc;
    ym_security_read_func   readFunc;
    ym_security_write_func  writeFunc;
    ym_security_close_func  closeFunc;
} _YMSecurityProvider;

bool YMNoSecurityInit(YMSecurityProviderRef);
bool YMNoSecurityRead(int fd, const uint8_t*, size_t);
bool YMNoSecurityWrite(int fd, const uint8_t*, size_t);
bool YMNoSecurityClose(YMSecurityProviderRef provider);

YMSecurityProviderRef YMSecurityProviderCreate(int fd)
{
    _YMSecurityProvider *provider = (_YMSecurityProvider *)calloc(1,sizeof(_YMSecurityProvider));
    provider->_typeID = _YMSecurityProviderTypeID;
    provider->initFunc = YMNoSecurityInit;
    provider->readFunc = YMNoSecurityRead;
    provider->writeFunc = YMNoSecurityWrite;
    provider->closeFunc = YMNoSecurityClose;
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

void YMSecurityProviderClose(YMSecurityProviderRef provider)
{
    
}

// passthrough
bool YMNoSecurityInit(YMSecurityProviderRef provider)
{
    return true;
}

bool YMNoSecurityRead(int fd, const uint8_t *buffer, size_t bytes)
{
    return YMRead(fd, buffer, bytes);
}

bool YMNoSecurityWrite(int fd, const uint8_t *buffer, size_t bytes)
{
    return YMWrite(fd, buffer, bytes);
}

bool YMNoSecurityClose(YMSecurityProviderRef provider)
{
    return true;
}
