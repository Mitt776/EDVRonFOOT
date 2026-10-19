@echo off
REM EDVRonFOOT: поставить нашу сборку EDVR в игру (официальная сохраняется). Подробности -- edvr\README.md.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0edvronfoot_game.ps1" -Action Install %*
set ERR=%errorlevel%
pause
exit /b %ERR%
