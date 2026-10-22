@echo off
setlocal
cd /d "%~dp0"
call build.bat || exit /b 1
call scripts\vcvars.bat || exit /b 1
cl /nologo /std:c++17 /EHsc /MT /utf-8 /DNOMINMAX test_options.cpp UsageBackend.cpp build\yyjson.obj /Fobuild\ /link /OUT:build\test_options.exe || exit /b 1
build\test_options.exe
