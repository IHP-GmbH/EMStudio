@echo off
setlocal EnableExtensions

set "OUT_IN=%~1"
set "TARGET=%~2"
set "SRC_SCRIPTS=%~3"
set "SRC_KEYWORDS=%~4"

rem ---- Canonicalize sources ----
for %%I in ("%SRC_SCRIPTS%") do set "SRC_SCRIPTS=%%~fI"
for %%I in ("%SRC_KEYWORDS%") do set "SRC_KEYWORDS=%%~fI"

rem ---- Determine OUT (robust): try OUT_IN, otherwise use current dir ----
set "OUT="

pushd "%OUT_IN%" >nul 2>nul
if not errorlevel 1 (
  set "OUT=%CD%"
  popd >nul
) else (
  pushd "%CD%" >nul 2>nul
  if not errorlevel 1 (
    set "OUT=%CD%"
    popd >nul
  )
)

if "%OUT%"=="" (
  echo [EMStudio] ERROR: cannot determine OUT dir.
  echo [EMStudio] OUT_IN=[%OUT_IN%]
  echo [EMStudio] CD=[%CD%]
  exit /b 8
)

rem ---- Decide RUNTIME ----
set "RUNTIME=%OUT%"

if exist "%OUT%\debug\%TARGET%"   set "RUNTIME=%OUT%\debug"
if exist "%OUT%\release\%TARGET%" set "RUNTIME=%OUT%\release"

echo %OUT% | findstr /I "\-Debug"   >nul && set "RUNTIME=%OUT%\debug"
echo %OUT% | findstr /I "\-Release" >nul && set "RUNTIME=%OUT%\release"

rem Ensure runtime exists
if not exist "%RUNTIME%" mkdir "%RUNTIME%" >nul 2>nul
if not exist "%RUNTIME%" (
  echo [EMStudio] ERROR: cannot create runtime dir: "%RUNTIME%"
  exit /b 8
)

set "DST_SCRIPTS=%RUNTIME%\scripts"
set "DST_KEYWORDS=%RUNTIME%\keywords"

echo [EMStudio] OUT=[%OUT%]
echo [EMStudio] TARGET=[%TARGET%]
echo [EMStudio] RUNTIME=[%RUNTIME%]
echo [EMStudio] SRC_SCRIPTS=[%SRC_SCRIPTS%]
echo [EMStudio] SRC_KEYWORDS=[%SRC_KEYWORDS%]
echo [EMStudio] DST_SCRIPTS=[%DST_SCRIPTS%]
echo [EMStudio] DST_KEYWORDS=[%DST_KEYWORDS%]

rem ---- scripts source must exist ----
if not exist "%SRC_SCRIPTS%\" (
  echo [EMStudio] ERROR: scripts dir not found: "%SRC_SCRIPTS%"
  exit /b 8
)

rem ---- If destination exists but is a file, try remove ----
if exist "%DST_SCRIPTS%" (
  if not exist "%DST_SCRIPTS%\" (
    echo [EMStudio] WARN: "%DST_SCRIPTS%" exists but is not a directory. Trying to remove it...
    attrib -R -H -S "%DST_SCRIPTS%" >nul 2>nul
    del /F /Q "%DST_SCRIPTS%" >nul 2>nul
    if exist "%DST_SCRIPTS%" (
      echo [EMStudio] ERROR: cannot remove file: "%DST_SCRIPTS%" (Access is denied?)
      dir /a "%RUNTIME%"
      exit /b 8
    )
  )
)

rem ---- Create scripts dir ----
if not exist "%DST_SCRIPTS%\" mkdir "%DST_SCRIPTS%" >nul 2>nul
if not exist "%DST_SCRIPTS%\" (
  echo [EMStudio] ERROR: cannot create destination dir: "%DST_SCRIPTS%"
  dir /a "%RUNTIME%"
  exit /b 8
)

echo [EMStudio] Sync scripts -> "%DST_SCRIPTS%"
robocopy "%SRC_SCRIPTS%" "%DST_SCRIPTS%" /MIR /R:1 /W:1 /NFL /NDL /NJH /NJS /NP >nul
set "RC=%ERRORLEVEL%"
if %RC% GEQ 8 (
  echo [EMStudio] ERROR: robocopy scripts failed (code %RC%)
  exit /b %RC%
)
echo [EMStudio] Scripts OK (robocopy code %RC%)

rem ---- keywords (optional) ----
if not exist "%SRC_KEYWORDS%\" (
  echo [EMStudio] WARN: keywords dir not found: "%SRC_KEYWORDS%"
  goto ok
)

if exist "%DST_KEYWORDS%" (
  if not exist "%DST_KEYWORDS%\" (
    echo [EMStudio] WARN: "%DST_KEYWORDS%" exists but is not a directory. Trying to remove it...
    attrib -R -H -S "%DST_KEYWORDS%" >nul 2>nul
    del /F /Q "%DST_KEYWORDS%" >nul 2>nul
  )
)

if not exist "%DST_KEYWORDS%\" mkdir "%DST_KEYWORDS%" >nul 2>nul
if not exist "%DST_KEYWORDS%\" (
  echo [EMStudio] ERROR: cannot create destination dir: "%DST_KEYWORDS%"
  exit /b 8
)

echo [EMStudio] Sync keywords -> "%DST_KEYWORDS%"
robocopy "%SRC_KEYWORDS%" "%DST_KEYWORDS%" /MIR /R:1 /W:1 /NFL /NDL /NJH /NJS /NP >nul
set "RC=%ERRORLEVEL%"
if %RC% GEQ 8 (
  echo [EMStudio] ERROR: robocopy keywords failed (code %RC%)
  exit /b %RC%
)
echo [EMStudio] Keywords OK (robocopy code %RC%)

:ok
endlocal
exit /b 0
