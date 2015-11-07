//
//  YMSecurityProvider.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMSecurityProvider.h"
#include "YMPrivate.h"

#include "YMUtilities.h"

typedef bool (*ym_security_init_func)(int,int);
typedef bool (*ym_security_read_func)(int,int,uint8_t*,size_t);
typedef bool (*ym_security_write_func)(int,int,const uint8_t*,size_t);
typedef bool (*ym_security_close_func)(int,int);

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

bool YMNoSecurityInit(int,int);
bool YMNoSecurityRead(int,int,uint8_t*,size_t);
bool YMNoSecurityWrite(int,int,const uint8_t*,size_t);
bool YMNoSecurityClose(int,int);

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
    return provider->initFunc(provider->readFile, provider->writeFile);
}

bool YMSecurityProviderRead(YMSecurityProviderRef provider, uint8_t *buffer, size_t bytes)
{
    return provider->readFunc(provider->readFile, provider->writeFile, buffer, bytes);
}

bool YMSecurityProviderWrite(YMSecurityProviderRef provider, const uint8_t *buffer, size_t bytes)
{
    return provider->writeFunc(provider->readFile, provider->writeFile, buffer, bytes);
}

bool YMSecurityProviderClose(YMSecurityProviderRef provider)
{
    return provider->closeFunc(provider->readFile, provider->writeFile);
}

// passthrough
bool YMNoSecurityInit(__unused int inFd, __unused int outFd)
{
    return true;
}

bool YMNoSecurityRead(int readFile, __unused int writeFile, uint8_t *buffer, size_t bytes)
{
    return YMReadFull(readFile, buffer, bytes);
}

bool YMNoSecurityWrite(__unused int readFile, int writeFile, const uint8_t *buffer, size_t bytes)
{
    return YMWriteFull(writeFile, buffer, bytes);
}

bool YMNoSecurityClose(__unused  int inFd, __unused int outFd)
{
    return true;
}
