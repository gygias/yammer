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

#if !defined(YMWIN32)
#if !defined(YMAPPLE)
#  define __USE_GNU
#endif
# include <fcntl.h>
# include <zlib.h>
# include <bzlib.h>
# include <lz4.h>
# include <arpa/inet.h>
#else
// compression for win32, linux
#endif

YM_EXTERN_C_PUSH

typedef struct __ym_compression __ym_compression_t;

typedef bool (*ym_compression_init_func)(__ym_compression_t *);
typedef YMIOResult (*ym_compression_read_func)(__ym_compression_t *,uint8_t*,YM_COMPRESSION_LENGTH,YM_COMPRESSION_LENGTH*);
typedef YMIOResult (*ym_compression_write_func)(__ym_compression_t *,const uint8_t*,YM_COMPRESSION_LENGTH,YM_COMPRESSION_LENGTH*,YM_COMPRESSION_LENGTH*);
typedef bool (*ym_compression_close_func)(__ym_compression_t *);

typedef struct __ym_compression
{
    _YMType _typeID;
    
    YMFILE file;
    YMCompressionType type;
    bool compressing;
    ym_compression_init_func   initFunc;
    ym_compression_read_func   readFunc;
    ym_compression_write_func  writeFunc;
    ym_compression_close_func  closeFunc;

// per algo context
#if !defined(YMWIN32)
    gzFile gzfile;
    BZFILE * bzfile;
#endif

    uint64_t inBytes;
    uint64_t outBytes;
} __ym_compression;

#define _YM_COMPRESSION_ALGO(x) bool x##Init(__ym_compression_t *); \
                                YMIOResult x##Read(__ym_compression_t *,uint8_t*,YM_COMPRESSION_LENGTH,YM_COMPRESSION_LENGTH*); \
                                YMIOResult x##Write(__ym_compression_t *,const uint8_t*,YM_COMPRESSION_LENGTH,YM_COMPRESSION_LENGTH*,YM_COMPRESSION_LENGTH*); \
                                bool x##Close(__ym_compression_t *);
#define YM_COMPRESSION_ALGO(x)  _YM_COMPRESSION_ALGO(YM##x)

YM_COMPRESSION_ALGO(NoCompression)
YM_COMPRESSION_ALGO(GZ)
YM_COMPRESSION_ALGO(BZ)
YM_COMPRESSION_ALGO(LZ)
YM_COMPRESSION_ALGO(LZ4)

YMCompressionRef YMCompressionCreate(YMCompressionType type, YMFILE file, bool compressing)
{
    __ym_compression_t *c = (__ym_compression_t *)_YMAlloc(_YMCompressionTypeID,sizeof(__ym_compression_t));
    
    switch(type) {
#if !defined(YMWIN32)
#ifdef YM_IMPLEMENTED
        case YMCompressionGZ:
            c->initFunc = YMGZInit;
            c->readFunc = YMGZRead;
            c->writeFunc = YMGZWrite;
            c->closeFunc = YMGZClose;
            break;
        case YMCompressionBZ2:
            c->initFunc = YMBZInit;
            c->readFunc = YMBZRead;
            c->writeFunc = YMBZWrite;
            c->closeFunc = YMBZClose;
            break;
        case YMCompressionLZ:
            c->initFunc = YMLZInit;
            c->readFunc = YMLZRead;
            c->writeFunc = YMLZWrite;
            c->closeFunc = YMLZClose;
            break;
#endif
        case YMCompressionLZ4:
            c->initFunc = YMLZ4Init;
            c->readFunc = YMLZ4Read;
            c->writeFunc = YMLZ4Write;
            c->closeFunc = YMLZ4Close;
            break;
#endif
        default:
            c->initFunc = YMNoCompressionInit;
            c->readFunc = YMNoCompressionRead;
            c->writeFunc = YMNoCompressionWrite;
            c->closeFunc = YMNoCompressionClose;
            break;
    }
    
    c->file = file;
    c->type = type;
    c->compressing = compressing;
    
#if !defined(YMWIN32)
    c->gzfile = NULL;
    c->bzfile = NULL;
#endif

    c->inBytes = 0;
    c->outBytes = 0;

    return c;
}

void _YMCompressionFree(YMCompressionRef c_)
{
    //__unused __ym_compression_t *c = (__ym_compression_t *)c_;
}

void YMCompressionSetInitFunc(YMCompressionRef c_, ym_compression_init_func func)
{
    __ym_compression_t *c = (__ym_compression_t *)c_;
    c->initFunc = func;
}

