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
    
    YMCompressionRef writeC;
    YMCompressionRef readC;
    size_t rawWritten;
    size_t rawRead;
    uint8_t *outBytes;
    YMFILE sourceFd;
} CompressionTest;

void _GZTestRun(CompressionTest *);
void _BZTestRun(CompressionTest *);
void _CompressionTest(CompressionTest *theTest, const char *sourcePath, YMCompressionType type);

YM_THREAD_RETURN YM_CALLING_CONVENTION compression_test_read_proc(YM_THREAD_PARAM);

void CompressionTestsRun(ym_test_assert_func assert, const void *context)
{
    struct CompressionTest theTest = { assert, context, NULL, NULL, 0, 0, NULL, NULL_FILE };
#if !defined(YMWIN32)
    _GZTestRun(&theTest);
    ymerr("_GZTestRun completed");
    _BZTestRun(&theTest);
    ymerr("_BZTestRun completed");
#endif
}

void _GZTestRun(CompressionTest *theTest)
{
#if defined(YMAPPLE)
    const char *sourcePath = "/usr/share/man/man1/gzip.1";
	YMCompressionType type = YMCompressionGZ;
#elif defined(YMLINUX)
    const char *sourcePath = "/usr/share/man/man1/gzip.1.gz";
	YMCompressionType type = YMCompressionNone;
#else
	const char *sourcePath = "\\\\Windows\\system.ini";
	YMCompressionType type = YMCompressionNone;
#endif
    _CompressionTest(theTest, sourcePath, type);
}

void _BZTestRun(CompressionTest *theTest)
{
#if defined(YMAPPLE)
    const char *sourcePath = "/usr/share/man/man1/bzip2.1";
	YMCompressionType type = YMCompressionGZ;
#elif defined(YMLINUX)
    const char *sourcePath = "/usr/share/man/man1/bzip2.1.gz";
	YMCompressionType type = YMCompressionNone;
#else
	const char *sourcePath = TEXT("\\\\Windows\\system.ini");
	YMCompressionType type = YMCompressionNone;
#endif
    _CompressionTest(theTest, sourcePath, type);
}

void _CompressionTest(CompressionTest *theTest, const char *sourcePath, YMCompressionType type)
{
    YM_IO_BOILERPLATE
    
    YM_OPEN_FILE(sourcePath, READ_FLAG);
    theTest->sourceFd = (YMFILE)result;
	testassert(result!=-1,"open %s",sourcePath);
    testassert(theTest->sourceFd>=0,"open %s",sourcePath);
    
    YMPipeRef pipe = YMPipeCreate(NULL);
    testassert(pipe,"pipe");
    
    theTest->writeC = YMCompressionCreate(type, YMPipeGetInputFile(pipe), true);
    bool okay = YMCompressionInit(theTest->writeC);
    testassert(okay,"write init");
    theTest->readC = YMCompressionCreate(type, YMPipeGetOutputFile(pipe), false);
    okay = YMCompressionInit(theTest->readC);
    testassert(okay,"read init");
    
    
    YMThreadRef readThread = YMThreadCreate(NULL, compression_test_read_proc, theTest);
    YMThreadStart(readThread);
    
#define by 128
    uint8_t outBuf[by];
    YMIOResult ymResult;
    do {
        
        size_t o = UINT32_MAX;
        ymResult = YMCompressionRead(theTest->readC, outBuf, by, &o);
        testassert(((ymResult==YMIOSuccess)&&o>0)||ymResult==YMIOEOF,"read");
        theTest->rawWritten += o;
        // todo factor low level FS stuff into library and diff this.
    } while (ymResult!=YMIOEOF);
    
    okay = YMCompressionClose(theTest->readC);
    ymassert(okay,"read close");
    ymassert(theTest->rawWritten==theTest->rawRead,"raw mismatch");
    
    YMThreadJoin(readThread);
    YMRelease(readThread);
    
    YMRelease(theTest->writeC);
    YMRelease(theTest->readC);
    YMRelease(pipe);
    
    size_t idx = 0;
    YM_REWIND_FILE(theTest->sourceFd);
    while(true) {
        YM_READ_FILE(theTest->sourceFd,outBuf,by);
        testassert(result>=0,"compare source");
        
        int cmp = memcmp(theTest->outBytes + idx, outBuf, result);
        testassert(cmp==0,"compare from %zd",idx);
        
        if ( result == 0 )
            break;
        
        idx += result;
    }
    
    free(theTest->outBytes);
    YM_CLOSE_FILE(theTest->sourceFd);
}

YM_THREAD_RETURN YM_CALLING_CONVENTION compression_test_read_proc(YM_THREAD_PARAM ctx)
{
    YM_IO_BOILERPLATE
    
    CompressionTest *theTest = ctx;
    
    uint8_t buf[by];
    theTest->outBytes = malloc(16384);
    size_t idx = 0;
    
    while(true) {
        YM_READ_FILE(theTest->sourceFd,buf,by);
        testassert(result>=0,"read source");
        if ( result == 0 ) {
            bool okay = YMCompressionClose(theTest->writeC);
            testassert(okay,"write close");
            break;
        }
        
        size_t o = UINT32_MAX;
        YMIOResult ymResult = YMCompressionWrite(theTest->writeC, buf, result, &o);
        testassert(ymResult==YMIOSuccess,"write");
        testassert((ssize_t)o==result,"o!=aRead");
        theTest->rawRead += o;
        
        testassert((idx+o)<16384,"overflow");
        memcpy(theTest->outBytes + idx,buf,o);
        idx += o;
    }
    
    YM_THREAD_END
}

YM_EXTERN_C_POP
