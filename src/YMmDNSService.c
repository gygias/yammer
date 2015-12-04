//
//  YMmDNSService.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMmDNSService.h"

#include "YMUtilities.h"
#include "YMThread.h"

#ifndef WIN32
#include <sys/socket.h>
# if defined(RPI)
# include <netinet/in.h>
# endif
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include <dns_sd.h>

#define ymlog_type YMLogmDNS
#include "YMLog.h"

YM_EXTERN_C_PUSH

typedef struct __ym_mdns_service_t
{
    _YMType _type;
    
    YMStringRef type;
    YMStringRef name;
    uint16_t port;
    
    // volatile stuff
    uint8_t *txtRecord;
    uint16_t txtRecordLen;
    DNSServiceRef *dnsService;
    bool advertising;
    YMThreadRef eventThread;
} __ym_mdns_service_t;
typedef struct __ym_mdns_service_t *__YMmDNSServiceRef;

void __YMmDNSRegisterCallback(__unused DNSServiceRef sdRef,
                              __unused DNSServiceFlags flags,
                              __unused DNSServiceErrorType errorCode,
                              __unused const char *name,
                              __unused const char *regtype,
                              __unused const char *domain,
                              void *context )
{
    __YMmDNSServiceRef service = (__YMmDNSServiceRef)context;
    ymlog("mdns: %s/%s:%u: %d", YMSTR(service->type), YMSTR(service->name), service->port, errorCode);
    // DNSServiceRefDeallocate?
}

//void __ym_mdns_service_event_thread(void *);

YMmDNSServiceRef YMmDNSServiceCreate(YMStringRef type, YMStringRef name, uint16_t port)
{
    // DNSServiceRegister will truncate this automatically, but keep the client well informed i suppose
    if ( ! name
        || YMLEN(name) >= mDNS_SERVICE_NAME_LENGTH_MAX
        || YMLEN(name) < mDNS_SERVICE_NAME_LENGTH_MIN )
    {
        ymlog("mdns: invalid service name");
        return NULL;
    }

	YMNetworkingInit();
    
    __YMmDNSServiceRef service = (__YMmDNSServiceRef)_YMAlloc(_YMmDNSServiceTypeID,sizeof(struct __ym_mdns_service_t));
    
    service->type = YMRetain(type);
    service->name = YMRetain(name);
    service->port = port;
    service->advertising = false;
    service->txtRecord = NULL;
    service->txtRecordLen = 0;
    return (YMmDNSServiceRef)service;
}

void _YMmDNSServiceFree(YMTypeRef object)
{
    __YMmDNSServiceRef service = (__YMmDNSServiceRef)object;
    if ( service->type )
        YMRelease(service->type);
    if ( service->name )
        YMRelease(service->name);
    if ( service->txtRecord )
#ifdef MANUAL_TXT
        free(service->txtRecord);
#else
        TXTRecordDeallocate((TXTRecordRef *)service->txtRecord);
#endif
}

// todo this should be replaced by the TxtRecord* family in dns_sd.h
bool YMmDNSServiceSetTXTRecord( YMmDNSServiceRef service_, YMmDNSTxtRecordKeyPair *keyPairs[], size_t nPairs )
{
    __YMmDNSServiceRef service = (__YMmDNSServiceRef)service_;
    
    if ( keyPairs == NULL || nPairs == 0 )
        return false;
    
    size_t idx;
#ifdef MANUAL_TXT
    size_t  offset = 0,
            bufferSize = 1024;
    char *buffer = calloc(1,bufferSize),
            *bufferWalker = buffer; // debugging, delete later
#endif
    
    TXTRecordRef *txtRecord = (TXTRecordRef *)YMALLOC(sizeof(TXTRecordRef));
    TXTRecordCreate(txtRecord, 0, NULL);
    for ( idx = 0; idx < nPairs; idx++ )
    {
        YMmDNSTxtRecordKeyPair **_keyPairs = (YMmDNSTxtRecordKeyPair **)keyPairs;
        const char *key = YMSTR(_keyPairs[idx]->key);
        const uint8_t *value = _keyPairs[idx]->value;
        uint8_t valueLen = _keyPairs[idx]->valueLen;
        
#ifndef MANUAL_TXT
        TXTRecordSetValue(txtRecord, key, valueLen, value);
#else
        YMmDNSTxtRecordKeyPair **_keyPairs = (YMmDNSTxtRecordKeyPair **)keyPairs;
        uint8_t keyLen = strlen(key);
        uint8_t keyEqualsLen = keyLen + 1;                  // key=
        uint8_t prefixedKeyEqualsLen = 1 + keyEqualsLen;    // %ckey=
        uint8_t tupleLen = prefixedKeyEqualsLen + valueLen; // %ckey=value
        
        
        while ( bufferSize - offset < tupleLen )
        {
            bufferSize *= 2;
            buffer = realloc(buffer, bufferSize);
        }
        
        ymlog("writing %dth keypair to %p + %u",idx,buffer,offset);
//        int written = snprintf(bufferWalker, prefixedKeyEqualsLen + 2, "%c%s=", tupleLen, key);
//        if ( written != prefixedKeyEqualsLen )
//        {
//            ymlog("YMmDNSServiceSetTXTRecord failed to format key '%s'",key);
//            return false;
//        }
        memcpy(buffer + offset, &tupleLen, sizeof(tupleLen));
        offset += sizeof(tupleLen);
        memcpy(buffer + offset, key, strlen(key));
        offset += strlen(key);
        memcpy(buffer + offset, "=", 1);
        offset += 1;
        memcpy(buffer + offset, value, valueLen);
        offset += valueLen;
        bufferWalker = buffer + offset;
#endif
    }
    
    service->txtRecord =
#ifdef MANUAL_TXT
        buffer;
    service->txtRecordLen = offset;
#else
        (uint8_t *)txtRecord;
#endif
    
    return true;
}

