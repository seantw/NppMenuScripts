@echo off
setlocal

if not exist test-build mkdir test-build
g++ -std=c++17 -Wall -Wextra -Werror tests\core_logic_tests.cpp -o test-build\core_logic_tests.exe
if errorlevel 1 exit /b %errorlevel%

test-build\core_logic_tests.exe
if errorlevel 1 exit /b %errorlevel%

echo All NppMenuScripts unit tests passed.
endlocal
