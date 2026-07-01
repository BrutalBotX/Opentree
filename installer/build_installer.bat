@echo off
setlocal

set "ISCC=C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" (
    echo Inno Setup compiler not found at:
    echo %ISCC%
    exit /b 1
)

if not exist "..\build-msvc\OpenTree.exe" (
    echo Build output not found. Build the app first.
    exit /b 1
)

"%ISCC%" "OpenTree.iss"
exit /b %errorlevel%