bool YMmDNSServiceStart( YMmDNSServiceRef service_ )
{
    __YMmDNSServiceRef service = (__YMmDNSServiceRef)service_;
    
    DNSServiceRef *serviceRef = (DNSServiceRef *)calloc( 1, sizeof(DNSServiceRef) );
    uint16_t netPort = htons(service->port);
    bool txtExists = (service->txtRecord != NULL);
    uint16_t txtLength = txtExists ? TXTRecordGetLength((TXTRecordRef *)service->txtRecord) : 0;
    const void *txt = txtExists ? TXTRecordGetBytesPtr((TXTRecordRef *)service->txtRecord) : NULL;
    DNSServiceErrorType result = DNSServiceRegister(serviceRef,
                                                    0, // DNSServiceFlags
                                                    0, // interfaceIndex (0=all)
                                                    YMSTR(service->name),
                                                    YMSTR(service->type),
                                                    NULL, // domain
                                                    NULL, // host
                                                    netPort,
#ifdef MANUAL_TXT
                                                    service->txtRecordLen,
                                                    service->txtRecord,
#else
                                                    txtLength,
                                                    txt,
#endif
                                                    (DNSServiceRegisterReply)__YMmDNSRegisterCallback, // DNSServiceRegisterReply
                                                    service); // context
    
    if( result != kDNSServiceErr_NoError )
    {
        // on error "the callback is never invoked and the DNSServiceRef is not initialized"
        // leading me to think we free instead of DNSServiceRefDeallocate
        free(serviceRef);
        ymlog("DNSServiceRegister failed: %d",result);
        return false;
    }
    
    service->dnsService = serviceRef;
    service->advertising = true;
    
//    YMStringRef threadName = YMStringCreateWithFormat("mdns-event-%s-%s",YMSTR(service->type),YMSTR(service->name),NULL);
//    YMThreadRef eventThread = YMThreadCreate(threadName, __ym_mdns_service_event_thread, service);
//    YMRelease(threadName);
//    
//    YMThreadStart(eventThread);

    ymlog("mdns: published %s/%s:%u",YMSTR(service->type),YMSTR(service->name),(unsigned)service->port);
    return true;
}

bool YMmDNSServiceStop( YMmDNSServiceRef service_, bool synchronous )
{
    __YMmDNSServiceRef service = (__YMmDNSServiceRef)service_;
    
    if ( ! service->advertising )
        return false;
    
    service->advertising = false; // let event thread fall out
    
    bool okay = true;
    
    DNSServiceRefDeallocate(*(service->dnsService));
    free(service->dnsService);
    service->dnsService = NULL;
    
    if ( synchronous )
        okay = YMThreadJoin(service->eventThread);
    
    ymlog("mdns: browser stopping");
    return okay;
}

//void __ym_mdns_service_event_thread(void * ctx)
//{
//    __YMmDNSServiceRef service = (__YMmDNSServiceRef)ctx;
//    YMRetain(service);
//    
//    ymlog("mdns: event thread for %s entered...",YMSTR(service->name));
//    while (service->advertising) {
//        sleep(1);
//    }
//    ymlog("mdns: event thread for %s exiting...",YMSTR(service->name));
//    YMRelease(service);
//}

YM_EXTERN_C_POP
