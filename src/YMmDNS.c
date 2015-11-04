//
//  YMmDNS.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMmDNS.h"

void _YMmDNSServiceListFree(YMmDNSServiceList *serviceList)
{
    YMmDNSServiceList *aListItem = serviceList;
    while ( aListItem )
    {
        struct _YMmDNSServiceRecord *service = (struct _YMmDNSServiceRecord *)aListItem->service;
        if ( service )
            _YMmDNSServiceRecordFree(service);
    }
}

void _YMmDNSServiceRecordFree(YMmDNSServiceRecord *record)
{
    if ( record->name )
        free( (char *)record->name );
    if ( record->type )
        free( (char *)record->type );
    if ( record->domain )
        free( (char *)record->domain );
    if ( record->txtRecordKeyPairs )
        _YMmDNSTxtRecordKeyPairsFree( (YMmDNSTxtRecordKeyPair **)record->txtRecordKeyPairs, record->txtRecordKeyPairsSize );
}

void _YMmDNSTxtRecordKeyPairsFree(YMmDNSTxtRecordKeyPair **keyPairList, size_t size)
{
    size_t idx;
    for ( idx = 0; idx < size; idx++ )
    {
        YMmDNSTxtRecordKeyPair *aPair = keyPairList[idx];
        if ( aPair->key )
            free((char *)aPair->key);
        if ( aPair->value )
            free((void *)aPair->value);
    }
}

YMmDNSServiceRecord *_YMmDNSCreateServiceRecord(const char *name, const char*type, const char *domain, bool resolved, uint16_t port, const unsigned char *txtRecord)
{
    YMmDNSServiceRecord *record = (YMmDNSServiceRecord *)malloc(sizeof(struct _YMmDNSServiceRecord));
    if ( name )
        record->name = strdup(name);
    if ( type )
        record->type = strdup(type);
    if ( domain )
        record->domain = strdup(domain);
    record->resolved = resolved;
    record->port = port;
    
    if ( txtRecord )
    {
        size_t txtSize = 0;
        record->txtRecordKeyPairs = __YMmDNSCreateTxtKeyPairs(txtRecord, &txtSize);
        record->txtRecordKeyPairsSize = txtSize;
    }
    
    return record;
}

YMmDNSTxtRecordKeyPair **__YMmDNSCreateTxtKeyPairs(const unsigned char *txtRecord, size_t *outSize)
{
    size_t  allocatedListSize = 20,
            listSize = 0;
    YMmDNSTxtRecordKeyPair **keyPairList = (YMmDNSTxtRecordKeyPair **)malloc(allocatedListSize * sizeof(YMmDNSTxtRecordKeyPair*));
    
    uint64_t pairOffset = 0;
    uint8_t pairLength = txtRecord[pairOffset++];
    while ( pairLength > 0 )
    {
        if ( listSize == allocatedListSize )
        {
            allocatedListSize *= 2;
            keyPairList = (YMmDNSTxtRecordKeyPair **)realloc(keyPairList, allocatedListSize * sizeof(struct YMmDNSTxtRecordKeyPair*)); // could be optimized
        }
        
        uint8_t subPairOffset = 0;
        char *keyStr = (char *)malloc(pairLength); // could be optimized
        while ( txtRecord[pairOffset + subPairOffset++] != '=' )
            keyStr[pairOffset] = txtRecord[pairOffset + subPairOffset];
        uint8_t *valueBuf = (uint8_t *)malloc(pairLength); // could be optimized
        uint8_t valueOffset = 0;
        while ( subPairOffset < pairLength )
            valueBuf[valueOffset++] = txtRecord[pairOffset + subPairOffset++];
        
        YMmDNSTxtRecordKeyPair *aKeyPair = (YMmDNSTxtRecordKeyPair *)malloc(sizeof(YMmDNSTxtRecordKeyPair));
        aKeyPair->key = keyStr;
        aKeyPair->value = valueBuf;
        aKeyPair->valueLen = valueOffset - 1;
        keyPairList[listSize] = aKeyPair;
        
        pairOffset += pairLength;
        pairLength = txtRecord[pairOffset++];
        
        listSize++;
    }
    
    if ( outSize )
        *outSize = listSize;
    
    return keyPairList;
}