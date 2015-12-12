//
//  YMmDNS.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMmDNS.h"

#include "YMAddress.h"

#if !defined(YMWIN32)
# if defined(YMLINUX)
#  define __USE_POSIX
# endif
# include <netdb.h>
#else
# define _WINSOCK_DEPRECATED_NO_WARNINGS // todo, gethostbyname
# include <winsock2.h>
# include <ws2tcpip.h>
#endif

//#include <dns_sd.h>

// mDNS rules:
//
// service type:
// The service type must be an underscore, followed
// *                  by 1-15 characters, which may be letters, digits, or hyphens.
// *                  The transport protocol must be "_tcp" or "_udp".
//
// service name:
// If a name is specified, it must be 1-63 bytes of UTF-8 text.
// *                  If the name is longer than 63 bytes it will be automatically truncated
// *                  to a legal length, unless the NoAutoRename flag is set,
// *                  in which case kDNSServiceErr_BadParam will be returned.
//
// txt record:
// http://www.zeroconf.org/rendezvous/txtrecords.html
// The characters of "Name" MUST be printable US-ASCII values (0x20-0x7E), excluding '=' (0x3D).
// Passing NULL for the txtRecord is allowed as a synonym for txtLen=1, txtRecord="",
// *                  i.e. it creates a TXT record of length one containing a single empty string.
// *                  RFC 1035 doesn't allow a TXT record to contain *zero* strings, so a single empty
// *                  string is the smallest legal DNS TXT record.

#define ymlog_type YMLogmDNS
#include "YMLog.h"

YM_EXTERN_C_PUSH

void _YMmDNSServiceListFree(YMmDNSServiceList *serviceList)
{
    YMmDNSServiceList *listIter = serviceList;
    while ( listIter )
    {
        YMmDNSServiceList *aListItem = listIter;
        struct _YMmDNSServiceRecord *service = (struct _YMmDNSServiceRecord *)aListItem->service;
        if ( service )
            _YMmDNSServiceRecordFree(service, false);
        listIter = listIter->next;
        free(aListItem);
    }
}

YMmDNSServiceRecord *_YMmDNSServiceRecordCreate(const char *name, const char*type, const char *domain)
{
	if ( ! name || ! type || ! domain )
		return NULL;
	
    YMmDNSServiceRecord *record = (YMmDNSServiceRecord *)YMALLOC(sizeof(struct _YMmDNSServiceRecord));
    
    if ( name )
        record->name = YMSTRC(name);
    else
        record->name = NULL;
    
    if ( type )
        record->type = YMSTRC(type);
    else
        record->type = NULL;
    
    if ( domain )
        record->domain = YMSTRC(domain);
    else
        record->domain = NULL;
    
    record->complete = false;
    record->port = 0;
    record->txtRecordKeyPairs = NULL;
    record->txtRecordKeyPairsSize = 0;
    record->sockaddrList = NULL;
    
    return record;
}

void YMAPI _YMmDNSServiceRecordSetPort(YMmDNSServiceRecord *record, uint16_t port)
{
    record->port = port;
}

void YMAPI _YMmDNSServiceRecordSetTxtRecord(YMmDNSServiceRecord *record, const unsigned char *txtRecord, uint16_t txtLength)
{
    size_t txtSize = 0;
    YMmDNSTxtRecordKeyPair **txtList = NULL;
    
    if ( txtRecord && txtLength > 1 )
    {
        txtList = _YMmDNSTxtKeyPairsCreate(txtRecord, txtLength, &txtSize);
        if ( ! txtList )
        {
            ymerr("mdns: failed to parse txt record for %s:%s",YMSTR(record->type),YMSTR(record->name));
            return;
        }
    }
    
    if ( record->txtRecordKeyPairs )
        _YMmDNSTxtKeyPairsFree(record->txtRecordKeyPairs, record->txtRecordKeyPairsSize);
    
    record->txtRecordKeyPairsSize = txtSize;
    record->txtRecordKeyPairs = txtList;
}

