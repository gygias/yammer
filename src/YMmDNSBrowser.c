//
//  YMmDNSBrowser.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMmDNSBrowser.h"

#include "YMUtilities.h"
#include "YMAddress.h"
#include "YMDictionary.h"
#include "YMDispatch.h"

#include <dns_sd.h>
#include <errno.h>

#if !defined(YMWIN32)
# if defined(YMLINUX)
#  ifndef __USE_POSIX
#   define __USE_POSIX
#  endif
#  include <netinet/in.h>
# endif
# include <netdb.h>
#else
# include <winsock2.h>
# include <ws2tcpip.h>
#endif

#define YM_EVENT_SIGNAL_CONTINUE    0xBE
#define YM_EVENT_SIGNAL_EXIT        0xEE

#define ymlog_pre "mdns%s"
#define ymlog_args ": "
#define ymlog_type YMLogmDNS
#include "YMLog.h"

YM_EXTERN_C_PUSH

typedef struct __ym_mdns_browser
{
    _YMType _common;
    
    YMStringRef type;
    
    bool browsing;
    YMArrayRef services;
    YMArrayRef serviceRefs;
    YMArrayRef sources;
    
    bool resolving;
    
    ym_mdns_service_appeared_func serviceAppeared;
    ym_mdns_service_removed_func serviceRemoved;
    ym_mdns_service_updated_func serviceUpdated;
    ym_mdns_service_resolved_func serviceResolved;
    void *callbackContext;
} __ym_mdns_browser;
typedef struct __ym_mdns_browser __ym_mdns_browser_t;

#pragma mark callback/event goo
static void DNSSD_API __ym_mdns_browse_callback(DNSServiceRef sdRef, DNSServiceFlags flags, uint32_t interfaceIndex, DNSServiceErrorType errorCode, const char *serviceName,
                                            const char *reg_type, const char *reply_domain, void *context);
void DNSSD_API __ym_mdns_resolve_callback(DNSServiceRef serviceRef, DNSServiceFlags flags, uint32_t interfaceIndex, DNSServiceErrorType errorCode, const char *name,
                                      const char *host, uint16_t port, uint16_t txtLen, const unsigned char *txtRecord, void *context );

YM_ENTRY_POINT(__ym_mdns_event_proc);
YMmDNSServiceRecord *__YMmDNSBrowserAddOrUpdateService(__ym_mdns_browser_t *, YMmDNSServiceRecord*);
void __YMmDNSBrowserRemoveServiceNamed(__ym_mdns_browser_t *, YMStringRef);
YMmDNSServiceRecord *__YMmDNSBrowserGetServiceWithName(__ym_mdns_browser_t *, YMStringRef, bool);
void __YMmDNSBrowserAddServiceRef(__ym_mdns_browser_t *, DNSServiceRef);
void __YMmDNSBrowserRemoveServiceRef(__ym_mdns_browser_t *, DNSServiceRef);

YMmDNSBrowserRef YMmDNSBrowserCreate(YMStringRef type)
{
    return YMmDNSBrowserCreateWithCallbacks(type, NULL, NULL, NULL, NULL, NULL);
}

YMmDNSBrowserRef YMmDNSBrowserCreateWithCallbacks(YMStringRef type,
                                                  ym_mdns_service_appeared_func serviceAppeared,
                                                  ym_mdns_service_updated_func serviceUpdated,
                                                  ym_mdns_service_resolved_func serviceResolved,
                                                  ym_mdns_service_removed_func serviceRemoved,
                                                  void *context)
{
	YMNetworkingInit();

    __ym_mdns_browser_t *b = (__ym_mdns_browser_t *)_YMAlloc(_YMmDNSBrowserTypeID,sizeof(__ym_mdns_browser_t));
    
    b->type = YMRetain(type);
    b->services = YMArrayCreate();
    b->serviceRefs = YMArrayCreate();
    b->sources = YMArrayCreate();
    YMmDNSBrowserSetServiceAppearedFunc(b, serviceAppeared);
    YMmDNSBrowserSetServiceUpdatedFunc(b, serviceUpdated);
    YMmDNSBrowserSetServiceResolvedFunc(b, serviceResolved);
    YMmDNSBrowserSetServiceRemovedFunc(b, serviceRemoved);
    YMmDNSBrowserSetCallbackContext(b, context);

    return b;
}

