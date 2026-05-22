@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 (
  echo ERROR: vcvars64.bat failed
  exit /b 1
)

cd /d "%~dp0.."
set NGKS_ALLOW_DIRECT_BUILDCORE=1
set QTFRAMEWORK_BYPASS_LICENSE_CHECK=1

set FABRIC=%~dp0..\..\NGKsDevFabEco\.venv\Scripts\ngksdevfabric.exe
if not exist "%FABRIC%" (
  echo ERROR: ngksdevfabric not found at %FABRIC%
  exit /b 1
)

"%FABRIC%" build . --profile release --target Vexara
if errorlevel 1 exit /b 1

if exist "assets" (
  if not exist "build_graph\release\bin\assets" mkdir "build_graph\release\bin\assets"
  xcopy /E /I /Y "assets\*" "build_graph\release\bin\assets\" >nul
)

echo.
echo BUILD OK: %CD%\build_graph\release\bin\Vexara.exe
exit /b 0
