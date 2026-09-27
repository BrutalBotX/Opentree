@echo off
setlocal

rem Builds the Windows installer with Inno Setup 6.
rem Usage: build_installer.bat [version]
rem If no version is given, it is read from CMakeLists.txt.

rem Work from the script's own folder so the relative paths below always resolve.
cd /d "%~dp0"

set "ISCC=C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=C:\Program Files\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" (
    echo Inno Setup 6 was not found. Install it from https://jrsoftware.org/isdl.php
    echo or run: winget install JRSoftware.InnoSetup
    exit /b 1
)

if not exist "..\build-msvc\OpenTree.exe" (
    echo Build output not found. Run build_msvc.bat first.
    exit /b 1
)

set "APP_VERSION=%~1"
if "%APP_VERSION%"=="" (
    for /f "tokens=3" %%v in ('findstr /r /c:"project(OpenTree VERSION" "..\CMakeLists.txt"') do set "APP_VERSION=%%v"
)
if "%APP_VERSION%"=="" (
    echo Could not determine the version.
    exit /b 1
)

echo Building installer for OpenTree %APP_VERSION%
"%ISCC%" /DMyAppVersion=%APP_VERSION% "OpenTree.iss"
exit /b %errorlevel%
