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
#import <Yammer.h>

//#define     Logging 1
#ifdef      Logging
#define     TestLog(x,...) NSLog((x),##__VA_ARGS__)
#else
#define     TestLog(x,...) ;
#endif

// this is the best way of ordering XCTestCase classes that i could find, at the time
#define DictionaryTests B_DictionaryTests
#define CryptoTests     C_CryptoTests
#define TLSTests        D_TLSTests
#define PlexerTests     A_PlexerTests
#define ConnectionTests F_ConnectionTests
#define SessionTests    G_SessionTests

NSString *YMRandomASCIIStringWithMaxLength(uint16_t maxLength, BOOL for_mDNSServiceName);
NSData *YMRandomDataWithMaxLength(uint16_t length);

#endif /* YammerTests_h */
