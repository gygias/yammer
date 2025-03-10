//
//  TLSTests.c
//  yammer
//
//  Created by david on 12/2/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "TLSTests.h"

#include "YMTLSProvider.h"
#include "YMLock.h"
#include "YMSemaphore.h"
#include "YMLocalSocketPair.h"
#include "YMSocket.h"
#include "YMUtilities.h"

#if defined (YMLINUX)
#include <signal.h>
#endif

YM_EXTERN_C_PUSH

#define TLSTestRoundTrips 5
#define TLSTestMessageRoundTrips 1000
#define TLSTestRandomMessages true
#define TLSTestRandomMessageMaxLength 1024

//#define TLSTestIndefinite
#define TLSTestTimeBased false // todo fix deadlock if client or server happens to be in read/write when time runs out, either close fds or find a cleaner way
#if TLSTestTimeBased
#ifdef TLSTestIndefinite
#define TLSTestEndDate ([NSDate distantFuture])
#else
#define TLSTestEndDate ([NSDate dateWithTimeIntervalSinceNow:10])
#endif
#endif

typedef struct TLSTest
{
    ym_test_assert_func assert;
    const void *context;
    
    YMLockRef stateLock;
    YMSemaphoreRef threadExitSemaphore;
    size_t bytesIn;
    size_t bytesOut;
    bool timeBasedEnd;
    uint8_t *lastMessageSent;
    uint16_t lastMessageSentLen;
} TLSTest;

void _TestTLS1(struct TLSTest *theTest);

typedef struct RunEndpointContext
{
    struct TLSTest *theTest;
    YMTLSProviderRef tls;
    bool isServer;
} RunEndpointContext;

YM_ENTRY_POINT(_RunEndpoint);
void _SendAMessage(struct TLSTest *theTest, YMTLSProviderRef tls, uint8_t *message, uint16_t messageLen);
uint8_t *_ReceiveAMessage(struct TLSTest *theTest, YMTLSProviderRef tls, uint16_t *outLen);

void __sigpipe_handler (__unused int signum)
{
    fprintf(stderr,"sigpipe happened\n");
}

void __tls_test_socket_disconnected(YMSocketRef s, const void *ctx)
{
    struct TLSTest *t = (struct TLSTest *)ctx;
    ymerr("socket disconnected! %p -- %p",(void *)s,(void *)t);
}

void TLSTestsRun(ym_test_assert_func assert, const void *context)
{
    YMStringRef name = YMSTRC("TLSTest");
    struct TLSTest theTest = { assert, context, YMLockCreateWithOptionsAndName(YMInternalLockType, name), YMSemaphoreCreateWithName(name, 0), 0, 0, false, NULL, 0 };
    YMRelease(name);
    
    _TestTLS1(&theTest);
    ymerr(" TLSTest1 completed %zub in %zub out",theTest.bytesIn,theTest.bytesOut);
    
    YMRelease(theTest.stateLock);
    YMRelease(theTest.threadExitSemaphore);
    YMFREE(theTest.lastMessageSent);
}

