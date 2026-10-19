@echo off
chcp 65001 >nul
setlocal
REM ===========================================================================
REM  EDVRonFOOT: сборка нашего EDVR.
REM
REM  На машине несколько Visual Studio, и build.bat сам выбирает "последнюю"
REM  через vswhere -- однажды это оказалась 2019 со старым Windows SDK, и сборка
REM  упала. Эта обёртка заранее включает компилятор Visual Studio 2022, а
REM  build.bat, увидев cl.exe в PATH, использует его.
REM
REM  Использование:  build_edvronfoot.bat [те же аргументы, что у build.bat]
REM ===========================================================================

set "VS2022=%ProgramFiles%\Microsoft Visual Studio\2022\Community"
if not exist "%VS2022%\VC\Auxiliary\Build\vcvars64.bat" (
    echo [edvronfoot] ERROR: не найдена Visual Studio 2022 Community в "%VS2022%"
    exit /b 1
)

REM Python ставился через winget. Ставим его в начало PATH всегда: иначе первой
REM может найтись заглушка Microsoft Store (WindowsApps\python.exe), которая не работает.
if exist "%LOCALAPPDATA%\Programs\Python\Python312\python.exe" set "PATH=%LOCALAPPDATA%\Programs\Python\Python312\;%LOCALAPPDATA%\Programs\Python\Python312\Scripts\;%PATH%"

REM vcvars64 ищет vswhere.exe в PATH.
set "PATH=%PATH%;%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"

call "%VS2022%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 ( echo [edvronfoot] ERROR: vcvars64 Visual Studio 2022 не запустился & exit /b 1 )

REM Часть Python-проверок EDVR считает относительный путь от папки проекта до
REM временной папки, а между дисками (проект на D:, %TEMP% на C:) это
REM невозможно. Поэтому на время сборки временная папка -- на том же диске,
REM что и проект, и без пробелов в пути.
set "EDVRONFOOT_TMP=%~d0\edvronfoot-tmp"
if not exist "%EDVRONFOOT_TMP%" mkdir "%EDVRONFOOT_TMP%"
set "TEMP=%EDVRONFOOT_TMP%"
set "TMP=%EDVRONFOOT_TMP%"

REM Тяжёлые GPU-стенды (native_motion_rigs) здесь идут 100-200 с при лимите
REM 180 с и иногда не укладываются. Штатный множитель лимитов EDVR для
REM медленной или загруженной машины; на сами проверки не влияет.
if not defined EDVR_RIG_TIMEOUT_SCALE set "EDVR_RIG_TIMEOUT_SCALE=2"

call "%~dp0build.bat" %*
exit /b %errorlevel%
