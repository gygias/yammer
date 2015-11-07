//
//  YammerTestUtilities.h
//  yammer
//
//  Created by david on 11/6/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YammerTestUtilities_h
#define YammerTestUtilities_h

#import <Foundation/Foundation.h>

NSString *YMRandomASCIIStringWithMaxLength(uint8_t maxLength, BOOL for_mDNSServiceName);
NSData *YMRandomDataWithMaxLength(uint8_t length);

#endif /* YammerTestUtilities_h */
