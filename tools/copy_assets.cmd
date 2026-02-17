@echo off
setlocal EnableExtensions

set "OUT_IN=%~1"
set "TARGET=%~2"
set "SRC_SCRIPTS=%~3"
set "SRC_KEYWORDS=%~4"

rem ---- Canonicalize sources ----
for %%I in ("%SRC_SCRIPTS%") do set "SRC_SCRIPTS=%%~fI"
for %%I in ("%SRC_KEYWORDS%") do set "SRC_KEYWORDS=%%~fI"

rem ---- Determine OUT dir ----
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
  exit /b 8
)

rem ---- Decide runtime dir ----
set "RUNTIME=%OUT%"

if exist "%OUT%\debug\%TARGET%"   set "RUNTIME=%OUT%\debug"
if exist "%OUT%\release\%TARGET%" set "RUNTIME=%OUT%\release"

echo %OUT% | findstr /I "\-Debug"   >nul && set "RUNTIME=%OUT%\debug"
echo %OUT% | findstr /I "\-Release" >nul && set "RUNTIME=%OUT%\release"

if not exist "%RUNTIME%" mkdir "%RUNTIME%" >nul 2>nul
if not exist "%RUNTIME%" (
  echo [EMStudio] ERROR: cannot create runtime dir.
  exit /b 8
)

set "DST_SCRIPTS=%RUNTIME%\scripts"
set "DST_KEYWORDS=%RUNTIME%\keywords"

rem ---- scripts ----
if not exist "%SRC_SCRIPTS%\" (
  echo [EMStudio] ERROR: scripts dir not found.
  exit /b 8
)

if not exist "%DST_SCRIPTS%\" mkdir "%DST_SCRIPTS%" >nul 2>nul

attrib -R "%DST_SCRIPTS%\*" /S /D >nul 2>nul

robocopy "%SRC_SCRIPTS%" "%DST_SCRIPTS%" /E /R:1 /W:1 /NFL /NDL /NJH /NJS /NP >nul
if %ERRORLEVEL% GEQ 8 (
  echo [EMStudio] ERROR: robocopy scripts failed.
  exit /b %ERRORLEVEL%
)

rem ---- keywords (optional) ----
if exist "%SRC_KEYWORDS%\" (
  if not exist "%DST_KEYWORDS%\" mkdir "%DST_KEYWORDS%" >nul 2>nul

  attrib -R "%DST_KEYWORDS%\*" /S /D >nul 2>nul

  robocopy "%SRC_KEYWORDS%" "%DST_KEYWORDS%" /E /R:1 /W:1 /NFL /NDL /NJH /NJS /NP >nul
  if %ERRORLEVEL% GEQ 8 (
    echo [EMStudio] ERROR: robocopy keywords failed.
    exit /b %ERRORLEVEL%
  )
)

endlocal
exit /b 0
