@echo off
setlocal
pushd "%~dp0"
if errorlevel 1 exit /b 1
REM ScanPort Windows build using the 32-bit MinGW-w64 toolchain.
set "CC="
set "WINDRES="
set "TARGET="
set "TOOLCHAIN_BIN="
set "RESULT=1"
set "OPTFLAGS=-Os -s"
if /I "%~1"=="debug" set "OPTFLAGS=-O0 -g3"

if exist "C:\msys64\mingw32\bin" set "PATH=C:\msys64\mingw32\bin;C:\msys64\usr\bin;%PATH%"
if exist "C:\msys64\mingw32\bin\i686-w64-mingw32-gcc.exe" (
    set "CC=C:\msys64\mingw32\bin\i686-w64-mingw32-gcc.exe"
    set "TOOLCHAIN_BIN=C:\msys64\mingw32\bin"
)
if exist "C:\msys64\mingw32\bin\i686-w64-mingw32-windres.exe" set "WINDRES=C:\msys64\mingw32\bin\i686-w64-mingw32-windres.exe"
if not defined WINDRES if exist "C:\msys64\mingw32\bin\windres.exe" set "WINDRES=C:\msys64\mingw32\bin\windres.exe"
for /f "delims=" %%P in ('where i686-w64-mingw32-gcc 2^>nul') do if not defined CC (
    set "CC=%%P"
    for %%D in ("%%P") do set "TOOLCHAIN_BIN=%%~dpD"
)
if not defined CC (
    for /f "delims=" %%P in ('where gcc 2^>nul') do if not defined CC (
        set "CC=%%P"
        for %%D in ("%%P") do set "TOOLCHAIN_BIN=%%~dpD"
    )
)
for /f "delims=" %%P in ('where i686-w64-mingw32-windres 2^>nul') do if not defined WINDRES set "WINDRES=%%P"
if not defined WINDRES (
    for /f "delims=" %%P in ('where windres 2^>nul') do if not defined WINDRES set "WINDRES=%%P"
)
if not defined CC (
    echo MinGW-w64 compiler not found. Add its bin directory to PATH.
    echo See README.txt for installation instructions.
    goto :finish
)
if not defined WINDRES (
    echo windres was not found. Check your MinGW-w64 installation.
    goto :finish
)
if not defined TOOLCHAIN_BIN (
    echo Could not determine the compiler bin directory.
    goto :finish
)
set "TOOLCHAIN_BIN=%TOOLCHAIN_BIN:\=/%"
for /f "delims=" %%T in ('"%CC%" -dumpmachine 2^>nul') do set "TARGET=%%T"
if /I not "%TARGET:~0,4%"=="i686" (
    echo The compiler must target 32-bit i686. Current target: %TARGET%
    goto :finish
)
"%WINDRES%" scanport.rc -O coff -o scanport.res
if errorlevel 1 (
    echo Resource compilation failed.
    goto :finish
)
"%CC%" %OPTFLAGS% -B "%TOOLCHAIN_BIN%" -mwindows -nostdlib -ffreestanding -fno-builtin -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-ident -finput-charset=UTF-8 -fexec-charset=GBK -Wl,-e,_Entry@0 -o ScanPort.exe main.c scanport.res -lkernel32 -luser32 -lgdi32 -lws2_32 -lcomctl32
if errorlevel 1 (
    echo Build failed.
    goto :finish
)
echo Build succeeded: ScanPort.exe
set "RESULT=0"

:finish
if exist scanport.res del /q scanport.res
if not defined SCANPORT_NO_PAUSE pause
popd
exit /b %RESULT%
