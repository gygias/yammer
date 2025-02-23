//
//  SessionTests.c
//  yammer
//
//  Created by david on 12/2/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "SessionTests.h"

#include "YMBase.h"
#include "YMSession.h"
#include "YMDispatch.h"
#include "YMDispatchUtils.h"
#include "YMStreamPriv.h" // todo need an 'internal' header
#include "YMPipe.h"
#include "YMPipePriv.h"
#include "YMSemaphore.h"
#include "YMUtilities.h"

#include <fcntl.h>
#if defined(YMWIN32)
# include "windirent.h"
#else
# include <dirent.h>
#endif

YM_EXTERN_C_PUSH

uint64_t gSomeLength = 5678900;
#define FAKE_DELAY_MAX 3

#ifndef YMWIN32
# if defined(YMAPPLE)
#  define ServerTestFile		"install.log"
#  define ServerTestPath		"/private/var/log/" ServerTestFile
# else
#  define ServerTestFile          "syslog"
#  define ServerTestPath          "/var/log/" ServerTestFile
# endif
# define ClientSparsePath	"/usr/share/man/man2"
# define OutSparseDir		"/tmp/ymsessiontest-sparse"
# define LargeSrcTemplate	"/tmp/ymsessiontest-%s-orig"
# define LargeDestTemplate	"/tmp/ymsessiontest-%s-dest"
#else
# include <tchar.h> // gonna have to deal with this shit eventually
# define ServerTestFile		L"WindowsUpdate.txt"
# define ServerTestPath		L"c:\\Windows\\WindowsUpdate.log"
# define ClientSparsePath	"c:\\Windows\\inf"
# define ClientSparsePathW	L"c:\\Windows\\inf"
# define OutSparseDir		"ymsessiontest-inf"
# define LargeSrcTemplate	"ymsessiontest-%s-orig"
# define LargeDestTemplate	"ymsessiontest-%s-dest"
#endif

struct SessionTest
{
    ym_test_assert_func assert;
    ym_test_diff_func diff;
    const void *context;
    
    YMSessionRef serverSession;
    YMSessionRef clientSession;
    YMStringRef testType;
    YMStringRef testName;
    
    YMConnectionRef serverConnection;
    YMConnectionRef clientConnection;
    bool serverAsync, serverBounding, lastClientAsync, lastClientBounded;
	YMFILE largeSrcFd;
    uint64_t lastClientFileSize;
    uint64_t nSparseFilesToRead;
    uint64_t nSparseFilesRead;
    
    YMDispatchQueueRef serverQueue;
    YMDispatchQueueRef clientQueue;
    
    YMDictionaryRef nonRegularFileNames;
    YMStringRef tempServerSrc;
    YMStringRef tempServerDst;
    YMStringRef tempSparseDir;
    
    YMSemaphoreRef connectAndAsyncClientCallbackSemaphore; // did you get all that?
    YMSemaphoreRef threadExitSemaphore;
    bool stopping;
    
    
};

typedef struct TestConnectionStream
{
    struct SessionTest *theTest;
    YMConnectionRef connection;
    YMStreamRef stream;
} TestConnectionStream;

void _TestSessionWritingLargeAndReadingSparseFiles(struct SessionTest *theTest);
YM_ENTRY_POINT(_ServerWriteLargeFile);
YM_ENTRY_POINT(_ClientWriteSparseFiles);
YM_ENTRY_POINT(_FlushMiddleman);
YM_ENTRY_POINT(_EatLargeFile);
YM_ENTRY_POINT(_EatASparseFile);
void _AsyncForwardCallback(struct SessionTest *theTest, YMConnectionRef connection, YMStreamRef stream, YMIOResult result, uint64_t bytesWritten, bool isServer);

