@echo off
setlocal
set "ROOT=%~dp0"
set "ROOT=%ROOT:~0,-1%"
set "VERSION=%~1"
if "%VERSION%"=="" (
  echo [edvr] usage: package.bat ^<version^> [--no-dlss]
  echo [edvr]   check out the tag edvronfoot-v^<version^> with a clean tree first
  exit /b 1
)
echo [edvr] building before packaging
call "%ROOT%\build.bat" --jobs 8 || exit /b 1
echo [edvr] staging the EDVRonFOOT release (VR installer only, no flat edition)
call python "%ROOT%\tools\package_native.py" "%VERSION%" %2 --release || exit /b 1
exit /b 0
