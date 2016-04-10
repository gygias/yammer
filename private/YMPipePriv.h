//
//  YMPipePriv.h
//  yammer
//
//  Created by david on 11/15/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMPipePriv_h
#define YMPipePriv_h

YM_EXTERN_C_PUSH

#include "YMPipe.h"

void YMAPI _YMPipeCloseInputFile(YMPipeRef pipe);

// akin to 'close on dealloc,' for clients that externally close files
void YMAPI _YMPipeSetClosed(YMPipeRef pipe);

YM_EXTERN_C_POP

#endif /* YMPipePriv_h */