void _TestTLS1(struct TLSTest *theTest)
{
    bool localIsServer = arc4random_uniform(2);
    
    YM_IO_BOILERPLATE
    
    const char *template = "ym-tls-test-%u";
    uint32_t suffix = arc4random();
    
    YMStringRef sockName = YMSTRCF(template,suffix);    
    YMLocalSocketPairRef localSocketPair = YMLocalSocketPairCreate(sockName, false);
    YMRelease(sockName);
    testassert(localSocketPair,"socket pair didn't initialize");
    YMSOCKET serverSocket = YMLocalSocketPairGetA(localSocketPair);
    YMSOCKET clientSocket = YMLocalSocketPairGetB(localSocketPair);
    
    testassert(serverSocket>=0,"server socket is bogus");
    testassert(clientSocket>=0,"client socket is bogus");
    
    bool serverFirst = arc4random_uniform(2);
    const char *testBuffer = "moshi moshi";
    size_t testLen = (ssize_t)strlen(testBuffer) + 1;
	
	YM_WRITE_SOCKET(serverFirst?serverSocket:clientSocket, testBuffer, testLen);
    testassert((size_t)result==testLen,"failed to write test message: %d (%s)",errno,strerror(errno));
    char testIncoming[32];
    
	YM_READ_SOCKET(serverFirst?clientSocket:serverSocket, testIncoming, testLen);
    testassert((size_t)result==testLen,"failed to receive test message: %d (%s)",errno,strerror(errno));
    testassert(strncmp(testBuffer,testIncoming,result)==0,"received test message does not match");
    
    YMSocketRef localSocket = YMSocketCreate(__tls_test_socket_disconnected,theTest);
    YMSocketSet(localSocket, localIsServer ? serverSocket : clientSocket);
    YMSocketRef remoteSocket = YMSocketCreate(__tls_test_socket_disconnected,theTest);
    YMSocketSet(remoteSocket, localIsServer ? clientSocket : serverSocket);
    
    YMTLSProviderRef localProvider = YMTLSProviderCreate(YMSocketGetOutput(localSocket), YMSocketGetInput(localSocket), localIsServer);
    testassert(localProvider,"local provider didn't initialize");
    YMTLSProviderRef remoteProvider = YMTLSProviderCreate(YMSocketGetOutput(remoteSocket), YMSocketGetInput(remoteSocket), !localIsServer);
    testassert(remoteProvider,"remote provider didn't initialize");
    
    YMTLSProviderRef theServer = localIsServer?localProvider:remoteProvider;
    YMTLSProviderRef theClient = localIsServer?remoteProvider:localProvider;
    
    struct RunEndpointContext serverContext = { theTest, theServer, true };
    ym_dispatch_user_t serverUser = { _RunEndpoint, &serverContext, NULL, ym_dispatch_user_context_noop };
    YMDispatchAsync(YMDispatchGetGlobalQueue(), &serverUser);
    
    sleep(2); // xxx let server reach accept()
    
    struct RunEndpointContext clientContext = { theTest, theClient, false };
    ym_dispatch_user_t clientUser = { _RunEndpoint, &clientContext, NULL, ym_dispatch_user_context_noop };
    YMDispatchAsync(YMDispatchGetGlobalQueue(), &clientUser);
    
#if TLSTestTimeBased
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, [TLSTestEndDate timeIntervalSinceDate:[NSDate date]], false);
#endif
    
    // signal threads to exit and wait for them
    YMLockLock(theTest->stateLock); // ensure the iterate the same number of times (for time-based where this thread flags the end)
    theTest->timeBasedEnd = true;
    YMLockUnlock(theTest->stateLock);
    YMSemaphoreWait(theTest->threadExitSemaphore); // todo: there's still a deadlock bug in this code, where server races client and timeBasedEnd to try to receive an additional message
    YMSemaphoreWait(theTest->threadExitSemaphore);
    
    usleep(500000);
    signal(SIGPIPE, __sigpipe_handler);
    
    bool clientFirst = arc4random_uniform(2);
    bool okay = YMSecurityProviderClose((YMSecurityProviderRef)(clientFirst?theClient:theServer));
    testassert(okay,"client close failed");
    
    okay = YMSecurityProviderClose((YMSecurityProviderRef)(clientFirst?theServer:theClient));
    testassert(okay,"server close failed");
    
    YMRelease(clientFirst?theClient:theServer);
    YMRelease(clientFirst?theServer:theClient);
    YMRelease(localSocketPair);
    YMRelease(localSocket);
    YMRelease(remoteSocket);
    
    usleep(500000);
    signal(SIGPIPE, SIG_DFL);
    
    ymlog("tls test finished (%lu in, %lu out)",theTest->bytesIn,theTest->bytesOut);
}

const char *testMessage = "security is important. put in the advanced technology. technology consultants international. please check. thanks.";
const char *testResponse = "creative technologist? or technology creative? foodie. quirky. here is a picture of my half-eaten food. ask me about my background in typesetting.";

