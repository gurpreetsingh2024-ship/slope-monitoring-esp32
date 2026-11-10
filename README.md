# Slope / Retaining-Wall Movement Monitoring: Final Project

IoT node (ESP32 + MPU-6500 + HW-390 soil moisture) with Isolation Forest anomaly detection and a live dashboard.

## Folder contents

| Folder | What is inside |
|---|---|
| `1_Firmware/ESP32_WiFi_Monitor` | **Final firmware.** Sampling, features, WiFi upload. Edit the 3 WiFi settings at the top. |
| `1_Firmware/ESP32_Serial_Backup` | Same features over USB serial only (fallback if WiFi fails). |
| `1_Firmware/Bringup_Tests` | I2C scanner, MPU-6500 bring-up and calibration, moisture calibration. |
| `1_Firmware/verify_features.py` | NumPy cross-check of the firmware's feature calculation. |
| `2_Backend` | `server.py` (service), `config.py` (all thresholds), `train_model.py`, `dashboard.html`, launcher `.bat` files, `data/demo_replay.txt`. |
| `3_Report` | `Final_Project_Report.docx`, the complete project report. |
| `4_Documentation` | **`DEMO_GUIDE.md` (how to demonstrate)**, wiring diagram, architecture and state-machine figures. |

## Quick start (Windows)

1. Install Python 3.10+ (tick "Add Python to PATH") and the Arduino IDE with the ESP32 board package.
2. In `2_Backend`, double-click `1_install_requirements.bat` (once).
3. **No hardware yet?** Double-click `4_start_replay_demo_no_hardware.bat` and open <http://localhost:8000>. It plays a scripted run: hand-held, tilt alert, vibration alert, rain-aware warning. The model trains itself on first start (about 10 s).
4. **With hardware:** wire the sensors (`4_Documentation/Wiring_Diagram.png`), start the phone hotspot (2.4 GHz), double-click `2_start_wifi_demo.bat`, put the printed `SERVER_IP` plus your hotspot name and password into `ESP32_WiFi_Monitor.ino`, upload it, and open the dashboard address.
5. Follow `4_Documentation/DEMO_GUIDE.md`.

## Firmware settings you may need to change

In `ESP32_WiFi_Monitor.ino`: `WIFI_SSID`, `WIFI_PASS`, `SERVER_IP`. The axis constants (`UP_AXIS 0`, `UP_SIGN 1`, `OUT_SIGN 1`) match a board mounted vertically with pins down and chip face outward.

## Tuning

All alert limits are in `2_Backend/config.py` (`TILT_FLOOR` 8°, `VIB_FLOOR` 300 mg, `N_CONFIRM` 2, and others). Edit and restart the server.

## Retraining on a real baseline

```
python server.py --log data/baseline.csv     (leave the rig undisturbed, with normal site activity)
python train_model.py --csv data/baseline.csv
python server.py                             (restart so the new model loads)
```
