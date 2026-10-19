@echo off
REM EDVRonFOOT: вернуть официальный EDVR v0.19.1 из слепка. Подробности -- edvr\README.md.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0edvronfoot_game.ps1" -Action Restore %*
set ERR=%errorlevel%
pause
exit /b %ERR%
