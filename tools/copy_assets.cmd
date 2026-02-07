@echo off
setlocal enableextensions

set "OUT=%~1"
set "TARGET=%~2"
set "SRC_SCRIPTS=%~3"
set "SRC_KEYWORDS=%~4"

set "RUNTIME=%OUT%"
if exist "%OUT%\debug\%TARGET%" set "RUNTIME=%OUT%\debug"
if exist "%OUT%\release\%TARGET%" set "RUNTIME=%OUT%\release"

echo [EMStudio] OUT=[%OUT%]
echo [EMStudio] TARGET=[%TARGET%]
echo [EMStudio] RUNTIME=[%RUNTIME%]
echo [EMStudio] SRC_SCRIPTS=[%SRC_SCRIPTS%]
echo [EMStudio] SRC_KEYWORDS=[%SRC_KEYWORDS%]

rem ---- scripts ----
if not exist "%SRC_SCRIPTS%\*" goto no_scripts

echo [EMStudio] Sync scripts -> "%RUNTIME%\scripts"
mkdir "%RUNTIME%\scripts" >nul 2>nul
robocopy "%SRC_SCRIPTS%" "%RUNTIME%\scripts" /MIR /NFL /NDL /NJH /NJS /NP >nul 2>nul
if errorlevel 8 goto scripts_fail
echo [EMStudio] Scripts OK (robocopy code %ERRORLEVEL%)

rem ---- keywords ----
if not exist "%SRC_KEYWORDS%\*" goto no_keywords

echo [EMStudio] Sync keywords -> "%RUNTIME%\keywords"
mkdir "%RUNTIME%\keywords" >nul 2>nul
robocopy "%SRC_KEYWORDS%" "%RUNTIME%\keywords" /MIR /NFL /NDL /NJH /NJS /NP >nul 2>nul
if errorlevel 8 goto keywords_fail
echo [EMStudio] Keywords OK (robocopy code %ERRORLEVEL%)

goto ok

:no_scripts
echo [EMStudio] ERROR: scripts dir not found: "%SRC_SCRIPTS%"
exit /b 8

:no_keywords
echo [EMStudio] WARN: keywords dir not found: "%SRC_KEYWORDS%"
goto ok

:scripts_fail
echo [EMStudio] ERROR: robocopy scripts failed (code %ERRORLEVEL%).
exit /b %ERRORLEVEL%

:keywords_fail
echo [EMStudio] ERROR: robocopy keywords failed (code %ERRORLEVEL%).
exit /b %ERRORLEVEL%

:ok
endlocal
exit /b 0
