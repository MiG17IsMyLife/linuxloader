@echo off
setlocal EnableDelayedExpansion

:: Configuration
set "MSYS2_URL=https://github.com/msys2/msys2-installer/releases/download/2024-01-13/msys2-x86_64-20240113.exe"
set "INSTALLER_EXE=msys2_installer.exe"
set "INSTALL_DIR=C:\msys64"

:: Define the specific bins to add to PATH
set "USR_BIN=%INSTALL_DIR%\usr\bin"
set "MINGW_BIN=%INSTALL_DIR%\mingw32\bin"

echo [1/4] Downloading MSYS2 installer...
curl -L %MSYS2_URL% -o %INSTALLER_EXE%

echo [2/4] Installing MSYS2 silently to %INSTALL_DIR%...
start /wait "" %INSTALLER_EXE% in --confirm-command --accept-messages --root %INSTALL_DIR%

if not exist "%USR_BIN%\bash.exe" (
    echo Installation failed.
    exit /b 1
)

echo [3/4] Installing MinGW32 and CMake...
"%USR_BIN%\bash.exe" -lc "pacman -Syu --noconfirm"
"%USR_BIN%\bash.exe" -lc "pacman -S --noconfirm mingw-w64-i686-toolchain mingw-w64-i686-cmake make"

echo [4/4] Adding MSYS2 and MinGW32 to User PATH...
:: Fetch current User PATH to avoid duplicates
for /f "tokens=2*" %%A in ('reg query "HKCU\Environment" /v PATH') do set "OLD_PATH=%%B"

:: Append new paths only if they don't already exist in the string
echo %OLD_PATH% | find /i "%USR_BIN%" >nul || set "NEW_PATH=%USR_BIN%;"
echo %OLD_PATH% | find /i "%MINGW_BIN%" >nul || set "NEW_PATH=%NEW_PATH%%MINGW_BIN%;"

if defined NEW_PATH (
    setx PATH "%OLD_PATH%;%NEW_PATH%"
    echo PATH updated successfully.
) else (
    echo Paths already exist in Environment Variables.
)

echo.
echo Setup Complete!

md build-win32
cd build-win32

echo [5/5] Executing build inside MSYS2 shell...

:: Use MSYSTEM=MINGW32 to ensure the correct environment variables are loaded
:: Use 'chroot' logic or 'cd' to the current directory inside the bash call
set "CHERE_INVOKING=1"
set "MSYSTEM=MINGW32"

"%USR_BIN%\bash.exe" -lc "cmake .. -G 'MinGW Makefiles' && cmake --build ."

copy c:\msys64\mingw32\bin\libwinpthread-1.dll C:\msys64\mingw32\lib\gcc\i686-w64-mingw32\16.1.0\

if %ERRORLEVEL% EQU 0 (
    echo Build complete.
) else (
    echo Build failed with error %ERRORLEVEL%
)

del %INSTALLER_EXE%
pause