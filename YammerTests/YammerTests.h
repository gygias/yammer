//
//  YammerTests.h
//  yammer
//
//  Created by david on 11/8/15.
//  Copyright © 2015 Combobulated Software. All rights reserved.
//

#ifndef YammerTests_h
#define YammerTests_h

#import <XCTest/XCTest.h>

//#define     Logging 1
#ifdef      Logging
#define     TestLog(x,...) NSLog((x),##__VA_ARGS__)
#else
#define     TestLog(x,...)
#endif

NSString *YMRandomASCIIStringWithMaxLength(uint16_t maxLength, BOOL for_mDNSServiceName);
NSData *YMRandomDataWithMaxLength(uint16_t length);

#endif /* YammerTests_h */
