//
//  YMRSAKeyPair.h
//  yammer
//
//  Created by david on 11/9/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMRSAKeyPair_h
#define YMRSAKeyPair_h

YM_EXTERN_C_PUSH

typedef const struct __ym_rsa_keypair * YMRSAKeyPairRef;

YMRSAKeyPairRef YMAPI YMRSAKeyPairCreate(void);
YMRSAKeyPairRef YMAPI YMRSAKeyPairCreateWithModuloSize(int moduloBits, int publicExponent);

bool YMAPI YMRSAKeyPairGenerate(YMRSAKeyPairRef keyPair); // blocking
YMAPI void * YMRSAKeyPairGetRSA(YMRSAKeyPairRef keyPair); // meh

YM_EXTERN_C_POP

#endif /* YMRSAKeyPair_h */
