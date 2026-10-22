@echo off
setlocal
cd /d "%~dp0"
call scripts\vcvars.bat || exit /b 1
if not exist build mkdir build
cl /nologo /O2 /MT /W3 /utf-8 /DYYJSON_DISABLE_WRITER=1 /DYYJSON_DISABLE_FILE=1 /DYYJSON_READER_DEPTH_LIMIT=64 /c third_party\yyjson\yyjson.c /Fobuild\yyjson.obj || exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /LD CodexUsage.cpp UsageBackend.cpp build\yyjson.obj /Fobuild\ /link /OUT:build\CodexUsage.dll /IMPLIB:build\CodexUsage.lib /Brepro /DEBUG:NONE /DYNAMICBASE /NXCOMPAT || exit /b 1
echo Built build\CodexUsage.dll
