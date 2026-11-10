"""Slope monitor backend.
  python server.py                           live over WiFi: the ESP32 POSTs each window to /ingest
  python server.py --port COM5               live over USB serial instead (close the Arduino Serial Monitor first!)
  python server.py --replay data/demo_replay.txt   no hardware: replay a scripted run (or any captured FEAT lines)
Then open http://localhost:8000
"""
import argparse, collections, math, os, socket, threading, time
import numpy as np, joblib
from fastapi import FastAPI, Request
from fastapi.responses import HTMLResponse
from config import CFG

HERE = os.path.dirname(os.path.abspath(__file__))
COLS = ["win", "t_ms", "tilt_out", "tilt_lat", "ax_g", "ay_g", "az_g", "vib_rms_mg", "dom_hz", "dom_ratio",
        "temp_c", "moist_raw", "moist_pct", "late", "bad", "dur_s", "proc_ms"]
LOG_COLS = ["win", "time", "tilt_out", "tilt_lat", "d_out", "d_lat", "vib_rms_mg", "dom_hz", "dom_ratio",
            "temp_c", "moist_raw", "moist_pct", "score", "flag", "naive", "status"]


class Monitor:
    def __init__(self):
        p = os.path.join(HERE, "model.joblib")
        self.m = None
        try:
            if os.path.exists(p):
                self.m = joblib.load(p)
        except Exception:
            self.m = None                      # unreadable or from another scikit-learn version
        if self.m is None:
            print("No usable model.joblib - training the default model now (about 10 s)...")
            try:
                import subprocess, sys
                subprocess.run([sys.executable, os.path.join(HERE, "train_model.py")], check=True, cwd=HERE,
                               stdout=subprocess.DEVNULL)
                self.m = joblib.load(p)
            except Exception as e:
                print("WARNING: could not train the model (%s). Using threshold rules only." % e)
        self.lock = threading.Lock()
        self.hist = collections.deque(maxlen=150)
        self.moist = collections.deque(maxlen=CFG["RAIN_WINDOWS"])
        self.set_log(os.path.join(HERE, "data", "windows.csv"))
        self.reset()

    def set_log(self, path):
        self.log_path = path
        os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
        if not os.path.exists(path):
            with open(path, "w") as f:
                f.write(",".join(LOG_COLS) + "\n")

    def reset(self):
        self.ref, self.refbuf, self.run, self.clear = None, [], 0, 0
        self.causes = set()
        self.status, self.note = "CALIBRATING", "Keep the rig still - setting baseline..."

    def process(self, r):
        with self.lock:
            self.moist.append(r["moist_pct"])
            score, flag, naive = None, False, False
            d_out = d_lat = float("nan")
            if self.ref is None:
                if max(abs(r["tilt_out"]), abs(r["tilt_lat"])) > CFG["UPRIGHT_MAX"]:
                    self.refbuf = []
                    self.note = ("Board is not upright (tilt %.0f / %.0f deg): stand it pins-down, chip facing out, "
                                 "keep it still, then press 'Set baseline now'" % (r["tilt_out"], r["tilt_lat"]))
                    self.status = "CALIBRATING"
                    self.hist.append(dict(win=int(r["win"]), time=time.time(), tilt_out=r["tilt_out"],
                                          tilt_lat=r["tilt_lat"], d_out=None, d_lat=None, vib_rms_mg=r["vib_rms_mg"],
                                          dom_hz=r["dom_hz"], dom_ratio=r["dom_ratio"], temp_c=r["temp_c"],
                                          moist_raw=r["moist_raw"], moist_pct=r["moist_pct"], score=None,
                                          flag=False, naive=False, status=self.status))
                    return None
                self.refbuf.append((r["tilt_out"], r["tilt_lat"]))
                if len(self.refbuf) >= CFG["REF_WINDOWS"]:
                    self.ref = tuple(np.mean(self.refbuf, axis=0))
                    self.status, self.note = "NORMAL", "Baseline set"
            else:
                d_out, d_lat = r["tilt_out"] - self.ref[0], r["tilt_lat"] - self.ref[1]
                mag, vib = math.hypot(d_out, d_lat), r["vib_rms_mg"]
                anom = True
                if self.m is not None:
                    x = [[mag, math.log10(max(vib, 0.1))]]
                    score = float(self.m["model"].score_samples(x)[0])
                    anom = score < self.m["thr"]
                tilt_hit, vib_hit = mag >= CFG["TILT_FLOOR"], vib >= CFG["VIB_FLOOR"]
                # vibration is a plain rule (predictable); tilt also needs the model to agree
                flag = bool(vib_hit or (anom and tilt_hit))
                if flag:
                    self.causes |= {c for c, h in (("TILT", tilt_hit), ("VIBRATION", vib_hit)) if h}
                cause = " + ".join(sorted(self.causes))
                naive = bool(mag >= CFG["NAIVE_TILT"] or vib >= CFG["NAIVE_VIB"])
                wet = (r["moist_pct"] - min(self.moist)) >= CFG["RAIN_RISE_PCT"]
                extreme = mag >= CFG["RAIN_EXTREME"] * CFG["TILT_FLOOR"] or vib >= CFG["RAIN_EXTREME"] * CFG["VIB_FLOOR"]
                if flag:
                    self.run += 1
                    self.clear = 0
                    if self.run < CFG["N_CONFIRM"]:
                        if self.status != "ALERT":
                            self.status, self.note = "WATCH", "Unusual %s - waiting for confirmation" % cause.lower()
                    elif wet and not extreme:
                        self.status, self.note = "WATCH", "Movement while moisture is rising (rain-associated)"
                    else:
                        self.status, self.note = "ALERT", "Sustained %s with no rain explanation" % cause.lower()
                else:
                    self.run = 0
                    if self.status == "ALERT":
                        self.clear += 1
                        if self.clear >= CFG["N_CLEAR"]:
                            self.status, self.note, self.causes = "NORMAL", "Back to normal", set()
                    else:
                        self.status, self.note, self.causes = "NORMAL", "", set()
            rec = dict(win=int(r["win"]), time=time.time(), tilt_out=r["tilt_out"], tilt_lat=r["tilt_lat"],
                       d_out=d_out, d_lat=d_lat, vib_rms_mg=r["vib_rms_mg"], dom_hz=r["dom_hz"],
                       dom_ratio=r["dom_ratio"], temp_c=r["temp_c"], moist_raw=r["moist_raw"],
                       moist_pct=r["moist_pct"], score=score, flag=flag, naive=naive, status=self.status)
            self.hist.append(rec)
            with open(self.log_path, "a") as f:
                f.write(",".join("" if rec[c] is None else str(rec[c]) for c in LOG_COLS) + "\n")
            return rec