void _YMmDNSBrowserFree(YMTypeRef o_)
{
    __ym_mdns_browser_t *b = (__ym_mdns_browser_t *)o_;
    
    YMmDNSBrowserStop(b);
    
    if ( b->type )
        YMRelease(b->type);
    
    YMSelfLock(b);
    for( int i = 0; i < YMArrayGetCount(b->serviceRefs); i++ ) {
        DNSServiceRef sdref = (DNSServiceRef)YMArrayGet(b->serviceRefs,i);
        ym_dispatch_source_t source = (ym_dispatch_source_t)YMArrayGet(b->sources,i);
        YMDispatchSourceDestroy(source);
        YMFREE(sdref);
    }
    YMRelease(b->serviceRefs);
    YMRelease(b->sources);
    
    for ( int i = 0; i < YMArrayGetCount(b->services); i++ ) {
        YMmDNSServiceRecord *record = (YMmDNSServiceRecord *)YMArrayGet(b->services,i);
        _YMmDNSServiceRecordFree(record,false);
    }
    YMRelease(b->services);
    YMSelfUnlock(b);
}

void YMmDNSBrowserSetServiceAppearedFunc(YMmDNSBrowserRef b, ym_mdns_service_appeared_func func)
{
    ((__ym_mdns_browser_t *)b)->serviceAppeared = func;
}

void YMmDNSBrowserSetServiceRemovedFunc(YMmDNSBrowserRef b, ym_mdns_service_removed_func func)
{
    ((__ym_mdns_browser_t *)b)->serviceRemoved = func;
}

void YMmDNSBrowserSetServiceUpdatedFunc(YMmDNSBrowserRef b, ym_mdns_service_updated_func func)
{
    ((__ym_mdns_browser_t *)b)->serviceUpdated = func;
}

void YMmDNSBrowserSetServiceResolvedFunc(YMmDNSBrowserRef b, ym_mdns_service_resolved_func func)
{
    ((__ym_mdns_browser_t *)b)->serviceResolved = func;
}

void YMmDNSBrowserSetCallbackContext(YMmDNSBrowserRef b, void *context)
{
    ((__ym_mdns_browser_t *)b)->callbackContext = context;
}

bool YMmDNSBrowserStart(YMmDNSBrowserRef b_)
{
    __ym_mdns_browser_t *b = (__ym_mdns_browser_t *)b_;
    
    if ( b->browsing )
        return false;
    b->browsing = true;

    DNSServiceRef sdref = NULL;
    
    DNSServiceErrorType result = DNSServiceBrowse(&sdref, // DNSServiceRef
                                                  0, // DNSServiceFlags
                                                  0, // interfaceIndex
                                                  YMSTR(b->type), // type
                                                  NULL, // domain
                                                  __ym_mdns_browse_callback, // callback
                                                  b); // context
    
    if( result != kDNSServiceErr_NoError ) {
        ymerr("service browse '%s' failed: %d", YMSTR(b->type), result);
        return false;
    }
    
    __YMmDNSBrowserAddServiceRef(b,sdref);
    
    return true;
}

bool YMmDNSBrowserStop(YMmDNSBrowserRef b_)
{
    __ym_mdns_browser_t *b = (__ym_mdns_browser_t *)b_;

	if ( ! b->browsing )
		return false;
    
    __YMmDNSBrowserRemoveServiceRef(b, NULL);

	b->browsing = false;

    return true;
}

typedef struct __ym_mdns_resolve {
    __ym_mdns_browser_t *b;
    YMStringRef unescapedName;
} __ym_mdns_resolve;
typedef struct __ym_mdns_resolve __ym_mdns_resolve_t;