void SessionTestsRun(ym_test_assert_func assert, ym_test_diff_func diff, const void *context)
{
    char suffix[10];
    YMRandomASCIIStringWithLength(suffix, arc4random_uniform(10)+1, true, false);
    struct SessionTest theTest = {  assert, diff, context,
                                    NULL, NULL, YMSTRC("_ymtest._tcp"), YMSTRCF("twitter-cliche:%s", suffix),
                                    NULL, NULL, false, false, false, false, NULL_FILE, 0, UINT64_MAX, 0,
                                    YMDispatchQueueCreate(YMSTRC("com.combobulated.SessionTestsQueue.server")),YMDispatchQueueCreate(YMSTRC("com.combobulated.SessionTestsQueue.client")),
                                    YMDictionaryCreate(), NULL, NULL, YMSTRC(OutSparseDir),
                                    YMSemaphoreCreate(0), YMSemaphoreCreate(0), false };
    
    ymerr(" Session test: '%s'",YMSTR(theTest.testName));
    _TestSessionWritingLargeAndReadingSparseFiles(&theTest);
    ymerr(" Session test finished with %lu sparse files",theTest.nSparseFilesToRead);
    
    while ( YMDictionaryGetCount(theTest.nonRegularFileNames) > 0 ) {
        const void *filename = YMDictionaryRemove(theTest.nonRegularFileNames, YMDictionaryGetRandomKey(theTest.nonRegularFileNames));
        free((void *)filename);
    }
    
    YMRelease(theTest.testType);
	YMRelease(theTest.testName);
    YMRelease(theTest.nonRegularFileNames);
    if ( theTest.tempServerSrc ) YMRelease(theTest.tempServerSrc);
    if ( theTest.tempServerDst ) YMRelease(theTest.tempServerDst);
    YMRelease(theTest.tempSparseDir);
    YMRelease(theTest.connectAndAsyncClientCallbackSemaphore);
    YMRelease(theTest.threadExitSemaphore);
    
    YMDispatchJoin(theTest.serverQueue);
    YMDispatchJoin(theTest.clientQueue);
    YMDispatchQueueRelease(theTest.serverQueue);
    YMDispatchQueueRelease(theTest.clientQueue);
}

void _server_async_forward_callback(YMConnectionRef connection, YMStreamRef stream, YMIOResult result, uint64_t bytesWritten, void * ctx);
void _client_async_forward_callback(YMConnectionRef connection, YMStreamRef stream, YMIOResult result, uint64_t bytesWritten, void * ctx);
void _added_peer_func(YMSessionRef session, YMPeerRef peer, void *context);
void _removed_peer_func(YMSessionRef session, YMPeerRef peer, void *context);
void _resolve_failed_func(YMSessionRef session, YMPeerRef peer, void *context);
void _resolved_func(YMSessionRef session, YMPeerRef peer, void *context);
void _connect_failed_func(YMSessionRef session, YMPeerRef peer, bool moreComing, void *context);
bool _should_accept_func(YMSessionRef session, YMPeerRef peer, void *context);
void _connected_func(YMSessionRef session, YMConnectionRef connection, void *context);
void _interrupted_func(YMSessionRef session, void *context);
void _new_stream_func(YMSessionRef session, YMConnectionRef connection, YMStreamRef stream, void *context);
void _closing_func(YMSessionRef session, YMConnectionRef connection, YMStreamRef stream, void *context);

