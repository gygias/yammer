//
//  YMmDNSService.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMmDNSService.h"
#include "YMPrivate.h"
#include "YMThreads.h"

#include <sys/socket.h>
#include <dns_sd.h>

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

void _YMmDNSReplyCallback( DNSServiceRef sdRef,
                            DNSServiceFlags flags,
                            DNSServiceErrorType errorCode,
                            const char                          *name,
                            const char                          *regtype,
                            const char                          *domain,
                            void                                *context )
{
    YMmDNSServiceRef service = (YMmDNSServiceRef)context;
    YMLog("_YMmDNSReplyCallback: %s/%s:%u: %d", service->type, service->name, service->port, errorCode);
}

void *_YMmDNSEventThread(void *context);

YMmDNSServiceRef YMmDNSServiceCreate(const char *type, const char *name, uint16_t port)
{
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
        free(service->txtRecord);
    free(service);
}

void YMmDNSServiceSetTXTRecord( YMmDNSServiceRef service, YMmDNSTxtRecordKeyPair *keyPairs[], size_t nPairs )
{
    int idx;
    size_t  offset = 0,
            bufferSize = 1024;
    uint8_t *buffer = calloc(1,bufferSize);
    for ( idx = 0; idx < nPairs; idx++ )
    {
        YMmDNSTxtRecordKeyPair **_keyPairs = (YMmDNSTxtRecordKeyPair **)keyPairs;
        const char *key = _keyPairs[idx]->key;
        const uint8_t *value = _keyPairs[idx]->value;
        uint8_t tupleLen = strlen(key) + 1 + _keyPairs[idx]->valueLen;
        size_t formatLength = 1 + tupleLen;
        
        if ( bufferSize - offset < formatLength )
        {
            bufferSize *= 2;
            buffer = realloc(buffer, bufferSize);
        }
        
        snprintf((char *)(buffer + offset), formatLength, "%c%s=%s", tupleLen, key, value);
        offset += formatLength;
    }
    
    service->txtRecord = buffer;
    service->txtRecordLen = offset;
}

bool YMmDNSServiceStart( YMmDNSServiceRef service )
{
    service->dnsService = (DNSServiceRef *)calloc( 1, sizeof(DNSServiceRef) );
    uint16_t netPort = htons(service->port);
    DNSServiceErrorType result = DNSServiceRegister(service->dnsService,
                                                    0, // DNSServiceFlags
                                                    0, // interfaceIndex (0=all)
                                                    service->name,
                                                    service->type,
                                                    NULL, // domain
                                                    NULL, // host
                                                    netPort,
                                                    service->txtRecordLen,
                                                    service->txtRecord,
                                                    _YMmDNSReplyCallback, // DNSServiceRegisterReply
                                                    service); // context
    
    if( result != kDNSServiceErr_NoError )
    {
        free(service->dnsService);
        YMLog("DNSServiceRegister failed: %d",result);
        return false;
    }
    
    service->advertising = true;
    YMThreadRef eventThread = YMThreadCreate(_YMmDNSEventThread, service);
    YMThreadStart(eventThread);

    YMLog("YMmDNSService published %s@%s:%u",service->name,service->type,service->port);
    return true;
}

bool YMmDNSServiceStop( YMmDNSServiceRef service, bool synchronous )
{
    service->advertising = false;
    
    bool okay = true;
    if ( synchronous )
        okay = YMThreadJoin(service->eventThread);
    
    return okay;
}

void *_YMmDNSEventThread(void *context)
{
    YMmDNSServiceRef service = (YMmDNSServiceRef)context;
    YMLog("event thread for %s entered...",service->name);
    while (service->advertising) {
        sleep(1);
    }
    return NULL;
}