bool YMCompressionInit(YMCompressionRef c_)
{
    __ym_compression_t *c = (__ym_compression_t *)c_;
    return c->initFunc(c);
}

YMIOResult __ym_compression_read_underlying(__ym_compression_t *c, uint8_t *b, YM_COMPRESSION_LENGTH l, YM_COMPRESSION_LENGTH *o)
{
    YM_IO_BOILERPLATE
    ymassert(!c->compressing,"attempt to read compressor %p",c);
    size_t ol = 0;
    result = YMReadFull(c->file, b, l, &ol);
    ymlog("%zd = read(%d,,%zu)",result,c->file,l);
    ymassert(ol<=UINT16_MAX,"ol overflow %zu",ol);
    ymassert(result!=YMIOError,"__ym_compression_read_underlying failed %d %s",errno,strerror(errno));
    if ( o ) { *o = (uint16_t)ol; }
    return result;
}

YMIOResult __ym_compression_write_underlying(__ym_compression_t *c, const uint8_t *b, YM_COMPRESSION_LENGTH l, YM_COMPRESSION_LENGTH *o)
{
    YM_IO_BOILERPLATE
    ymassert(c->compressing,"attempt to write decompressor %p",c);
    size_t ol = 0;
    result = YMWriteFull(c->file, b, l, &ol);
    ymlog("%zd = write(%d,,%zu)",result,c->file,l);
    ymassert(ol<=UINT16_MAX,"ol overflow %zu",ol);
    ymassert(result!=YMIOError,"__ym_compression_write_underlying failed %d %s",errno,strerror(errno));
    if ( o ) { *o = (uint16_t)ol; }
    return result;
}

YMIOResult YMCompressionRead(YMCompressionRef c_, uint8_t *b, YM_COMPRESSION_LENGTH l, YM_COMPRESSION_LENGTH *o)
{
    __ym_compression_t *c = (__ym_compression_t *)c_;
    ymassert(!c->compressing,"attempt to read compressor %p",c);
    return c->readFunc(c, b, l, o);
}

YMIOResult YMAPI YMCompressionWrite(YMCompressionRef c_, const uint8_t *b, YM_COMPRESSION_LENGTH l, YM_COMPRESSION_LENGTH *o, YM_COMPRESSION_LENGTH *oh)
{
    __ym_compression_t *c = (__ym_compression_t *)c_;
    ymassert(c->compressing,"attempt to write decompressor %p",c);
    return c->writeFunc(c, b, l, o, oh);
}

bool YMCompressionClose(YMCompressionRef c_)
{
    __ym_compression_t *c = (__ym_compression_t *)c_;
    return c->closeFunc(c);
}

// passthrough
bool YMNoCompressionInit(__unused __ym_compression_t *c)
{
    return true;
}

bool __ym_compression_close_underlying(__ym_compression_t *c)
{
    YM_IO_BOILERPLATE

    YM_CLOSE_FILE(c->file);

    return (result == 0);
}

YMIOResult YMNoCompressionRead(__ym_compression_t *c, uint8_t *b, YM_COMPRESSION_LENGTH l, YM_COMPRESSION_LENGTH *o)
{
    c->inBytes += l;
    c->outBytes += l;

    return __ym_compression_read_underlying(c,b,l,o);
}

YMIOResult YMNoCompressionWrite(__ym_compression_t *c, const uint8_t *b, YM_COMPRESSION_LENGTH l, YM_COMPRESSION_LENGTH *o, YM_COMPRESSION_LENGTH *oh)
{
    c->inBytes += l;
    c->outBytes += l;

    YMIOResult result = __ym_compression_write_underlying(c,b,l,o);

    if ( oh ) *oh = 0;

    return result;
}

bool YMNoCompressionClose(__ym_compression_t *c)
{
    return __ym_compression_close_underlying(c);
}

void YMCompressionGetPerformance(YMCompressionRef c_, uint64_t *oIn, uint64_t *oOut)
{
    __ym_compression_t *c = (__ym_compression_t *)c_;
    if ( oIn ) *oIn = c->inBytes;
    if ( oOut ) *oOut = c->outBytes;
}

#if !defined(YMWIN32)
#ifdef YM_IMPLEMENTED
bool YMGZInit(__ym_compression_t *c)
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

YMIOResult YMGZRead(__ym_compression_t *c, uint8_t *b, YM_COMPRESSION_LENGTH l, YM_COMPRESSION_LENGTH *o)
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

