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
#include "YMArray.h"

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
    YMmDNSServiceList *serviceList; // convert to collection
    
    bool browsing;
    YMThreadRef browseEventThread;
    YMArrayRef activeServiceRefs;
    
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

YM_THREAD_RETURN YM_CALLING_CONVENTION __ym_mdns_event_proc(YM_THREAD_PARAM);
YMmDNSServiceRecord *__YMmDNSBrowserAddOrUpdateService(__ym_mdns_browser_t *, YMmDNSServiceRecord*);
void __YMmDNSBrowserRemoveServiceNamed(__ym_mdns_browser_t *, YMStringRef);
YMmDNSServiceRecord *__YMmDNSBrowserGetServiceWithName(__ym_mdns_browser_t *, YMStringRef, bool);
void __YMmDNSBrowserAddServiceRef(__ym_mdns_browser_t *, DNSServiceRef*);
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
    b->serviceList = NULL;
    b->activeServiceRefs = YMArrayCreate();
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
    
    YMRelease(b->activeServiceRefs);
        
    //YMFREE(browser->browseServiceRef); // released by event thread
    if ( b->browseEventThread )
        YMRelease(b->browseEventThread);
    
    if ( b->serviceList )
        _YMmDNSServiceListFree((YMmDNSServiceList *)(b->serviceList));
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

    DNSServiceRef *sdref = (DNSServiceRef *)YMALLOC( sizeof(DNSServiceRef) );
    
    DNSServiceErrorType result = DNSServiceBrowse(sdref, // DNSServiceRef
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
    
    YMStringRef threadName = YMSTRCF("mdns-browse-%s",YMSTR(b->type));
    b->browseEventThread = YMThreadCreate(threadName, __ym_mdns_event_proc, (void *)YMRetain(b));
    YMRelease(threadName);
    
    bool okay = YMThreadStart(b->browseEventThread);
    
    return okay;
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
    
    DNSServiceRef *sdref = YMALLOC( sizeof(DNSServiceRef) );
    __ym_mdns_resolve_t *ctx = YMALLOC(sizeof(__ym_mdns_resolve_t));
    ctx->b = (__ym_mdns_browser_t *)YMRetain(b);
    ctx->unescapedName = YMRetain(serviceName);
    
    __YMmDNSBrowserAddServiceRef(b, sdref);
    
    DNSServiceErrorType result = DNSServiceResolve ( sdref, // DNSServiceRef
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
        
        YMSelfLock(b);
            YMArrayRemoveObject(b->activeServiceRefs, sdref);
        YMSelfUnlock(b);
        YMFREE(sdref);
        return false;
    }
    
    ymlog("resolving '%s::%s' (%s)...", YMSTR(b->type), YMSTR(serviceName), theService->domain ? YMSTR(theService->domain) : "(NULL)");
    
    return true;
}

YMmDNSServiceRecord *__YMmDNSBrowserAddOrUpdateService(__ym_mdns_browser_t *b, YMmDNSServiceRecord *record)
{
    YMmDNSServiceList *aListItem = b->serviceList;
    
    // update?
    while ( aListItem ) {
        if ( 0 == strcmp(YMSTR(record->name),YMSTR(((YMmDNSServiceRecord *)aListItem->service)->name)) ) {
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
            if ( b->serviceUpdated )
                b->serviceUpdated(b, existingRecord, b->callbackContext);
            return existingRecord;
        }
        aListItem = aListItem->next;
    }
    
    // add
    YMmDNSServiceList *newItem = YMALLOC(sizeof(YMmDNSServiceList));
    newItem->service = record;
    newItem->next = b->serviceList;
    b->serviceList = newItem;
    
    if ( b->serviceAppeared )
        b->serviceAppeared(b, record, b->callbackContext);
    
    return record;
}

