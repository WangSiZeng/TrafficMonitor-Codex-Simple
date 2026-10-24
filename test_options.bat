@echo off
setlocal
cd /d "%~dp0"
call build.bat || exit /b 1
call scripts\vcvars.bat || exit /b 1
cl /nologo /std:c++17 /EHsc /MT /utf-8 /DUNICODE /D_UNICODE /c ColorPicker.cpp /Fobuild\ColorPickerTest.obj || exit /b 1
cl /nologo /std:c++17 /EHsc /MT /utf-8 /DNOMINMAX /DCODEX_USE_MFC test_options.cpp UsageBackend.cpp build\ColorPickerTest.obj build\ColorPicker.res build\yyjson.obj /Fobuild\ /link /OUT:build\test_options.exe || exit /b 1
build\test_options.exe || exit /b 1
cl /nologo /std:c++17 /EHsc /MT /utf-8 /DNOMINMAX /I. tests\color_dll_tests.cpp /Fobuild\color_dll_tests.obj /link /OUT:build\color_dll_tests.exe /Brepro || exit /b 1
build\color_dll_tests.exe