void _TestSessionWritingLargeAndReadingSparseFiles(struct SessionTest *theTest) {
    
    theTest->serverSession = YMSessionCreate(theTest->testType);
    testassert(theTest->serverSession,"server alloc");
    YMSessionSetCommonCallbacks(theTest->serverSession, NULL, _connected_func, _interrupted_func, _new_stream_func, _closing_func);
    YMSessionSetAdvertisingCallbacks(theTest->serverSession, _should_accept_func, theTest);
    
    theTest->clientSession = YMSessionCreate(theTest->testType);
    testassert(theTest->clientSession,"client alloc");
    YMSessionSetCommonCallbacks(theTest->clientSession, NULL, _connected_func, _interrupted_func, _new_stream_func, _closing_func);
    YMSessionSetBrowsingCallbacks(theTest->clientSession, _added_peer_func, _removed_peer_func, _resolve_failed_func, _resolved_func, _connect_failed_func, theTest);
    
    bool started = YMSessionStartAdvertising(theTest->serverSession, theTest->testName);
    testassert(started,"server start");
    
    started = YMSessionStartBrowsing(theTest->clientSession);
    testassert(started,"client start");
    
    // wait for 2 connects
    YMSemaphoreWait(theTest->connectAndAsyncClientCallbackSemaphore);
    YMSemaphoreWait(theTest->connectAndAsyncClientCallbackSemaphore);
    
    testassert(YMSessionGetDefaultConnection(theTest->clientSession),"client default connection is nil!");
    testassert(YMSessionGetDefaultConnection(theTest->serverSession),"server default connection is nil!");
    
#define RUN_SERVER
#ifdef RUN_SERVER
    YMThreadRef serverThread = YMThreadCreate(NULL, _ServerWriteLargeFile, theTest);
    YMThreadStart(serverThread);
#endif
#define CLIENT_TOO // debugging forward-file hang-up
#ifdef CLIENT_TOO
    YMThreadRef clientThread = YMThreadCreate(NULL, _ClientWriteSparseFiles, theTest);
    YMThreadStart(clientThread);
#endif
    
    // wait for 4 thread exits
#ifdef RUN_SERVER
    YMSemaphoreWait(theTest->threadExitSemaphore); // write large
    YMSemaphoreWait(theTest->threadExitSemaphore); // read large
#endif
#ifdef CLIENT_TOO
    YMSemaphoreWait(theTest->threadExitSemaphore); // write sparse
    //YMSemaphoreWait(theTest->threadExitSemaphore); // read sparse done differently
    while ( theTest->nSparseFilesRead < theTest->nSparseFilesToRead )
        sleep(1);
#endif
        
    YMConnectionRef sC = YMSessionGetDefaultConnection(theTest->serverSession);
    testassert(sC,"server connection");
    YMConnectionRef cC = YMSessionGetDefaultConnection(theTest->clientSession);
    testassert(cC,"client connection");
    
    theTest->stopping = true;
    bool okay = true;
    bool stopServerFirst = arc4random_uniform(2);
    ymerr("stopping %s",stopServerFirst?"server":"client");
    okay = stopServerFirst ? YMSessionStopAdvertising(theTest->serverSession) : YMSessionStopBrowsing(theTest->clientSession);
    testassert(okay,"(%s) session stop",stopServerFirst?"server":"client");
    okay = stopServerFirst ? YMSessionCloseAllConnections(theTest->serverSession) : YMSessionCloseAllConnections(theTest->clientSession);
    testassert(okay,"(%s) session close all",stopServerFirst?"server":"client");
    okay = stopServerFirst ? YMSessionStopBrowsing(theTest->clientSession) : YMSessionStopAdvertising(theTest->serverSession);
    testassert(okay,"(%s) session stop",stopServerFirst?"client":"server");
    okay = stopServerFirst ? YMSessionCloseAllConnections(theTest->clientSession) : YMSessionCloseAllConnections(theTest->serverSession);
    //testassert(okay,"(%s) session close all",stopServerFirst?"client":"server");
    // i don't think we can expect this to always succeed in-process.
    // we're racing the i/o threads as soon as we stop the server
    // but we can randomize which we close first to find real bugs.
    //XCTAssert(okay,@"second (%@) session close",stopServerFirst?@"client":@"server");
    
    bool diffOK;
#ifdef RUN_SERVER
    ymlog("diffing %s - %s",YMSTR(theTest->tempServerSrc),YMSTR(theTest->tempServerDst));
    diffOK = theTest->diff(theTest->context, YMSTR(theTest->tempServerSrc), YMSTR(theTest->tempServerDst), false, NULL);
    testassert(diffOK, "diff large");
#endif
#ifdef CLIENT_TOO
    ymlog("diffing %s",YMSTR(theTest->tempSparseDir));
    diffOK = theTest->diff(theTest->context, "/usr/share/man/man2", YMSTR(theTest->tempSparseDir), true, theTest->nonRegularFileNames);
    //testassert(diffOK, "diff sparse");
    //todo, deal with symlinks (examine output?)
#endif
    
    YMRelease(theTest->serverSession);
    YMRelease(theTest->clientSession);

    ymlog("session test finished");
}

#ifndef NAME_MAX // YMWIN32, unless "dirent.h"
#define NAME_MAX 256
#endif

typedef struct SparseFileHeader
{
    uint64_t len;
    char     name[NAME_MAX+1];
    bool     willBoundDataStream;
} SparseFileHeader;

#define THXFORSPARSETEMPLATE "%s thx 4 sparse, man!"
typedef struct SparseFileThanks
{
    char thx4Sparse[NAME_MAX+1+15];
} SparseFileThanks;

