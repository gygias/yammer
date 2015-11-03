//
//  YMmDNSService.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 Combobulated Software. All rights reserved.
//

#include "YMmDNSService.h"
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
    DNSServiceRef dnsService;
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

YMmDNSServiceRef YMmDNSServiceCreate(char *type, char *name, uint16_t port)
{
    _YMmDNSService *service = (_YMmDNSService *)calloc(1, sizeof(_YMmDNSService));
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

void YMmDNSServiceSetTXTRecord( YMmDNSServiceRef service, YMmDNSTxtRecordKeyPair *keyPairs[], int nPairs )
{
    int idx;
    size_t  offset = 0,
            bufferSize = 1024;
    uint8_t *buffer = calloc(1,bufferSize);
    for ( idx = 0; idx < nPairs; idx++ )
    {
        char *key = keyPairs[idx]->key;
        uint8_t *value = keyPairs[idx]->value;
        uint8_t tupleLen = strlen(key) + 1 + keyPairs[idx]->valueLen;
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

bool YMmDNSServicePublish( YMmDNSServiceRef service, int port )
{
    DNSServiceRef *sdRef = (DNSServiceRef *)calloc( 1, sizeof(DNSServiceRef) );
    uint16_t netPort = htons(service->port);
    DNSServiceErrorType result = DNSServiceRegister(sdRef,
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
        YMLog("DNSServiceRegister failed: %d",result);
        return false;
    }
    
    service->advertising = true;
    YMThreadRef eventThread = YMThreadCreate(_YMmDNSEventThread, service);
    YMThreadStart(eventThread);

    YMLog("YMmDNSService published %s@%s:%u",service->name,service->type,service->port);
    return true;
}

void *_YMmDNSEventThread(void *context)
{
    YMmDNSServiceRef service = (YMmDNSServiceRef)context;
    YMLog("event thread for %s entered...",service->name);
    while (true) {
        sleep(1);
    }
    return NULL;
}