void YMAPI _YMmDNSServiceRecordAppendSockaddr(YMmDNSServiceRecord *record, const void *mdnsPortlessSockaddr_)
{
    const struct sockaddr *mdnsPortlessSockaddr = mdnsPortlessSockaddr_;
    if ( ! record->sockaddrList )
        record->sockaddrList = YMArrayCreate();
    
    for ( int i = 0; i < YMArrayGetCount(record->sockaddrList); i++ ) {
        YMAddressRef aAddress = (YMAddressRef)YMArrayGet(record->sockaddrList, i);
        const struct sockaddr *aSockaddr = YMAddressGetAddressData(aAddress);
        if ( mdnsPortlessSockaddr->sa_family == aSockaddr->sa_family ) {
            if ( aSockaddr->sa_family == AF_INET ) {
                if ( ((struct sockaddr_in *)aSockaddr)->sin_addr.s_addr == ((struct sockaddr_in *)mdnsPortlessSockaddr)->sin_addr.s_addr ) {
                    return;
                    ymerr("service record already contains this ipv4 address");
                }
            } else if ( aSockaddr->sa_family == AF_INET6 ) {
                if ( memcmp(&((struct sockaddr_in6 *)aSockaddr)->sin6_addr,&((struct sockaddr_in6 *)mdnsPortlessSockaddr)->sin6_addr,sizeof(struct in6_addr)) ) {
                    ymerr("service record already contains this ipv6 address");
                    return;
                }
            }
        }
    }
    
    YMAddressRef address = YMAddressCreate(mdnsPortlessSockaddr,record->port);
    YMArrayAdd(record->sockaddrList, address);
}

void YMAPI _YMmDNSServiceRecordSetComplete(YMmDNSServiceRecord *record)
{
    record->complete = true;
}

void _YMmDNSServiceRecordFree(YMmDNSServiceRecord *record, bool floatResolvedInfo)
{
    if ( record->name )
        YMRelease(record->name);
    if ( record->type )
        YMRelease(record->type);
    if ( record->domain )
        YMRelease(record->domain);
    if ( ! floatResolvedInfo ) // allows in-place update of existing service record
    {
        if ( record->sockaddrList ) {
            while ( YMArrayGetCount(record->sockaddrList) > 0 ) {
                YMAddressRef address = (YMAddressRef)YMArrayGet(record->sockaddrList, 0);
                YMRelease(address);
                YMArrayRemove(record->sockaddrList, 0);
            }
            YMRelease(record->sockaddrList);
        }
        if ( record->txtRecordKeyPairs )
            _YMmDNSTxtKeyPairsFree( (YMmDNSTxtRecordKeyPair **)record->txtRecordKeyPairs, record->txtRecordKeyPairsSize );
    }
    free(record);
}

YMmDNSTxtRecordKeyPair **_YMmDNSTxtKeyPairsCreate(const unsigned char *txtRecord, uint16_t txtLength, size_t *outSize)
{
    if ( txtLength <= 1 )
        return NULL;
    
    size_t  allocatedListSize = 20,
            listSize = 0;
    YMmDNSTxtRecordKeyPair **keyPairList = (YMmDNSTxtRecordKeyPair **)YMALLOC(allocatedListSize * sizeof(YMmDNSTxtRecordKeyPair*));
    
	char keyBuf[256];
    size_t currentPairOffset = 0;
    uint8_t aPairLength = txtRecord[currentPairOffset];
    while ( currentPairOffset < txtLength )
    {
        const unsigned char* aPairWalker = txtRecord + currentPairOffset + 1;
        __unused const char* debugThisKey = (char *)aPairWalker;
        
        if ( listSize == allocatedListSize )
        {
            allocatedListSize *= 2;
            keyPairList = (YMmDNSTxtRecordKeyPair **)realloc(keyPairList, allocatedListSize * sizeof(YMmDNSTxtRecordKeyPair*)); // could be optimized
        }
        
        char *equalsPtr = strstr((const char *)aPairWalker,"=");
        if ( equalsPtr == NULL )
        {
            ymlog("mdns: warning: failed to parse %ub txt record",txtLength);
            _YMmDNSTxtKeyPairsFree(keyPairList, listSize);
            return NULL;
        }
        
        size_t keyLength = (size_t)(equalsPtr - (char *)aPairWalker);
        memcpy(keyBuf,aPairWalker,keyLength);
		keyBuf[keyLength] = '\0';
        aPairWalker += keyLength + 1; // skip past '='
        
        size_t valueLength = aPairLength - keyLength - 1;
        uint8_t *valueBuf = YMALLOC(valueLength);
        memcpy(valueBuf, aPairWalker, valueLength);
        
        YMmDNSTxtRecordKeyPair *aKeyPair = (YMmDNSTxtRecordKeyPair *)YMALLOC(sizeof(YMmDNSTxtRecordKeyPair));
        aKeyPair->key = YMSTRC(keyBuf);
        aKeyPair->value = valueBuf;
        aKeyPair->valueLen = (uint8_t)valueLength;
        keyPairList[listSize] = aKeyPair;
        
        ymlog("mdns: parsed [%zd][%zu] <- [%zu]'%s'",listSize,valueLength,keyLength,keyBuf);
        
        listSize++;
        
        currentPairOffset += aPairLength + 1;
        if ( currentPairOffset == txtLength )
            break;
        
        aPairLength = txtRecord[currentPairOffset];
    }
    
    if ( outSize )
        *outSize = listSize;
    
    ymlog("mdns: created list size %zd from blob length %u",listSize,txtLength);
    
    return keyPairList;
}