YM_ENTRY_POINT(_ServerWriteLargeFile)
{
	struct SessionTest *theTest = context;

	YM_IO_BOILERPLATE

    YMSessionRef server = theTest->serverSession;
    YMConnectionRef connection = YMSessionGetDefaultConnection(server);
    // todo sometimes this is inexplicably null, yet not in the session by the time the test runs
    testassert(connection,"server connection");
    
    theTest->tempServerSrc = YMSTRCF(LargeSrcTemplate,ServerTestFile);
    unlink(YMSTR(theTest->tempServerSrc));
    
    uint64_t copyBytes = 0;
    theTest->serverBounding = 
#if !defined(YMWIN32) && !defined(YMLINUX) || defined(FOUND_LARGE_WELL_KNOWN_WINDOWS_TEXT_FILE_THATS_BIGGER_THAN_5_MB_TO_USE_FOR_THIS)
		arc4random_uniform(2);
#else
		false;
#endif
    if ( theTest->serverBounding )
        copyBytes = gSomeLength;

	YM_OPEN_FILE(ServerTestPath, READ_FLAG);
    testassert(result>=0,"open(r) %s: %d %s", ServerTestPath,error,errorStr);
    YMFILE origFd = (YMFILE)result;
    YM_STOMP_FILE(YMSTR(theTest->tempServerSrc), READ_WRITE_FLAG);
	testassert(result >= 0, "failed to open large file copy");
    theTest->largeSrcFd = (YMFILE)result;
    testassert(theTest->largeSrcFd>=0,"open(rw) %s: %d %s",YMSTR(theTest->tempServerSrc),error,errorStr);
    uint8_t buff[512];
    while(1) {
        size_t toRead = (size_t)(copyBytes ? ( copyBytes < 512 ? copyBytes : 512 ) : 512);
		YM_READ_FILE(origFd,buff,toRead);
        testassert(result>=0,"aRead: %d %s",error,errorStr);
        if ( result == 0 ) break;
        ssize_t aRead = result;
		YM_WRITE_FILE(theTest->largeSrcFd,buff,aRead);
        testassert(aRead==result, "aRead%d!=aWrite%d: %d %s",aRead,result,error,errorStr);
        if ( copyBytes && copyBytes - aRead == 0 ) break;
        copyBytes -= aRead;
    }
	YM_CLOSE_FILE(origFd);
    testassert(result==0, "close orig: %d %s",error,errorStr);
    YM_REWIND_FILE(theTest->largeSrcFd);
    testassert(result==0, "rewind src: %d %s",error,errorStr);
    
    YMStringRef name = YMSTRCF("test-server-write-%s",ServerTestFile);
    YMStreamRef stream = YMConnectionCreateStream(connection, name, YMCompressionLZ4);
    YMRelease(name);
    testassert(stream,"server create stream");
    
    theTest->serverAsync = arc4random_uniform(2);
    ym_connection_forward_context_t *ctx = NULL;
    if ( theTest->serverAsync ) {
        ctx = calloc(sizeof(struct ym_connection_forward_context_t),1);
        ctx->callback = _server_async_forward_callback;
        ctx->context = theTest;
    }
    
    ymerr("writing large file '%s' %sbounded, %ssync from f%d",ServerTestPath,theTest->serverBounding?"":"un",theTest->serverAsync?"a":"",theTest->largeSrcFd);
    bool okay = YMConnectionForwardFile(connection, theTest->largeSrcFd, stream, theTest->serverBounding ? &gSomeLength : NULL, !theTest->serverAsync, ctx);
    testassert(okay,"server forward file");

    if ( ! theTest->serverAsync ) {
        uint64_t dI,dO;
        YMStreamGetPerformance(stream,&dI,&dO,NULL,NULL);
        double percentCompressed = (1 - (double)dO / (double)dI) * 100;
        ymlog("wrote large file with %0.2f%%",percentCompressed);
    }
    
    if ( ! theTest->serverAsync ) {
        YMConnectionCloseStream(connection,stream);
        YMSemaphoreSignal(theTest->threadExitSemaphore);
    }
	
    if ( ! theTest->serverAsync ) {
        YM_CLOSE_FILE(theTest->largeSrcFd);
        testassert(result==0, "failed to close large src f%d sync",theTest->largeSrcFd);
    }
    
    ymlog("wrote large file thread (%sSYNC) exiting",theTest->serverAsync?"A":"");
}

