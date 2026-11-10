@echo off
cd /d "%~dp0"
echo Starting the backend (WiFi mode).
echo Copy the SERVER_IP shown below into the ESP32 firmware, then open the dashboard address.
python server.py
pause
