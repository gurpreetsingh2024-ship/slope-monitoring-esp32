# Demo thresholds - edit here, then restart server.py (and re-run train_model.py if you change floors).
# Units: degrees, milli-g (RMS), percent. One window = 4 s.
CFG = dict(
    TILT_FLOOR=8.0,      # tilt change from baseline that CAN count as an alert (hand wobble is below this)
    VIB_FLOOR=300.0,     # vibration RMS that CAN count as an alert (hand-held is about 50-200 mg)
    NAIVE_TILT=5.0,      # plain fixed-threshold detector, logged only for comparison
    NAIVE_VIB=150.0,
    N_CONFIRM=2,         # consecutive flagged windows before ALERT (about 8 s)
    N_CLEAR=3,           # consecutive normal windows before an ALERT clears (about 12 s)
    UPRIGHT_MAX=45.0,    # a baseline is only accepted if the board is within this many degrees of upright
    REF_WINDOWS=3,       # windows averaged to set the baseline (about 12 s)
    RAIN_WINDOWS=6,      # moisture history used for "rain context"
    RAIN_RISE_PCT=5.0,   # moisture rise over that history that means "it is raining"
    RAIN_EXTREME=1.75,   # during rain, ALERT only if tilt/vibration exceeds this x the floors
)
