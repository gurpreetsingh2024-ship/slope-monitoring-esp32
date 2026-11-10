"""Train the Isolation Forest.
  python train_model.py                       synthetic 'noisy class demo' baseline (default)
  python train_model.py --csv data/windows.csv  retrain on a REAL baseline log written by server.py
  python train_model.py --demo                also write data/demo_replay.txt (scripted test run)
"""
import argparse, os
import numpy as np, joblib
from sklearn.ensemble import IsolationForest
from sklearn.pipeline import make_pipeline
from sklearn.preprocessing import StandardScaler
from config import CFG

HERE = os.path.dirname(os.path.abspath(__file__))


def lognorm(r, med, sig, n):
    return med * np.exp(r.normal(0, sig, n))


def make(r, kind, n):
    """Synthetic windows (deltas from baseline): d_out, d_lat [deg], vib [mg], dom_hz, dom_ratio."""
    if kind in ("rest", "held", "baseline"):
        held = np.ones(n, bool) if kind == "held" else (np.zeros(n, bool) if kind == "rest" else r.random(n) < 0.45)
        tilt = lambda: np.where(held, np.clip(r.normal(0, 2.5, n), -7, 7), r.normal(0, 0.4, n))
        return (tilt(), tilt(),
                np.where(held, lognorm(r, 60, 0.6, n), lognorm(r, 4, 0.4, n)),
                np.where(held, r.uniform(0.5, 12, n), r.uniform(0.5, 60, n)),
                np.where(held, r.uniform(0.05, 0.4, n), r.uniform(0.005, 0.06, n)))
    if kind == "tilt":       # deliberate push / lean
        return (r.normal(14, 2.5, n), r.normal(0, 2, n), lognorm(r, 60, 0.6, n),
                r.uniform(0.5, 12, n), r.uniform(0.05, 0.4, n))
    if kind == "raintilt":   # moderate lean (used with rising moisture)
        return (r.normal(10, 1.2, n), r.normal(0, 1.5, n), lognorm(r, 40, 0.5, n),
                r.uniform(0.5, 12, n), r.uniform(0.05, 0.4, n))
    if kind == "shake":      # deliberate firm shaking
        return (np.clip(r.normal(0, 2.5, n), -7, 7), np.clip(r.normal(0, 2.5, n), -7, 7),
                lognorm(r, 600, 0.3, n), r.uniform(2, 8, n), r.uniform(0.3, 0.7, n))
    if kind == "both":
        return (r.normal(12, 2, n), r.normal(0, 2, n), lognorm(r, 450, 0.3, n),
                r.uniform(2, 8, n), r.uniform(0.3, 0.7, n))
    raise ValueError(kind)


def X_of(d):
    o, l, v, _, _ = d          # model inputs: tilt magnitude and log vibration (frequency features
    return np.c_[np.hypot(o, l), np.log10(v)]   # were pure noise for the forest and diluted it)


def rates(m, thr, d):
    o, l, v, _, _ = d
    iforest = m.score_samples(X_of(d)) < thr
    sig = (np.hypot(o, l) >= CFG["TILT_FLOOR"]) | (v >= CFG["VIB_FLOOR"])
    naive = (np.hypot(o, l) >= CFG["NAIVE_TILT"]) | (v >= CFG["NAIVE_VIB"])
    return iforest.mean(), (iforest & sig).mean(), naive.mean()


def write_demo(path, r):
    base = (-1.0, -2.4)                    # absolute tilt of the rig at rest (from your tests)
    script = [("rest", 7, 0), ("held", 8, 0), ("rest", 3, 0), ("tilt", 5, 0), ("rest", 6, 0),
              ("shake", 5, 0), ("rest", 6, 0), ("raintilt", 8, 1), ("rest", 4, 0)]
    lines, w = [], 0
    for kind, n, rain in script:
        d = make(r, kind, n)
        moist = np.linspace(0, 40, n) if rain else np.zeros(n)
        for i in range(n):
            w += 1
            lines.append("FEAT,%d,%d,%.3f,%.3f,1.05000,0.00000,0.00000,%.3f,%.3f,%.4f,34.50,%.1f,%.1f,0,0,3.992,8"
                         % (w, w * 4000, base[0] + d[0][i], base[1] + d[1][i], d[2][i], d[3][i], d[4][i],
                            3115 - moist[i] / 100 * 1857, moist[i]))
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")
    print("wrote", path, "(%d windows)" % w)


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--csv")
    ap.add_argument("--demo", action="store_true")
    a = ap.parse_args()
    r = np.random.default_rng(0)

    if a.csv:
        t = np.genfromtxt(a.csv, delimiter=",", names=True, dtype=None, encoding="utf-8")
        ok = np.isfinite(t["d_out"].astype(float)) & np.isfinite(t["d_lat"].astype(float))
        n_alert = int((ok & (t["status"] == "ALERT")).sum())
        ok &= (t["status"] != "ALERT")                 # drop windows that were obvious events
        d = tuple(t[c][ok].astype(float) for c in ("d_out", "d_lat", "vib_rms_mg", "dom_hz", "dom_ratio"))
        print("training on %d real windows (%.1f h) from %s; left out %d ALERT windows"
              % (ok.sum(), ok.sum() * 4 / 3600, a.csv, n_alert))
        q = lambda x: tuple(np.percentile(x, [50, 95, 99]))
        print("baseline summary  tilt change 50/95/99%% = %.1f/%.1f/%.1f deg   vibration 50/95/99%% = %.0f/%.0f/%.0f mg"
              % (q(np.hypot(d[0], d[1])) + q(d[2])))
        if "temp_c" in t.dtype.names:
            tc = t["temp_c"][ok].astype(float)
            print("die temperature range seen: %.1f - %.1f C" % (tc.min(), tc.max()))
    else:
        d = make(r, "baseline", 4000)
        print("training on 4000 synthetic baseline windows (55%% still, 45%% hand-held/noisy)")

    # standardize the features, then Isolation Forest (fitted on baseline data only)
    m = make_pipeline(StandardScaler(), IsolationForest(n_estimators=200, contamination="auto", random_state=0)).fit(X_of(d))
    sc = m[0]
    print("standardization  mean = %s  std = %s" % (np.round(sc.mean_, 2), np.round(sc.scale_, 2)))
    thr = float(np.percentile(m.score_samples(X_of(d)), 2.0))   # about 2 % of baseline windows fall below
    joblib.dump({"model": m, "thr": thr}, os.path.join(HERE, "model.joblib"))
    print("saved model.joblib  (score threshold %.4f)\n" % thr)

    print("Per-window flag rate, fresh synthetic test windows:")
    print("%-22s %10s %12s %12s" % ("scenario", "IF alone", "IF + floors", "naive fixed"))
    for kind, label in [("baseline", "baseline (normal)"), ("held", "hand-held (normal)"),
                        ("tilt", "deliberate tilt"), ("shake", "deliberate shake"), ("both", "tilt + shake")]:
        x = rates(m, thr, make(np.random.default_rng(7), kind, 2000))
        print("%-22s %9.1f%% %11.1f%% %11.1f%%" % (label, 100 * x[0], 100 * x[1], 100 * x[2]))

    if a.demo:
        os.makedirs(os.path.join(HERE, "data"), exist_ok=True)
        write_demo(os.path.join(HERE, "data", "demo_replay.txt"), np.random.default_rng(11))
