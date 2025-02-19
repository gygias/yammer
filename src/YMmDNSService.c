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

#if !defined(YMWIN32)
# include <sys/socket.h>
# if defined(YMLINUX)
#  include <netinet/in.h>
# endif
#else
# include <winsock2.h>
# include <ws2tcpip.h>
#endif

#include <dns_sd.h>

#define ymlog_type YMLogDefault
#include "YMLog.h"

YM_EXTERN_C_PUSH

typedef struct __ym_mdns_service
{
    _YMType _common;
    
    YMStringRef type;
    YMStringRef name;
    uint16_t port;
    
    // volatile stuff
    uint8_t *txtRecord;
    uint16_t txtRecordLen;
    DNSServiceRef *dnsService;
    bool advertising;
    YMThreadRef eventThread;
} __ym_mdns_service;
typedef struct __ym_mdns_service __ym_mdns_service_t;

void DNSSD_API __ymmdns_register_callback(__unused DNSServiceRef sdRef,
                                          __unused DNSServiceFlags flags,
                                          __unused DNSServiceErrorType errorCode,
                                          __unused const char *name,
                                          __unused const char *regtype,
                                          __unused const char *domain,
                                          void *context )
{
    __ym_mdns_service_t *s = context;
    ymsoftassert(0==strcmp(regtype,YMSTR(s->type)),"register type: %s",regtype);
    ymsoftassert(0==strcmp(name,YMSTR(s->name)),"register name: %s",name);
    ymlog("mdns: %s/%s:%u: %d", YMSTR(s->type), YMSTR(s->name), s->port, errorCode);
}

YMmDNSServiceRef YMmDNSServiceCreate(YMStringRef type, YMStringRef name, uint16_t port)
{
    // DNSServiceRegister will truncate this automatically, but keep the client well informed i suppose
    if ( ! name
        || YMLEN(name) >= mDNS_SERVICE_NAME_LENGTH_MAX
        || YMLEN(name) < mDNS_SERVICE_NAME_LENGTH_MIN ) {
        ymlog("mdns: invalid service name");
        return NULL;
    }

	YMNetworkingInit();
    
    __ym_mdns_service_t *s = (__ym_mdns_service_t *)_YMAlloc(_YMmDNSServiceTypeID,sizeof(__ym_mdns_service_t));
    
    s->type = YMRetain(type);
    s->name = YMRetain(name);
    s->port = port;
    s->advertising = false;
    s->txtRecord = NULL;
    s->txtRecordLen = 0;
    return s;
}

void _YMmDNSServiceFree(YMTypeRef o_)
{
    __ym_mdns_service_t *s = (__ym_mdns_service_t *)o_;
    if ( s->type )
        YMRelease(s->type);
    if ( s->name )
        YMRelease(s->name);
    if ( s->txtRecord ) {
        TXTRecordDeallocate((TXTRecordRef *)s->txtRecord);
        YMFREE(s->txtRecord);
    }
}

bool YMmDNSServiceSetTXTRecord( YMmDNSServiceRef s_, YMmDNSTxtRecordKeyPair *keyPairs[], size_t nPairs )
{
    __ym_mdns_service_t *s = (__ym_mdns_service_t *)s_;
    
    if ( keyPairs == NULL || nPairs == 0 )
        return false;
    
    size_t idx;
    
    TXTRecordRef *txtRecord = (TXTRecordRef *)YMALLOC(sizeof(TXTRecordRef));
    TXTRecordCreate(txtRecord, 0, NULL);
    for ( idx = 0; idx < nPairs; idx++ ) {
        YMmDNSTxtRecordKeyPair **_keyPairs = (YMmDNSTxtRecordKeyPair **)keyPairs;
        const char *key = YMSTR(_keyPairs[idx]->key);
        const uint8_t *value = _keyPairs[idx]->value;
        uint8_t valueLen = _keyPairs[idx]->valueLen;
        
        TXTRecordSetValue(txtRecord, key, valueLen, value);
    }
    
    s->txtRecord = (uint8_t *)txtRecord;
    
    return true;
}

bool YMmDNSServiceStart( YMmDNSServiceRef s_ )
{
    __ym_mdns_service_t *s = (__ym_mdns_service_t *)s_;
    
    DNSServiceRef *serviceRef = YMALLOC( sizeof(DNSServiceRef) );
    uint16_t netPort = htons(s->port);
    bool txtExists = (s->txtRecord != NULL);
    uint16_t txtLength = txtExists ? TXTRecordGetLength((TXTRecordRef *)s->txtRecord) : 0;
    const void *txt = txtExists ? TXTRecordGetBytesPtr((TXTRecordRef *)s->txtRecord) : NULL;
    DNSServiceErrorType result = DNSServiceRegister(serviceRef,
                                                    0, // DNSServiceFlags
                                                    0, // interfaceIndex (0=all)
                                                    YMSTR(s->name),
                                                    YMSTR(s->type),
                                                    NULL, // domain
                                                    NULL, // host
                                                    netPort,
                                                    txtLength,
                                                    txt,
                                                    __ymmdns_register_callback, // DNSServiceRegisterReply
                                                    s); // context
    
    if( result != kDNSServiceErr_NoError ) {
        // on error "the callback is never invoked and the DNSServiceRef is not initialized"
        // leading me to think we free instead of DNSServiceRefDeallocate
        YMFREE(serviceRef);
        ymlog("mdns: DNSServiceRegister failed: %s/%s:%u: %d",YMSTR(s->type),YMSTR(s->name),(unsigned)s->port,result);
        return false;
    }
    
    s->dnsService = serviceRef;
    s->advertising = true;

    ymlog("mdns: published %s/%s[%lu]]:%u",YMSTR(s->type),YMSTR(s->name),strlen(YMSTR(s->name)),(unsigned)s->port);
    return true;
}

bool YMmDNSServiceStop( YMmDNSServiceRef s_, bool synchronous )
{
    __ym_mdns_service_t *s = (__ym_mdns_service_t *)s_;
    
    if ( ! s->advertising )
        return false;
    
    s->advertising = false; // let event thread fall out
    
    bool okay = true;
    
    DNSServiceRefDeallocate(*(s->dnsService));
    YMFREE(s->dnsService);
    s->dnsService = NULL;
    
    if ( synchronous )
        okay = YMThreadJoin(s->eventThread);
    
    ymlog("mdns: browser stopping");
    return okay;
}

YM_EXTERN_C_POP
