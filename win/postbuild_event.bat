cd

:: are VS macros really unavailable from 'build events'?
set DEBUG=Debug
set RELEASE=Release
set CPSRC=lib\*
IF NOT EXIST lib (
	set CPSRC=..\%CPSRC%
)

set CPDST=%DEBUG%

:CPAGAIN
xcopy /Y %CPSRC% %CPDST%\
IF NOT %ERRORLEVEL% EQU 2 IF NOT %ERRORLEVEL% EQU 0 (
	IF NOT EXIST %CPDST% (
		set CPDST=%RELEASE%
		GOTO CPAGAIN
	)
	ECHO xcopy %CPSRC% %CPDST% returned %ERRORLEVEL%
	exit /B 1
)

ECHO copied %CPSRC% to %CPDST%

IF EXIST %RELEASE% (
	IF %CPDST% neq %RELEASE% (
		set CPDST=%RELEASE%
		GOTO CPAGAIN
	)
)

exit /B 0