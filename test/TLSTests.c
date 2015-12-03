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
#include "YMThread.h"

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
    uint64_t bytesIn;
    uint64_t bytesOut;
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

void _RunEndpoint(struct RunEndpointContext *context);
void _SendMessage(struct TLSTest *theTest, YMTLSProviderRef tls, uint8_t *message, uint16_t messageLen);
uint8_t *_ReceiveMessage(struct TLSTest *theTest, YMTLSProviderRef tls, uint16_t *outLen);

void __sigpipe_handler (__unused int signum)
{
    fprintf(stderr,"sigpipe happened\n");
}

void TLSTestRun(ym_test_assert_func assert, const void *context)
{
    YMStringRef name = YMSTRC("TLSTest");
    struct TLSTest theTest = { assert, context, YMLockCreateWithOptionsAndName(YMInternalLockType, name), YMSemaphoreCreateWithName(name, 0), 0, 0, false, NULL, 0 };
    YMRelease(name);
    
    _TestTLS1(&theTest);
    
}

void _TestTLS1(struct TLSTest *theTest)
{
    bool localIsServer = arc4random_uniform(2);
    
    const char *template = "ym-tls-test-%u";
    uint32_t suffix = arc4random();
    
    YMStringRef sockName = YMSTRCF(template,suffix);
    testassert(sockName,"ymstring: %s",YMSTR(sockName));
    
    YMLocalSocketPairRef localSocketPair = YMLocalSocketPairCreate(sockName, false);
    YMRelease(sockName);
    testassert(localSocketPair,"socket pair didn't initialize");
    int serverSocket = YMLocalSocketPairGetA(localSocketPair);
    int clientSocket = YMLocalSocketPairGetB(localSocketPair);
    
    testassert(serverSocket>=0,"server socket is bogus");
    testassert(clientSocket>=0,"client socket is bogus");
    
    bool serverFirst = arc4random_uniform(2);
    const char *testBuffer = "moshi moshi";
    ssize_t testLen = (ssize_t)strlen(testBuffer) + 1;
    ssize_t testResult = write(serverFirst?serverSocket:clientSocket, testBuffer, testLen);
    testassert(testResult==testLen,"failed to write test message: %d (%s)",errno,strerror(errno));
    char testIncoming[testLen];
    testResult = read(serverFirst?clientSocket:serverSocket, testIncoming, testLen);
    testassert(testResult==testLen,"failed to receive test message: %d (%s)",errno,strerror(errno));
    testassert(strcmp(testBuffer,testIncoming)==0,"received test message does not match");
    
    YMTLSProviderRef localProvider = YMTLSProviderCreateWithSocket(localIsServer ? serverSocket : clientSocket, localIsServer);
    testassert(localProvider,"local provider didn't initialize");
    YMTLSProviderRef remoteProvider = YMTLSProviderCreateWithSocket(localIsServer ? clientSocket : serverSocket, !localIsServer);
    testassert(remoteProvider,"remote provider didn't initialize");
    
    YMTLSProviderRef theServer = localIsServer?localProvider:remoteProvider;
    YMTLSProviderRef theClient = localIsServer?remoteProvider:localProvider;
    
    struct RunEndpointContext serverContext = { theTest, theServer, true };
    YMThreadRef serverThread = YMThreadCreate(NULL, (void (*)(void *))_RunEndpoint, &serverContext);
    YMThreadStart(serverThread);
    
    sleep(2); // xxx let server reach accept()
    
    struct RunEndpointContext clientContext = { theTest, theClient, false };
    YMThreadRef clientThread = YMThreadCreate(NULL, (void (*)(void *))_RunEndpoint, &clientContext);
    YMThreadStart(clientThread);
    
#if TLSTestTimeBased
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, [TLSTestEndDate timeIntervalSinceDate:[NSDate date]], false);
#endif
    
    // signal threads to exit and wait for them
    YMLockLock(theTest->stateLock); // ensure the iterate the same number of times (for time-based where this thread flags the end)
    theTest->timeBasedEnd = true;
    YMLockUnlock(theTest->stateLock);
    YMSemaphoreWait(theTest->threadExitSemaphore); // todo: there's still a deadlock bug in this code, where server races client and timeBasedEnd to try to receive an additional message
    YMSemaphoreWait(theTest->threadExitSemaphore);
    
    bool clientFirst = arc4random_uniform(2);
    bool okay = YMSecurityProviderClose((YMSecurityProviderRef)(clientFirst?theClient:theServer));
    testassert(okay,"client close failed");
    
    signal(SIGPIPE, __sigpipe_handler);
    okay = YMSecurityProviderClose((YMSecurityProviderRef)(clientFirst?theServer:theClient));
    testassert(okay,"server close failed");
    
    YMRelease(clientFirst?theClient:theServer);
    YMRelease(clientFirst?theServer:theClient);
    YMRelease(localSocketPair);
    
    signal(SIGPIPE, SIG_DFL);
    ymlog("tls test finished (%llu in, %llu out)",theTest->bytesIn,theTest->bytesOut);
}