bool YMmDNSBrowserResolve(YMmDNSBrowserRef b_, YMStringRef serviceName)
{
    __ym_mdns_browser_t *b = (__ym_mdns_browser_t *)b_;
    YMmDNSServiceRecord *theService = __YMmDNSBrowserGetServiceWithName(b, serviceName, false);
    if ( ! theService ) {
        ymerr("asked to resolve '%s::%s' without record",YMSTR(b->type),YMSTR(serviceName));
        return false;
    }
    
    DNSServiceRef sdref;
    __ym_mdns_resolve_t *ctx = YMALLOC(sizeof(__ym_mdns_resolve_t));
    ctx->b = (__ym_mdns_browser_t *)YMRetain(b);
    ctx->unescapedName = YMRetain(serviceName);

    DNSServiceErrorType result = DNSServiceResolve ( &sdref, // DNSServiceRef
                                                    0, // DNSServiceFlags
                                                    0, // interfaceIndex
                                                    YMSTR(serviceName),
                                                    YMSTR(b->type), // type
                                                    YMSTR(theService->domain), // domain
                                                    __ym_mdns_resolve_callback,
                                                    ctx );

    if ( result != kDNSServiceErr_NoError ) {
        ymerr("service resolve '%s' failed: %d",YMSTR(b->type),result);
        YMRelease(ctx->b);
        YMRelease(ctx->unescapedName);
        YMFREE(ctx);
        
        YMFREE(sdref);

        return false;
    }

    __YMmDNSBrowserAddServiceRef(b, sdref);
    
    ymlog("resolving '%s::%s' (%s)...", YMSTR(b->type), YMSTR(serviceName), theService->domain ? YMSTR(theService->domain) : "(NULL)");
    
    return true;
}

YMmDNSServiceRecord *__YMmDNSBrowserAddOrUpdateService(__ym_mdns_browser_t *b, YMmDNSServiceRecord *record)
{
    YMSelfLock(b);
    for( int i = 0; i < YMArrayGetCount(b->services); i++ ) {
        YMmDNSServiceRecord *service = (YMmDNSServiceRecord *)YMArrayGet(b->services,i);

        if ( 0 == strcmp(YMSTR(record->name),YMSTR(service->name)) ) {
            
            // the mDNSResponder api behaves such that when ProcessResult is called either from
            // either the browser event thread or after Resolve, we're called back out to on the
            // same thread, so if the client performs a resolve synchronously upon service discovery,
            // we're actually in:
            // event thread -> browse callback -> appeared callback -> resolve -> resolve callback
            // ServiceRecords are public via the C api, so we can't safely replace this object here.
            // we assume name type and domain will never move beneath us, only that upon resolution
            // we get address information, so update the structure here.
            // we don't update synchronize around updating these members, concurrent client calls
            // on the same service are reasonably not thread safe.
            // an optimization would be to refactor these private methods so that we didn't construct
            // a throw-away copy of the record structure.
            if ( record->txtRecordKeyPairs )
                _YMmDNSTxtKeyPairsFree(record->txtRecordKeyPairs, record->txtRecordKeyPairsSize);
            if ( record->sockaddrList ) {
                while ( YMArrayGetCount(record->sockaddrList) > 0 ) {
                    YMAddressRef address = (YMAddressRef)YMArrayGet(record->sockaddrList, 0);
                    YMRelease(address);
                    YMArrayRemove(record->sockaddrList, 0);
                }
                YMRelease(record->sockaddrList);
            }
            
            record->txtRecordKeyPairs = record->txtRecordKeyPairs;
            record->txtRecordKeyPairsSize = record->txtRecordKeyPairsSize;
            record->sockaddrList = record->sockaddrList ? YMRetain(record->sockaddrList) : NULL;
            record->port = record->port;
            
            _YMmDNSServiceRecordFree(record, true);
            
            if ( b->serviceUpdated )
                b->serviceUpdated(b, record, b->callbackContext);
            YMSelfUnlock(b);
            return record;
        }
    }
    
    // add
    YMArrayAdd(b->services,record);
    YMSelfUnlock(b);
    
    if ( b->serviceAppeared )
        b->serviceAppeared(b, record, b->callbackContext);
    
    return record;
}

