@echo off

set INSTALL_DIR=C:\re\x64dbg_dev\release\x32\plugins\
set CMAKE_BIN=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe

if not exist %INSTALL_DIR% mkdir %INSTALL_DIR%
"%CMAKE_BIN%" -B build32 -G "Visual Studio 17 2022" -A Win32
"%CMAKE_BIN%" --build build32 --config Release
copy /Y build32\Release\x64dbg-automate.dp32 %INSTALL_DIR%\
copy /Y build32\Release\libzmq-mt-4_3_5.dll %INSTALL_DIR%\