const char *testMessage = "security is important. put in the advanced technology. technology consultants international. please check. thanks.";
const char *testResponse = "creative technologist? or technology creative? foodie. quirky. here is a picture of my half-eaten food. ask me about my background in typesetting.";

void _RunEndpoint(struct RunEndpointContext *context)
{
    struct TLSTest *theTest = context->theTest;
    YMTLSProviderRef tls = context->tls;
    bool isServer = context->isServer;
    
    bool okay = YMSecurityProviderInit((YMSecurityProviderRef)tls);
    testassert(okay,"provider init");
    
    bool looped = false;
    for ( unsigned idx = 0; (TLSTestTimeBased && ! theTest->timeBasedEnd) || (idx < TLSTestMessageRoundTrips); idx++ )
    {
        if ( looped )
            YMLockUnlock(theTest->stateLock);
        
        uint8_t *outgoingMessage;
        uint16_t outgoingMessageLen;
        uint16_t incomingMessageLen;
        if ( TLSTestRandomMessages )
            outgoingMessage = YMRandomDataWithMaxLength(TLSTestRandomMessageMaxLength,&outgoingMessageLen);
        else
        {
            outgoingMessage = (uint8_t *)testMessage;
            outgoingMessageLen = strlen(testMessage);
        }
        
        if ( isServer )
        {
            uint8_t *incomingMessage = _ReceiveMessage(theTest,tls,&incomingMessageLen);
            testassert(incomingMessageLen&&theTest->lastMessageSentLen&&0==memcmp(incomingMessage, theTest->lastMessageSent, incomingMessageLen),
                      "incoming and last written do not match (i%zu o%zu)",incomingMessageLen,theTest->lastMessageSentLen);
            free(incomingMessage);
            
            if ( TLSTestRandomMessages && theTest->lastMessageSent ) free(theTest->lastMessageSent);
            theTest->lastMessageSent = outgoingMessage;
            theTest->lastMessageSentLen = outgoingMessageLen;
            _SendMessage(theTest, tls, outgoingMessage, outgoingMessageLen);
        }
        else
        {
            if ( TLSTestRandomMessages && theTest->lastMessageSent ) free(theTest->lastMessageSent);
            theTest->lastMessageSent = outgoingMessage;
            theTest->lastMessageSentLen = outgoingMessageLen;
            _SendMessage(theTest, tls, outgoingMessage, outgoingMessageLen);
            
            uint8_t *incomingMessage = _ReceiveMessage(theTest, tls, &incomingMessageLen);
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
    
    ymlog("run %s exiting...",isServer?"server":"client");
    YMSemaphoreSignal(theTest->threadExitSemaphore);
}

void _SendMessage(struct TLSTest *theTest, YMTLSProviderRef tls, uint8_t *message, uint16_t messageLen)
{
    bool okay = YMSecurityProviderWrite((YMSecurityProviderRef)tls, (void *)&messageLen, sizeof(messageLen));
    testassert(okay,"failed to write message length");
    okay = YMSecurityProviderWrite((YMSecurityProviderRef)tls, message, messageLen);
    testassert(okay,"failed to write message");
}

uint8_t *_ReceiveMessage(struct TLSTest *theTest, YMTLSProviderRef tls, uint16_t *outLen)
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
