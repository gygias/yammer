// The following ifdef block is the standard way of creating macros which make exporting 
// from a DLL simpler. All files within this DLL are compiled with the LIBYAMMER_EXPORTS
// symbol defined on the command line. this symbol should not be defined on any project
// that uses this DLL. This way any other project whose source files include this file see 
// LIBYAMMER_API functions as being imported from a DLL, whereas this DLL sees symbols
// defined with this macro as being exported.
#ifdef LIBYAMMER_EXPORTS
#define LIBYAMMER_API __declspec(dllexport)
#else
#define LIBYAMMER_API __declspec(dllimport)
#endif

// This class is exported from the libyammer.dll
class LIBYAMMER_API Clibyammer {
public:
	Clibyammer(void);
	// TODO: add your methods here.
};

extern LIBYAMMER_API int nlibyammer;

LIBYAMMER_API int fnlibyammer(void);
