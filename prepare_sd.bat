@echo off
setlocal
if "%~1"=="" (
  echo Usage: prepare_sd.bat [DriveLetter:]
  echo Example: prepare_sd.bat E:
  exit /b 1
)
set "TARGET=%~1\"
if not exist "%TARGET%" (
  echo Drive %~1 not found.
  exit /b 1
)
xcopy /E /Y /I "%~dp0sdcard\*" "%TARGET%"
echo SD card content copied to %TARGET%
endlocal
