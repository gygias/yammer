//
//  YammerTests.h
//  yammer
//
//  Created by david on 11/8/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YammerTests_h
#define YammerTests_h

#import <XCTest/XCTest.h>

//#define     Logging 1
#ifdef      Logging
#define     TestLog(x,...) NSLog((x),##__VA_ARGS__)
#else
#define     TestLog(x,...) ;
#endif

// this is the best way of ordering XCTestCase classes that i could find, at the time
#define DictionaryTests A_DictionaryTests
#define CryptoTests     B_CryptoTests
#define TLSTests        C_TLSTests
#define PlexerTests     D_PlexerTests

NSString *YMRandomASCIIStringWithMaxLength(uint16_t maxLength, BOOL for_mDNSServiceName);
NSData *YMRandomDataWithMaxLength(uint16_t length);

#endif /* YammerTests_h */
