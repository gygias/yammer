//
//  YMRSAKeyPair.h
//  yammer
//
//  Created by david on 11/9/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMRSAKeyPair_h
#define YMRSAKeyPair_h

typedef const struct __ym_rsa_keypair_t *YMRSAKeyPairRef;

YMRSAKeyPairRef YMAPI YMRSAKeyPairCreate();
YMRSAKeyPairRef YMAPI YMRSAKeyPairCreateWithModuloSize(int moduloBits, int publicExponent);

bool YMAPI YMRSAKeyPairGenerate(YMRSAKeyPairRef keyPair); // blocking
YMAPI void * YMRSAKeyPairGetRSA(YMRSAKeyPairRef keyPair); // meh

#endif /* YMRSAKeyPair_h */