YM_ENTRY_POINT(_ClientWriteSparseFiles)
{
    YM_IO_BOILERPLATE

	struct SessionTest *theTest = context;
    YMSessionRef client = theTest->clientSession;
    YMConnectionRef connection = YMSessionGetDefaultConnection(client);
    
    bool okay = YMRecursiveDelete(theTest->tempSparseDir);
    testassert(okay,"delete temp sparse failed");

#ifdef YMWIN32
    for ( int i = 0; i < 5; i++ ) {
#endif
		result = mkdir(YMSTR(theTest->tempSparseDir),0755);
#ifdef YMWIN32
		if ( result == 0 ) break;
		ymerr("looping on mkdir %s...",YMSTR(theTest->tempSparseDir));
		sleep(1);
	}
#endif
    testassert(result==0, "mkdir failed %d %s",errno,strerror(errno));
    
    uint64_t actuallyWritten = 0;
    DIR *dir = opendir(ClientSparsePath);
    testassert(dir, "opendir");
    
    while (1) {
        struct dirent *dir_ent = readdir(dir);
        if ( ! dir_ent )
            break;
        
        char *aFile = dir_ent->d_name;
        
        if ( dir_ent->d_type != DT_REG ) {
            ymlog("client skipping %s",aFile);
            char *entry = strdup(aFile);
            YMDictionaryAdd(theTest->nonRegularFileNames, (YMDictionaryKey)entry, entry);
            continue;
        }

		char fullPath[PATH_MAX];
		strcpy(fullPath, ClientSparsePath);
		strcat(fullPath, "/");
		strcat(fullPath, aFile);
        ymdbg("client sending %s",fullPath);

#ifndef YMWIN32
		YM_OPEN_FILE(fullPath, READ_FLAG);
#else
		wchar_t wOpenPath[PATH_MAX];
		wcscpy(wOpenPath, ClientSparsePathW);
		wcscat(wOpenPath, L"/");
		wchar_t wAFile[NAME_MAX];
		mbstowcs(wAFile, aFile, strlen(aFile) + 1);
		wcscat(wOpenPath, wAFile);
		YM_OPEN_FILE(wOpenPath, READ_FLAG);
#endif
		YMFILE aSparseFd = (YMFILE)result;
		testassert(aSparseFd >= 0, "client file handle %s", fullPath);
        
        YMStringRef name = YMSTRCF("test-client-write-%s",aFile);
        YMStreamRef stream = YMConnectionCreateStream(connection, name, YMCompressionNone); // man pages are already compressed! i feel powerful!
        YMRelease(name);
        testassert(stream,"client stream %s",fullPath);
        
        struct stat sparseStat;
        result = stat(fullPath,&sparseStat);
        testassert(result==0,"stat %s",fullPath);
        theTest->lastClientFileSize = sparseStat.st_size; // ???
        theTest->lastClientAsync = arc4random_uniform(2); // don't let filehandle go out of scope on the last iter
        theTest->lastClientBounded = arc4random_uniform(2); // also tell remote whether we're bounding the file or pretending we don't know for testing
        struct SparseFileHeader header = { theTest->lastClientFileSize , {0}, theTest->lastClientBounded };
        strncpy(header.name, aFile, NAME_MAX+1);
        result = YMStreamWriteDown(stream, (uint8_t *)&header, sizeof(header));
        if ( theTest->stopping ) {
            YMConnectionCloseStream(connection, stream);
            YM_CLOSE_FILE(aSparseFd);
            break;
        }
        
        ym_connection_forward_context_t *ctx = NULL;
        if ( theTest->lastClientAsync ) {
            ctx = calloc(sizeof(struct ym_connection_forward_context_t),1);
            ctx->callback = _client_async_forward_callback;
            ctx->context = theTest;
        }
        
        ymdbg("writing sparse file '%s' %sbounded, %ssync from f%d",aFile,theTest->lastClientBounded?"":"un",theTest->lastClientAsync?"a":"",aSparseFd);
        okay = YMConnectionForwardFile(connection, aSparseFd, stream, theTest->lastClientBounded ? &theTest->lastClientFileSize : NULL, !theTest->lastClientAsync, ctx);
        testassert(okay,"forwardfile failed");

        if ( ! theTest->lastClientAsync ) {
            uint64_t dI,dO;
            YMStreamGetPerformance(stream,&dI,&dO,NULL,NULL);
            double percentCompressed = (1 - (double)dO / (double)dI) * 100;
            ymlog("wrote sparse file '%s' with %0.2f%% compression",aFile,percentCompressed);
        }

        if ( theTest->lastClientAsync )
            YMSemaphoreWait(theTest->connectAndAsyncClientCallbackSemaphore);
        
        struct SparseFileThanks thx;
        uint16_t outLength = 0, length = sizeof(thx);
        result = YMStreamReadUp(stream, (uint8_t *)&thx, length, &outLength);
        if ( theTest->stopping ) {
            YMConnectionCloseStream(connection, stream);
            YM_CLOSE_FILE(aSparseFd);
            break;
        }
        testassert(result==YMIOSuccess,"read thx header");
        testassert(length==outLength,"length!=outLength");
        YMStringRef thxFormatStr = YMSTRCF(THXFORSPARSETEMPLATE,header.name);
        const char *thxFormat = YMSTR(thxFormatStr);
        testassert(0==strcmp(thx.thx4Sparse,thxFormat),"%s, is this how one thx for sparse, man? %s",YMSTR(_YMStreamGetName(stream)),thx.thx4Sparse);
        YMRelease(thxFormatStr);

        YMConnectionCloseStream(connection, stream);
        YM_CLOSE_FILE(aSparseFd);
        
        actuallyWritten++;
        ymdbg("wrote the %luth sparse file, %d open files in process",actuallyWritten,YMGetNumberOfOpenFilesForCurrentProcess());
    }
    
    result = closedir(dir);
    testassert(result==0,"close DIR");
    
    theTest->nSparseFilesToRead = actuallyWritten;
    YMSemaphoreSignal(theTest->threadExitSemaphore);
    ymlog("write sparse files thread exiting");
}

