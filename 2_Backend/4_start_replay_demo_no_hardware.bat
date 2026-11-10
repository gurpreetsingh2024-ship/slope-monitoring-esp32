@echo off
cd /d "%~dp0"
echo Hardware-free demo: open http://localhost:8000 in your browser.
python server.py --replay data\demo_replay.txt --delay 1
pause
