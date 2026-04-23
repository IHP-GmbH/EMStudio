@echo off
setlocal enabledelayedexpansion

REM =========================
REM Config
REM =========================
set REPORT_NAME=coverage.html
set TEST_LOG=test_results.txt
set BIN_NAME=emstudio_golden_tests.exe
set FOUND_EXE=

REM =========================
REM Resolve paths
REM =========================
cd /d "%~dp0\.."

set ROOT_DIR=%CD%
set BUILD_DIR=%ROOT_DIR%\build
set TEST_BUILD_DIR=%ROOT_DIR%\tests\build
set TEST_OBJECT_DIR=
set ROOT_FWD=%ROOT_DIR:\=/%

echo ROOT_DIR      = %ROOT_DIR%
echo BUILD_DIR     = %BUILD_DIR%
echo TEST_BUILD_DIR= %TEST_BUILD_DIR%

REM =========================
REM Use explicit executable from argument if provided
REM =========================
if not "%~1"=="" (
    if exist "%~1" (
        set FOUND_EXE=%~1
        goto :found
    )
)

REM =========================
REM Find test executable
REM =========================
for /f "delims=" %%f in ('dir /s /b "%BUILD_DIR%\%BIN_NAME%" 2^>nul') do (
    set FOUND_EXE=%%f
    goto :found
)

for /f "delims=" %%f in ('dir /s /b "%TEST_BUILD_DIR%\%BIN_NAME%" 2^>nul') do (
    set FOUND_EXE=%%f
    goto :found
)

:found
if "%FOUND_EXE%"=="" (
    echo Error: %BIN_NAME% not found.
    echo Searched in:
    echo   %BUILD_DIR%
    echo   %TEST_BUILD_DIR%
    exit /b 1
)

for %%d in ("%FOUND_EXE%\..") do (
    set TEST_OBJECT_DIR=%%~fd
)

echo FOUND_EXE       = %FOUND_EXE%
echo TEST_OBJECT_DIR = %TEST_OBJECT_DIR%

REM =========================
REM Clean coverage artifacts
REM =========================
echo Cleaning old coverage data...
del /s /q "%ROOT_DIR%\*.gcda" > nul 2>&1
del /s /q "%ROOT_DIR%\*.gcov" > nul 2>&1

REM =========================
REM Run tests
REM =========================
echo Running: "%FOUND_EXE%"

if exist "%ROOT_DIR%\%TEST_LOG%" del /q "%ROOT_DIR%\%TEST_LOG%" > nul 2>&1

pushd "%ROOT_DIR%"
call "%FOUND_EXE%"
set TEST_EXIT=%ERRORLEVEL%
popd

if exist "%ROOT_DIR%\%TEST_LOG%" (
    echo Merged test log created: "%ROOT_DIR%\%TEST_LOG%"
) else (
    echo Warning: %TEST_LOG% was not created.
)

echo Test exit code: %TEST_EXIT%
echo Continuing with coverage generation...

REM =========================
REM Coverage (gcovr)
REM =========================
pushd "%ROOT_DIR%"

echo Using object directory: "%TEST_OBJECT_DIR%"
echo Using source root: "%ROOT_FWD%"

python -m gcovr -j 1 ^
  -r "%ROOT_FWD%" ^
  --object-directory "%TEST_OBJECT_DIR%" ^
  --gcov-ignore-errors=all ^
  --filter "%ROOT_FWD%/src/.*" ^
  --exclude "%ROOT_FWD%/tests/.*" ^
  --exclude "%ROOT_FWD%/build/.*" ^
  --exclude "%ROOT_FWD%/build-tests/.*" ^
  --exclude ".*moc_.*" ^
  --exclude ".*qrc_.*" ^
  --html-details -o "%REPORT_NAME%" ^
  --print-summary

set GCOVR_EXIT=%ERRORLEVEL%

if exist "%REPORT_NAME%" (
    start "" "%REPORT_NAME%"
    del /s /q "%ROOT_DIR%\*.gcov" > nul 2>&1
) else (
    echo Error: %REPORT_NAME% not generated.
)

popd

if not "%GCOVR_EXIT%"=="0" (
    exit /b %GCOVR_EXIT%
)

exit /b %TEST_EXIT%
