//
//  CompressionTests.c
//  yammer
//
//  Created by david on 4/1/16.
//  Copyright © 2016 combobulated. All rights reserved.
//

#include "CompressionTests.h"

#include "YMBase.h"
#include "YMCompression.h"
#include "YMPipe.h"
#include "YMPipePriv.h"
#include "YMThread.h"

#include <sys/types.h>
#if !defined(YMWIN32)
#include <sys/uio.h>
#include <unistd.h>
#endif
#include <fcntl.h>

YM_EXTERN_C_PUSH

typedef struct CompressionTest
{
    ym_test_assert_func assert;
    const void *context;
    
    YMCompressionRef compressC;
    YMCompressionRef decompressC;
    size_t rawRead;
    size_t compressedWrite;
    size_t rawWritten;
    uint8_t *outBytes;
    YMFILE sourceFd;
    YMFILE destFd;
} CompressionTest;

#ifdef YM_IMPLEMENTED
void _GZTestRun(CompressionTest *);
void _BZTestRun(CompressionTest *);
#endif
void _LZ4TestRun(CompressionTest *);
void _CompressionTest(CompressionTest *, const char *, const char *, YMCompressionType type);

YM_ENTRY_POINT(compression_test_compress_proc);

void CompressionTestsRun(ym_test_assert_func assert, const void *context)
{
    struct CompressionTest theTest = { assert, context, NULL, NULL, 0, 0, 0, NULL, NULL_FILE, NULL_FILE };
#ifdef YM_IMPLEMENTED
#if !defined(YMWIN32)
    _GZTestRun(&theTest);
    ymerr("_GZTestRun completed");
    _BZTestRun(&theTest);
    ymerr("_BZTestRun completed");
#endif
#endif
    _LZ4TestRun(&theTest);
}

#ifdef YM_IMPLEMENTED
void _GZTestRun(CompressionTest *theTest)
{
#if defined(YMAPPLE)
    const char *sourcePath = "/usr/share/man/man1/gzip.1";
#elif defined(YMLINUX)
    const char *sourcePath = "/usr/share/man/man1/gzip.1.gz";
#else
	const char *sourcePath = "\\\\Windows\\system.ini";
#endif
    _CompressionTest(theTest, sourcePath, YMCompressionGZ);
}

void _BZTestRun(CompressionTest *theTest)
{
#if defined(YMAPPLE)
    const char *sourcePath = "/usr/share/man/man1/bzip2.1";
#elif defined(YMLINUX)
    const char *sourcePath = "/usr/share/man/man1/bzip2.1.gz";
#else
	const char *sourcePath = TEXT("\\\\Windows\\system.ini");
#endif
    _CompressionTest(theTest, sourcePath, YMCompressionBZ2);
}
#endif

void _LZ4TestRun(CompressionTest *theTest)
{
#if defined(YMAPPLE)
    const char *sourcePath = "/private/var/log/install.log";
#elif defined(YMLINUX)
    const char *sourcePath = "/var/log/syslog";
#else
    const char *sourcePath = TEXT("\\\\Windows\\system.ini");
    #error destpath
#endif
    _CompressionTest(theTest, sourcePath, "/tmp/YMLZ4TestOut.txt", YMCompressionLZ4);

}

