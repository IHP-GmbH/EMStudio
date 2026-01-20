@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem -----------------------------------------------------------------------------
rem Run EMStudio KLayout driver, trying to locate KLayout automatically.
rem This .bat may be placed either in:
rem   - {app}\Run_EMStudio_in_KLayout.bat
rem   - {app}\scripts\Run_EMStudio_in_KLayout.bat
rem Driver is expected at:
rem   - {app}\scripts\klEmsDriver.py
rem -----------------------------------------------------------------------------

set "APP_DIR=%~dp0"

rem Resolve driver path
if exist "%APP_DIR%klEmsDriver.py" (
  set "DRIVER=%APP_DIR%klEmsDriver.py"
) else (
  set "DRIVER=%APP_DIR%scripts\klEmsDriver.py"
)

if not exist "%DRIVER%" (
  echo ERROR: Driver not found: "%DRIVER%"
  echo Expected it either next to this .bat or in a "scripts" subfolder.
  pause
  exit /b 1
)

rem -----------------------------------------------------------------------------
rem Locate KLayout executable
rem -----------------------------------------------------------------------------

set "KLAYOUT="

rem 1) If KLAYOUT_EXE is set by user/environment, prefer it
if defined KLAYOUT_EXE (
  if exist "%KLAYOUT_EXE%" (
    set "KLAYOUT=%KLAYOUT_EXE%"
    goto :run
  )
)

rem 2) Try PATH (prefer klayout_app.exe, fallback to klayout.exe, also without .exe)
for %%X in (klayout_app.exe klayout.exe klayout_app klayout) do (
  if not "%%~$PATH:X"=="" (
    set "KLAYOUT=%%~$PATH:X"
    goto :run
  )
)

rem 3) Try common install locations (Program Files)
set "CAND1=%ProgramFiles%\KLayout\klayout_app.exe"
set "CAND2=%ProgramFiles%\KLayout\klayout.exe"
set "CAND3=%ProgramFiles(x86)%\KLayout\klayout_app.exe"
set "CAND4=%ProgramFiles(x86)%\KLayout\klayout.exe"

for %%C in ("%CAND1%" "%CAND2%" "%CAND3%" "%CAND4%") do (
  if exist "%%~C" (
    set "KLAYOUT=%%~C"
    goto :run
  )
)

rem 4) Try per-user locations (AppData)
set "CAND5=%APPDATA%\KLayout\klayout_app.exe"
set "CAND6=%APPDATA%\KLayout\klayout_app"
set "CAND7=%LOCALAPPDATA%\KLayout\klayout_app.exe"
set "CAND8=%LOCALAPPDATA%\KLayout\klayout_app"

for %%C in ("%CAND5%" "%CAND6%" "%CAND7%" "%CAND8%") do (
  if exist "%%~C" (
    set "KLAYOUT=%%~C"
    goto :run
  )
)

rem 5) Try registry App Paths (may or may not exist)
for %%N in (klayout_app.exe klayout.exe) do (
  for /f "tokens=2,*" %%A in ('reg query "HKCU\SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\%%N" /ve 2^>nul ^| find /i "REG_SZ"') do (
    set "KLAYOUT=%%B"
    goto :run
  )
  for /f "tokens=2,*" %%A in ('reg query "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\%%N" /ve 2^>nul ^| find /i "REG_SZ"') do (
    set "KLAYOUT=%%B"
    goto :run
  )
  for /f "tokens=2,*" %%A in ('reg query "HKLM\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\App Paths\%%N" /ve 2^>nul ^| find /i "REG_SZ"') do (
    set "KLAYOUT=%%B"
    goto :run
  )
)

rem 6) Try Uninstall keys (sometimes KLayout is registered there)
for /f "tokens=2,*" %%A in ('reg query "HKCU\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall" /s /v InstallLocation 2^>nul ^| find /i "KLayout"') do (
  if exist "%%B\klayout_app.exe" set "KLAYOUT=%%B\klayout_app.exe" & goto :run
  if exist "%%B\klayout.exe"     set "KLAYOUT=%%B\klayout.exe"     & goto :run
)
for /f "tokens=2,*" %%A in ('reg query "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall" /s /v InstallLocation 2^>nul ^| find /i "KLayout"') do (
  if exist "%%B\klayout_app.exe" set "KLAYOUT=%%B\klayout_app.exe" & goto :run
  if exist "%%B\klayout.exe"     set "KLAYOUT=%%B\klayout.exe"     & goto :run
)

echo ERROR: Could not find KLayout (klayout_app.exe / klayout.exe / klayout_app / klayout).
echo Searched: PATH, Program Files, %%APPDATA%%\KLayout, registry App Paths, Uninstall keys.
echo
echo You can set it once like:
echo   setx KLAYOUT_EXE "C:\Full\Path\To\klayout_app.exe"
pause
exit /b 2

:run
if not exist "%KLAYOUT%" (
  echo ERROR: Found KLayout path, but file does not exist: "%KLAYOUT%"
  pause
  exit /b 3
)

rem -----------------------------------------------------------------------------
rem Launch KLayout with your driver and forward any passed args/files (e.g. *.gds)
rem NOTE: pass %* directly to preserve quoting for paths with spaces.
rem -----------------------------------------------------------------------------
if "%~1"=="" (
  start "" "%KLAYOUT%" -e -rm "%DRIVER%"
) else (
  start "" "%KLAYOUT%" -e -rm "%DRIVER%" %*
)

exit /b 0
