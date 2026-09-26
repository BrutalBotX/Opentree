@echo off
setlocal

rem ============================================================
rem  MinGW + Ninja build helper (no Visual Studio required).
rem  Tools are expected from:
rem    - Qt 6.8.x MinGW kit          (aqt install-qt ... win64_mingw)
rem    - MinGW 13.1 toolchain        (aqt install-tool ... tools_mingw1310)
rem    - CMake + Ninja via pip       (pip install --user cmake ninja)
rem  Edit the paths below if your install differs.
rem ============================================================

set "QT_ROOT=%USERPROFILE%\Qt"
set "QT_PREFIX=%QT_ROOT%\6.8.0\mingw_64"
set "MINGW=%QT_ROOT%\Tools\mingw1310_64"

for /f "delims=" %%i in ('python -c "import sysconfig;print(sysconfig.get_path('scripts','nt_user'))"') do set "PY_SCRIPTS=%%i"

set "PATH=%QT_PREFIX%\bin;%MINGW%\bin;%PY_SCRIPTS%;%PATH%"

rem windres cannot handle spaces in include paths, so the build runs through a
rem junction that has no spaces in its path.
set "LINK=%USERPROFILE%\otv2"
set "BUILD=%USERPROFILE%\otv2-build"
if not exist "%LINK%" cmd /c mklink /J "%LINK%" "%~dp0." >nul

cmake -S "%LINK%" -B "%BUILD%" -G Ninja ^
  -DCMAKE_PREFIX_PATH="%QT_PREFIX%" ^
  -DCMAKE_CXX_COMPILER="%MINGW%\bin\g++.exe" ^
  -DCMAKE_MAKE_PROGRAM="%PY_SCRIPTS%\ninja.exe" ^
  -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 exit /b %errorlevel%

cmake --build "%BUILD%"
exit /b %errorlevel%
