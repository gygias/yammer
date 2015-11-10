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

#endif /* YMX509Certificate_h */
