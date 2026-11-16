@echo off
rem Double-click: converts the photos in the "photos" folder and puts them on the calculator.
rem Options go after the name, for example: upload.bat --contrast high   (see convert.py -h)
cd /d "%~dp0"
set PYTHONUTF8=1
set "PY="
py -3 -c "pass" >nul 2>nul && set "PY=py -3"
if not defined PY python -c "pass" >nul 2>nul && set "PY=python"
if not defined PY (
    echo Python was not found.
    echo Install Python 3 from https://www.python.org/downloads/ and tick "Add python.exe to PATH" in the installer.
    echo Then run: pip install pillow
) else (
    %PY% -c "import PIL" >nul 2>nul
    if errorlevel 1 (
        echo Pillow, the image library, is missing. Run this once, then double-click again:
        echo     %PY% -m pip install pillow
    ) else (
        %PY% convert.py %*
    )
)
echo.
pause
