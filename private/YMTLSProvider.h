//
//  YMTLSProvider.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMTLSProvider_h
#define YMTLSProvider_h

#include "YMSecurityProvider.h"
#include "YMX509Certificate.h"

#if defined(YMWIN32)
# include <winsock2.h>
#endif

YM_EXTERN_C_PUSH

typedef const struct __ym_tls_provider_t *YMTLSProviderRef;

YMTLSProviderRef YMAPI YMTLSProviderCreateWithSocket(YMSOCKET socket, bool isServer);

// callbacks
// returns a malloc'd list of local certificates to be used for identification.
// todo list should be free'd and members released
typedef YMX509CertificateRef *(*ym_tls_provider_get_certs)(YMTLSProviderRef tls, int *nCerts, void *context);
// allows the client to do their own validation of the peer certificate(s). returning false will terminate the
// incoming connection.
typedef bool                  (*ym_tls_provider_should_accept)(YMTLSProviderRef tls, YMX509CertificateRef *certs, int nCerts, void *context);

void YMAPI YMTLSProviderSetLocalCertsFunc(YMTLSProviderRef tls, ym_tls_provider_get_certs func, void *context);
void YMAPI YMTLSProviderSetAcceptPeerCertsFunc(YMTLSProviderRef tls, ym_tls_provider_should_accept func, void *context);

void YMAPI YMTLSProviderFreeGlobals();

YM_EXTERN_C_POP

#endif /* YMTLSProvider_h */
