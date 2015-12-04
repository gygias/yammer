//
//  YMmDNSBrowser.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMmDNSBrowser_h
#define YMmDNSBrowser_h

#include "YMmDNS.h"

YM_EXTERN_C_PUSH

typedef const struct __ym_mdns_browser_t *YMmDNSBrowserRef;

// callback definitions
typedef void (*ym_mdns_service_appeared_func)(YMmDNSBrowserRef browser, YMmDNSServiceRecord * service, void *context);
typedef void (*ym_mdns_service_removed_func)(YMmDNSBrowserRef browser, YMStringRef name, void *context);
typedef void (*ym_mdns_service_updated_func)(YMmDNSBrowserRef browser, YMmDNSServiceRecord *service, void *context);
// if success is false, service is undefined
typedef void (*ym_mdns_service_resolved_func)(YMmDNSBrowserRef browser, bool success, YMmDNSServiceRecord *service, void *context);

YMmDNSBrowserRef YMAPI YMmDNSBrowserCreate(YMStringRef type);
YMmDNSBrowserRef YMAPI YMmDNSBrowserCreateWithCallbacks(YMStringRef type,
														ym_mdns_service_appeared_func serviceAppeared,
														ym_mdns_service_updated_func serviceUpdated,
														ym_mdns_service_resolved_func serviceResolved,
														ym_mdns_service_removed_func serviceRemoved,
														void *context);

// these are broken out in case, say, sequestering certain events later on is useful
void YMAPI YMmDNSBrowserSetServiceAppearedFunc(YMmDNSBrowserRef browser, ym_mdns_service_appeared_func func);
void YMAPI YMmDNSBrowserSetServiceRemovedFunc(YMmDNSBrowserRef browser, ym_mdns_service_removed_func func);
void YMAPI YMmDNSBrowserSetServiceUpdatedFunc(YMmDNSBrowserRef browser, ym_mdns_service_updated_func func);
void YMAPI YMmDNSBrowserSetServiceResolvedFunc(YMmDNSBrowserRef browser, ym_mdns_service_resolved_func func);
void YMAPI YMmDNSBrowserSetCallbackContext(YMmDNSBrowserRef browser, void *context);

#ifdef YMmDNS_ENUMERATION
bool YMAPI YMmDNSBrowserEnumeratingStart(YMmDNSBrowserRef browser);
bool YMAPI YMmDNSBrowserEnumeratingStop(YMmDNSBrowserRef browser);
#endif

bool YMAPI YMmDNSBrowserStart(YMmDNSBrowserRef browser);
bool YMAPI YMmDNSBrowserStop(YMmDNSBrowserRef browser);

bool YMAPI YMmDNSBrowserResolve(YMmDNSBrowserRef browser, YMStringRef serviceName);

YM_EXTERN_C_POP

#endif /* YMmDNSBrowser_h */
