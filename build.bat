@echo off
setlocal
cd /d "%~dp0"
call scripts\vcvars.bat || exit /b 1
if not exist "%VCToolsInstallDir%atlmfc\include\afxcolordialog.h" (
  echo Install the Visual Studio C++ MFC component for x86/x64 before building.
  exit /b 1
)
if not exist build mkdir build
rc /nologo /fobuild\ColorPicker.res ColorPicker.rc || exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /utf-8 /DUNICODE /D_UNICODE /D_USRDLL /c ColorPicker.cpp /Fobuild\ColorPicker.obj || exit /b 1
cl /nologo /O2 /MT /W3 /utf-8 /DYYJSON_DISABLE_WRITER=1 /DYYJSON_DISABLE_FILE=1 /DYYJSON_READER_DEPTH_LIMIT=64 /c third_party\yyjson\yyjson.c /Fobuild\yyjson.obj || exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /DCODEX_USE_MFC /LD CodexUsage.cpp UsageBackend.cpp build\ColorPicker.obj build\ColorPicker.res build\yyjson.obj /Fobuild\ /link uafxcw.lib libcmt.lib /OUT:build\CodexUsage.dll /IMPLIB:build\CodexUsage.lib /Brepro /DEBUG:NONE /DYNAMICBASE /NXCOMPAT || exit /b 1
echo Built build\CodexUsage.dll