void _YMmDNSTxtKeyPairsFree(YMmDNSTxtRecordKeyPair **keyPairList, size_t size)
{
    size_t idx;
    for ( idx = 0; idx < size; idx++ )
    {
        YMmDNSTxtRecordKeyPair *aPair = keyPairList[idx];
        if ( aPair )
        {
            if ( aPair->key )
                YMRelease(aPair->key);
            if ( aPair->value )
                free((void*)aPair->value);
            free(aPair);
        }
    }
    free(keyPairList);
}

unsigned char  *_YMmDNSTxtBlobCreate(YMmDNSTxtRecordKeyPair **keyPairList, uint16_t *inSizeOutLength)
{
    size_t listSize = *inSizeOutLength;
    size_t blobSize = 0;
    
    uint8_t keyValueLenMax = UINT8_MAX - 1; // assuming data can be empty, and len byte isn't part of the 'max'
    for( size_t i = 0; i < listSize; i++ )
    {
        size_t keyLen = YMStringGetLength(keyPairList[i]->key);
        if ( keyLen > keyValueLenMax )
        {
            ymerr("mdns: key is too long for txt record (%zd)",keyLen);
            return NULL;
        }
        
        uint8_t valueLen = keyPairList[i]->valueLen;
        if ( keyLen + valueLen > keyValueLenMax )
        {
            ymerr("mdns: key+value are too long for txt record (%zd+%u)",keyLen,valueLen);
            return NULL;
        }
        
        uint16_t aBlobSize = (uint8_t)keyLen + valueLen + 2;
        if ( ( blobSize + aBlobSize ) > UINT16_MAX )
        {
            ymerr("mdns: keyPairList exceeds uint16 max");
            return NULL;
        }
        blobSize += aBlobSize;
    }
    
    *inSizeOutLength = (uint16_t)blobSize;
    
    if ( blobSize == 0 )
        return NULL;
    
    unsigned char *txtBlob = YMALLOC(blobSize);
    size_t off = 0;
    for ( size_t i = 0; i < listSize; i++ )
    {
        size_t keyLen = YMStringGetLength(keyPairList[i]->key);
        txtBlob[off] = (unsigned char)keyLen +
                                        1 +
                                        keyPairList[i]->valueLen;
        memcpy(txtBlob+off+1, YMSTR(keyPairList[i]->key), keyLen);
        txtBlob[off+1+keyLen] = '=';
        memcpy(txtBlob+off+1+keyLen+1, keyPairList[i]->value, keyPairList[i]->valueLen);
        off += 1 + keyLen + 1 + keyPairList[i]->valueLen;
    }
    
    return txtBlob;
}

YM_EXTERN_C_POP
