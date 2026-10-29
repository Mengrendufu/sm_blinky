@echo off
rem WSL owns both firmware builds; preserve the legacy Windows entry.
set "BASE=/home/sunnymatato/projects/sm_repos/embedded"
wsl.exe -d Ubuntu-24.04 -- bash ^
  %BASE%/hmi/sm_blinky/tools/flash.sh %*
exit /b %ERRORLEVEL%
