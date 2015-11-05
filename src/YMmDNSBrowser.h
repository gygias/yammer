//
//  YMmDNSBrowser.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMmDNSBrowser_h
#define YMmDNSBrowser_h

#include "YMBase.h"
#include "YMmDNS.h"

#include <dns_sd.h>

typedef struct __YMmDNSBrowser *YMmDNSBrowserRef;

// callback definitions
typedef void (*ym_mdns_service_appeared_func)(YMmDNSBrowserRef browser, YMmDNSServiceRecord * service, void *context);
typedef void (*ym_mdns_service_removed_func)(YMmDNSBrowserRef browser, const char *name, void *context);
typedef void (*ym_mdns_service_updated_func)(YMmDNSBrowserRef browser, YMmDNSServiceRecord *service, void *context);
// if success is false, service is undefined
typedef void (*ym_mdns_service_resolved_func)(YMmDNSBrowserRef browser, bool success, YMmDNSServiceRecord *service, void *context);

YMmDNSBrowserRef YMmDNSBrowserCreate(char *type);
YMmDNSBrowserRef YMmDNSBrowserCreateWithCallbacks(char *type,
                                                  ym_mdns_service_appeared_func serviceAppeared,
                                                  ym_mdns_service_updated_func serviceUpdated,
                                                  ym_mdns_service_resolved_func serviceResolved,
                                                  ym_mdns_service_removed_func serviceRemoved,
                                                  void *context);

// these are broken out in case, say, sequestering certain events later on is useful
void YMmDNSBrowserSetServiceAppearedFunc(YMmDNSBrowserRef browser, ym_mdns_service_appeared_func func);
void YMmDNSBrowserSetServiceRemovedFunc(YMmDNSBrowserRef browser, ym_mdns_service_removed_func func);
void YMmDNSBrowserSetServiceUpdatedFunc(YMmDNSBrowserRef browser, ym_mdns_service_updated_func func);
void YMmDNSBrowserSetServiceResolvedFunc(YMmDNSBrowserRef browser, ym_mdns_service_resolved_func func);
void YMmDNSBrowserSetCallbackContext(YMmDNSBrowserRef browser, void *context);

#ifdef YMmDNS_ENUMERATION
bool YMmDNSBrowserEnumeratingStart(YMmDNSBrowserRef browser);
bool YMmDNSBrowserEnumeratingStop(YMmDNSBrowserRef browser);
#endif

bool YMmDNSBrowserStart(YMmDNSBrowserRef browser);
bool YMmDNSBrowserStop(YMmDNSBrowserRef browser);

bool YMmDNSBrowserResolve(YMmDNSBrowserRef browser, const char *serviceName);

YMmDNSServiceRecord *YMmDNSBrowserGetServiceWithName(YMmDNSBrowserRef browser, const char *name);

#endif /* YMmDNSBrowser_h */
