@echo off
setlocal

rem ============================================================
rem  MSVC + Qt WebEngine build helper.
rem  Locates Visual Studio 2022 (Build Tools or full) with vswhere,
rem  then configures build-msvc/ and builds OpenTree.exe.
rem ============================================================

set "VSWHERE=C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo Could not find vswhere.exe. Install Visual Studio 2022 Build Tools first.
  exit /b 1
)

set "VS_PATH="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_PATH=%%i"
if not defined VS_PATH (
  echo Could not locate a Visual Studio install with the C++ toolset.
  exit /b 1
)

call "%VS_PATH%\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64
if errorlevel 1 exit /b %errorlevel%

rem Locate the Qt 6 MSVC kit (aqt installs under %USERPROFILE%\Qt).
set "QT_PREFIX=%USERPROFILE%\Qt\6.8.0\msvc2022_64"
if not exist "%QT_PREFIX%\bin\qmake.exe" set "QT_PREFIX=C:\Qt\6.8.0\msvc2022_64"
if not exist "%QT_PREFIX%\bin\qmake.exe" (
  echo Could not locate the Qt 6.8.0 msvc2022_64 kit.
  exit /b 1
)

echo ===== CONFIGURE MSVC BUILD =====
cmake -S . -B build-msvc -G "NMake Makefiles" -DCMAKE_PREFIX_PATH="%QT_PREFIX%" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 exit /b %errorlevel%

echo ===== BUILD MSVC TARGET =====
cmake --build build-msvc --config Release
exit /b %errorlevel%
