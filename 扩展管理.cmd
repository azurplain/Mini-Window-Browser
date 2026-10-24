@echo off
setlocal
set "LAB_EXE=%~dp0XiaoChuang-ExtensionsLab.exe"
if not exist "%LAB_EXE%" (
  echo Cannot find XiaoChuang-ExtensionsLab.exe in the same folder.
  pause
  exit /b 2
)
start "" "%LAB_EXE%" --extensions-manager
