//
//  Yammer.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef Yammer_h
#define Yammer_h

#include <sys/types.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef WIN32
#define YMAPI __declspec( dllimport ) 
#else
#define YMAPI
#endif

#include <libyammer/YMBase.h>
#include <libyammer/YMString.h>
#include <libyammer/YMDictionary.h>
#include <libyammer/YMAddress.h>
#include <libyammer/YMPeer.h>
#include <libyammer/YMStream.h>
#include <libyammer/YMConnection.h>
#include <libyammer/YMSession.h>

#endif /* Yammer_h */