YMmDNSServiceRecord *__YMmDNSBrowserGetServiceWithName(__ym_mdns_browser_t *b, YMStringRef name, bool remove)
{
    YMmDNSServiceRecord *matchedRecord = NULL;

    YMSelfLock(b);
    for( int i = 0; i < YMArrayGetCount(b->services); i++ ) {
        YMmDNSServiceRecord *aRecord = (YMmDNSServiceRecord *)YMArrayGet(b->services,i);
        if ( 0 == strcmp(YMSTR(aRecord->name), YMSTR(name)) ) {
            if ( remove ) {
                YMArrayRemove(b->services,i);
                _YMmDNSServiceRecordFree(aRecord,false);
            }
            else
                matchedRecord = aRecord;
            break;
        }
    }
    YMSelfUnlock(b);
    
    return matchedRecord;
}

YMmDNSServiceRecord *YMmDNSBrowserGetServiceWithName(YMmDNSBrowserRef b_, YMStringRef name)
{
    return __YMmDNSBrowserGetServiceWithName((__ym_mdns_browser_t *)b_, name, false);
}

void __YMmDNSBrowserRemoveServiceNamed(__ym_mdns_browser_t *b, YMStringRef name)
{
    __YMmDNSBrowserGetServiceWithName(b, name, true);
    if ( b->serviceRemoved )
        b->serviceRemoved(b, name, b->callbackContext);
}

typedef struct __ym_mdns_context
{
    YMmDNSBrowserRef browser;
    DNSServiceRef sdref;
    YMDispatchQueueRef queue;
} __ym_mdns_context;
typedef struct __ym_mdns_context __ym_mdns_context_t;

YM_ENTRY_POINT(__ym_mdns_source_service) // ServicesServices.framework
{
    __ym_mdns_context *c = context;
    __ym_mdns_browser_t *b = (__ym_mdns_browser_t *)c->browser;

    YMFILE fd = DNSServiceRefSockFD(c->sdref);
    if ( fd == -1 ) {
        ymlog("%s fd == -1, do nothing",__FUNCTION__);
        return;
    }

    bool tracking = false;
    YMSelfLock(b);
    tracking = YMArrayContains(b->serviceRefs,c->sdref);
    YMSelfUnlock(b);
    if ( ! tracking ) {
        ymlog("%s no longer tracking %p",__FUNCTION__,c->sdref);
        return;
    }

    ymlog("processing service ref fd f%d s%p",fd,c->sdref);

    DNSServiceErrorType err = DNSServiceProcessResult(c->sdref);
    if (err != kDNSServiceErr_NoError) {
        ymerr("mdns: process result on f%d failed: %d", fd, err);
        __YMmDNSBrowserRemoveServiceRef(b,NULL);
    }
}

YM_ENTRY_POINT(__ym_mdns_source_destroy)
{
    __ym_mdns_context_t *c = context;
    YMDispatchQueueRelease(c->queue);
}

void __YMmDNSBrowserAddServiceRef(__ym_mdns_browser_t *b, DNSServiceRef serviceRef)
{
    __ym_mdns_context_t *c = YMALLOC(sizeof(__ym_mdns_context_t));
    c->browser = b;
    c->sdref = serviceRef;
    YMStringRef name = YMStringCreateWithFormat("com.combobulated.mdns.browser.sdref.%p",serviceRef,NULL);
    c->queue = YMDispatchQueueCreate(name);
    YMRelease(name);

    YMSelfLock(b);
    ym_dispatch_user_t user = { __ym_mdns_source_service, c, __ym_mdns_source_destroy, ym_dispatch_user_context_free };
    ym_dispatch_source_t source = YMDispatchSourceCreate(c->queue, ym_dispatch_source_readable, DNSServiceRefSockFD(serviceRef), &user);
    YMArrayAdd(b->serviceRefs,serviceRef);
    YMArrayAdd(b->sources,source);
    YMSelfUnlock(b);

    ymlog("added active sdref: %p (%d)",serviceRef,DNSServiceRefSockFD(serviceRef)); // socketFD is probably not valid at this point
}