def parse_feat(line):
    p = line.strip().split(",")
    if p[0] != "FEAT" or len(p) != len(COLS) + 1:
        return None
    try:
        return dict(zip(COLS, (float(v) for v in p[1:])))
    except ValueError:
        return None


mon = Monitor()
app = FastAPI()


@app.get("/", response_class=HTMLResponse)
def index():
    return open(os.path.join(HERE, "dashboard.html"), encoding="utf-8").read()


@app.get("/api/state")
def state():
    clean = lambda v: None if isinstance(v, float) and math.isnan(v) else v     # JSON has no NaN
    with mon.lock:
        hist = [{k: clean(v) for k, v in rec.items()} for rec in mon.hist]
        age = time.time() - mon.hist[-1]["time"] if mon.hist else None
        return {"status": mon.status, "note": mon.note, "ref": mon.ref, "cfg": CFG, "hist": hist, "age": age,
                "cause": " + ".join(sorted(mon.causes))}


@app.post("/ingest")
async def ingest(request: Request):
    """The ESP32 POSTs one FEAT line (plain text) per window."""
    r = parse_feat((await request.body()).decode(errors="ignore"))
    if r is None:
        return {"ok": False, "error": "bad FEAT line"}
    mon.process(r)
    return {"ok": True}


@app.post("/api/reset")
def api_reset():
    mon.reset()
    return {"ok": True}


def lan_ip():
    for target in ("8.8.8.8", "10.255.255.255"):
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.connect((target, 1))
            ip = s.getsockname()[0]
            s.close()
            return ip
        except OSError:
            pass
    return socket.gethostbyname(socket.gethostname())


def serial_loop(port):
    import serial
    while True:
        try:
            with serial.Serial(port, 115200, timeout=2) as s:
                print("serial open:", port)
                while True:
                    r = parse_feat(s.readline().decode(errors="ignore"))
                    if r:
                        mon.process(r)
        except Exception as e:
            print("serial error:", e, "- retrying")
            time.sleep(2)


def replay_loop(path, delay):
    for line in open(path, encoding="utf-8", errors="ignore"):
        r = parse_feat(line)
        if r:
            mon.process(r)
            time.sleep(delay)
    print("replay finished")


if __name__ == "__main__":
    import uvicorn
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", help="serial port, e.g. COM5 (omit to receive over WiFi)")
    ap.add_argument("--replay", help="text file of FEAT lines")
    ap.add_argument("--log", help="CSV file to log every window to (default data/windows.csv)")
    ap.add_argument("--delay", type=float, default=1.0, help="seconds per window in replay (4.0 = real time)")
    a = ap.parse_args()
    if a.log:
        mon.set_log(a.log)
    if a.replay:
        threading.Thread(target=replay_loop, args=(a.replay, a.delay), daemon=True).start()
    elif a.port:
        threading.Thread(target=serial_loop, args=(a.port,), daemon=True).start()
    ip = lan_ip()
    print("=" * 60)
    print("Put this in the ESP32 firmware:  SERVER_IP = \"%s\"" % ip)
    print("Dashboard (laptop or phone browser):  http://%s:8000" % ip)
    print("=" * 60)
    uvicorn.run(app, host="0.0.0.0", port=8000, log_level="warning")
