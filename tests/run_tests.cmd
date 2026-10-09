@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
if not exist build mkdir build
cl /nologo /W4 /WX /std:c11 /TC /Iinclude src\nearby_face.c tests\test_face.c /Febuild\test_face.exe /Fobuild\
if errorlevel 1 exit /b 1
build\test_face.exe