YM_ENTRY_POINT(__ym_mdns_remove_ref_async)
{
    ym_dispatch_source_t source = context;
    YMDispatchSourceDestroy(source);
}

void __YMmDNSBrowserRemoveServiceRef(__ym_mdns_browser_t *b, DNSServiceRef serviceRef)
{
    YMSelfLock(b);
    bool removed = false;
    for ( int i = 0; i < YMArrayGetCount(b->serviceRefs); i++ ) {
        DNSServiceRef sdref = (DNSServiceRef)YMArrayGet(b->serviceRefs,i);
        ym_dispatch_source_t source = (ym_dispatch_source_t)YMArrayGet(b->sources,i);
        
        // the pointer we pass into DNSServiceProcessResult, from event thread (below us here), isn't the same as the one passed in here
        if ( ! serviceRef || DNSServiceRefSockFD(sdref) == DNSServiceRefSockFD(serviceRef) ) {
            YMArrayRemove(b->serviceRefs, i);
            DNSServiceRefDeallocate(sdref);

            // the avahi compat layer calls back out to us from the same thread we called into
            YMArrayRemove(b->sources,i);
            ym_dispatch_user_t user = { __ym_mdns_remove_ref_async, source, NULL, ym_dispatch_user_context_noop };
            YMDispatchAsync(YMDispatchGetGlobalQueue(),&user);
            removed = true;
            break;
        }
    }
    ymassert(!serviceRef||removed,"failed to find sdref to remove");
    YMSelfUnlock(b);
}

static void DNSSD_API __ym_mdns_browse_callback(__unused DNSServiceRef serviceRef,
                                                DNSServiceFlags flags,
                                                __unused uint32_t ifIdx,
                                                DNSServiceErrorType result,
                                                const char *name,
                                                const char *type,
                                                const char *domain,
                                                void *context)
{
    
    __ym_mdns_browser_t *b = (__ym_mdns_browser_t *)context;
    
    ymlog("__ym_mdns_browse_callback: %s/%s.%s:?: r: %d if: %u flags: %04x", type, name, domain, result, ifIdx, flags);
    
    if ( result != kDNSServiceErr_NoError ) {
        ymerr("browse '%s' callback: %d", YMSTR(b->type), result);
        return;
    }
    if ( domain == NULL ) {
        ymerr("service '%s::%s' has no domain", YMSTR(b->type), name);
        return;
    }
    
    // "An enumeration callback with the "Add" flag NOT set indicates a "Remove", i.e. the domain is no longer valid.
    bool remove = ! ((flags & kDNSServiceFlagsAdd) || (flags & kDNSServiceFlagsMoreComing));

    YMStringRef ymName = YMSTRC(name);
    if ( remove )
        __YMmDNSBrowserRemoveServiceNamed(b, ymName);
    else {
        YMmDNSServiceRecord *existingRecord = __YMmDNSBrowserGetServiceWithName(b, ymName, false);
        if ( ! existingRecord ) {
            YMmDNSServiceRecord *record = _YMmDNSServiceRecordCreate(name, type, domain);
            __YMmDNSBrowserAddOrUpdateService(b, record);
        }
    }
    YMRelease(ymName);
}

struct __ym_get_addr_info {
    __ym_mdns_browser_t *b;
    YMStringRef unescapedName;
} __ym_get_addr_info;
typedef struct __ym_get_addr_info __ym_get_addr_info_t;

