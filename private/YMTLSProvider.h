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

#endif /* YMTLSProvider_h */

typedef YMTypeRef YMTLSProviderRef;

//YMTLSProviderRef YMTLSProviderCreate(int inFile, int outFile, bool isServer);
YMTLSProviderRef YMTLSProviderCreateWithFullDuplexFile(int file, bool isServer);

// callbacks
// returns a malloc'd list of local certificates to be used for identification.
// todo list should be free'd and members released
typedef YMX509CertificateRef *(*ym_tls_provider_get_certs)(YMTLSProviderRef tls, int *nCerts, void *context);
// allows the client to do their own validation of the peer certificate(s). returning false will terminate the
// incoming connection.
typedef bool                  (*ym_tls_provider_should_accept)(YMTLSProviderRef tls, YMX509CertificateRef *certs, int nCerts, void *context);

void YMTLSProviderSetLocalCertsFunc(YMTLSProviderRef tls, ym_tls_provider_get_certs func, void *context);
void YMTLSProviderSetAcceptPeerCertsFunc(YMTLSProviderRef tls, ym_tls_provider_should_accept func, void *context);

