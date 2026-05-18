@echo off
setlocal
cd /d "%~dp0"

where python >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Python not found on PATH.
    pause
    exit /b 1
)

if "%PIO_EDITOR_PORT%"=="" (set PORT=8765) else (set PORT=%PIO_EDITOR_PORT%)

REM Stop a previous editor instance on the same port
for /f "tokens=5" %%a in ('netstat -ano 2^>nul ^| findstr ":%PORT% " ^| findstr LISTENING') do taskkill /PID %%a /F >nul 2>&1

REM Wait for server, then open browser (do not open index.html directly)
start "Open browser" powershell -NoProfile -WindowStyle Hidden -Command ^
  "$u='http://127.0.0.1:%PORT%/'; for($i=0;$i -lt 40;$i++){ try { Invoke-WebRequest -Uri ($u+'api/health') -UseBasicParsing -TimeoutSec 1 | Out-Null; Start-Process $u; exit 0 } catch { Start-Sleep -Milliseconds 500 } }; Write-Host 'Editor server did not start.'; pause"

echo.
echo Park Firmware Editor  http://127.0.0.1:%PORT%/
echo Keep this window open. Press Ctrl+C to stop.
echo.
python server.py
if errorlevel 1 pause
