@echo off
rem Stand-in for the Palace (gds2palace) Python in tests: never runs gds2palace / gmsh.
rem Writes an empty config.json where gds2palace would (palace_model\<model>_data).
set "dir=%~dp1"
set "base=%~n1"
if not exist "%dir%palace_model\%base%_data" mkdir "%dir%palace_model\%base%_data"
echo {}> "%dir%palace_model\%base%_data\config.json"
exit /b 0