YMIOResult YMGZWrite(__ym_compression_t *c, const uint8_t *b, YM_COMPRESSION_LENGTH l, YM_COMPRESSION_LENGTH *o)
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

bool YMGZClose(__ym_compression_t *c)
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

bool YMBZInit(__ym_compression_t * c)
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

YMIOResult YMBZRead(__ym_compression_t *c, uint8_t *b, YM_COMPRESSION_LENGTH l, YM_COMPRESSION_LENGTH *o)
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

YMIOResult YMBZWrite(__ym_compression_t *c, const uint8_t *b, YM_COMPRESSION_LENGTH l, YM_COMPRESSION_LENGTH *o)
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

bool YMBZClose(__ym_compression_t *c)
{
    ymassert(c->bzfile,"bzfile is not initialized");
    // todo need to flush before close? find updated manual
    BZ2_bzclose(c->bzfile);
    return true;
}

bool YMLZInit(__ym_compression_t *c)
{
    c = NULL;
    ymabort("someone set up is the \"expert\"");
    return false;
}

YMIOResult YMLZRead(__ym_compression_t *c, uint8_t *b, YM_COMPRESSION_LENGTH l, YM_COMPRESSION_LENGTH *o)
{
    c = NULL; b = NULL; l = 0; o = NULL;
    ymabort("someone set up is the \"expert\"");
    return false;
}

YMIOResult YMLZWrite(__ym_compression_t *c, const uint8_t *b, YM_COMPRESSION_LENGTH l, YM_COMPRESSION_LENGTH *o)
{
    c = NULL; b = NULL; l = 0; o = NULL;
    ymabort("someone set up is the \"expert\"");
    return false;
}

bool YMLZClose(__ym_compression_t *c)
{
    c = NULL;
    ymabort("someone set up is the \"expert\"");
    return false;
}

#endif

bool YMLZ4Init(__ym_compression_t *c)
{
    return true;
}

typedef struct __ym_lz4_packed
{
    uint8_t compressed;
    uint16_t size;
} __ym_lz4_packed; // endian

// https://stackoverflow.com/a/25755758/147192
//#define __ym_lz4_decompress_buf_max_size (255*sizeof(uint8_t) - 2526) //~2.3mb
YMIOResult YMLZ4Read(__ym_compression_t *c, uint8_t *b, YM_COMPRESSION_LENGTH l, YM_COMPRESSION_LENGTH *o)
{
    ymassert(!c->compressing,"attempt to read lz4 compressor %p(%p,%zu,%p)",c,b,l,o);
    ymassert(l<=UINT16_MAX,"YMLZ4Read too large %zu",l); // todo

    // instinct was to statically allocate and grow this, we're very multithreaded and forcing relatively small i/o
    #define bufSize (sizeof(__ym_lz4_packed) + UINT16_MAX)
    char *buf = malloc(bufSize);

    int ret = 0;
    uint8_t *dst = NULL;
    __ym_lz4_packed lengthPacked;
    size_t ol = 0;
    YMIOResult result = __ym_compression_read_underlying(c,(uint8_t *)&lengthPacked,sizeof(lengthPacked),&ol);
    ymlog("%d(ol=%zu,lengthPacked%u[%d]) = __ym_compression_read_underlying(%p[%d],%p,%lu,&ol)",result,ol,lengthPacked.size,lengthPacked.compressed,c,c->file,&lengthPacked,sizeof(lengthPacked));

    if ( ( result != YMIOSuccess ) || ( ol != sizeof(lengthPacked) ) ) {
        if ( result != YMIOEOF )
            ymerr("lz4 read header failed %d %zu %ld",result,ol,sizeof(lengthPacked));
        goto catch_return;
    }

    uint16_t length = lengthPacked.size;

    if ( ! lengthPacked.compressed ) {
        result = __ym_compression_read_underlying(c,b,length,&ol);
        ymlog("%d(ol=%zu,length=%u[%d]) = __ym_compression_read_underlying(%p[%d],%p,%zu,&ol)",result,ol,length,lengthPacked.compressed,c,c->file,b,l);
        if ( ( result != YMIOSuccess ) || ( ol != l ) ) {
            ymerr("lz4 read uncompressed failed %d %zu %zu",result,ol,l);
            goto catch_return;
        }
        ret = ol;
    } else {
        dst = YMALLOC(length*sizeof(uint8_t));
        ymassert(dst,"failed to allocate lz4 header buffer");

        result = __ym_compression_read_underlying(c,dst,length,&ol);
        if ( result != YMIOSuccess ) {
            ymerr("lz4 read buffer failed");
            goto catch_return;
        }

        ymlog("%d(ol=%zu) = __ym_compression_read_underlying(%p[%d],%p,%u,&ol)",result,ol,c,c->file,dst,length);

        ret = LZ4_decompress_safe((const char *)dst,buf,length,bufSize);
        if ( ret < 0 ) {
            ymerr("lz4 decompress failed (%zu %u)",l,length);
            result = YMIOError;
            goto catch_return;
        } else if ( ret > l ) {
            ymassert(false,"lz4 decompress too big (%d %zu)",ret,l);
            result = YMIOError;
            goto catch_return;
        }

        memcpy(b,buf,ret);
        result = YMIOSuccess;
    }

    ymlog("lz4[%s] %u -> %d",lengthPacked.compressed?"d":"-r",length,ret);

catch_return:
    c->inBytes += l;
    c->outBytes += ret;
    if ( o ) *o = ret;
    if ( dst ) free(dst);
    return result;
}

