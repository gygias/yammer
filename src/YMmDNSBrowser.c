//
//  YMmDNSBrowser.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMmDNSBrowser.h"

#include "YMUtilities.h"
#include "YMThread.h"
#include "YMAddress.h"

#include <dns_sd.h>
#include <errno.h>

#if !defined(YMWIN32)
# if defined(YMLINUX)
#  define __USE_POSIX
#  include <netinet/in.h>
# endif
# include <netdb.h>
#else
# include <winsock2.h>
# include <ws2tcpip.h>
#endif

#define ymlog_type YMLogmDNS
#include "YMLog.h"

YM_EXTERN_C_PUSH

typedef struct __ym_mdns_browser_t
{
    _YMType _type;
    
    YMStringRef type;
    YMmDNSServiceList *serviceList; // convert to collection
    
#ifdef YMmDNS_ENUMERATION
    bool enumerating;
    DNSServiceRef *enumerateServiceRef;
    YMThreadRef enumerateEventThread;
#endif
    
    bool browsing;
    DNSServiceRef *browseServiceRef;
    YMThreadRef browseEventThread;
    
    bool resolving;
    DNSServiceRef *resolveServiceRef;
    
    ym_mdns_service_appeared_func serviceAppeared;
    ym_mdns_service_removed_func serviceRemoved;
    ym_mdns_service_updated_func serviceUpdated;
    ym_mdns_service_resolved_func serviceResolved;
    void *callbackContext;
} __ym_mdns_browser_t;
typedef struct __ym_mdns_browser_t *__YMmDNSBrowserRef;

#pragma mark callback/event goo
#ifdef YMmDNS_ENUMERATION
void DNSSD_API __YMmDNSEnumerateReply(DNSServiceRef sdRef, DNSServiceFlags flags, uint32_t interfaceIndex, DNSServiceErrorType errorCode, const char *replyDomain, void *context);
#endif
static void DNSSD_API __ym_mdns_browse_callback(DNSServiceRef sdRef, DNSServiceFlags flags, uint32_t interfaceIndex, DNSServiceErrorType errorCode, const char *serviceName,
                                            const char *reg_type, const char *reply_domain, void *context);
void DNSSD_API __ym_mdns_resolve_callback(DNSServiceRef serviceRef, DNSServiceFlags flags, uint32_t interfaceIndex, DNSServiceErrorType errorCode, const char *name,
                                      const char *host, uint16_t port, uint16_t txtLen, const unsigned char *txtRecord, void *context );

YM_THREAD_RETURN YM_CALLING_CONVENTION __ym_mdns_event_proc(YM_THREAD_PARAM);
YMmDNSServiceRecord *__YMmDNSBrowserAddOrUpdateService(__YMmDNSBrowserRef browser, YMmDNSServiceRecord *record);
void __YMmDNSBrowserRemoveServiceNamed(__YMmDNSBrowserRef browser, YMStringRef name);

YMmDNSServiceRecord *__YMmDNSBrowserGetServiceWithName(__YMmDNSBrowserRef browser, YMStringRef name, bool remove);

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

    __YMmDNSBrowserRef browser = (__YMmDNSBrowserRef)_YMAlloc(_YMmDNSBrowserTypeID,sizeof(struct __ym_mdns_browser_t));
    
    browser->type = YMRetain(type);
    browser->serviceList = NULL;
    YMmDNSBrowserSetServiceAppearedFunc(browser, serviceAppeared);
    YMmDNSBrowserSetServiceUpdatedFunc(browser, serviceUpdated);
    YMmDNSBrowserSetServiceResolvedFunc(browser, serviceResolved);
    YMmDNSBrowserSetServiceRemovedFunc(browser, serviceRemoved);
    YMmDNSBrowserSetCallbackContext(browser, context);

    return browser;
}