#define _YMmDNSServiceFoundAndRemovedHack ((YMmDNSServiceRecord *)0xEA75F00D)
YMmDNSServiceRecord *__YMmDNSBrowserGetServiceWithName(__ym_mdns_browser_t *b, YMStringRef name, bool remove)
{    
    YMmDNSServiceRecord *matchedRecord = NULL;
    YMmDNSServiceList *aListItem = b->serviceList,
                        *previousListItem = b->serviceList;
    while ( aListItem ) {
        YMmDNSServiceRecord *aRecord = aListItem->service;
        if ( 0 == strcmp(YMSTR(aRecord->name), YMSTR(name)) ) {
            if ( remove ) {
                previousListItem->next = aListItem->next; // nil or not
                if ( aListItem == b->serviceList )
                    b->serviceList = NULL;
                _YMmDNSServiceRecordFree(aRecord,false);
                YMFREE(aListItem);
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

#ifdef YMmDNS_ENUMERATION
void DNSSD_API __YMmDNSEnumerateReply(DNSServiceRef sdRef, DNSServiceFlags flags, uint32_t interfaceIndex, DNSServiceErrorType errorCode, const char *replyDomain, void *context)
{
    ymlog("enumerate reply '%s' flags: %04x", YMSTR(browser->type), flags);
}
#endif

void __YMmDNSBrowserAddServiceRef(__ym_mdns_browser_t *b, DNSServiceRef *serviceRef)
{
    YMSelfLock(b);
    YMArrayAdd(b->activeServiceRefs, serviceRef);
    YMSelfUnlock(b);
    ymerr("added active sdref: %p",serviceRef); // socketFD is probably not valid at this point
}

void __YMmDNSBrowserRemoveServiceRef(__ym_mdns_browser_t *b, DNSServiceRef serviceRef)
{
    YMSelfLock(b);
    bool removed = false;
    for ( int i = 0; i < YMArrayGetCount(b->activeServiceRefs); i++ ) {
        DNSServiceRef *sdref = (DNSServiceRef *)YMArrayGet(b->activeServiceRefs, i);
        
        // the pointer we pass into DNSServiceProcessResult, from event thread (below us here), isn't the same as the one passed in here
        if ( ! serviceRef || DNSServiceRefSockFD(*sdref) == DNSServiceRefSockFD(serviceRef) ) {
            YMArrayRemove(b->activeServiceRefs, i);
            DNSServiceRefDeallocate(*sdref);
            YMFREE(sdref);
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
    ymerr("__ym_mdns_addr_info_callback: %s: %d: %s",hostname,address->sa_family,ymAddress?YMSTR(YMAddressGetDescription(ymAddress)):"*");
    if ( ymAddress ) {
        YMRelease(ymAddress);
        if ( address->sa_family == AF_INET )
            _YMmDNSServiceRecordAppendSockaddr(record,address);
    }
        
    if ( ! ( flags & kDNSServiceFlagsMoreComing ) ) {
        ymlog("finished enumerating addresses");
        
        if ( b->serviceResolved )
            b->serviceResolved(b, true, record, b->callbackContext);
        
        __YMmDNSBrowserRemoveServiceRef(b,sdRef);
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
            __YMmDNSBrowserAddServiceRef(b, record->addrinfoSdref);

            struct addrinfo a, *c;
            char portstr[10];
            snprintf(portstr, 10, "%u", hostPort);
            int ret = getaddrinfo(host, portstr, NULL, &c);
            if ( ret != 0 ) {
                ymerr("getting addr info for '%s::%s::%s:%s': %d %s", YMSTR(b->type), YMSTR(unescapedName), host, portstr, ret, strerror(ret));
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
                    ymlog("getaddrinfo unknown type: %d",c->ai_family);
                c = c->ai_next; idx++;
            } while (c);
        }
    } else if ( b->serviceResolved )
        b->serviceResolved(b, false, record, b->callbackContext);
}

// this function was mostly lifted from "Zero Configuration Networking: The Definitive Guide" -o'reilly
YM_THREAD_RETURN YM_CALLING_CONVENTION __ym_mdns_event_proc(YM_THREAD_PARAM ctx)
{
    YM_IO_BOILERPLATE
    
    __ym_mdns_browser_t *b = ctx;
    
    ymlog("event thread entered");
    
    bool keepGoing = true;
    while ( keepGoing ) {
		int nfds = 1;
        int maxFd = -1; // unused on win32, parameter only exists for compatibility
        fd_set readfds;
        fd_set* nullFd = (fd_set*) NULL;
        struct timeval tv;
        
        // 3. Set up the timeout.
        tv.tv_sec = 0; // wakes up every .5 sec if no socket activity occurs
        tv.tv_usec = 500000;
        
        // 1. Set up the fd_set as usual here.
        FD_ZERO(&readfds);
        
        // 2. Add the fds to the fd_set
        YMSelfLock(b);
        {
            for ( int i = 0; i < YMArrayGetCount(b->activeServiceRefs); i++ ) {
                DNSServiceRef *sdref = (DNSServiceRef *)YMArrayGet(b->activeServiceRefs, i);
                int aFd = DNSServiceRefSockFD(*sdref);
                if ( aFd > maxFd )
                    maxFd = aFd;
				FD_SET(aFd, &readfds);
                nfds++;
            }
        }
        YMSelfUnlock(b);
        
        // wait for pending data or .5 secs to elapse:
        result = select(maxFd + 1, &readfds, nullFd, nullFd, &tv);
        if (result > 0) {
            YMSelfLock(b);
            {
                for ( int i = 0; i < YMArrayGetCount(b->activeServiceRefs); i++ ) {
                    DNSServiceRef *sdref = (DNSServiceRef *)YMArrayGet(b->activeServiceRefs, i);
                    int aFd = DNSServiceRefSockFD(*sdref);
                    if ( FD_ISSET(aFd, &readfds) ) {
                        ymerr("processing service ref fd f%d",aFd);
                        
                        DNSServiceErrorType err = DNSServiceProcessResult(*(sdref));
                        if (err != kDNSServiceErr_NoError) {
                            ymerr("event thread process result on f%d failed: %d", aFd, err);
                            keepGoing = false;
                        }
                    }
                }
            }
            YMSelfUnlock(b);
        } else if (result == 0) {
            // timeout elapsed but no fd-s were signalled.
        } else {
#if defined(YMWIN32)
			error = WSAGetLastError();
			errorStr = "*";
#else
			error = errno;
			errorStr = strerror(errno);
#endif
            ymerr("event thread select failed from n%d fds: %ld: %d (%s)",nfds,result,error,errorStr);
            keepGoing = false;
        }
    }
    
    __YMmDNSBrowserRemoveServiceRef(b, NULL);
    
    ymlog("event thread exiting");

	YM_THREAD_END
}

YM_EXTERN_C_POP
