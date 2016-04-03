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

#if defined(YMAPPLE)
#include <zlib.h>
#include <bzlib.h>
#else
// compression for win32, linux
#endif

YM_EXTERN_C_PUSH

typedef struct __ym_compression_t *__YMCompressionRef;

typedef bool (*ym_compression_init_func)(__YMCompressionRef compression);
typedef YMIOResult (*ym_compression_read_func)(__YMCompressionRef compression,uint8_t*,size_t,size_t*);
typedef YMIOResult (*ym_compression_write_func)(__YMCompressionRef compression,const uint8_t*,size_t,size_t*);
typedef bool (*ym_compression_close_func)(__YMCompressionRef compression);

typedef struct __ym_compression_t
{
    _YMType _typeID;
    
    YMFILE file;
    YMCompressionType type;
    bool input;
    ym_compression_init_func   initFunc;
    ym_compression_read_func   readFunc;
    ym_compression_write_func  writeFunc;
    ym_compression_close_func  closeFunc;
#if defined(YMAPPLE)
    gzFile gzfile;
    BZFILE * bzfile;
#endif
} __ym_compression_t;

bool YMNoCompressionInit(__YMCompressionRef compression);
YMIOResult YMNoCompressionRead(__YMCompressionRef compression,uint8_t*,size_t,size_t*);
YMIOResult YMNoCompressionWrite(__YMCompressionRef compression,const uint8_t*,size_t,size_t*);
bool YMNoCompressionClose(__YMCompressionRef compression);

bool YMGZInit(__YMCompressionRef compression);
YMIOResult YMGZRead(__YMCompressionRef compression,uint8_t*,size_t,size_t*);
YMIOResult YMGZWrite(__YMCompressionRef compression,const uint8_t*,size_t,size_t*);
bool YMGZClose(__YMCompressionRef compression);

bool YMBZInit(__YMCompressionRef compression);
YMIOResult YMBZRead(__YMCompressionRef compression,uint8_t*,size_t,size_t*);
YMIOResult YMBZWrite(__YMCompressionRef compression,const uint8_t*,size_t,size_t*);
bool YMBZClose(__YMCompressionRef compression);

bool YMLZInit(__YMCompressionRef compression);
YMIOResult YMLZRead(__YMCompressionRef compression,uint8_t*,size_t,size_t*);
YMIOResult YMLZWrite(__YMCompressionRef compression,const uint8_t*,size_t,size_t*);
bool YMLZClose(__YMCompressionRef compression);

YMCompressionRef YMCompressionCreate(YMCompressionType type, YMFILE file, bool input)
{
    __YMCompressionRef compression = (__YMCompressionRef)_YMAlloc(_YMCompressionTypeID,sizeof(struct __ym_compression_t));
    
    switch(type) {
#if defined(YMAPPLE)
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
#endif
        default:
            compression->initFunc = YMNoCompressionInit;
            compression->readFunc = YMNoCompressionRead;
            compression->writeFunc = YMNoCompressionWrite;
            compression->closeFunc = YMNoCompressionClose;
            break;
    }
    
    compression->file = file;
    compression->type = type;
    compression->input = input;
    
#if defined(YMAPPLE)
    compression->gzfile = NULL;
    compression->bzfile = NULL;
#endif
    return compression;
}

void _YMCompressionFree(YMCompressionRef c_)
{
    __unused __YMCompressionRef c = (__YMCompressionRef)c_;
}

void YMCompressionSetInitFunc(YMCompressionRef c_, ym_compression_init_func func)
{
    __YMCompressionRef c = (__YMCompressionRef)c_;
    c->initFunc = func;
}

bool YMCompressionInit(YMCompressionRef c_)
{
    __YMCompressionRef c = (__YMCompressionRef)c_;
    return c->initFunc(c);
}

YMIOResult YMCompressionRead(YMCompressionRef c_, uint8_t *b, size_t l, size_t *o)
{
    __YMCompressionRef c = (__YMCompressionRef)c_;
    return c->readFunc(c, b, l, o);
}

YMIOResult YMCompressionWrite(YMCompressionRef c_, const uint8_t *b, size_t l, size_t *o)
{
    __YMCompressionRef c = (__YMCompressionRef)c_;
    return c->writeFunc(c, b, l, o);
}

bool YMCompressionClose(YMCompressionRef c_)
{
    __YMCompressionRef c = (__YMCompressionRef)c_;
    return c->closeFunc(c);
}

// passthrough
bool YMNoCompressionInit(__unused __YMCompressionRef c)
{
    return true;
}

YMIOResult YMNoCompressionRead(__YMCompressionRef c, uint8_t *b, size_t l, size_t *o)
{
    YM_IO_BOILERPLATE
    YM_READ_FILE(c->file, b, l);
    if ( o )
        *o = aRead;
    if ( aRead == -1 )
        return YMIOError;
    else if ( aRead == 0 )
        return YMIOEOF;
    return YMIOSuccess;
}

YMIOResult YMNoCompressionWrite(__YMCompressionRef c, const uint8_t *b, size_t l, size_t *o)
{
    YM_IO_BOILERPLATE
    YM_WRITE_FILE(c->file, b, l);
    if ( o )
        *o = aWrite;
    if ( aWrite != (ssize_t)l )
        return YMIOError;
    return YMIOSuccess;
}

