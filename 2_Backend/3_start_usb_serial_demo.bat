@echo off
cd /d "%~dp0"
set /p PORT=Enter the ESP32 COM port (for example COM5): 
python server.py --port %PORT%
pause
