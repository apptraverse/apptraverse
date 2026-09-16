@echo off
setlocal EnableExtensions
"%~dp0apptraverse_chat.exe" --client --state-dir "%LOCALAPPDATA%\App Traverse\ChatExample\client" %*
if errorlevel 1 pause
