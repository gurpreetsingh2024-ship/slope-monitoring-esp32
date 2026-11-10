# How to Demonstrate the Project

Total time: about 8–10 minutes. Setup: about 10 minutes before you start.

## 1. The 30-second pitch

> Retaining walls move slowly before they fail, and nobody can see it. This node measures the wall's tilt, vibration and soil moisture, learns what normal looks like, and raises a labelled alert only when something sustained and unexplained happens. It uses a ₹1,400 sensor node and works without a person watching.

Show `4_Documentation/System_Architecture.png` while you say it: sensor → ESP32 (features) → WiFi → backend (Isolation Forest + rules) → dashboard.

## 2. Before you go to class

**The night before**
- Run the hardware-free demo once (`4_start_replay_demo_no_hardware.bat`) so you know the dashboard works on your laptop.
- Put your hotspot name, password and the laptop address into `ESP32_WiFi_Monitor.ino` and upload it (see section 3 if the address changed).
- Charge the phone; turn off laptop sleep.

**30 minutes before**
1. Phone hotspot ON, **2.4 GHz band** (Android: Hotspot, AP band; iPhone: Maximize Compatibility). Join the laptop to it.
2. Double-click `2_start_wifi_demo.bat`. It prints `SERVER_IP`. If Windows asks about the firewall, allow **Private and Public**.
3. If `SERVER_IP` differs from the one in the firmware, edit it and re-upload (1 minute).
4. Plug the ESP32 into the laptop. In the Arduino Serial Monitor you should see `# WiFi OK` and a `FEAT` line every 4 s. Close the Serial Monitor afterwards if you plan to use USB serial mode.
5. Open `http://<SERVER_IP>:8000` in the browser.

**5 minutes before**
- Stand the rig upright on the table, sensor pins down and chip facing out.
- Press **Set baseline now** and keep your hands off for 12 seconds until the banner turns green.
- The "Last data" tile should count 0–4 s.

## 3. The demonstration (follow in order)

| # | You do | The dashboard shows | You say |
|---|---|---|---|
| 1 | Leave the rig still | CALIBRATING, then green NORMAL | "The system takes the first 12 seconds as this wall's reference. Tilt is always measured as change from it, so it also works on an uneven surface." |
| 2 | Pick it up and handle it gently, wobble it | Stays NORMAL; tiles move a little | "A plain fixed threshold would alarm here. The learned baseline and the minimum-significance limits don't, because this is ordinary handling." |
| 3 | Put it back, then lean the top **12–15° outward** and hold ~10 s | WATCH, then ALERT: TILT (red) | "One unusual window is only a WATCH. Two in a row, about 8 seconds, is an alert, so a single bump doesn't trigger it." |
| 4 | Return it to upright | ALERT clears about 12 s later | "It holds the alert for three normal windows so it doesn't flicker." |
| 5 | Shake it firmly for ~10 s | ALERT: VIBRATION | "Vibration has its own trigger and the banner says what caused it." |
| 6 | Wait for green. Pour a little water beside the moisture probe while tilting ~10° | Moisture rises; status stays WATCH, "rain-associated" | "During rain, movement is expected, so it stays at a warning. If the movement continues after the moisture stops rising, it escalates to an alert." |
| 7 | Point at the charts and the "Anomaly score" tile | | "Every window is also logged to a CSV, so the model can be retrained on a site's own baseline." |

Optional closing: put the rig back, press **Set baseline now**, show it returns to green.

## 4. If something goes wrong

| Problem | Fix |
|---|---|
| Dashboard says "No data from the ESP32" | ESP32 unpowered, hotspot off, or wrong `SERVER_IP`. Check the Serial Monitor for `POST failed (code -1)`. |
| Banner says "Board is not upright" | Stand it pins-down, chip outward, press **Set baseline now**. |
| It alerts while you only hold it | Lower sensitivity: raise `TILT_FLOOR` or `VIB_FLOOR` in `2_Backend/config.py`, restart the server. |
| It doesn't alert on your shake | Look at the Vibration tile while shaking, set `VIB_FLOOR` to about 70 % of that peak, restart the server. |
| WiFi won't connect | Hotspot on 5 GHz, wrong password, or a hotspot name with unusual characters. |
| Everything fails | Switch to `4_start_replay_demo_no_hardware.bat` and narrate the scripted run, which shows all the same behaviours. |
| Hotspot unavailable | Upload `ESP32_Serial_Backup.ino`, close the Serial Monitor, run `3_start_usb_serial_demo.bat`. |

## 5. Questions you may be asked

**Why Isolation Forest?** It needs only normal data. A wall rarely gives labelled failures, and it adapts to each structure instead of using a generic threshold.

**Why not just a fixed threshold?** It alarms on handling, traffic and wind. In the evaluation it produced about 149 false alerts per simulated day against about 0.4 for this system.

**How does it avoid false alarms?** Learned baseline, minimum tilt and vibration limits, and two-window confirmation (about 8 s).

**What does the rain sensor do?** It gives context. Movement while moisture rises is a warning marked rain-associated; movement without a rain explanation is an alert.

**What baseline did the prototype use?** A baseline profile of the rig at rest and under handling. The backend logs every window, and `train_model.py --csv` retrains on logged data, so a field node learns from its own site; a multi-week site baseline is planned for the field trial.

**Are 8° and 300 mg realistic for a real wall?** They are set wide so hand handling does not alarm. On a real wall the limits are derived from the site's own baseline and are much tighter. The measured sensor repeatability is about 0.1–0.2°.

**How would it run in the field?** Battery and solar, deep sleep with a reading every 5–10 minutes, and a long-range link. The features, model and rules are unchanged (report, Section 9).

**How accurate is the tilt?** At rest the accelerometer noise is 1–2 mg, about 0.06° per sample; run-to-run repeatability is about 0.1–0.2°.

**What are the limitations?** Single node, bench-scale model; field validation, a multi-week baseline, drift correction and the duty-cycled power design are the next stage.
