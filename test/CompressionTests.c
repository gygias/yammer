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

#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <fcntl.h>

YM_EXTERN_C_PUSH

typedef struct CompressionTest
{
    ym_test_assert_func assert;
    const void *context;
} CompressionTest;

void _GZTestRun(CompressionTest *);
void _BZTestRun(CompressionTest *);
void _CompressionTest(CompressionTest *theTest, const char *sourcePath, YMCompressionType type);

void CompressionTestsRun(ym_test_assert_func assert, const void *context)
{
    struct CompressionTest theTest = { assert, context };
    
    _GZTestRun(&theTest);
    ymerr("_GZTestRun completed");
    _BZTestRun(&theTest);
    ymerr("_BZTestRun completed");
}

void _GZTestRun(CompressionTest *theTest)
{
#if defined(YMAPPLE)
    const char *sourcePath = "/usr/share/man/man1/gzip.1";
#else
#error implement me 4/1/2016
#endif
    _CompressionTest(theTest, sourcePath, YMCompressionGZ);
}

void _BZTestRun(CompressionTest *theTest)
{
#if defined(YMAPPLE)
    const char *sourcePath = "/usr/share/man/man1/bzip2.1";
#else
#error implement me 4/1/2016
#endif
    _CompressionTest(theTest, sourcePath, YMCompressionBZ2);
}

void _CompressionTest(CompressionTest *theTest, const char *sourcePath, YMCompressionType type)
{
    YM_IO_BOILERPLATE
    
    YM_OPEN_FILE(sourcePath, READ_FLAG);
    YMFILE sourceFd = (YMFILE)result;
    testassert(sourceFd>=0,"open %s",sourcePath);
    
    YMPipeRef pipe = YMPipeCreate(NULL);
    testassert(pipe,"pipe");
    
    YMCompressionRef writeC = YMCompressionCreate(type, YMPipeGetInputFile(pipe), true);
    bool okay = YMCompressionInit(writeC);
    testassert(okay,"write init");
    YMCompressionRef readC = YMCompressionCreate(type, YMPipeGetOutputFile(pipe), false);
    okay = YMCompressionInit(readC);
    testassert(okay,"read init");
    
#define by 128
    uint8_t buf[by], outBuf[by];
    bool eof = false;
    
    do {
        YM_READ_FILE(sourceFd,buf,by);
        testassert(aRead>=0,"read source");
        if ( aRead == 0 ) {
            okay = YMCompressionClose(writeC);
            testassert(okay,"write close");
            eof = true;
        }
        
        size_t o = SIZE_T_MAX;
        YMIOResult ymResult = YMCompressionWrite(writeC, buf, aRead, &o);
        testassert(ymResult==YMIOSuccess,"write");
        testassert((ssize_t)o==aRead,"o!=aRead");
        
        o = SIZE_T_MAX;
        ymResult = YMCompressionRead(readC, outBuf, aRead, &o);
        testassert(ymResult==YMIOSuccess||(eof&&ymResult==YMIOEOF),"read");
        testassert((ssize_t)o==aRead,"o!=aRead");
        
        int compare = memcmp(buf, outBuf, aRead);
        testassert(compare==0,"compare");
        
    } while (!eof);
    
    YMRelease(writeC);
    YMRelease(readC);    
    YMRelease(pipe);
    YM_CLOSE_FILE(sourceFd);
}

YM_EXTERN_C_POP
