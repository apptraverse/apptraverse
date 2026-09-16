@echo off
setlocal EnableExtensions
"%~dp0apptraverse_chat.exe" --host --state-dir "%LOCALAPPDATA%\App Traverse\ChatExample\host" %*
if errorlevel 1 pause
