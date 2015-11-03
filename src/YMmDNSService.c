//
//  YMmDNSService.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 Combobulated Software. All rights reserved.
//

#include "YMmDNSService.h"

#include <dns_sd.h>

typedef struct __YMmDNSService
{
    YMTypeID _typeID;
    
    char *type;
    char *name;
    uint8_t *txtRecord;
    bool advertising;
} _YMmDNSService;

YMmDNSServiceRef YMmDNSServiceCreate(char *type, char *name)
{
    _YMmDNSService *service = (_YMmDNSService *)calloc(1, sizeof(_YMmDNSService));
    service->type = strdup(type);
    service->name = strdup(name);
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
}