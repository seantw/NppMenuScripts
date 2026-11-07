@echo off
setlocal

rem Set DEBUG=1 when a debug build should write NppMenuScripts-debug.log by default.
rem menu.json "debug": true/false overrides this default.
if not defined DEBUG set DEBUG=0

if not "%DEBUG%"=="0" if not "%DEBUG%"=="1" (
  echo DEBUG must be 0 or 1.
  exit /b 1
)

if not exist build mkdir build
if not exist third_party\cJSON.c (
  echo Missing third_party\cJSON.c
  echo Run get-cjson.bat first.
  exit /b 1
)
if not exist third_party\cJSON.h (
  echo Missing third_party\cJSON.h
  echo Run get-cjson.bat first.
  exit /b 1
)
if not exist third_party\webview2\build\native\include\WebView2.h (
  call get-webview2.bat
  if errorlevel 1 exit /b %errorlevel%
)

rem Recent MinGW-w64 CRTs need CRT_fp10.o when linking a DLL that uses fenv.h.
for /f "delims=" %%I in ('g++ -print-file-name^=CRT_fp10.o') do set "CRT_FP10=%%I"
set "CRT_FP10_ARG="
if exist "%CRT_FP10%" set "CRT_FP10_ARG=%CRT_FP10%"

pushd src
windres resource.rc ..\build\resource.o
if errorlevel 1 (
  popd
  exit /b 1
)
popd

g++ -m64 -std=c++17 -Oz -ffunction-sections -fdata-sections -s -shared -static -static-libgcc -static-libstdc++ ^
  -DNPPMENUSCRIPTS_DEBUG_DEFAULT=%DEBUG% ^
  -Isrc -Ithird_party -Ithird_party\webview2\build\native\include ^
  src/main.cpp src/MenuConfigEditor.cpp src/HtmlPane.cpp src/HtmlSecurity.cpp third_party/cJSON.c build/resource.o %CRT_FP10_ARG% ^
  -o build/NppMenuScripts.dll ^
  -Wl,--gc-sections -luser32 -lgdi32 -lkernel32 -lcomdlg32 -lcomctl32 -lshell32 -lole32 -loleaut32 -lshlwapi -luuid -luxtheme -lbcrypt

if errorlevel 1 exit /b %errorlevel%

copy /Y "third_party\webview2\build\native\x64\WebView2Loader.dll" build\ >nul
if errorlevel 1 exit /b %errorlevel%

rem Stage the complete plugin payload with support files at the plugin root.
for %%F in (i18n.json menu-helper.js MenuHelper.pm menu_helper.py package.json) do (
  if not exist "support\%%F" (
    echo Missing support\%%F
    exit /b 1
  )
  copy /Y "support\%%F" build\ >nul
  if errorlevel 1 exit /b 1
)
xcopy /E /I /Y workspace-template build\workspace-template >nul
if errorlevel 2 exit /b %errorlevel%
echo Built build\NppMenuScripts.dll (DEBUG=%DEBUG%)
endlocal