void _YMmDNSBrowserFree(YMTypeRef object)
{
    __YMmDNSBrowserRef browser = (__YMmDNSBrowserRef)object;
    if ( browser->type )
        YMRelease(browser->type);
    
#ifdef YMmDNS_ENUMERATION
    if ( browser->enumerateServiceRef )
        free(browser->enumerateServiceRef);
    if ( browser->enumerateEventThread )
        YMRelease(browser->enumerateEventThread)
#endif
        
    //if ( browser->browseServiceRef ) // released by event thread
    //    free(browser->browseServiceRef);
    if ( browser->browseEventThread )
        YMRelease(browser->browseEventThread);
    
    if ( browser->resolveServiceRef )
        free(browser->resolveServiceRef);
    
    if ( browser->serviceList )
        _YMmDNSServiceListFree((YMmDNSServiceList *)(browser->serviceList));
}

void YMmDNSBrowserSetServiceAppearedFunc(YMmDNSBrowserRef browser_, ym_mdns_service_appeared_func func)
{
    __YMmDNSBrowserRef browser = (__YMmDNSBrowserRef)browser_;
    browser->serviceAppeared = func;
}

void YMmDNSBrowserSetServiceRemovedFunc(YMmDNSBrowserRef browser_, ym_mdns_service_removed_func func)
{
    __YMmDNSBrowserRef browser = (__YMmDNSBrowserRef)browser_;
    browser->serviceRemoved = func;
}

void YMmDNSBrowserSetServiceUpdatedFunc(YMmDNSBrowserRef browser_, ym_mdns_service_updated_func func)
{
    __YMmDNSBrowserRef browser = (__YMmDNSBrowserRef)browser_;
    browser->serviceUpdated = func;
}

void YMmDNSBrowserSetServiceResolvedFunc(YMmDNSBrowserRef browser_, ym_mdns_service_resolved_func func)
{
    __YMmDNSBrowserRef browser = (__YMmDNSBrowserRef)browser_;
    browser->serviceResolved = func;
}

void YMmDNSBrowserSetCallbackContext(YMmDNSBrowserRef browser_, void *context)
{
    __YMmDNSBrowserRef browser = (__YMmDNSBrowserRef)browser_;
    browser->callbackContext = context;
}

#ifdef YMmDNS_ENUMERATION
bool YMmDNSBrowserEnumeratingStart(YMmDNSBrowserRef browser)
{
    if ( browser->enumerating )
        return false;
    browser->enumerating = true;
    
    if ( browser->enumerateServiceRef )
        free(browser->enumerateServiceRef);
    browser->enumerateServiceRef = (DNSServiceRef *)calloc( 1, sizeof(DNSServiceRef) );
    
    DNSServiceErrorType result = DNSServiceEnumerateDomains(browser->enumerateServiceRef, // DNSServiceRef
                                                            kDNSServiceFlagsBrowseDomains, // DNSServiceFlags
                                                            0, // interfaceIndex
                                                            _YMmDNSEnumerateReply, // DNSServiceDomainEnumReply
                                                            browser ); // context
    if( result != kDNSServiceErr_NoError )
    {
        ymerr("mdns[%s]: enumerate domains failed: %ld ", YMSTR(browser->type), result);
        return false;
    }
    
    if ( browser->enumerateEventThread )
        YMFree(browser->enumerateEventThread);
    char *threadName = YMSTRCF("mdns-enum-%s",YMSTR(browser->type));
    browser->enumerateEventThread = YMThreadCreate(threadName, _YMmDNSBrowserThread, browser);
    free(threadName);
    bool okay = YMThreadStart(_browser->enumerateEventThread);
    
    return okay;
}

bool YMmDNSBrowserEnumeratingStop(YMmDNSBrowserRef browser)
{
    if ( browser->enumerateServiceRef )
    {
        DNSServiceRefDeallocate(browser->enumerateServiceRef);
        free(browser->enumerateServiceRef); // not sure this is right
        browser->enumerateServiceRef = NULL;
    }
    return true;
}
#endif

