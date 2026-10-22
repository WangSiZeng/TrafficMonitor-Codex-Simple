@echo off
setlocal
cd /d "%~dp0"
call build.bat || exit /b 1
call scripts\vcvars.bat || exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX tests\mock_codex.cpp /Fobuild\mock_codex.obj /link /OUT:build\mock_codex.exe /Brepro || exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /I. tests\backend_tests.cpp UsageBackend.cpp build\yyjson.obj /Fobuild\ /link /OUT:build\backend_tests.exe /Brepro || exit /b 1
build\backend_tests.exe || exit /b 1