YMIOResult YMLZ4Write(__ym_compression_t *c, const uint8_t *b, YM_COMPRESSION_LENGTH l, YM_COMPRESSION_LENGTH *o, YM_COMPRESSION_LENGTH *oh)
{
    ymassert(c->compressing,"attempt to write to lz4 decompressor %p(%p,%zu,%p)",c,b,l,o);
    ymassert(l<=UINT16_MAX,"YMLZ4Write overflow %zu",l);
    // instinct was to statically allocate and grow this, we're very multithreaded and forcing relatively small i/o
    int bound = LZ4_compressBound(l);
    ymlog("%d = LZ4_compressBound(%zu)",bound,l);
    char *buf = malloc(bound);

    int compressedSize = LZ4_compress_default((const char *)b, buf, l, bound);
    if ( compressedSize == 0 ) {
        ymerr("LZ4_compress_default failed");
        return YMIOError;
    }

    ymlog("%d = LZ4_compress_default(%p,%p,%zu,%d)",compressedSize,b,buf,l,bound);

    bool compressUnderlying = (compressedSize < l);
    __ym_lz4_packed lengthPacked = { .compressed = compressUnderlying, .size = ( compressUnderlying ? compressedSize : l ) };

    size_t ol = 0;
    YMIOResult result = __ym_compression_write_underlying(c,(const uint8_t *)&lengthPacked,sizeof(lengthPacked),&ol);
    ymlog("%d(ol=%zu) = __ym_compression_write_underlying(%p[%d],%p,%ld,%p)",result,ol,c,c->file,&lengthPacked,sizeof(lengthPacked),&ol);

    if ( ( result != YMIOSuccess ) || ( ol != sizeof(lengthPacked) ) ) {
        ymerr("lz4 write packed failed %d %zu %ld",result,ol,sizeof(lengthPacked));
        result = YMIOError;
        goto catch_return;
    }

    const uint8_t *underlyingBuf = compressUnderlying ? (const uint8_t *)buf : b;

    #warning we don't "know" this is a pipe at our level
    int pipeSize = fcntl(c->file, F_GETPIPE_SZ);
    ymlog("pipesize %d",pipeSize);

    int idx = 0;
    while ( idx < lengthPacked.size ) {
        size_t remaining = ( lengthPacked.size - idx );
        size_t toWrite = ( pipeSize < remaining ) ? pipeSize : remaining;
        result = __ym_compression_write_underlying(c,underlyingBuf,toWrite,&ol);
        ymlog("%d(ol=%zu) = __ym_compression_write_underlying(%p[%d],%p,%hu,%p)",result,ol,c,c->file,underlyingBuf,lengthPacked.size,&ol);

        if ( ( result != YMIOSuccess ) || ( ol != lengthPacked.size ) ) {
            ymerr("lz4 write packed failed %d %zu %hu",result,ol,lengthPacked.size);
            result = YMIOError;
            goto catch_return;
        }
        idx += ol;
    }

    ymlog("lz4[%s] %zu -> %zu (%hu)",lengthPacked.compressed?"c":"-w",l,ol,lengthPacked.size);
    if ( l == 1 ) {
        ymlog("      %c vs %c",b[0],underlyingBuf[0]);
    }
    c->inBytes += l;
    c->outBytes += ol;

    if ( o ) *o = ol;
    if ( oh ) *oh = sizeof(lengthPacked);

catch_return:
    free(buf);
    return result;
}

bool YMLZ4Close(__ym_compression_t *c)
{
    return __ym_compression_close_underlying(c);
}

#endif

YM_EXTERN_C_POP
