@echo off
setlocal enabledelayedexpansion

echo ============================================
echo  NEVO Server Build Script
echo ============================================

:: Step 1: Setup MSVC environment
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 (
    echo ERROR: Failed to setup MSVC environment
    exit /b 1
)
echo [OK] MSVC environment configured

:: Verify compiler
where cl >nul 2>&1
if errorlevel 1 (
    echo ERROR: cl.exe not found in PATH
    exit /b 1
)
echo [OK] Compiler found: 
cl 2>&1 | findstr "Microsoft"

:: Step 2: Clean and create build directory
if exist "c:\Users\yzd20\Desktop\NEVO\build" rd /s /q "c:\Users\yzd20\Desktop\NEVO\build"
mkdir "c:\Users\yzd20\Desktop\NEVO\build" 2>nul
cd /d "c:\Users\yzd20\Desktop\NEVO\build"
echo [OK] Build directory ready

:: Step 3: Run CMake configuration
echo.
echo [STEP] Running CMake configuration...
cmake .. -G "Visual Studio 17 2022" -A x64 ^
    -DCMAKE_TOOLCHAIN_FILE="C:\vcpkg\scripts\buildsystems\vcpkg.cmake" ^
    -DVCPKG_TARGET_TRIPLET=x64-windows ^
    2>&1
if errorlevel 1 (
    echo.
    echo ERROR: CMake configuration failed!
    exit /b 1
)
echo.
echo [OK] CMake configuration successful!

:: Step 4: Build
echo.
echo [STEP] Building nevo_server (Release)...
cmake --build . --config Release --target nevo_server -- /p:CL_MPCount=4 2>&1
if errorlevel 1 (
    echo.
    echo ERROR: Build failed!
    exit /b 1
)
echo.
echo ============================================
echo  BUILD SUCCESSFUL!
echo ============================================
echo Output: c:\Users\yzd20\Desktop\NEVO\build\bin\Release\nevo_server.exe

endlocal
exit /b 0