void DNSSD_API __ym_mdns_addr_info_callback
(
 __unused DNSServiceRef sdRef,
 __unused  DNSServiceFlags flags,
 __unused  uint32_t interfaceIndex,
 __unused  DNSServiceErrorType errorCode,
 __unused  const char                       *hostname,
 __unused  const struct sockaddr            *address,
 __unused  uint32_t ttl,
 __unused  void                             *context
 )
{
    __ym_get_addr_info_t *ctx = context;
    __ym_mdns_browser_t *b = ctx->b;
    YMStringRef unescapedName = ctx->unescapedName;
    
    ymassert(address->sa_family==AF_INET||address->sa_family==AF_INET6,"bad address family %d",address->sa_family);    
    YMmDNSServiceRecord *record = __YMmDNSBrowserGetServiceWithName(b, unescapedName, false);
    ymsoftassert(record, "failed to find existing record for %s on addrinfo callback",YMSTR(unescapedName));
    
    YMAddressRef ymAddress = YMAddressCreate(address,record->port);
    ymlog("__ym_mdns_addr_info_callback: %s: %d: %s",hostname,address->sa_family,ymAddress?YMSTR(YMAddressGetDescription(ymAddress)):"*");
    if ( ymAddress ) {
        YMRelease(ymAddress);
        if ( address->sa_family == AF_INET )
            _YMmDNSServiceRecordAppendSockaddr(record,address);
    }
        
    if ( ! ( flags & kDNSServiceFlagsMoreComing ) ) {
        ymlog("finished enumerating addresses");
        
        if ( b->serviceResolved )
            b->serviceResolved(b, true, record, b->callbackContext);
        
        YMFREE(context);
    }
}

void DNSSD_API __ym_mdns_resolve_callback(__unused DNSServiceRef serviceRef,
                                          __unused DNSServiceFlags flags,
                                          __unused uint32_t interfaceIndex,
                                          DNSServiceErrorType dnsResult,
                                          const char *fullname,
                                          const char *host,
                                          uint16_t port,
                                          __unused uint16_t txtLength,
                                          __unused const unsigned char *txtRecord,
                                          void *context )
{
    YM_IO_BOILERPLATE
    
    __ym_mdns_resolve_t *ctx = context;
    __ym_mdns_browser_t *b = ctx->b;
    YMStringRef unescapedName = ctx->unescapedName;
    YMFREE(context);
    
    uint16_t hostPort = ntohs(port);
    
    ymlog("__ym_mdns_resolve_callback: %s/%s(%s) -> %s:%u[txt%ub]",YMSTR(b->type),fullname,YMSTR(unescapedName),host,(unsigned)hostPort,txtLength);
    
    __YMmDNSBrowserRemoveServiceRef(b,serviceRef);

    YMmDNSServiceRecord *record = __YMmDNSBrowserGetServiceWithName(b, unescapedName, false);
    ymassert(record,"failed to lookup existing service from %s: %s",fullname,YMSTR(unescapedName));
    
    if ( dnsResult == kDNSServiceErr_NoError ) {
        
        _YMmDNSServiceRecordSetPort(record, hostPort);
        _YMmDNSServiceRecordSetTxtRecord(record, txtRecord, txtLength);
        
        if ( ! record->addrinfoSdref ) {
            record->addrinfoSdref = YMALLOC(sizeof(DNSServiceRef));

            struct addrinfo *c;
            char portstr[10];
            snprintf(portstr, 10, "%u", hostPort);
            int ret = getaddrinfo(host, portstr, NULL, &c);
            if ( ret != 0 ) {
                ymlog("getting addr info for '%s::%s::%s:%s': %d %s", YMSTR(b->type), YMSTR(unescapedName), host, portstr, ret, strerror(ret));
                YMFREE(record->addrinfoSdref);
                record->addrinfoSdref = NULL;
                return;
            }
            
            int idx = 0;
            __ym_get_addr_info_t *aCtx = YMALLOC(sizeof(__ym_get_addr_info_t));
            aCtx->b = (__ym_mdns_browser_t *)YMRetain(b);
            aCtx->unescapedName = YMRetain(unescapedName);
            do {
                if (c->ai_family == AF_INET || c->ai_family == AF_INET6)
                    __ym_mdns_addr_info_callback(record->addrinfoSdref,c->ai_next?kDNSServiceFlagsMoreComing:0,idx,0,host,c->ai_addr,0,aCtx);
                else
                    ymerr("getaddrinfo unknown type: %d",c->ai_family);
                c = c->ai_next; idx++;
            } while (c);
        }
    } else if ( b->serviceResolved )
        b->serviceResolved(b, false, record, b->callbackContext);
}

YM_EXTERN_C_POP