YM_ENTRY_POINT(_EatASparseFile)
{
    YM_IO_BOILERPLATE

	struct TestConnectionStream *ctx = context;
    struct SessionTest *theTest = ctx->theTest;

    YMConnectionRef connection = ctx->connection;
    YMStreamRef stream = ctx->stream;
    
    struct SparseFileHeader header;
    uint16_t outLength = 0, length = sizeof(header);
    YMIOResult ymResult = YMStreamReadUp(stream, (uint8_t *)&header, length, &outLength);
    if ( theTest->stopping )
        goto catch_release;
    
    testassert(ymResult==YMIOSuccess&&outLength==length,"read sparse header");
    testassert(strlen(header.name)>0&&strlen(header.name)<=NAME_MAX, "??? %s",header.name);
    uint64_t outBytes = 0;
    
    char filePath[PATH_MAX];
    strcpy(filePath, YMSTR(theTest->tempSparseDir));
    strcat(filePath, "/");
    strcat(filePath, header.name);
    
    YM_STOMP_FILE(filePath, READ_WRITE_FLAG);
    YMFILE sparseDstFd = (YMFILE)result;
    testassert(sparseDstFd>=0,"create '%s' dst %d %s",header.name,errno,strerror(errno))
    
    uint64_t len64 = header.len;
    ymdbg("reading sparse file '%s'[%lu] %sbounded, sync to f%d",header.name,header.len,header.willBoundDataStream?"":"un",sparseDstFd);
    ymResult = YMStreamWriteToFile(stream, sparseDstFd, header.willBoundDataStream ? &len64 : NULL, &outBytes);
    testassert(ymResult==YMIOSuccess||(!header.willBoundDataStream&&ymResult==YMIOEOF),"eat sparse result");
    testassert(outBytes==header.len,"eat sparse result");
    ymdbg("read sparse file '%s'[%lu] bytes: %s : %s",header.name,outBytes,YMSTR(theTest->tempSparseDir),header.name);
    
	YM_CLOSE_FILE(sparseDstFd);
    testassert(result==0,"close sparse dst %s",header.name);
    
    struct SparseFileThanks thx;
    YMStringRef thxStr = YMSTRCF(THXFORSPARSETEMPLATE,header.name);
    strncpy(thx.thx4Sparse,YMSTR(thxStr),sizeof(thx.thx4Sparse));
    YMRelease(thxStr);
    YMStreamWriteDown(stream, (uint8_t *)&thx, sizeof(thx));
    
catch_release:
    // todo randomize whether we close here, during streamClosing, after streamClosing, dispatch_after?
    // it's also worth noting that if you [forcibly] interrupt the session and immediately
    // dealloc the session, async clients working on incoming streams might fault doing this
    YMConnectionCloseStream(connection,stream);
    
    theTest->nSparseFilesRead++;
    
    YMRelease(connection);
    YMRelease(stream);
}

YM_ENTRY_POINT(_EatLargeFile)
{
    YM_IO_BOILERPLATE;

	struct TestConnectionStream *ctx = context;
    struct SessionTest *theTest = ctx->theTest;

    YMConnectionRef connection = ctx->connection;
    YMStreamRef stream = ctx->stream;
    
    theTest->tempServerDst = YMSTRCF(LargeDestTemplate,ServerTestFile);
    result = unlink(YMSTR(theTest->tempServerDst));
    testassert(result==0||errno==ENOENT,"delete large dst file %d %s",errno,strerror(errno));
    YM_STOMP_FILE(YMSTR(theTest->tempServerDst),WRITE_FLAG);
    YMFILE largeOutFd = (YMFILE)result;
    testassert(largeOutFd>=0,"create out file");

    uint64_t outBytes = 0;
    ymlog("reading large %sbounded...",theTest->serverBounding?"":"un");
    YMIOResult ymResult = YMStreamWriteToFile(stream, largeOutFd, theTest->serverBounding ? &gSomeLength : NULL, &outBytes);
    testassert(ymResult!=YMIOError,"eat large result");
    testassert(!theTest->serverBounding||outBytes==gSomeLength,"eat large outBytes");

    uint64_t uI,uO;
    YMStreamGetPerformance(stream,NULL,NULL,&uI,&uO);
    double percentCompressed = (double)uI/(double)uO;
    ymlog("reading large finished: %lu bytes (%0.2f%% compression)",outBytes,percentCompressed);

    YM_CLOSE_FILE(largeOutFd);
    testassert(result==0, "close large out fd %d %s",errno,strerror(errno));
    
    // todo randomize whether we close here, during streamClosing, after streamClosing, dispatch_after?
    // it's also worth noting that if you [forcibly] interrupt the session and immediately
    // dealloc the session, async clients working on incoming streams might fault doing this
    YMConnectionCloseStream(connection,stream);
    
    YMSemaphoreSignal(theTest->threadExitSemaphore);
    
    ymlog("eat large exiting");
    
    YMRelease(connection);
    YMRelease(stream);
}

