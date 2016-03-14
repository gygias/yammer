//
//  TestsMain.c
//  yammer
//
//  Created by david on 12/9/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "Tests.h"

// this is a separate file so that other platforms can use Tests.{c,h}, but have their own main


int main( __unused int argc, __unused const char *argv[] )
{
    bool indefinite = argc > 1;
    
    do {
        RunAllTests();
    } while (indefinite);
    
    return 0;
}
