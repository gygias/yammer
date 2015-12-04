//
//  YMX509Certificate.h
//  yammer
//
//  Created by david on 11/10/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMX509Certificate_h
#define YMX509Certificate_h

#include "YMRSAKeyPair.h"

YM_EXTERN_C_PUSH

typedef const struct __ym_x509_certificate_t *YMX509CertificateRef;

YMX509CertificateRef YMAPI YMX509CertificateCreate(YMRSAKeyPairRef);

size_t YMAPI YMX509CertificateGetPublicKeyData(void *buffer);

YM_EXTERN_C_POP

#endif /* YMX509Certificate_h */
