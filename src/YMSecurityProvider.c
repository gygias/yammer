//
//  YMSecurityProvider.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMSecurityProvider.h"
#include "YMPrivate.h"

typedef bool (*ym_security_init_func)(int,int);
typedef bool (*ym_security_read_func)(int,int,uint8_t*,size_t);
typedef bool (*ym_security_write_func)(int,int,const uint8_t*,size_t);
typedef bool (*ym_security_close_func)(int,int);

typedef struct __YMSecurityProvider
{
    YMTypeID _typeID;
    
    int inFd;
    int outFd;
    ym_security_init_func   initFunc;
    ym_security_read_func   readFunc;
    ym_security_write_func  writeFunc;
    ym_security_close_func  closeFunc;
} _YMSecurityProvider;

bool YMNoSecurityInit(int,int);
bool YMNoSecurityRead(int,int,uint8_t*,size_t);
bool YMNoSecurityWrite(int,int,const uint8_t*,size_t);
bool YMNoSecurityClose(int,int);

YMSecurityProviderRef YMSecurityProviderCreateWithFullDuplexFile(int fd)
{
    return YMSecurityProviderCreate(fd, fd);
}

YMSecurityProviderRef YMSecurityProviderCreate(int inFd, int outFd)
{
    _YMSecurityProvider *provider = (_YMSecurityProvider *)calloc(1,sizeof(_YMSecurityProvider));
    provider->_typeID = _YMSecurityProviderTypeID;
    provider->initFunc = YMNoSecurityInit;
    provider->readFunc = YMNoSecurityRead;
    provider->writeFunc = YMNoSecurityWrite;
    provider->closeFunc = YMNoSecurityClose;
    provider->inFd = inFd;
    provider->outFd = outFd;
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
    return provider->initFunc(provider->inFd, provider->outFd);
}

bool YMSecurityProviderRead(YMSecurityProviderRef provider, uint8_t *buffer, size_t bytes)
{
    return provider->readFunc(provider->inFd, provider->outFd, buffer, bytes);
}

bool YMSecurityProviderWrite(YMSecurityProviderRef provider, const uint8_t *buffer, size_t bytes)
{
    return provider->writeFunc(provider->inFd, provider->outFd, buffer, bytes);
}

bool YMSecurityProviderClose(YMSecurityProviderRef provider)
{
    return provider->closeFunc(provider->inFd, provider->outFd);
}

// passthrough
bool YMNoSecurityInit(int inFd, int outFd)
{
    return true;
}

bool YMNoSecurityRead(int inFd, int outFd, uint8_t *buffer, size_t bytes)
{
    return YMReadFull(outFd, buffer, bytes);
}

bool YMNoSecurityWrite(int inFd, int outFd, const uint8_t *buffer, size_t bytes)
{
    return YMWriteFull(inFd, buffer, bytes);
}

bool YMNoSecurityClose(int inFd, int outFd)
{
    return true;
}