bool YMmDNSBrowserStart(YMmDNSBrowserRef browser_)
{
    __YMmDNSBrowserRef browser = (__YMmDNSBrowserRef)browser_;
    
    if ( browser->browsing )
        return false;
    browser->browsing = true;

    browser->browseServiceRef = (DNSServiceRef *)calloc( 1, sizeof(DNSServiceRef) );
    
    DNSServiceErrorType result = DNSServiceBrowse(browser->browseServiceRef, // DNSServiceRef
                                                  0, // DNSServiceFlags
                                                  0, // interfaceIndex
                                                  YMSTR(browser->type), // type
                                                  NULL, // domain
                                                  __ym_mdns_browse_callback, // callback
                                                  browser); // context
    
    if( result != kDNSServiceErr_NoError )
    {
        ymerr("mdns[%s]: service browse failed: %d", YMSTR(browser->type), result);
        return false;
    }
    
    YMStringRef threadName = YMSTRCF("mdns-browse-%s",YMSTR(browser->type));
    browser->browseEventThread = YMThreadCreate(threadName, __ym_mdns_event_proc, browser->browseServiceRef);
    YMRelease(threadName);
    
    bool okay = YMThreadStart(browser->browseEventThread);
    
    return okay;
}

bool YMmDNSBrowserStop(YMmDNSBrowserRef browser_)
{
    __YMmDNSBrowserRef browser = (__YMmDNSBrowserRef)browser_;

	if ( ! browser->browsing )
		return false;
    
    if ( browser->browseServiceRef )
    {
        int fd  = DNSServiceRefSockFD(*(browser->browseServiceRef));
        int result = shutdown(fd, SHUT_RDWR);
        ymassert(result==0,"close service ref fd");
    }

	browser->browsing = false;

    return true;
}

typedef struct __ym_mdns_browser_resolve_context_t {
    __YMmDNSBrowserRef browser;
    YMStringRef unescapedName;
} __ym_mdns_browser_resolve_context_t;
typedef struct __ym_mdns_browser_resolve_context_t *__ym_mdns_browser_resolve_context_ref;

bool YMmDNSBrowserResolve(YMmDNSBrowserRef browser_, YMStringRef serviceName)
{
    __YMmDNSBrowserRef browser = (__YMmDNSBrowserRef)browser_;
    
    YMmDNSServiceRecord *theService = __YMmDNSBrowserGetServiceWithName(browser, serviceName, false);
    if ( ! theService )
    {
        ymerr("mdns[%s]: asked to resolve '%s' without record",YMSTR(browser->type),YMSTR(serviceName));
        return false;
    }
    
    browser->resolveServiceRef = (DNSServiceRef *)calloc( 1, sizeof(DNSServiceRef) );
    __ym_mdns_browser_resolve_context_ref ctx = YMALLOC(sizeof(struct __ym_mdns_browser_resolve_context_t));
    ctx->browser = (__YMmDNSBrowserRef)YMRetain(browser);
    ctx->unescapedName = YMRetain(serviceName);
    DNSServiceErrorType result = DNSServiceResolve ( browser->resolveServiceRef, // DNSServiceRef
                                                    0, // DNSServiceFlags
                                                    0, // interfaceIndex
                                                    YMSTR(serviceName),
                                                    YMSTR(browser->type), // type
                                                    YMSTR(theService->domain), // domain
                                                    __ym_mdns_resolve_callback,
                                                    ctx );
    if ( result != kDNSServiceErr_NoError )
    {
        ymerr("mdns[%s]: service resolve failed: %d",YMSTR(browser->type),result);
        // on error "the callback is never invoked and the DNSServiceRef is not initialized"
        // leading me to believe we free not DNSServiceRefDeallocate here
        free(browser->resolveServiceRef);
        browser->resolveServiceRef = NULL;
        return false;
    }
    
    result = DNSServiceProcessResult( *(browser->resolveServiceRef) );
    if ( result != kDNSServiceErr_NoError )
    {
        ymerr("mdns[%s]: resolve process result failed: %d", YMSTR(browser->type), result);
        DNSServiceRefDeallocate(*browser->resolveServiceRef);
        free(browser->resolveServiceRef);
        browser->resolveServiceRef = NULL;
        return false;
    }
    
    ymlog("mdns[%s]: resolving %s (%s)...", YMSTR(browser->type), YMSTR(serviceName), theService->domain ? YMSTR(theService->domain) : "(NULL)");
    
    return true;
}

