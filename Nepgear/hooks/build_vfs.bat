@echo off
setlocal
echo [VFS Build Verification Script]
echo Looking for Visual Studio environment...

:: List of common vcvarsall.bat locations
set "vs_path=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
if not exist "%vs_path%" set "vs_path=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat"
if not exist "%vs_path%" set "vs_path=C:\Program Files (x86)\Microsoft Visual Studio\2017\Community\VC\Auxiliary\Build\vcvarsall.bat"

if not exist "%vs_path%" (
    echo [ERROR] Could not find vcvarsall.bat. Please run this script from a "Developer Command Prompt for VS".
    goto compile
)

echo Found VS at: %vs_path%
call "%vs_path%" x86

:compile
echo Compiling vfs.cpp...
:: /c means compile only, /I adds include directories, /W3 for warnings
cl.exe /c /W3 /I. /I.. vfs.cpp

if %errorlevel% neq 0 (
    echo.
    echo [FAILURE] Compilation failed with errors.
    exit /b %errorlevel%
)

echo.
echo [SUCCESS] vfs.cpp compiled successfully.
endlocal
pause
