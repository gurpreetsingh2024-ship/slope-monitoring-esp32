#!/usr/bin/env python3
"""
Phase 3 cross-check: recompute the window features in numpy from the raw
samples the ESP32 dumped, and compare them with the firmware's own FEAT line.

Usage:
    1. In the Serial Monitor send  r  (arms a raw dump of the NEXT window).
    2. Wait for the RAW,... lines and the FEAT line that follows them.
    3. Copy the Serial Monitor text into a file, e.g. capture.txt
    4. python verify_features.py capture.txt

If the capture also contains the boot line "# MAPPING UP_AXIS=.. UP_SIGN=..
OUT_SIGN=.." the script uses those values; otherwise it falls back to the
defaults below (set them to match the firmware).
"""
import re
import sys
import numpy as np

# Fallback settings (must match the firmware if the MAPPING line is missing)
UP_AXIS, UP_SIGN, OUT_SIGN = 1, 1, 1

FS_HZ = 128
N = 512
F_MIN_HZ = 0.5

FEAT_COLS = ["win", "t_ms", "tilt_out_deg", "tilt_lat_deg", "ax_g", "ay_g", "az_g",
             "vib_rms_mg", "dom_hz", "dom_ratio", "temp_c", "moist_raw",
             "moist_pct", "late", "bad", "dur_s", "proc_ms"]


def parse(path):
    mapping = (UP_AXIS, UP_SIGN, OUT_SIGN)
    raw_rows, result = [], None
    with open(path, encoding="utf-8", errors="ignore") as fh:
        for line in fh:
            line = line.strip()
            m = re.search(r"MAPPING UP_AXIS=(\d) UP_SIGN=(-?\d) OUT_SIGN=(-?\d)", line)
            if m:
                mapping = tuple(int(g) for g in m.groups())
            elif line.startswith("RAW,"):
                p = line.split(",")
                raw_rows.append((int(p[1]), float(p[2]), float(p[3]), float(p[4])))
            elif line.startswith("FEAT,"):
                vals = [float(v) for v in line.split(",")[1:]]
                if len(raw_rows) == N:
                    result = (np.array([r[1:] for r in raw_rows]), dict(zip(FEAT_COLS, vals)))
                raw_rows = []
    if result is None:
        sys.exit("No complete RAW block (512 lines) followed by a FEAT line found. "
                 "Send 'r', wait for the dump, and copy everything including the FEAT line.")
    return mapping, result[0], result[1]


def features(x, mapping):
    up_axis, up_sign, out_sign = mapping
    lat_axis = 1 - up_axis
    mean = x.mean(axis=0)
    var = x.var(axis=0)                       # population variance, like the firmware
    vib = 1000.0 * np.sqrt(var.sum())

    up, lat, out = up_sign * mean[up_axis], mean[lat_axis], out_sign * mean[2]
    tilt_out = np.degrees(np.arctan2(-out, up))
    tilt_lat = np.degrees(np.arctan2(lat, up))

    w = np.hanning(N)
    psd = np.zeros(N // 2)
    for a in range(3):
        spec = np.fft.rfft((x[:, a] - mean[a]) * w)
        psd += np.abs(spec[:N // 2]) ** 2
    k_min = int(np.ceil(F_MIN_HZ * N / FS_HZ))
    band = psd[k_min:N // 2]
    k_pk = k_min + int(np.argmax(band))
    return {
        "tilt_out_deg": tilt_out, "tilt_lat_deg": tilt_lat,
        "ax_g": mean[0], "ay_g": mean[1], "az_g": mean[2],
        "vib_rms_mg": vib,
        "dom_hz": k_pk * FS_HZ / N,
        "dom_ratio": psd[k_pk] / band.sum(),
    }


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    mapping, x, fw = parse(sys.argv[1])
    print(f"Mapping used: UP_AXIS={mapping[0]} UP_SIGN={mapping[1]} OUT_SIGN={mapping[2]}")
    ref = features(x, mapping)
    tol = {"tilt_out_deg": 0.01, "tilt_lat_deg": 0.01, "ax_g": 2e-5, "ay_g": 2e-5,
           "az_g": 2e-5, "vib_rms_mg": 0.01, "dom_hz": 1e-6, "dom_ratio": 5e-4}
    ok_all = True
    print(f"\n{'feature':14s} {'firmware':>12s} {'numpy':>12s} {'diff':>10s}  result")
    for key, t in tol.items():
        d = fw[key] - ref[key]
        ok = abs(d) <= t
        ok_all &= ok
        print(f"{key:14s} {fw[key]:12.5f} {ref[key]:12.5f} {d:10.5f}  {'PASS' if ok else 'FAIL'}")
    print("\nTiming: late=%d bad=%d dur_s=%.3f (ideal 3.992) proc_ms=%d"
          % (fw["late"], fw["bad"], fw["dur_s"], fw["proc_ms"]))
    print("\nOVERALL:", "PASS" if ok_all else "FAIL")


if __name__ == "__main__":
    main()
