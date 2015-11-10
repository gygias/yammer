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

bool YMRSAKeyPairGenerate(YMRSAKeyPairRef keyPair); // blocking

// undefined if called before
int YMRSAKeyPairGetN(); // public modulo
int YMRSAKeyPairGete(); // public exponent


#endif /* YMRSAKeyPair_h */