bool YMNoCompressionClose(__unused __YMCompressionRef c)
{
  int result = 0;
#if defined(YMLINUX)
  result = close(c->file);
#endif
  return ( result == 0 );
}

#if defined(YMAPPLE)
bool YMGZInit(__YMCompressionRef c)
{
    const char *mode = "r";
    if ( c->input )
        mode = "w";
    c->gzfile = gzdopen(c->file, mode);
    if ( ! c->gzfile ) {
        ymerr("gzdopen: %d %s: %d (%s)",c->file,mode,errno,strerror(errno));
        return false;
    }
    return true;
}

YMIOResult YMGZRead(__YMCompressionRef c, uint8_t *b, size_t l, size_t *o)
{
    ymassert(!c->input,"reading from input compression stream");
    ymassert(c->gzfile,"gzfile is not initialized");
    ymassert(l<=UINT_MAX,"gzread(%zu) exceeds api limit",l);
    int result = gzread(c->gzfile, b, (unsigned)l);
    if ( o )
        *o = result;
    if ( result == -1 ) {
        ymerr("gzread: %d (%s)",errno,strerror(errno));
        return YMIOError;
    } else if ( result == 0 )
        return YMIOEOF;
    return YMIOSuccess;
}

YMIOResult YMGZWrite(__YMCompressionRef c, const uint8_t *b, size_t l, size_t *o)
{
    ymassert(c->input,"writing to output compression stream");
    ymassert(c->gzfile,"gzfile is not initialized");
    ymassert(l<=UINT_MAX,"gzwrite(%zu) exceeds api limit",l);
    int result = gzwrite(c->gzfile, b, (unsigned)l);
    if ( o )
        *o = result;
    if ( result <= 0 ) {
        ymerr("gzwrite: %d (%s)",errno,strerror(errno));
        return YMIOError;
    }
    return YMIOSuccess;
}

bool YMGZClose(__YMCompressionRef c)
{
    ymassert(c->gzfile,"gzfile is not initialized");
    int result = gzclose(c->gzfile); // we go both ways, so _r _w variants buy nothing?
    if ( result != Z_OK ) {
        if ( result == Z_STREAM_ERROR )
            ymerr("gzclose: Z_STREAM_ERROR");
        else if ( result == Z_ERRNO )
            ymerr("gzclose: %d (%s)",errno,strerror(errno));
        else
            ymerr("gzclose: %d?",result);
        return false;
    }
    return true;
}

bool YMBZInit(__YMCompressionRef c)
{
    const char *mode = "r";
    if ( c->input )
        mode = "w";
    c->bzfile = BZ2_bzdopen(c->file, mode);
    if ( ! c->bzfile ) {
        ymerr("bzdopen: %d %s: %d (%s)",c->file,mode,errno,strerror(errno));
        return false;
    }
    return true;
}

YMIOResult YMBZRead(__YMCompressionRef c, uint8_t *b, size_t l, size_t *o)
{
    ymassert(!c->input,"reading from input compression stream");
    ymassert(c->bzfile,"bzfile is not initialized");
    ymassert(l<=INT_MAX,"bzread(%zu) exceeds api limit",l);
    int result = BZ2_bzread(c->bzfile, b, (int)l);
    if ( o )
        *o = result;
    if ( result == -1 ) {
        ymerr("bzread: %d (%s)",errno,strerror(errno));
        return YMIOError;
    } else if ( result == 0 )
        return YMIOEOF;
    return YMIOSuccess;
}

YMIOResult YMBZWrite(__YMCompressionRef c, const uint8_t *b, size_t l, size_t *o)
{
    ymassert(c->input,"writing to output compression stream");
    ymassert(c->bzfile,"bzfile is not initialized");
    ymassert(l<=INT_MAX,"bzwrite(%zu) exceeds api limit",l);
    int result = BZ2_bzwrite(c->bzfile, (void *)b, (int)l);
    if ( o )
        *o = result;
    if ( result <= 0 ) {
        ymerr("bzwrite: %d (%s)",errno,strerror(errno));
        return YMIOError;
    }
    return YMIOSuccess;
}

bool YMBZClose(__YMCompressionRef c)
{
    ymassert(c->bzfile,"bzfile is not initialized");
    // todo need to flush before close? find updated manual
    BZ2_bzclose(c->bzfile);
    return true;
}

bool YMLZInit(__YMCompressionRef c)
{
    c = NULL;
    ymabort("someone set up is the \"expert\"");
    return false;
}

YMIOResult YMLZRead(__YMCompressionRef c, uint8_t *b, size_t l, size_t *o)
{
    c = NULL; b = NULL; l = 0; o = NULL;
    ymabort("someone set up is the \"expert\"");
    return false;
}

YMIOResult YMLZWrite(__YMCompressionRef c, const uint8_t *b, size_t l, size_t *o)
{
    c = NULL; b = NULL; l = 0; o = NULL;
    ymabort("someone set up is the \"expert\"");
    return false;
}

bool YMLZClose(__YMCompressionRef c)
{
    c = NULL;
    ymabort("someone set up is the \"expert\"");
    return false;
}
#endif

YM_EXTERN_C_POP