YM_ENTRY_POINT(_RunEndpoint)
{
	struct RunEndpointContext *ctx = context;
    struct TLSTest *theTest = ctx->theTest;
    YMTLSProviderRef tls = ctx->tls;
    bool isServer = ctx->isServer;
    
    bool okay = YMSecurityProviderInit((YMSecurityProviderRef)tls);
    testassert(okay,"provider init");
    
    bool looped = false;
    for ( unsigned idx = 0; (TLSTestTimeBased && ! theTest->timeBasedEnd) || (idx < TLSTestMessageRoundTrips); idx++ ) {
        if ( looped )
            YMLockUnlock(theTest->stateLock);
        
        uint8_t *outgoingMessage;
        uint16_t outgoingMessageLen;
        uint16_t incomingMessageLen;
        if ( TLSTestRandomMessages ) {
            outgoingMessageLen = (uint16_t)arc4random_uniform(TLSTestRandomMessageMaxLength) + 1;
            outgoingMessage = calloc(1,outgoingMessageLen);
            YMRandomDataWithLength(outgoingMessage,outgoingMessageLen);
        } else {
            outgoingMessage = (uint8_t *)testMessage;
            outgoingMessageLen = (uint16_t)strlen(testMessage);
        }
        
        if ( isServer ) {
            uint8_t *incomingMessage = _ReceiveAMessage(theTest,tls,&incomingMessageLen);
            testassert(incomingMessageLen&&theTest->lastMessageSentLen&&0==memcmp(incomingMessage, theTest->lastMessageSent, incomingMessageLen),
                      "incoming and last written do not match (i%zu o%zu)",incomingMessageLen,theTest->lastMessageSentLen);
            free(incomingMessage);
            
            if ( TLSTestRandomMessages ) free(theTest->lastMessageSent);
            theTest->lastMessageSent = outgoingMessage;
            theTest->lastMessageSentLen = outgoingMessageLen;
            _SendAMessage(theTest, tls, outgoingMessage, outgoingMessageLen);
        } else {
            if ( TLSTestRandomMessages ) free(theTest->lastMessageSent);
            theTest->lastMessageSent = outgoingMessage;
            theTest->lastMessageSentLen = outgoingMessageLen;
            _SendAMessage(theTest, tls, outgoingMessage, outgoingMessageLen);
            
            uint8_t *incomingMessage = _ReceiveAMessage(theTest, tls, &incomingMessageLen);
            testassert(incomingMessageLen&&theTest->lastMessageSentLen&&0==memcmp(incomingMessage, theTest->lastMessageSent, incomingMessageLen),
                      "incoming and last written do not match (i%zu o%zu)",incomingMessageLen,theTest->lastMessageSentLen);
            free(incomingMessage);
        }
        
        
        // for races between server, client and main thread signaling time-based end
        looped = true;
        YMLockLock(theTest->stateLock);
        theTest->bytesOut += outgoingMessageLen;
        theTest->bytesIn += incomingMessageLen;
    }
    
    if ( looped )
        YMLockUnlock(theTest->stateLock);
    
    ymdbg("run %s exiting...",isServer?"server":"client");
    YMSemaphoreSignal(theTest->threadExitSemaphore);
}

void _SendAMessage(__unused struct TLSTest *theTest, YMTLSProviderRef tls, uint8_t *message, uint16_t messageLen)
{
    bool okay = YMSecurityProviderWrite((YMSecurityProviderRef)tls, (void *)&messageLen, sizeof(messageLen));
    testassert(okay,"failed to write message length");
    okay = YMSecurityProviderWrite((YMSecurityProviderRef)tls, message, messageLen);
    testassert(okay,"failed to write message");
}

uint8_t *_ReceiveAMessage(struct TLSTest *theTest, YMTLSProviderRef tls, uint16_t *outLen)
{
    uint16_t length;
    bool okay = YMSecurityProviderRead((YMSecurityProviderRef)tls, (void *)&length, sizeof(length));
    testassert(okay,"failed to read header");
    uint8_t *buffer = malloc(length);
    YMSecurityProviderRead((YMSecurityProviderRef)tls, (void*)buffer, length);
    testassert(okay,"failed to read buffer");
    
    *outLen = length;
    return buffer;
}

YM_EXTERN_C_POP
