// libyammer.cpp : Defines the exported functions for the DLL application.
//

#include "stdafx.h"
#include "libyammer.h"


// This is an example of an exported variable
LIBYAMMER_API int nlibyammer=0;

// This is an example of an exported function.
LIBYAMMER_API int fnlibyammer(void)
{
    return 42;
}

// This is the constructor of a class that has been exported.
// see libyammer.h for the class definition
Clibyammer::Clibyammer()
{
    return;
}
