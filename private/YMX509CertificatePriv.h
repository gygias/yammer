//
//  YMX509CertificatePriv.h
//  yammer
//
//  Created by david on 11/19/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMX509CertificatePriv_h
#define YMX509CertificatePriv_h

#include <openssl/x509.h>

YM_EXTERN_C_PUSH

YMX509CertificateRef _YMX509CertificateCreateWithX509(X509 *x509, bool copy);
X509 *_YMX509CertificateGetX509(YMX509CertificateRef);

YM_EXTERN_C_POP

#endif /* YMX509CertificatePriv_h */