void _server_async_forward_callback(YMConnectionRef connection, YMStreamRef stream, YMIOResult result, uint64_t bytesWritten, void * ctx)
{
    ymdbg("%s %p %p %d %lu %p",__FUNCTION__,connection,stream,result,bytesWritten,ctx);
    struct SessionTest *theTest = ctx;
    _AsyncForwardCallback(theTest, connection, stream, result, bytesWritten, true);
}

void _client_async_forward_callback(YMConnectionRef connection, YMStreamRef stream, YMIOResult result, uint64_t bytesWritten, void * ctx)
{
    ymdbg("%s %p %p %d %lu %p",__FUNCTION__,connection,stream,result,bytesWritten,ctx);
    struct SessionTest *theTest = ctx;
    testassert(theTest,"async forward cb context");
    _AsyncForwardCallback(theTest, connection, stream, result, bytesWritten, false);
}

void _AsyncForwardCallback(struct SessionTest *theTest, YMConnectionRef connection, YMStreamRef stream, YMIOResult ioResult, uint64_t bytesWritten, bool isServer)
{
    YM_IO_BOILERPLATE

    testassert(connection,"connection nil");
    testassert(stream,"stream nil");
    testassert(ioResult==YMIOSuccess||(isServer&&!theTest->serverBounding&&ioResult==YMIOEOF)||
              (!isServer&&!theTest->lastClientBounded&&ioResult==YMIOEOF),"!ioResult");
    testassert((isServer&&theTest->serverAsync)||(!isServer&&theTest->lastClientAsync),"callback for sync forward (%d)",isServer);
    
    if ( isServer && theTest->serverBounding )
        testassert(bytesWritten==gSomeLength,"lengths don't match")
    else if ( ! isServer )
        testassert(bytesWritten==theTest->lastClientFileSize,"lengths don't match");

    uint64_t dI,dO;
    YMStreamGetPerformance(stream,&dI,&dO,NULL,NULL);
    double percentCompressed = (1 - (double)dO / (double)dI) * 100;

    ymlog("_async_forward_callback (%s): %lu (%0.2f%% compression)",isServer?"large":"sparse",bytesWritten,percentCompressed);
    
    if ( isServer ) {
        YMConnectionCloseStream(connection, stream);
        YMSemaphoreSignal(theTest->threadExitSemaphore);
        YM_CLOSE_FILE(theTest->largeSrcFd);
        testassert(result==0, "failed to close large src f f%d async",theTest->largeSrcFd);
    } else
        YMSemaphoreSignal(theTest->connectAndAsyncClientCallbackSemaphore);
}

// client, discover->connect
void _added_peer_func(YMSessionRef session, YMPeerRef peer, void *context)
{
    ymlog("%s",__FUNCTION__);
    struct SessionTest *theTest = context;
    
    testassert(theTest,"added context");
    testassert(session==theTest->clientSession,"added session");
    
    if ( theTest->stopping )
        return;
    
    if ( YMStringEquals(YMPeerGetName(peer), theTest->testName) ) {
        ymerr("resolving %s",YMSTR(YMPeerGetName(peer)));
        YMSessionResolvePeer(session, peer);
    }
}

void _removed_peer_func(YMSessionRef session, YMPeerRef peer, void *context)
{
    ymlog("%s",__FUNCTION__);
    struct SessionTest *theTest = context;
    
    testassert(theTest,"removed context");
    testassert(session==theTest->clientSession,"removed session");
    testassert(0==strcmp(YMSTR(YMPeerGetName(peer)),YMSTR(theTest->testName)),"removed name");
    testassert(theTest->stopping,"removed");
}

void _resolve_failed_func(YMSessionRef session, YMPeerRef peer, void *context)
{
    ymlog("%s",__FUNCTION__);
    struct SessionTest *theTest = context;
    
    testassert(theTest,"resolveFailed context");
    testassert(session==theTest->clientSession,"resolveFailed session");
    testassert(0==strcmp(YMSTR(YMPeerGetName(peer)),YMSTR(theTest->testName)),"resolveFailed name");
    testassert(false,"resolveFailed");
}

