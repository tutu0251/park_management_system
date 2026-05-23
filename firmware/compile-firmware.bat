@echo off
setlocal EnableExtensions
cd /d "%~dp0"

where pio >nul 2>&1
if errorlevel 1 (
    echo [ERROR] PlatformIO ^(pio^) not found on PATH.
    echo Install offline:  cd python-offline ^&^& powershell -ExecutionPolicy Bypass -File install-offline.ps1
    echo Or online:        pip install platformio
    if not "%PARK_FW_NO_PAUSE%"=="1" pause
    exit /b 1
)

if "%~1"=="" goto build_all
if /i "%~1"=="all" goto build_all
goto build_one

:build_all
echo.
echo === Building all firmware environments from platformio.ini ===
echo.
call scripts\build-all.ps1
goto done

:build_one
echo.
echo === Building environment: %~1 ===
echo.
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\validate-env.ps1 "%~1"
if errorlevel 1 goto done
pio run -e %~1 -j 1
goto done

:done
if errorlevel 1 (
    echo.
    echo [FAILED] Build failed.
    if not "%PARK_FW_NO_PAUSE%"=="1" pause
    exit /b 1
)

echo.
if "%~1"=="" (
    echo [OK] Hex outputs under .pio\build\ for platformio.ini environments
) else if /i not "%~1"=="all" (
    echo [OK] .pio\build\%~1\firmware.hex
) else (
    echo [OK] Hex outputs under .pio\build\  ^(all environments^)
)
echo.
echo Usage: compile-firmware.bat [environment-name^|all]
exit /b 0
