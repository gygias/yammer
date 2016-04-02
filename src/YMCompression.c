//
//  YMCompression.c
//  yammer
//
//  Created by david on 4/1/16.
//  Copyright © 2016 combobulated. All rights reserved.
//

#include "YMCompression.h"

#include "YMUtilities.h"

#define ymlog_type YMLogCompression
#include "YMLog.h"

YM_EXTERN_C_PUSH

typedef struct __ym_compression_t *__YMCompressionRef;

typedef bool (*ym_compression_init_func)(__YMCompressionRef compression);
typedef bool (*ym_compression_read_func)(__YMCompressionRef compression,uint8_t*,size_t);
typedef bool (*ym_compression_write_func)(__YMCompressionRef compression,const uint8_t*,size_t);
typedef bool (*ym_compression_close_func)(__YMCompressionRef compression);

typedef struct __ym_compression_t
{
    _YMType _typeID;
    
    YMFILE file;
    YMCompressionType type;
    ym_compression_init_func   initFunc;
    ym_compression_read_func   readFunc;
    ym_compression_write_func  writeFunc;
    ym_compression_close_func  closeFunc;
} __ym_compression_t;

bool YMNoCompressionInit(__YMCompressionRef compression);
bool YMNoCompressionRead(__YMCompressionRef compression,uint8_t*,size_t);
bool YMNoCompressionWrite(__YMCompressionRef compression,const uint8_t*,size_t);
bool YMNoCompressionClose(__YMCompressionRef compression);

bool YMGZInit(__YMCompressionRef compression);
bool YMGZRead(__YMCompressionRef compression,uint8_t*,size_t);
bool YMGZWrite(__YMCompressionRef compression,const uint8_t*,size_t);
bool YMGZClose(__YMCompressionRef compression);

bool YMBZInit(__YMCompressionRef compression);
bool YMBZRead(__YMCompressionRef compression,uint8_t*,size_t);
bool YMBZWrite(__YMCompressionRef compression,const uint8_t*,size_t);
bool YMBZClose(__YMCompressionRef compression);

bool YMLZInit(__YMCompressionRef compression);
bool YMLZRead(__YMCompressionRef compression,uint8_t*,size_t);
bool YMLZWrite(__YMCompressionRef compression,const uint8_t*,size_t);
bool YMLZClose(__YMCompressionRef compression);

YMCompressionRef YMCompressionCreate(YMCompressionType type, YMFILE file)
{
    __YMCompressionRef compression = (__YMCompressionRef)_YMAlloc(_YMCompressionTypeID,sizeof(struct __ym_compression_t));
    
    switch(type) {
        case YMCompressionGZ:
            compression->initFunc = YMGZInit;
            compression->readFunc = YMGZRead;
            compression->writeFunc = YMGZWrite;
            compression->closeFunc = YMGZClose;
            break;
        case YMCompressionBZ2:
            compression->initFunc = YMBZInit;
            compression->readFunc = YMBZRead;
            compression->writeFunc = YMBZWrite;
            compression->closeFunc = YMBZClose;
            break;
        case YMCompressionLZ:
            compression->initFunc = YMLZInit;
            compression->readFunc = YMLZRead;
            compression->writeFunc = YMLZWrite;
            compression->closeFunc = YMLZClose;
            break;
        default:
            compression->initFunc = YMNoCompressionInit;
            compression->readFunc = YMNoCompressionRead;
            compression->writeFunc = YMNoCompressionWrite;
            compression->closeFunc = YMNoCompressionClose;
            break;
    }
    
    compression->file = file;
    compression->type = type;
    
    return compression;
}

void _YMCompressionFree(YMCompressionRef compression_)
{
    __unused __YMCompressionRef provider = (__YMCompressionRef)compression_;
}

void YMCompressionSetInitFunc(YMCompressionRef provider_, ym_compression_init_func func)
{
    __YMCompressionRef provider = (__YMCompressionRef)provider_;
    provider->initFunc = func;
}

bool YMCompressionInit(YMCompressionRef provider_)
{
    __YMCompressionRef provider = (__YMCompressionRef)provider_;
    return provider->initFunc(provider);
}

bool YMCompressionRead(YMCompressionRef compression_, uint8_t *buffer, size_t bytes)
{
    __YMCompressionRef compression = (__YMCompressionRef)compression_;
    return compression->readFunc(compression, buffer, bytes);
}

bool YMCompressionWrite(YMCompressionRef compression_, const uint8_t *buffer, size_t bytes)
{
    __YMCompressionRef compression = (__YMCompressionRef)compression_;
    return compression->writeFunc(compression, buffer, bytes);
}

bool YMCompressionClose(YMCompressionRef compression_)
{
    __YMCompressionRef compression = (__YMCompressionRef)compression_;
    return compression->closeFunc(compression);
}

// passthrough
bool YMNoCompressionInit(__unused __YMCompressionRef compression)
{
    return true;
}

bool YMNoCompressionRead(__YMCompressionRef compression, uint8_t *buffer, size_t bytes)
{
    YM_IO_BOILERPLATE
    YM_READ_FILE(compression->file, buffer, bytes);
    return ( (size_t)aRead == bytes );
}

bool YMNoCompressionWrite(__YMCompressionRef compression, const uint8_t *buffer, size_t bytes)
{
    YM_IO_BOILERPLATE
    YM_WRITE_FILE(compression->file, buffer, bytes);
    return ( (size_t)aWrite == bytes );
}

bool YMNoCompressionClose(__unused __YMCompressionRef compression_)
{
    return true;
}

bool YMGZInit(__YMCompressionRef c)
{
    c = NULL;
    return false;
}

bool YMGZRead(__YMCompressionRef c, uint8_t *b,size_t l)
{
    c = NULL; b = NULL; l = 0;
    return false;
}

bool YMGZWrite(__YMCompressionRef c, const uint8_t *b,size_t l)
{
    c = NULL; b = NULL; l = 0;
    return false;
}

bool YMGZClose(__YMCompressionRef c)
{
    c = NULL;
    return false;
}

bool YMBZInit(__YMCompressionRef c)
{
    c = NULL;
    return false;
}

bool YMBZRead(__YMCompressionRef c, uint8_t *b, size_t l)
{
    c = NULL; b = NULL; l = 0;
    return false;
}

bool YMBZWrite(__YMCompressionRef c, const uint8_t *b, size_t l)
{
    c = NULL; b = NULL; l = 0;
    return false;
}

bool YMBZClose(__YMCompressionRef c)
{
    c = NULL;
    return false;
}

bool YMLZInit(__YMCompressionRef c)
{
    c = NULL;
    return false;
}

bool YMLZRead(__YMCompressionRef c, uint8_t *b, size_t l)
{
    c = NULL; b = NULL; l = 0;
    return false;
}

bool YMLZWrite(__YMCompressionRef c,const uint8_t *b,size_t l)
{
    c = NULL; b = NULL; l = 0;
    return false;
}

bool YMLZClose(__YMCompressionRef c)
{
    c = NULL;
    return false;
}

YM_EXTERN_C_POP
