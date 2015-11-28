xcopy /Y lib\*.dll Debug\
IF NOT %ERRORLEVEL% EQU 2 IF NOT %ERRORLEVEL% EQU 0 (
  ECHO xcopy returned %ERRORLEVEL%
  exit /B 1
)

ECHO copied lib\*.dll to Debug
exit /B 0