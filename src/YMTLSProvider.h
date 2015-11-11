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

#endif /* YMTLSProvider_h */

typedef struct __YMTLSProvider *YMTLSProviderRef;

YMTLSProviderRef YMTLSProviderCreate(int inFile, int outFile);
YMTLSProviderRef YMTLSProviderCreateWithFullDuplexFile(int file);
