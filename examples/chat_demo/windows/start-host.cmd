@echo off
setlocal EnableExtensions
set "EXE=%~dp0apptraverse_chat.exe"
set "PROFILE=%LOCALAPPDATA%\App Traverse\ChatExample\host"
"%EXE%" --host --state-dir "%PROFILE%"
if errorlevel 1 pause
