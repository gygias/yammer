//
//  YMX509Certificate.h
//  yammer
//
//  Created by david on 11/10/15.
//  Copyright © 2015 Combobulated Software. All rights reserved.
//

#ifndef YMX509Certificate_h
#define YMX509Certificate_h

#include "YMRSAKeyPair.h"

typedef struct __YMX509Certificate *YMX509CertificateRef;

YMX509CertificateRef YMX509CertificateCreate(YMRSAKeyPairRef);

void *YMX509CertificateGetX509(YMX509CertificateRef);

#endif /* YMX509Certificate_h */
