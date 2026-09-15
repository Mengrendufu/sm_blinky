@echo off
setlocal
rem Build the debug preset and flash it over SWD with J-Link Commander.
rem   tools\flash.cmd            build + flash
rem   tools\flash.cmd -nobuild   flash only
rem Run from any directory: paths are resolved from this file's location.

set "ROOT=%~dp0.."
set "JLINK=C:\mengrendufu\software_ins\JLink\JLink.exe"
set "CFG=%~dp0flash.jlink"

if not exist "%JLINK%" (
    echo [flash] J-Link Commander not found: %JLINK%
    goto :fail
)
if not exist "%CFG%" (
    echo [flash] command file not found: %CFG%
    goto :fail
)

pushd "%ROOT%" || goto :fail

if /i "%~1"=="-nobuild" goto :jlink

echo [flash] build...
cmake --preset debug || goto :fail
cmake --build --preset build-debug || goto :fail

:jlink
echo [flash] J-Link Commander over SWD...
"%JLINK%" -device STM32F103ZE -if SWD -speed 4000 -autoconnect 1 -ExitOnError 1 -NoGui 1 -CommandFile "%CFG%" || goto :fail

popd
echo [flash] done.
exit /b 0

:fail
set "RC=%ERRORLEVEL%"
popd
echo [flash] FAILED (ERRORLEVEL=%RC%)
exit /b %RC%