YMmDNSServiceRecord *__YMmDNSBrowserAddOrUpdateService(__YMmDNSBrowserRef browser, YMmDNSServiceRecord *record)
{
    YMmDNSServiceList *aListItem = (YMmDNSServiceList *)browser->serviceList;
    
    // update?
    while ( aListItem )
    {
        if ( 0 == strcmp(YMSTR(record->name),YMSTR(((YMmDNSServiceRecord *)aListItem->service)->name)) )
        {
            YMmDNSServiceRecord *existingRecord = aListItem->service;
            
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
            if ( existingRecord->txtRecordKeyPairs )
                _YMmDNSTxtKeyPairsFree(existingRecord->txtRecordKeyPairs, existingRecord->txtRecordKeyPairsSize);
            if ( existingRecord->sockaddrList ) {
                while ( YMArrayGetCount(existingRecord->sockaddrList) > 0 ) {
                    YMAddressRef address = (YMAddressRef)YMArrayGet(existingRecord->sockaddrList, 0);
                    YMRelease(address);
                    YMArrayRemove(existingRecord->sockaddrList, 0);
                }
                YMRelease(existingRecord->sockaddrList);
            }
            
            existingRecord->txtRecordKeyPairs = record->txtRecordKeyPairs;
            existingRecord->txtRecordKeyPairsSize = record->txtRecordKeyPairsSize;
            existingRecord->sockaddrList = record->sockaddrList ? YMRetain(record->sockaddrList) : NULL;
            existingRecord->port = record->port;
            
            _YMmDNSServiceRecordFree(record, true);
            
            // keep the list item for now
            if ( browser->serviceUpdated )
                browser->serviceUpdated((YMmDNSBrowserRef)browser, existingRecord, browser->callbackContext);
            return existingRecord;
        }
        aListItem = aListItem->next;
    }
    
    // add
    YMmDNSServiceList *newItem = (YMmDNSServiceList *)YMALLOC(sizeof(YMmDNSServiceList));
    newItem->service = record;
    newItem->next = browser->serviceList;
    browser->serviceList = newItem;
    
    if ( browser->serviceAppeared )
        browser->serviceAppeared((YMmDNSBrowserRef)browser, record, browser->callbackContext);
    
    return record;
}

#define _YMmDNSServiceFoundAndRemovedHack ((YMmDNSServiceRecord *)0xEA75F00D)
YMmDNSServiceRecord *__YMmDNSBrowserGetServiceWithName(__YMmDNSBrowserRef browser, YMStringRef name, bool remove)
{    
    YMmDNSServiceRecord *matchedRecord = NULL;
    YMmDNSServiceList *aListItem = (YMmDNSServiceList *)browser->serviceList,
                        *previousListItem = (YMmDNSServiceList *)browser->serviceList;
    while ( aListItem )
    {
        YMmDNSServiceRecord *aRecord = aListItem->service;
        if ( 0 == strcmp(YMSTR(aRecord->name), YMSTR(name)) )
        {
            if ( remove )
            {
                previousListItem->next = aListItem->next; // nil or not
                if ( aListItem == browser->serviceList )
                    browser->serviceList = NULL;
                _YMmDNSServiceRecordFree(aRecord,false);
                free(aListItem);
                matchedRecord = _YMmDNSServiceFoundAndRemovedHack;
            }
            else
                matchedRecord = aRecord;
            break;
        }
        
        previousListItem = aListItem;
        aListItem = aListItem->next;
    }
    
    return matchedRecord;
}

YMmDNSServiceRecord *YMmDNSBrowserGetServiceWithName(YMmDNSBrowserRef browser_, YMStringRef name)
{
    __YMmDNSBrowserRef browser = (__YMmDNSBrowserRef)browser_;
    return __YMmDNSBrowserGetServiceWithName(browser, name, false);
}

void __YMmDNSBrowserRemoveServiceNamed(__YMmDNSBrowserRef browser, YMStringRef name)
{
    __YMmDNSBrowserGetServiceWithName(browser, name, true);
    if ( browser->serviceRemoved )
        browser->serviceRemoved((YMmDNSBrowserRef)browser, name, browser->callbackContext);
}

#ifdef YMmDNS_ENUMERATION
void DNSSD_API __YMmDNSEnumerateReply(DNSServiceRef sdRef, DNSServiceFlags flags, uint32_t interfaceIndex, DNSServiceErrorType errorCode, const char *replyDomain, void *context)
{
    ymlog("mdns[%s]: enumerate reply flags: %04x", YMSTR(browser->type), flags);
}
#endif

