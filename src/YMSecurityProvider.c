//
//  YMSecurityProvider.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMSecurityProvider.h"
#include "YMPrivate.h"

typedef bool (*ym_security_init_func)(int);
typedef bool (*ym_security_read_func)(int,uint8_t*,size_t);
typedef bool (*ym_security_write_func)(int,const uint8_t*,size_t);
typedef bool (*ym_security_close_func)(int);

typedef struct __YMSecurityProvider
{
    YMTypeID _typeID;
    
    int fd;
    ym_security_init_func   initFunc;
    ym_security_read_func   readFunc;
    ym_security_write_func  writeFunc;
    ym_security_close_func  closeFunc;
} _YMSecurityProvider;

bool YMNoSecurityInit(int);
bool YMNoSecurityRead(int, uint8_t*, size_t);
bool YMNoSecurityWrite(int, const uint8_t*, size_t);
bool YMNoSecurityClose(int);

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

bool YMSecurityProviderInit(YMSecurityProviderRef provider)
{
#warning note we're not casting providerRef here... fix everywhere else
    return provider->initFunc(provider->fd);
}

bool YMSecurityProviderRead(YMSecurityProviderRef provider, uint8_t *buffer, size_t bytes)
{
    return provider->readFunc(provider->fd,buffer,bytes);
}

bool YMSecurityProviderWrite(YMSecurityProviderRef provider, const uint8_t *buffer, size_t bytes)
{
    return provider->writeFunc(provider->fd,buffer,bytes);
}

bool YMSecurityProviderClose(YMSecurityProviderRef provider)
{
    return provider->closeFunc(provider->fd);
}

// passthrough
bool YMNoSecurityInit(int fd)
{
    return true;
}

bool YMNoSecurityRead(int fd, uint8_t *buffer, size_t bytes)
{
    return YMReadFull(fd, buffer, bytes);
}

bool YMNoSecurityWrite(int fd, const uint8_t *buffer, size_t bytes)
{
    return YMWriteFull(fd, buffer, bytes);
}

bool YMNoSecurityClose(int fd)
{
    return true;
}
