//
//  YMmDNSService.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMmDNSService.h"
#include "YMPrivate.h"
#include "YMUtilities.h"

#include "YMThread.h"

#include <sys/socket.h>
#include <dns_sd.h>

#include "YMLog.h"
#undef ymlog_type
#define ymlog_type YMLogmDNS
#if ( ymlog_type > ymlog_target )
#undef ymlog
#define ymlog(x,...) ;
#endif

typedef struct __YMmDNSService
{
    YMTypeID _typeID;
    
    char *type;
    char *name;
    uint16_t port;
    
    // volatile stuff
    uint8_t *txtRecord;
    uint16_t txtRecordLen;
    DNSServiceRef *dnsService;
    bool advertising;
    YMThreadRef eventThread;
} _YMmDNSService;

void _YMmDNSRegisterCallback(__unused DNSServiceRef sdRef,
                            __unused DNSServiceFlags flags,
                            __unused DNSServiceErrorType errorCode,
                            __unused const char *name,
                            __unused const char *regtype,
                            __unused const char *domain,
                            void *context )
{
    __unused YMmDNSServiceRef service = (YMmDNSServiceRef)context;
    ymlog("_YMmDNSRegisterCallback: %s/%s:%u: %d", service->type, service->name, service->port, errorCode);
    // DNSServiceRefDeallocate?
}

void __ym_mdns_service_event_thread(void *);

YMmDNSServiceRef YMmDNSServiceCreate(const char *type, const char *name, uint16_t port)
{
    // DNSServiceRegister will truncate this automatically, but keep the client well informed i suppose
    if ( ! name
        || strlen(name) >= mDNS_SERVICE_NAME_LENGTH_MAX
        || strlen(name) < mDNS_SERVICE_NAME_LENGTH_MIN )
    {
        ymlog("invalid service name specified to YMmDNSServiceCreate");
        return NULL;
    }
    
    _YMmDNSService *service = (_YMmDNSService *)calloc(1, sizeof(_YMmDNSService));
    service->_typeID = _YMmDNSServiceTypeID;
    
    service->type = strdup(type);
    service->name = strdup(name);
    service->port = port;
    service->advertising = false;
    service->txtRecord = NULL;
    return (YMmDNSServiceRef)service;
}

void _YMmDNSServiceFree(YMTypeRef object)
{
    _YMmDNSService *service = (_YMmDNSService *)object;
    if ( service->type )
        free(service->type);
    if ( service->name )
        free(service->name);
    if ( service->txtRecord )
#ifdef MANUAL_TXT
        free(service->txtRecord);
#else
        TXTRecordDeallocate((TXTRecordRef *)service->txtRecord);
#endif
    free(service);
}

// todo this should be replaced by the TxtRecord* family in dns_sd.h
ymbool YMmDNSServiceSetTXTRecord( YMmDNSServiceRef service, YMmDNSTxtRecordKeyPair *keyPairs[], size_t nPairs )
{
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
        const char *key = _keyPairs[idx]->key;
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

ymbool YMmDNSServiceStart( YMmDNSServiceRef service )
{
    DNSServiceRef *serviceRef = (DNSServiceRef *)calloc( 1, sizeof(DNSServiceRef) );
    uint16_t netPort = htons(service->port);
    bool txtExists = (service->txtRecord != NULL);
    uint16_t txtLength = txtExists ? TXTRecordGetLength((TXTRecordRef *)service->txtRecord) : 0;
    const void *txt = txtExists ? TXTRecordGetBytesPtr((TXTRecordRef *)service->txtRecord) : NULL;
    DNSServiceErrorType result = DNSServiceRegister(serviceRef,
                                                    0, // DNSServiceFlags
                                                    0, // interfaceIndex (0=all)
                                                    service->name,
                                                    service->type,
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
                                                    _YMmDNSRegisterCallback, // DNSServiceRegisterReply
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
    
    char *threadName = YMStringCreateWithFormat("mdns-event-%s-%s",service->type,service->name);
    YMThreadRef eventThread = YMThreadCreate(threadName, __ym_mdns_service_event_thread, service);
    free(threadName);
    
    YMThreadStart(eventThread);

    ymlog("YMmDNSService published %s/%s:%u",service->type,service->name,(unsigned)service->port);
    return true;
}

ymbool YMmDNSServiceStop( YMmDNSServiceRef service, ymbool synchronous )
{
    if ( ! service->advertising )
        return false;
    
    service->advertising = false; // let event thread fall out
    
    ymbool okay = true;
    
    DNSServiceRefDeallocate(*(service->dnsService));
    free(service->dnsService);
    service->dnsService = NULL;
    
    if ( synchronous )
        okay = YMThreadJoin(service->eventThread);
    
    ymlog("YMmDNSService stopping");
    return okay;
}

void __ym_mdns_service_event_thread(void * ctx)
{
    YMmDNSServiceRef service = (YMmDNSServiceRef)ctx;
    ymlog("event thread for %s entered...",service->name);
    while (service->advertising) {
        sleep(1);
    }
    ymlog("event thread for %s exiting...",service->name);
}