static void DNSSD_API __ym_mdns_browse_callback(__unused DNSServiceRef serviceRef,
                                                DNSServiceFlags flags,
                                                __unused uint32_t ifIdx,
                                                DNSServiceErrorType result,
                                                const char *name,
                                                const char *type,
                                                const char *domain,
                                                void *context)
{
    
    __YMmDNSBrowserRef browser = (__YMmDNSBrowserRef)context;
    
    ymlog("__ym_mdns_browse_callback: %s/%s.%s:?: r: %d if: %u flags: %04x", type, name, domain, result, ifIdx, flags);
    
    if ( result != kDNSServiceErr_NoError )
    {
        ymerr("mDNS[%s]: error: browse callback: %d", YMSTR(browser->type), result);
        return;
    }
    if ( domain == NULL )
    {
        ymerr("mDNS[%s]: error: service '%s' has no domain", YMSTR(browser->type), name);
        return;
    }
    
    // "An enumeration callback with the "Add" flag NOT set indicates a "Remove", i.e. the domain is no longer valid.
    bool remove = (flags & kDNSServiceFlagsAdd) == 0;
    
    if ( remove )
    {
        YMStringRef ymName = YMSTRC(name);
        __YMmDNSBrowserRemoveServiceNamed(browser, ymName);
        YMRelease(ymName);
    }
    else
    {
        YMmDNSServiceRecord *record = _YMmDNSServiceRecordCreate(name, type, domain);
        __YMmDNSBrowserAddOrUpdateService(browser, record);
    }    
}

struct __ym_get_addr_info_context_t {
    YMStringRef unescapedName;
    __YMmDNSBrowserRef browser;
} __ym_get_addr_info_context_t;
typedef struct __ym_get_addr_info_context_t *__ym_get_addr_info_context_ref;

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
    __ym_get_addr_info_context_ref ctx = context;
    __YMmDNSBrowserRef browser = ctx->browser;
    YMStringRef unescapedName = ctx->unescapedName;
    
    ymassert(address->sa_family==AF_INET||address->sa_family==AF_INET6,"bad address family %d",address->sa_family);    
    YMmDNSServiceRecord *record = __YMmDNSBrowserGetServiceWithName(browser, unescapedName, false);
    ymsoftassert(record, "failed to find existing record for %s on addrinfo callback",YMSTR(unescapedName));
    
    YMAddressRef ymAddress = YMAddressCreate(address,record->port);
    ymerr("__ym_mdns_addr_info_callback: %s: %s",hostname,YMSTR(YMAddressGetDescription(ymAddress)));
    YMRelease(ymAddress);
    
    if ( address->sa_family == AF_INET ) {
        _YMmDNSServiceRecordAppendSockaddr(record,address);
    }
    
    if ( ! ( flags & kDNSServiceFlagsMoreComing ) ) {
        ymlog("mdns[&]: finished enumerating addresses");
        
        _YMmDNSServiceRecordSetComplete(record);
        
        if ( browser->serviceResolved )
            browser->serviceResolved((YMmDNSBrowserRef)browser, true, record, browser->callbackContext);
        
        shutdown(DNSServiceRefSockFD(sdRef),SHUT_RDWR);
        YMRelease(record->getAddrInfoThread);
        record->getAddrInfoThread = NULL;
    }
}

