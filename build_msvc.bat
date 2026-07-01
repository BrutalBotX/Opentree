@echo off
setlocal

call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64
if errorlevel 1 exit /b %errorlevel%

set "QT_PREFIX=C:\Qt\6.8.0\msvc2022_64"

echo ===== CONFIGURE MSVC BUILD =====
cmake -S . -B build-msvc -G "NMake Makefiles" -DCMAKE_PREFIX_PATH="%QT_PREFIX%" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 exit /b %errorlevel%

echo ===== BUILD MSVC TARGET =====
cmake --build build-msvc --config Release
exit /b %errorlevel%