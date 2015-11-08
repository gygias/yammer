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

NSString *YMRandomASCIIStringWithMaxLength(uint32_t maxLength, BOOL for_mDNSServiceName);
NSData *YMRandomDataWithMaxLength(uint32_t length);

#endif /* YammerTestUtilities_h */
