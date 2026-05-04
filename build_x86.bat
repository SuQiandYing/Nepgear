@echo off
setlocal enabledelayedexpansion

:: Search for vswhere.exe
set "VS_WHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "!VS_WHERE!" set "VS_WHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"

if not exist "!VS_WHERE!" (
    echo [ERROR] vswhere.exe not found. Please ensure Visual Studio is installed.
    pause
    exit /b 1
)

:: Get the installation path of the latest MSBuild-capable VS
for /f "usebackq tokens=*" %%i in (`"!VS_WHERE!" -latest -requires Microsoft.Component.MSBuild -property installationPath`) do (
    set "VS_PATH=%%i"
)

if not exist "!VS_PATH!\VC\Auxiliary\Build\vcvarsall.bat" (
    echo [ERROR] vcvarsall.bat not found. Please ensure C++ workload is installed.
    pause
    exit /b 1
)

:: Set up x86 build environment
echo [INFO] Configuring x86 build environment...
call "!VS_PATH!\VC\Auxiliary\Build\vcvarsall.bat" x86

:: Execute build
echo [INFO] Compiling Nepgear (Release^|x86)...
msbuild "Nepgear.sln" /p:Configuration=Release /p:Platform=x86 /t:Rebuild /m

if %ERRORLEVEL% equ 0 (
    echo.
    echo [SUCCESS] Build completed!
) else (
    echo.
    echo [ERROR] Build failed with exit code %ERRORLEVEL%.
)

pause
