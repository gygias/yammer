//
//  YMRSAKeyPair.h
//  yammer
//
//  Created by david on 11/9/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMRSAKeyPair_h
#define YMRSAKeyPair_h

typedef YMTypeRef YMRSAKeyPairRef;

YMRSAKeyPairRef YMRSAKeyPairCreate();
YMRSAKeyPairRef YMRSAKeyPairCreateWithModuloSize(int moduloBits, int publicExponent);

bool YMRSAKeyPairGenerate(YMRSAKeyPairRef keyPair); // blocking

void *YMRSAKeyPairGetRSA(YMRSAKeyPairRef keyPair); // meh

#endif /* YMRSAKeyPair_h */
