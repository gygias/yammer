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
#import "Yammer.h"

//#define     Logging 1
#ifdef      Logging
#define     NoisyTestLog(x,...) NSLog((x),##__VA_ARGS__)
#else
#define     NoisyTestLog(x,...) ;
#endif

// this is the best way of ordering XCTestCase classes that i could find, at the time
#define DictionaryTests         B_DictionaryTests
#define LocalSocketPairTests    C_LocalSocketPairTests
#define CryptoTests             D_CryptoTests
#define mDNSTests               E_mDNSTests
#define TLSTests                F_TLSTests
#define PlexerTests             G_PlexerTests
#define ConnectionTests         H_ConnectionTests
#define SessionTests            I_SessionTests
#define CheckStateTest          Z_CheckStateTest

NSString *YMRandomASCIIStringWithMaxLength(uint16_t maxLength, BOOL for_mDNSServiceName);
NSData *YMRandomDataWithMaxLength(uint16_t length);

#endif /* YammerTests_h */
