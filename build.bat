@echo off
rem Build the CCPL compiler with TinyCC.
rem
rem Resolution order for the compiler driver:
rem   1. %%TCC%% environment variable
rem   2. toolchain\tcc\tcc.exe next to this script
rem   3. tcc on PATH
setlocal
set "ROOT=%~dp0"

set "TCCBIN="
if defined TCC set "TCCBIN=%TCC%"
if not defined TCCBIN if exist "%ROOT%toolchain\tcc\tcc.exe" set "TCCBIN=%ROOT%toolchain\tcc\tcc.exe"
if not defined TCCBIN set "TCCBIN=tcc"

if not exist "%ROOT%build" mkdir "%ROOT%build"

"%TCCBIN%" "%ROOT%src\compiler.c" -o "%ROOT%build\coolc.exe"
if errorlevel 1 (
    echo.
    echo build failed - is TinyCC installed? Set TCC to the full path of tcc.exe
    echo or place it at toolchain\tcc\tcc.exe.
    exit /b 1
)

copy /Y "%ROOT%src\runtime.h" "%ROOT%build\runtime.h" >nul
echo built %ROOT%build\coolc.exe
