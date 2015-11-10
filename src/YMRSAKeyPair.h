//
//  YMRSAKeyPair.h
//  yammer
//
//  Created by david on 11/9/15.
//  Copyright © 2015 Combobulated Software. All rights reserved.
//

#ifndef YMRSAKeyPair_h
#define YMRSAKeyPair_h

typedef struct __YMRSAKeyPair *YMRSAKeyPairRef;

YMRSAKeyPairRef YMRSAKeyPairCreate();
YMRSAKeyPairRef YMRSAKeyPairCreateWithModuloSize(int moduloBits, int publicExponent);

bool YMRSAKeyPairGenerate(YMRSAKeyPairRef keyPair); // blocking

#endif /* YMRSAKeyPair_h */
