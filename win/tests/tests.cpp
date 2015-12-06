// tests.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include "Tests.h"
#include "DictionaryTests.h"
#include "CryptoTests.h"
#include "LocalSocketPairTests.h"
#include "mDNSTests.h"
#include "TLSTests.h"
#include "PlexerTests.h"
#include "SessionTests.h"


int main()
{
	RunAllTests();
    return 0;
}