void _resolved_func(YMSessionRef session, YMPeerRef peer, void *context)
{
    ymlog("%s",__FUNCTION__);
    struct SessionTest *theTest = context;
    
    testassert(theTest,"resolved context");
    testassert(session==theTest->clientSession,"resolved session");
    testassert(0==strcmp(YMSTR(YMPeerGetName(peer)),YMSTR(theTest->testName)),"resolved name %s != %s",YMSTR(YMPeerGetName(peer)),YMSTR(theTest->testName));
    testassert(YMPeerGetAddresses(peer),"peer addresses empty");
    
    if ( theTest->stopping )
        return;
    
    uint32_t fakeDelay = 0;// arc4random_uniform(FAKE_DELAY_MAX);
    int64_t fakeDelayNsec = (int64_t)fakeDelay * 1000000000;
    //dispatch_after(dispatch_time(DISPATCH_TIME_NOW, fakeDelay), dispatch_get_global_queue(0, 0), ^{
        bool testSync = arc4random_uniform(2);
        ymlog("connecting to %s after %ld delay (%ssync)...",YMSTR(YMPeerGetName(peer)),fakeDelayNsec,testSync?"":"a");
        bool okay = YMSessionConnectToPeer(session,peer,testSync);
        testassert(okay,"client connect to peer");
        
        if ( testSync ) {
            YMConnectionRef connection = YMSessionGetDefaultConnection(session);
            _connected_func(session,connection,theTest);
        }
    //});
}

void _connect_failed_func(YMSessionRef session, YMPeerRef peer, bool moreComing, void *context)
{
    ymlog("%s",__FUNCTION__);
    struct SessionTest *theTest = context;
    
    testassert(theTest,"connectFailed context");
    testassert(session==theTest->clientSession,"connectFailed session");
    testassert(0==strcmp(YMSTR(YMPeerGetName(peer)),YMSTR(theTest->testName)),"connectFailed name");
    testassert(moreComing||YMSessionGetDefaultConnection(session),"connectFailed");
}

// server
bool _should_accept_func(YMSessionRef session, YMPeerRef peer, void *context)
{
    ymlog("%s",__FUNCTION__);
    struct SessionTest *theTest = context;
    
    testassert(theTest,"should accept context");
    testassert(session==theTest->serverSession,"shouldAccept session");
    testassert(peer,"shouldAccept peer");
    return true;
}

// connection
void _connected_func(YMSessionRef session, YMConnectionRef connection, void *context)
{
    ymlog("%s: s%p c%p",__FUNCTION__,(void*)session,(void*)connection);
    struct SessionTest *theTest = context;
    
    testassert(theTest,"connected context");
    testassert(session==theTest->clientSession||session==theTest->serverSession,"connected session");
    testassert(connection,"connected peer");
    
    if ( session == theTest->clientSession )
        theTest->clientConnection = connection;
    else
        theTest->serverConnection = connection;
            
    YMSemaphoreSignal(theTest->connectAndAsyncClientCallbackSemaphore);
}

void _interrupted_func(YMSessionRef session, void *context)
{
    ymlog("%s",__FUNCTION__);
    struct SessionTest *theTest = context;
    
    testassert(theTest,"interrupted context");
    testassert(session==theTest->clientSession||session==theTest->serverSession,"interrupted session");
    testassert(theTest->stopping,"interrupted");
}

// streams
void _new_stream_func(YMSessionRef session, YMConnectionRef connection, YMStreamRef stream, void *context)
{
    ymdbg("%s",__FUNCTION__);
    struct SessionTest *theTest = context;
    
    testassert(theTest,"newStream context");
    testassert(session==theTest->clientSession||session==theTest->serverSession,"newStream session");
    testassert(stream,"newStream stream");
    
    bool isServer = session==theTest->serverSession;
    
    struct TestConnectionStream *ctx = calloc(1,sizeof(struct TestConnectionStream));
    ctx->theTest = theTest;
    ctx->connection = YMRetain(connection);
    ctx->stream = YMRetain(stream);
    
    ym_dispatch_user_t dispatch = { (isServer ? _EatASparseFile : _EatLargeFile), ctx, NULL, ym_dispatch_user_context_free };
    YMDispatchAsync(isServer?theTest->serverQueue:theTest->clientQueue, &dispatch);
}

void _closing_func(YMSessionRef session, YMConnectionRef connection, YMStreamRef stream, void *context)
{
    ymdbg("%s",__FUNCTION__);
    struct SessionTest *theTest = context;
    
    testassert(theTest,"stream closing context");
    testassert(session==theTest->clientSession||session==theTest->serverSession,"streamClosing session");
    testassert(connection,"streamClosing connection");
    testassert(stream,"streamClosing stream");
}

YM_EXTERN_C_POP
