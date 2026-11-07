@echo off
setlocal

set "WEBVIEW2_VERSION=1.0.4258.31"
set "PACKAGE=third_party\webview2-%WEBVIEW2_VERSION%.nupkg"
set "ARCHIVE=third_party\webview2-%WEBVIEW2_VERSION%.zip"
set "DEST=third_party\webview2"

if exist "%DEST%\build\native\include\WebView2.h" exit /b 0
if not exist third_party mkdir third_party
if exist "%PACKAGE%" del /q "%PACKAGE%"
if exist "%ARCHIVE%" del /q "%ARCHIVE%"

powershell -NoProfile -ExecutionPolicy Bypass -Command "Invoke-WebRequest -Uri 'https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/%WEBVIEW2_VERSION%' -OutFile '%PACKAGE%'"
if errorlevel 1 exit /b %errorlevel%
copy /Y "%PACKAGE%" "%ARCHIVE%" >nul
if errorlevel 1 exit /b %errorlevel%
powershell -NoProfile -ExecutionPolicy Bypass -Command "Expand-Archive -LiteralPath '%ARCHIVE%' -DestinationPath '%DEST%' -Force"
if errorlevel 1 exit /b %errorlevel%
del /q "%PACKAGE%" "%ARCHIVE%"

if not exist "%DEST%\build\native\include\WebView2.h" (
  echo WebView2 SDK extraction failed.
  exit /b 1
)
if not exist "%DEST%\build\native\x64\WebView2Loader.dll" (
  echo WebView2 x64 loader is missing from the SDK package.
  exit /b 1
)
echo WebView2 SDK %WEBVIEW2_VERSION% is ready.
endlocal
