@echo off
setlocal
cd /d "%~dp0\.."

if "%~1"=="" (
    echo Usage: build_target.bat ^<cmake-target^>
    exit /b 1
)

cmake -S . -B build -G "MinGW Makefiles" ^
    -DCMAKE_PREFIX_PATH=C:/Qt/5.15.2/mingw81_64 ^
    -DQt5_DIR=C:/Qt/5.15.2/mingw81_64/lib/cmake/Qt5 ^
    -DCMAKE_CXX_COMPILER=C:/Qt/Tools/mingw810_64/bin/g++.exe
if errorlevel 1 exit /b 1

cmake --build build --parallel 4 --target %~1
exit /b %errorlevel%