void DNSSD_API __ym_mdns_resolve_callback(__unused DNSServiceRef serviceRef,
                                          __unused DNSServiceFlags flags,
                                          __unused uint32_t interfaceIndex,
                                          DNSServiceErrorType result,
                                          const char *fullname,
                                          const char *host,
                                          uint16_t port,
                                          __unused uint16_t txtLength,
                                          __unused const unsigned char *txtRecord,
                                          void *context )
{
    __ym_mdns_browser_resolve_context_ref ctx = context;
    __YMmDNSBrowserRef browser = ctx->browser;
    YMStringRef unescapedName = ctx->unescapedName;
    ymlog("__ym_mdns_resolve_callback: %s/%s(%s) -> %s:%u",YMSTR(browser->type),fullname,YMSTR(unescapedName),host,(unsigned)port);
    
    bool okay = ( result == kDNSServiceErr_NoError );
    
    YMmDNSServiceRecord *record = __YMmDNSBrowserGetServiceWithName(browser, unescapedName, false);
    ymassert(record,"failed to lookup existing service from %s: %s",fullname,YMSTR(unescapedName));
    
    if ( okay ) {
        
        while ( record->getAddrInfoThread ) {} ; /// todo YOLO SPINLOCK
        
        _YMmDNSServiceRecordSetPort(record, port);
        _YMmDNSServiceRecordSetTxtRecord(record, txtRecord, txtLength);
        
        if ( ! record->complete ) {
            record->getAddrInfoRef = YMALLOC(sizeof(DNSServiceRef));
            __ym_get_addr_info_context_ref aCtx = YMALLOC(sizeof(struct __ym_get_addr_info_context_t));
            aCtx->browser = (__YMmDNSBrowserRef)YMRetain(browser);
            aCtx->unescapedName = YMRetain(unescapedName);
            DNSServiceErrorType err = DNSServiceGetAddrInfo(record->getAddrInfoRef, kDNSServiceFlagsForceMulticast, kDNSServiceInterfaceIndexAny, kDNSServiceProtocol_IPv4|kDNSServiceProtocol_IPv6, host, __ym_mdns_addr_info_callback, aCtx);
            if ( err == kDNSServiceErr_NoError) {
                record->getAddrInfoThread = YMThreadCreate(NULL, __ym_mdns_event_proc, record->getAddrInfoRef);
                YMThreadStart(record->getAddrInfoThread);
            } else {
                ymerr("mdns[%s]: error: failed to get addr info for %s",YMSTR(browser->type),YMSTR(unescapedName));
            }
        }
    } else if ( browser->serviceResolved )
        browser->serviceResolved((YMmDNSBrowserRef)browser, false, record, browser->callbackContext);
    
    if ( ! ( flags & kDNSServiceFlagsMoreComing ) ) {
        YMRelease(browser);
        YMRelease(unescapedName);
        free(ctx);
    }
}

// this function was mostly lifted from "Zero Configuration Networking: The Definitive Guide" -o'reilly
YM_THREAD_RETURN YM_CALLING_CONVENTION __ym_mdns_event_proc(YM_THREAD_PARAM ctx)
{
	DNSServiceRef *serviceRef = ctx;
    int fd  = DNSServiceRefSockFD(*(serviceRef));
    
    ymlog("mdns[&]: event thread f%d entered", fd);
    
    bool keepGoing = true;
    while ( keepGoing )
    {
        int nfds = fd + 1;
        fd_set readfds;
        fd_set* nullFd = (fd_set*) NULL;
        struct timeval tv;
        int result;
        
        // 1. Set up the fd_set as usual here.
        FD_ZERO(&readfds);
        
        // 2. Add the fd to the fd_set
        FD_SET(fd , &readfds);
        
        // 3. Set up the timeout.
        tv.tv_sec = 0; // wakes up every .5 sec if no socket activity occurs
        tv.tv_usec = 500000;
        
        // wait for pending data or .5 secs to elapse:
        result = select(nfds, &readfds, nullFd, nullFd, &tv);
        if (result > 0)
        {
            DNSServiceErrorType err = kDNSServiceErr_NoError;
            if (FD_ISSET(fd , &readfds))
            {
                err = DNSServiceProcessResult(*(serviceRef));
            }
            if (err != kDNSServiceErr_NoError)
            {
                ymerr("mdns[&]: event thread process result on f%d failed: %d", fd, err);
                keepGoing = false;
            }
        }
        else if (result == 0)
        {
            // timeout elapsed but no fd-s were signalled.
        }
        else
        {
            ymerr("mdns[&]: event thread select on f%d failed: %d: %d (%s)", fd, result,errno,strerror(errno));
            keepGoing = false;
        }
    }
    
    ymlog("mdns[&] event thread f%d exiting", fd);
    
    DNSServiceRefDeallocate(*(serviceRef));
	free(serviceRef);

	YM_THREAD_END
}

YM_EXTERN_C_POP