void _CompressionTest(CompressionTest *theTest, const char *sourcePath, const char *destPath, YMCompressionType type)
{
    YM_IO_BOILERPLATE
    
    YM_OPEN_FILE(sourcePath, READ_FLAG);
    theTest->sourceFd = (YMFILE)result;
	testassert(result!=-1,"open %s",sourcePath);
    testassert(theTest->sourceFd>=0,"open fd %s",sourcePath);

    YM_STOMP_FILE(destPath, READ_WRITE_FLAG);
    theTest->destFd = (YMFILE)result;
    testassert(result!=-1,"open %s %d %s",destPath,error,errorStr);
    testassert(theTest->destFd>=0,"open fd %s %d %s",destPath,error,errorStr);
    
    YMPipeRef pipe = YMPipeCreate(NULL);
    testassert(pipe,"pipe");
    
    theTest->compressC = YMCompressionCreate(type, YMPipeGetInputFile(pipe), true);
    bool okay = YMCompressionInit(theTest->compressC);
    testassert(okay,"write init");
    theTest->decompressC = YMCompressionCreate(type, YMPipeGetOutputFile(pipe), false);
    okay = YMCompressionInit(theTest->decompressC);
    testassert(okay,"read init");
    
    
    YMThreadRef readThread = YMThreadCreate(NULL, compression_test_compress_proc, theTest);
    YMThreadStart(readThread);
    
#define by UINT16_MAX
    int nOutBytes = by;
    theTest->outBytes = malloc(nOutBytes);
    theTest->rawWritten = 0;

    while(true) {

        while ( theTest->rawWritten + by > nOutBytes ) {
            theTest->outBytes = realloc(theTest->outBytes,nOutBytes*2);
            testassert(theTest->outBytes,"failed to reallocate outBytes");
            nOutBytes = nOutBytes * 2;
        }

        size_t o = 0;
        YMIOResult ymResult = YMCompressionRead(theTest->decompressC, theTest->outBytes + theTest->rawWritten, by, &o);
        testassert(((ymResult==YMIOSuccess)&&o>0)||ymResult==YMIOEOF,"YMCompressionRead failed %d %zu",ymResult,o);
        if ( ymResult == YMIOEOF )
            break;

        size_t destWritten = 0;
        ymResult = YMWriteFull(theTest->destFd,theTest->outBytes + theTest->rawWritten,o,&destWritten);
        testassert(ymResult==YMIOSuccess&&o==destWritten,"write dest file failed (%d %zu) %d %s",destWritten,o,errno,strerror(errno));

        theTest->rawWritten += o;
    }
    
    okay = YMCompressionClose(theTest->decompressC);
    ymassert(okay,"read close");
    
    // these are available from YMCompressionGetPerformance, but since it's already here leaving as another sanity check
    ymassert(theTest->rawWritten==theTest->rawRead,"raw mismatch w%lu v r%lu",theTest->rawWritten,theTest->rawRead);
    
    YMThreadJoin(readThread);
    YMRelease(readThread);
    _YMPipeSetClosed(pipe);
    YMRelease(pipe);
    
    size_t idx = 0;
    YM_REWIND_FILE(theTest->sourceFd);
    char buf[by];
    while(true) {
        YM_READ_FILE(theTest->sourceFd,buf,by);
        testassert(result>=0,"compare source");
        
        int cmp = memcmp(theTest->outBytes + idx, buf, result);
        testassert(cmp==0,"compare from %zd",idx);
        
        if ( result == 0 )
            break;
        
        idx += result;
    }

    uint64_t cIn,cOut;
    YMCompressionGetPerformance(theTest->compressC,&cIn,&cOut);
    ymlog("compression test succeeded with %0.2f percent compression (%lu vs %lu)",(1 - (double)cOut/(double)cIn)*100,cOut,cIn);

    YMRelease(theTest->compressC);
    YMRelease(theTest->decompressC);
    free(theTest->outBytes);
    YM_CLOSE_FILE(theTest->sourceFd);
    YM_CLOSE_FILE(theTest->destFd);
}

YM_ENTRY_POINT(compression_test_compress_proc)
{
    YM_IO_BOILERPLATE
    
    CompressionTest *theTest = context;
    
    uint8_t buf[by];
    bool hitEOF = false;

    do {
        size_t aRead = 0;
        YMIOResult result = YMReadFull(theTest->sourceFd,buf,by,&aRead);
        testassert((result==YMIOSuccess&&aRead==by)||result==YMIOEOF,"read source %d[%zu]",result,aRead);
        if ( result == YMIOEOF ) {
            ymlog("compressor hit EOF (aRead %zu)",aRead);
            hitEOF = true;
        }
        
        size_t o = 0, oh = 0;
        YMIOResult ymResult = YMCompressionWrite(theTest->compressC, buf, aRead, &o, &oh);
        testassert((ymResult==YMIOSuccess)&&(o>0),"write failed %d %zu %zu",ymResult,o,aRead);
        theTest->rawRead += aRead;
        theTest->compressedWrite += o + oh;
    } while(!hitEOF);

    bool okay = YMCompressionClose(theTest->compressC);
    testassert(okay,"write close");
}

YM_EXTERN_C_POP
