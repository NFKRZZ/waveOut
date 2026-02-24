import sys
import numpy as np
import soundfile as sf
import librosa
import pyqtgraph as pg
from PyQt6 import QtCore, QtWidgets
from dataclasses import dataclass
from threading import Lock

try:
    import sounddevice as sd
    SOUNDDEVICE_OK = True
except Exception:
    SOUNDDEVICE_OK = False

# Debug plots (matplotlib)
import matplotlib.pyplot as plt
import aubio


# ============================================================
# CONFIG
# ============================================================

# --- Grid anchor behavior ---
ANCHOR_MODE = "audio_start"       # "audio_start" | "kick_attack"
SNAP_AFTER_AUDIO_START = False    # if True: anchor to audio_start then snap forward to first kick
EARLIEST_EDGE_MODE = False        # if True: kick-attack detector is more aggressive/early (more noise-sensitive)

# --- BPM ensemble + refinement ---
USE_BPM_ENSEMBLE = True

BPM_TARGET_SR = 44100             # resample to this for tempo analysis (None to disable)
BPM_CROP_OFFSET_S = 10.0          # skip intros
BPM_CROP_SECONDS = 150.0          # analyze this much audio
BPM_FOLD_LO = 80.0                # fold into [lo, hi] to kill half/double-time disagreements
BPM_FOLD_HI = 180.0
BPM_CLUSTER_TOL = 1.5             # cluster tolerance in BPM

# local refinement around ensemble result
BPM_REFINE_SEARCH = 2.0           # +/- BPM around initial estimate
BPM_REFINE_STEP = 0.01            # step size in BPM for refinement
BPM_REFINE_HOP = 256              # hop for onset strength in refinement (smaller = finer, a bit heavier)
BPM_REFINE_SMOOTH_AC = 5          # smooth autocorr a bit (odd int; 0 disables)

# final snapping (integer clamp)
SNAP_FINAL_BPM_TO_INT = False      # clamp to whole numbers (after drift-refine too)
BPM_INT_RANGE_LO = 60
BPM_INT_RANGE_HI = 200
# Optionally allow decimal snapping (e.g. 125.1 BPM)
SNAP_FINAL_BPM_TO_TENTH = True  # when True, snap to `BPM_DECIMAL_PLACES` places
BPM_DECIMAL_PLACES = 1          # number of decimal places for decimal snapping (1 => tenths)


# ============================================================
# Drift-based refinement (phase drift check mid-song)
# ============================================================

ENABLE_DRIFT_REFINEMENT = True

DRIFT_SEARCH = 1.0                # search +/- around bpm_refined
DRIFT_STEP = 0.01                 # step in BPM
DRIFT_WINDOWS = 7                 # number of windows across song
DRIFT_WINDOW_LEN_S = 18.0         # seconds per window
DRIFT_START_FRAC = 0.10           # start analyzing at 10% into song
DRIFT_END_FRAC = 0.90             # end analyzing at 90% into song
DRIFT_LAMBDA = 0.25               # penalty weight on drift slope

# Kick-ish detector used for drift scoring
KICK_LP_FC_HZ = 180.0
KICK_SMOOTH_MS = 2.5
KICK_THR_SIGMA = 2.0
KICK_MIN_SEP_S = 0.18

# Debug plot behavior
DEBUG_PLOTS_AUTORUN = False       # auto-generate plots at startup
DEBUG_PLOTS_SHOW = False          # plt.show(block=False) (Qt + mpl can be finicky)
DEBUG_PLOTS_SAVE = True
DEBUG_PLOTS_PREFIX = "bpm_debug"  # saved as bpm_debug_loss.png, etc.


# ============================================================
# "Color sound" waveform rendering (copied style)
# ============================================================

USE_OPENGL = True
ANTIALIAS = True
PLOT_Y_RANGE = 1.05

# Envelope resolution (precompute step)
ENV_BLOCK = 512 + 256             # same style as your color-wave viewer
DISPLAY_NORMALIZE = True
DISPLAY_HEADROOM = 0.98

# Visual 3-band split (Hz)
LOW_CUTOFF = 200.0
HIGH_CUTOFF = 2000.0


# ============================================================
# General helpers
# ============================================================

def load_mono(path: str):
    """
    Loads audio, converts to mono, and NORMALIZES if it's in "int-like" scale.
    """
    x, sr = sf.read(path, dtype="float32", always_2d=True)
    mono = x.mean(axis=1).astype(np.float32)

    mx = float(np.max(np.abs(mono)) + 1e-12)
    if mx > 2.0:
        mono = mono / mx

    return mono, int(sr)


def fold_bpm(bpm: float, lo=80.0, hi=180.0):
    bpm = float(bpm) if bpm is not None else 0.0
    if not np.isfinite(bpm) or bpm <= 0:
        return 0.0
    while bpm > hi:
        bpm *= 0.5
    while bpm < lo:
        bpm *= 2.0
    return float(bpm)


def snap_bpm_integer(bpm: float):
    if not np.isfinite(bpm) or bpm <= 0:
        return 0.0
    b = int(round(bpm))
    if b < BPM_INT_RANGE_LO or b > BPM_INT_RANGE_HI:
        return float(bpm)
    return float(b)


def snap_bpm_decimal(bpm: float, places: int = 1):
    """
    Snap BPM to a given number of decimal places (e.g. 1 -> tenths).
    Behavior mirrors integer snapping: if the rounded integer is outside
    BPM_INT_RANGE_LO..BPM_INT_RANGE_HI we return the original bpm.
    """
    if not np.isfinite(bpm) or bpm <= 0:
        return 0.0
    # check integer range based on rounded integer value
    b_int = int(round(bpm))
    if b_int < BPM_INT_RANGE_LO or b_int > BPM_INT_RANGE_HI:
        return float(bpm)
    fmt = round(float(bpm), int(max(0, int(places))))
    return float(fmt)


# ============================
# Simple 1-pole filters (same as color-wave viewer style)
# ============================

def onepole_lowpass(x: np.ndarray, sr: int, fc: float):
    x = x.astype(np.float32, copy=False)
    dt = 1.0 / sr
    rc = 1.0 / (2.0 * np.pi * fc)
    a = dt / (rc + dt)
    y = np.empty_like(x)
    z = 0.0
    for i in range(x.size):
        z = z + a * (x[i] - z)
        y[i] = z
    return y

def onepole_highpass(x: np.ndarray, sr: int, fc: float):
    return x.astype(np.float32, copy=False) - onepole_lowpass(x, sr, fc)

def split_3bands(mono: np.ndarray, sr: int):
    x = mono.astype(np.float32, copy=False)
    low = onepole_lowpass(x, sr, LOW_CUTOFF)
    hp_low = onepole_highpass(x, sr, LOW_CUTOFF)
    mid = onepole_lowpass(hp_low, sr, HIGH_CUTOFF)
    high = onepole_highpass(x, sr, HIGH_CUTOFF)
    return low, mid, high


# ============================
# Precompute envelopes (min/max per ENV_BLOCK)  (copied)
# ============================

def envelope_minmax(x: np.ndarray, block: int):
    n = x.size
    nb = int(np.ceil(n / block))
    pad = nb * block - n
    xx = np.pad(x, (0, pad), mode="constant") if pad > 0 else x
    xx = xx.reshape(nb, block)
    mn = xx.min(axis=1).astype(np.float32)
    mx = xx.max(axis=1).astype(np.float32)
    return mn, mx  # length nb


# ============================================================
# BPM ensemble methods (pip-only)
# ============================================================

def cluster_pick(bpms, tol=1.5):
    vals = sorted([float(b) for b in bpms if np.isfinite(b) and b > 0])
    if not vals:
        return 0.0, []
    clusters = []
    for v in vals:
        placed = False
        for c in clusters:
            if abs(v - float(np.median(c))) <= tol:
                c.append(v)
                placed = True
                break
        if not placed:
            clusters.append([v])
    clusters.sort(key=lambda c: (-len(c), np.std(c)))
    best = clusters[0]
    return float(np.median(best)), clusters


def bpm_librosa_beat_track(y, sr, start_bpm=128.0):
    tempo, _ = librosa.beat.beat_track(y=y, sr=sr, start_bpm=float(start_bpm), units="time")
    return float(tempo)


def bpm_librosa_tempogram(y, sr, bpm_min=60.0, bpm_max=200.0, hop_length=512):
    oenv = librosa.onset.onset_strength(y=y, sr=sr, hop_length=hop_length)
    tg = librosa.feature.tempogram(onset_envelope=oenv, sr=sr, hop_length=hop_length)
    tg_mean = tg.mean(axis=1)
    tempi = librosa.tempo_frequencies(tg.shape[0], sr=sr, hop_length=hop_length)

    mask = (tempi >= bpm_min) & (tempi <= bpm_max)
    if not np.any(mask):
        return 0.0
    tempi_sel = tempi[mask]
    tg_sel = tg_mean[mask]
    return float(tempi_sel[int(np.argmax(tg_sel))])


def bpm_autocorr_onset(y, sr, bpm_min=60.0, bpm_max=200.0, hop_length=512):
    oenv = librosa.onset.onset_strength(y=y, sr=sr, hop_length=hop_length)
    oenv = (oenv - oenv.mean()) / (oenv.std() + 1e-9)
    ac = np.correlate(oenv, oenv, mode="full")[len(oenv)-1:]
    if ac.size < 2:
        return 0.0
    ac[0] = 0.0

    fps = sr / hop_length
    lag_min = int(np.floor(60.0 * fps / bpm_max))
    lag_max = int(np.ceil (60.0 * fps / bpm_min))
    lag_min = max(lag_min, 1)
    lag_max = min(lag_max, ac.size - 1)
    if lag_max <= lag_min:
        return 0.0

    lag = lag_min + int(np.argmax(ac[lag_min:lag_max+1]))
    return float(60.0 * fps / lag)


def bpm_aubio_tempo(y, sr, win_s=1024, hop_s=256, method="default"):
    y = y.astype(np.float32, copy=False)
    t = aubio.tempo(method, win_s, hop_s, int(sr))
    bpms = []
    for i in range(0, len(y) - hop_s, hop_s):
        frame = y[i:i+hop_s]
        if t(frame):
            b = float(t.get_bpm())
            if np.isfinite(b) and b > 0:
                bpms.append(b)
    if not bpms:
        return 0.0
    return float(np.median(bpms))


def _smooth_1d(x: np.ndarray, win: int):
    if win is None or win <= 1:
        return x
    if win % 2 == 0:
        win += 1
    k = np.ones(win, dtype=np.float64) / win
    return np.convolve(x, k, mode="same")


def refine_bpm_local(y: np.ndarray, sr: int, bpm0: float,
                     search: float = 2.0, step: float = 0.01,
                     hop_length: int = 256,
                     fold_lo: float = 80.0, fold_hi: float = 180.0,
                     smooth_ac_win: int = 5):
    bpm0 = float(bpm0)
    if bpm0 <= 0 or not np.isfinite(bpm0):
        return 0.0

    oenv = librosa.onset.onset_strength(y=y, sr=sr, hop_length=hop_length)
    if oenv.size < 8:
        return bpm0

    oenv = (oenv - oenv.mean()) / (oenv.std() + 1e-9)
    ac = np.correlate(oenv, oenv, mode="full")[len(oenv)-1:]
    if ac.size < 2:
        return bpm0
    ac[0] = 0.0
    ac = _smooth_1d(ac.astype(np.float64), smooth_ac_win)

    fps = sr / hop_length
    candidates_centers = [bpm0, bpm0 * 0.5, bpm0 * 2.0]
    best_bpm = bpm0
    best_score = -np.inf

    for center in candidates_centers:
        center = fold_bpm(center, fold_lo, fold_hi)
        if center <= 0:
            continue
        bpms = np.arange(center - search, center + search + step, step, dtype=np.float64)
        bpms = bpms[(bpms > 0) & np.isfinite(bpms)]
        if bpms.size == 0:
            continue

        lags = np.rint(60.0 * fps / bpms).astype(np.int64)
        valid = (lags >= 1) & (lags < ac.size)
        if not np.any(valid):
            continue

        scores = np.full(bpms.shape, -np.inf, dtype=np.float64)
        scores[valid] = ac[lags[valid]]

        idx = int(np.argmax(scores))
        if scores[idx] > best_score:
            best_score = float(scores[idx])
            best_bpm = float(bpms[idx])

    return fold_bpm(best_bpm, fold_lo, fold_hi)


def bpm_ensemble_from_audio(mono: np.ndarray, sr: int):
    y = mono.astype(np.float32, copy=False)

    start = int(max(0.0, BPM_CROP_OFFSET_S) * sr)
    end = int((BPM_CROP_OFFSET_S + BPM_CROP_SECONDS) * sr)
    if end > start and end <= y.size:
        y = y[start:end]
    elif start < y.size:
        y = y[start:]

    used_sr = int(sr)
    if BPM_TARGET_SR is not None and int(BPM_TARGET_SR) != int(sr) and y.size > 0:
        y = librosa.resample(y, orig_sr=sr, target_sr=int(BPM_TARGET_SR))
        used_sr = int(BPM_TARGET_SR)

    raw = {}
    raw["librosa_beat_track"] = bpm_librosa_beat_track(y, used_sr, start_bpm=128.0)
    raw["librosa_tempogram"]  = bpm_librosa_tempogram(y, used_sr, bpm_min=60.0, bpm_max=200.0, hop_length=512)
    raw["autocorr_onset"]     = bpm_autocorr_onset(y, used_sr, bpm_min=60.0, bpm_max=200.0, hop_length=512)
    raw["aubio_tempo"]        = bpm_aubio_tempo(y, used_sr, win_s=1024, hop_s=256)

    folded = {k: fold_bpm(v, BPM_FOLD_LO, BPM_FOLD_HI) for k, v in raw.items()}
    candidates = list(folded.values())

    bpm_initial, clusters = cluster_pick(candidates, tol=BPM_CLUSTER_TOL)

    bpm_refined = refine_bpm_local(
        y=y, sr=used_sr, bpm0=bpm_initial,
        search=BPM_REFINE_SEARCH, step=BPM_REFINE_STEP,
        hop_length=BPM_REFINE_HOP,
        fold_lo=BPM_FOLD_LO, fold_hi=BPM_FOLD_HI,
        smooth_ac_win=BPM_REFINE_SMOOTH_AC
    )

    bpm_final = bpm_refined
    if SNAP_FINAL_BPM_TO_INT:
        bpm_final = snap_bpm_integer(bpm_refined)
    elif SNAP_FINAL_BPM_TO_TENTH:
        bpm_final = snap_bpm_decimal(bpm_refined, BPM_DECIMAL_PLACES)

    details = {
        "raw_bpms": raw,
        "folded_bpms": folded,
        "candidates": candidates,
        "clusters": clusters,
        "bpm_initial": float(bpm_initial),
        "bpm_refined": float(bpm_refined),
        "bpm_final": float(bpm_final),
        "used_sr": used_sr,
    }
    return float(bpm_final), float(bpm_refined), details


# ============================================================
# Silence / Onset / Kick start (for grid anchoring)
# ============================================================

def first_audio_time_by_rms(mono: np.ndarray, sr: int,
                            frame_s: float = 0.02,
                            hop_s: float = 0.01,
                            threshold_db: float = -45.0):
    frame = max(16, int(frame_s * sr))
    hop = max(8, int(hop_s * sr))
    if mono.size < frame:
        return 0.0
    eps = 1e-12
    thr = 10.0 ** (threshold_db / 20.0)
    for i in range(0, mono.size - frame, hop):
        x = mono[i:i+frame]
        rms = np.sqrt(np.mean(x * x) + eps)
        if rms >= thr:
            return i / sr
    return 0.0


def lowpass_onepole(x: np.ndarray, sr: int, fc_hz: float = 180.0):
    return onepole_lowpass(x, sr, fc_hz)


def aubio_first_onset_time(mono: np.ndarray, sr: int, start_s: float,
                           win_s: int = 1024, hop_s: int = 128,
                           method: str = "hfc",
                           threshold: float = 0.25,
                           silence_db: float = -60.0,
                           min_ioi_s: float = 0.08):
    o = aubio.onset(method, win_s, hop_s, sr)
    o.set_threshold(threshold)
    o.set_silence(silence_db)
    o.set_minioi_s(min_ioi_s)

    start_i = int(max(0.0, start_s) * sr)
    n = len(mono)
    pos = start_i
    while pos < n:
        frame = mono[pos:pos + hop_s]
        if len(frame) < hop_s:
            frame = np.pad(frame, (0, hop_s - len(frame)))
        if o(frame):
            t = float(o.get_last_s())
            if t >= start_s:
                return t
        pos += hop_s
    return None


def find_kick_attack_start(mono: np.ndarray, sr: int, approx_onset_s: float,
                           lookback_ms: float = 200.0,
                           baseline_ms: float = 80.0,
                           smooth_ms: float = 2.5,
                           rise_sigma: float = 6.0,
                           sustain_ms: float = 8.0,
                           lp_fc_hz: float = 180.0,
                           earliest_edge: bool = False):
    onset_i = int(approx_onset_s * sr)
    L = int((lookback_ms / 1000.0) * sr)
    i0 = max(0, onset_i - L)
    seg = mono[i0:onset_i].astype(np.float32, copy=False)
    if seg.size < 32:
        return approx_onset_s

    seg_lp = lowpass_onepole(seg, sr, fc_hz=lp_fc_hz)
    env = np.abs(seg_lp)

    win = max(1, int((smooth_ms / 1000.0) * sr))
    if win > 1:
        kernel = np.ones(win, dtype=np.float32) / win
        env = np.convolve(env, kernel, mode="same")

    bN = max(16, int((baseline_ms / 1000.0) * sr))
    bN = min(bN, env.size)
    base = env[:bN]
    mu = float(base.mean())
    sig = float(base.std()) + 1e-12

    if earliest_edge:
        thr = mu + 2.0 * sig
        susN = max(1, int((1.0 / 1000.0) * sr))
    else:
        thr = mu + rise_sigma * sig
        susN = max(1, int((sustain_ms / 1000.0) * sr))

    susN = min(susN, max(1, env.size - 1))

    for idx in range(0, env.size - susN):
        if env[idx] > thr and np.all(env[idx:idx + susN] > thr):
            return (i0 + idx) / sr

    crossings = np.where(env > thr)[0]
    if crossings.size:
        return (i0 + int(crossings[0])) / sr

    return approx_onset_s


def find_first_kick_after_time(mono: np.ndarray, sr: int, start_s: float,
                              search_ms: float = 800.0,
                              lp_fc_hz: float = 180.0,
                              smooth_ms: float = 2.5,
                              rise_sigma: float = 6.0,
                              sustain_ms: float = 8.0,
                              earliest_edge: bool = False):
    start_i = int(start_s * sr)
    end_i = min(len(mono), start_i + int((search_ms / 1000.0) * sr))
    if end_i - start_i < 64:
        return None

    seg = mono[start_i:end_i].astype(np.float32, copy=False)
    seg_lp = lowpass_onepole(seg, sr, fc_hz=lp_fc_hz)
    env = np.abs(seg_lp)

    win = max(1, int((smooth_ms / 1000.0) * sr))
    if win > 1:
        kernel = np.ones(win, dtype=np.float32) / win
        env = np.convolve(env, kernel, mode="same")

    bN = min(env.size, max(16, int(0.08 * sr)))
    base = env[:bN]
    mu = float(base.mean())
    sig = float(base.std()) + 1e-12

    if earliest_edge:
        thr = mu + 2.0 * sig
        susN = max(1, int((1.0 / 1000.0) * sr))
    else:
        thr = mu + rise_sigma * sig
        susN = max(1, int((sustain_ms / 1000.0) * sr))

    susN = min(susN, max(1, env.size - 1))

    for idx in range(0, env.size - susN):
        if env[idx] > thr and np.all(env[idx:idx + susN] > thr):
            return (start_i + idx) / sr
    return None


# ============================================================
# Drift refinement: kick attacks across windows + drift loss
# ============================================================

def detect_kick_attacks_times(mono, sr, t_start, t_end,
                              lp_fc_hz=180.0,
                              smooth_ms=2.5,
                              thr_sigma=5.0,
                              min_sep_s=0.18):
    i0 = int(max(0.0, t_start) * sr)
    i1 = int(min(len(mono) / sr, t_end) * sr)
    if i1 - i0 < int(0.2 * sr):
        return []

    seg = mono[i0:i1].astype(np.float32, copy=False)
    seg_lp = lowpass_onepole(seg, sr, fc_hz=lp_fc_hz)
    env = np.abs(seg_lp)

    win = max(1, int((smooth_ms / 1000.0) * sr))
    if win > 1:
        kernel = np.ones(win, dtype=np.float32) / win
        env = np.convolve(env, kernel, mode="same")

    bN = min(env.size, max(32, int(0.2 * sr)))
    base = env[:bN]
    mu = float(base.mean())
    sig = float(base.std()) + 1e-12
    thr = mu + thr_sigma * sig

    above = env > thr
    idx = np.where(np.logical_and(above[1:], ~above[:-1]))[0] + 1
    if idx.size == 0:
        return []

    min_sep = int(min_sep_s * sr)
    picks = []
    last = -10**9
    for j in idx:
        if j - last >= min_sep:
            picks.append(j)
            last = j

    out = []
    thr2 = mu + 2.0 * sig
    lookback = int(0.08 * sr)
    for j in picks:
        lb = max(0, j - lookback)
        local = env[lb:j+1]
        k = np.where(local > thr2)[0]
        jj = (lb + int(k[0])) if k.size else j
        out.append((i0 + jj) / sr)

    return out


def residuals_for_bpm(kick_times, t0, bpm):
    if bpm <= 0 or not kick_times:
        return np.array([], dtype=np.float64)
    T = 60.0 / bpm
    kt = np.asarray(kick_times, dtype=np.float64)
    k = np.rint((kt - t0) / T)
    tg = t0 + k * T
    return kt - tg


def drift_loss(kick_times_by_window, window_centers, t0, bpm, lam=0.25):
    meds = []
    abs_all = []
    used_centers = []

    for c, kicks in zip(window_centers, kick_times_by_window):
        r = residuals_for_bpm(kicks, t0, bpm)
        if r.size == 0:
            continue
        meds.append(float(np.median(r)))
        abs_all.append(np.abs(r))
        used_centers.append(float(c))

    if not abs_all:
        return np.inf, {"median_abs": np.inf, "slope": np.inf, "n": 0}

    abs_all = np.concatenate(abs_all)
    med_abs = float(np.median(abs_all))

    if len(meds) >= 2:
        t = np.asarray(used_centers, dtype=np.float64)
        y = np.asarray(meds, dtype=np.float64)
        a, b = np.polyfit(t, y, 1)
        slope = float(a)
    else:
        slope = 0.0

    loss = med_abs + lam * abs(slope)
    return loss, {"median_abs": med_abs, "slope": slope, "n": int(abs_all.size)}


def refine_bpm_by_drift(mono, sr, t0, bpm0,
                        search=1.0, step=0.01,
                        windows=7, window_len_s=8.0,
                        start_frac=0.10, end_frac=0.90,
                        lam=0.25):
    duration = len(mono) / sr
    if duration < 30 or bpm0 <= 0:
        return bpm0, {}

    centers = np.linspace(duration * start_frac, duration * end_frac, windows)

    kick_times_by_window = []
    for c in centers:
        t_start = max(0.0, c - 0.5 * window_len_s)
        t_end   = min(duration, c + 0.5 * window_len_s)
        kicks = detect_kick_attacks_times(
            mono, sr, t_start, t_end,
            lp_fc_hz=KICK_LP_FC_HZ,
            smooth_ms=KICK_SMOOTH_MS,
            thr_sigma=KICK_THR_SIGMA,
            min_sep_s=KICK_MIN_SEP_S
        )
        kick_times_by_window.append(kicks)

    candidates = np.arange(bpm0 - search, bpm0 + search + step, step)
    losses = []

    best_bpm = float(bpm0)
    best_loss = np.inf
    best_info = None

    for bpm in candidates:
        loss, info = drift_loss(kick_times_by_window, centers, t0, float(bpm), lam=lam)
        losses.append(float(loss))
        if loss < best_loss:
            best_loss = float(loss)
            best_bpm = float(bpm)
            best_info = info

    out = {
        "bpm0": float(bpm0),
        "best_bpm": float(best_bpm),
        "best_loss": float(best_loss),
        "best_info": best_info,
        "candidates": candidates,
        "losses": np.asarray(losses, dtype=np.float64),
        "centers": centers,
        "kicks_per_window": [len(k) for k in kick_times_by_window],
        "kick_times_by_window": kick_times_by_window,
        "search": float(search),
        "step": float(step),
        "window_len_s": float(window_len_s),
        "lam": float(lam),
    }
    return best_bpm, out


# ============================================================
# Debug plots
# ============================================================

def plot_loss_curve(drift_dbg, bpm_markers=None, title_extra=""):
    candidates = drift_dbg["candidates"]
    losses = drift_dbg["losses"]

    plt.figure(figsize=(10, 4))
    plt.plot(candidates, losses, linewidth=1.5)
    plt.xlabel("BPM candidate")
    plt.ylabel("Drift loss = median(|resid|) + λ|slope|")
    plt.title(f"Drift loss vs BPM {title_extra}".strip())
    plt.grid(True, alpha=0.25)

    if bpm_markers:
        ymin = float(np.min(losses)) if losses.size else 0.0
        for b, label in bpm_markers:
            plt.axvline(b, linestyle="--", linewidth=1.2)
            plt.text(b, ymin, f" {label}", rotation=90, va="bottom")


def generate_debug_plots(drift_dbg, t0, bpm_refined, bpm_final, prefix=DEBUG_PLOTS_PREFIX,
                         show=DEBUG_PLOTS_SHOW, save=DEBUG_PLOTS_SAVE):
    if not drift_dbg or "candidates" not in drift_dbg:
        print("[debug] No drift_dbg available; cannot plot.")
        return

    best = float(drift_dbg["best_bpm"])
    title_extra = f"(t0={t0:.3f}s, refined={bpm_refined:.3f}, best={best:.3f}, final={bpm_final:.3f})"

    plot_loss_curve(
        drift_dbg,
        bpm_markers=[(bpm_refined, "bpm_refined"), (best, "bpm_drift_best"), (bpm_final, "bpm_final")],
        title_extra=title_extra
    )

    if save:
        plt.tight_layout()
        plt.savefig(f"{prefix}_loss.png", dpi=160)
        print(f"[debug] Saved: {prefix}_loss.png")

    if show:
        plt.show(block=False)


# ============================================================
# Playback engine with looping
# ============================================================

@dataclass
class TransportState:
    sr: int
    n_samples: int
    play_pos: int = 0
    playing: bool = True
    loop_on: bool = False
    loop_start: int = 0
    loop_end: int = 0  # exclusive


class LoopingPlayer:
    def __init__(self, mono: np.ndarray, sr: int):
        self.audio = mono.astype(np.float32, copy=False)
        self.state = TransportState(sr=sr, n_samples=len(self.audio))
        self.lock = Lock()
        self.stream = None
        self.enabled = False

    def start(self, blocksize: int = 1024):
        if not SOUNDDEVICE_OK:
            print("[playback] sounddevice unavailable")
            self.enabled = False
            return

        def callback(outdata, frames, time_info, status):
            with self.lock:
                st = self.state
                if not st.playing:
                    outdata[:] = 0
                    return

                out = np.zeros(frames, dtype=np.float32)
                pos = st.play_pos
                n = st.n_samples

                if st.loop_on and st.loop_end > st.loop_start:
                    ls, le = st.loop_start, st.loop_end
                    if pos < ls or pos >= le:
                        pos = ls

                    remaining = frames
                    w = 0
                    while remaining > 0:
                        chunk = min(remaining, le - pos)
                        out[w:w + chunk] = self.audio[pos:pos + chunk]
                        w += chunk
                        remaining -= chunk
                        pos += chunk
                        if pos >= le:
                            pos = ls
                else:
                    end = min(pos + frames, n)
                    out[:end - pos] = self.audio[pos:end]
                    pos = end
                    if pos >= n:
                        st.playing = False

                st.play_pos = int(pos)
                outdata[:, 0] = out

        try:
            self.stream = sd.OutputStream(
                samplerate=self.state.sr,
                channels=1,
                dtype="float32",
                blocksize=blocksize,
                callback=callback,
            )
            self.stream.start()
            self.enabled = True
            print(f"[playback] started @ {self.state.sr} Hz")
        except Exception as e:
            print("[playback] FAILED:", repr(e))
            self.stream = None
            self.enabled = False

    def stop(self):
        if self.stream is not None:
            try:
                self.stream.stop()
                self.stream.close()
            except Exception:
                pass
            self.stream = None

    def toggle_play(self):
        with self.lock:
            self.state.playing = not self.state.playing

    def get_pos(self) -> int:
        with self.lock:
            return self.state.play_pos

    def set_loop(self, on: bool, start: int = None, end: int = None):
        with self.lock:
            if start is not None and end is not None:
                self.state.loop_start = int(start)
                self.state.loop_end = int(end)
            self.state.loop_on = bool(on)

    def shift_loop(self, delta_samples: int):
        with self.lock:
            st = self.state
            if st.loop_end <= st.loop_start:
                return
            length = st.loop_end - st.loop_start
            new_start = st.loop_start + delta_samples
            new_start = max(0, min(new_start, st.n_samples - length))
            st.loop_start = new_start
            st.loop_end = new_start + length


# ============================================================
# Viewer (BeatGrid + "color sound" waveform)
# ============================================================

class BeatGridViewer(QtWidgets.QMainWindow):
    def __init__(self, audio_path: str, window_s: float = 8.0, fps: int = 120, beats_per_bar: int = 4):
        super().__init__()
        self.setWindowTitle("Beat Grid Viewer — Color Waveform Embedded + BPM refine + drift refine")

        pg.setConfigOptions(useOpenGL=USE_OPENGL, antialias=ANTIALIAS)

        self.mono, self.sr = load_mono(audio_path)
        self.duration_s = len(self.mono) / self.sr
        self.beats_per_bar = beats_per_bar

        # ----------------------------
        # Display-normalize copy (same style as color-wave viewer)
        # ----------------------------
        disp = self.mono.copy()
        if DISPLAY_NORMALIZE:
            mx = float(np.max(np.abs(disp)) + 1e-12)
            if mx > 0:
                disp = disp / mx
        disp = np.clip(disp, -DISPLAY_HEADROOM, DISPLAY_HEADROOM).astype(np.float32)

        low, mid, high = split_3bands(disp, self.sr)
        low  = np.clip(low,  -DISPLAY_HEADROOM, DISPLAY_HEADROOM).astype(np.float32)
        mid  = np.clip(mid,  -DISPLAY_HEADROOM, DISPLAY_HEADROOM).astype(np.float32)
        high = np.clip(high, -DISPLAY_HEADROOM, DISPLAY_HEADROOM).astype(np.float32)

        # Precompute min/max envelopes for stable rendering
        self.env_block = int(ENV_BLOCK)
        self.n_samples = int(len(disp))
        self.nb = int(np.ceil(self.n_samples / self.env_block))

        self.base_min, self.base_max = envelope_minmax(disp, self.env_block)
        self.low_min,  self.low_max  = envelope_minmax(low,  self.env_block)
        self.mid_min,  self.mid_max  = envelope_minmax(mid,  self.env_block)
        self.high_min, self.high_max = envelope_minmax(high, self.env_block)

        # ----------------------------
        # BPM estimate (ensemble + local refine)
        # ----------------------------
        bpm_details = None
        if USE_BPM_ENSEMBLE:
            bpm_int, bpm_refined, bpm_details = bpm_ensemble_from_audio(self.mono, self.sr)
            self.bpm_refined = float(bpm_refined)
            self.bpm = float(bpm_int) if bpm_int > 0 else float(bpm_refined)
        else:
            b = bpm_aubio_tempo(self.mono, self.sr)
            b = fold_bpm(b, BPM_FOLD_LO, BPM_FOLD_HI)
            b = refine_bpm_local(self.mono, self.sr, b, search=BPM_REFINE_SEARCH, step=BPM_REFINE_STEP,
                                 hop_length=BPM_REFINE_HOP, fold_lo=BPM_FOLD_LO, fold_hi=BPM_FOLD_HI,
                                 smooth_ac_win=BPM_REFINE_SMOOTH_AC)
            self.bpm_refined = float(b)
            if SNAP_FINAL_BPM_TO_INT:
                self.bpm = snap_bpm_integer(b)
            elif SNAP_FINAL_BPM_TO_TENTH:
                self.bpm = snap_bpm_decimal(b, BPM_DECIMAL_PLACES)
            else:
                self.bpm = float(b)

        # ----------------------------
        # Anchor candidates
        # ----------------------------
        audio_start = first_audio_time_by_rms(self.mono, self.sr, threshold_db=-45.0)

        approx_onset = aubio_first_onset_time(
            self.mono, self.sr, start_s=audio_start,
            win_s=1024, hop_s=128, method="hfc",
            threshold=0.25, silence_db=-60.0, min_ioi_s=0.08
        )
        if approx_onset is None:
            approx_onset = audio_start

        kick_attack = find_kick_attack_start(
            self.mono, self.sr, approx_onset_s=float(approx_onset),
            earliest_edge=EARLIEST_EDGE_MODE
        )

        if ANCHOR_MODE == "audio_start":
            t0 = float(audio_start)
            if SNAP_AFTER_AUDIO_START:
                t_kick = find_first_kick_after_time(
                    self.mono, self.sr, start_s=t0,
                    earliest_edge=EARLIEST_EDGE_MODE
                )
                if t_kick is not None:
                    t0 = float(t_kick)
            self.t0 = t0
        elif ANCHOR_MODE == "kick_attack":
            self.t0 = float(kick_attack)
        else:
            self.t0 = float(audio_start)

        # ----------------------------
        # Drift-based BPM refinement using t0
        # ----------------------------
        self.drift_dbg = {}
        if ENABLE_DRIFT_REFINEMENT and self.bpm_refined > 0:
            bpm_drift_best, drift_dbg = refine_bpm_by_drift(
                mono=self.mono, sr=self.sr, t0=self.t0,
                bpm0=self.bpm_refined,
                search=DRIFT_SEARCH, step=DRIFT_STEP,
                windows=DRIFT_WINDOWS, window_len_s=DRIFT_WINDOW_LEN_S,
                start_frac=DRIFT_START_FRAC, end_frac=DRIFT_END_FRAC,
                lam=DRIFT_LAMBDA
            )
            self.drift_dbg = drift_dbg
            self.bpm_drift_best = float(bpm_drift_best)

            bpm_final = float(self.bpm_drift_best)
            if SNAP_FINAL_BPM_TO_INT:
                bpm_final = snap_bpm_integer(bpm_final)
            elif SNAP_FINAL_BPM_TO_TENTH:
                bpm_final = snap_bpm_decimal(bpm_final, BPM_DECIMAL_PLACES)

            self.bpm_before_drift = float(self.bpm_refined)
            self.bpm = float(bpm_final)
        else:
            self.bpm_drift_best = float(self.bpm_refined)
            self.bpm_before_drift = float(self.bpm_refined)

        self.T = float(60.0 / self.bpm) if self.bpm > 0 else 0.0

        # ----------------------------
        # Prints
        # ----------------------------
        print(f"Loaded: {audio_path}")
        print(f"duration={self.duration_s:.2f}s  sr={self.sr}")
        print(f"BPM refined(float)={self.bpm_refined:.4f}  BPM drift(best)={self.bpm_drift_best:.4f}  BPM final={self.bpm:.2f}  T={self.T:.6f}s")
        if bpm_details is not None:
            print("BPM raw:", bpm_details["raw_bpms"])
            print("BPM folded:", bpm_details["folded_bpms"])
            print("BPM clusters:", bpm_details["clusters"])
            print(f"bpm_initial={bpm_details['bpm_initial']:.4f}  bpm_refined={bpm_details['bpm_refined']:.4f}  bpm_final={bpm_details['bpm_final']:.2f}")
        if self.drift_dbg:
            print("Drift best_info:", self.drift_dbg.get("best_info", {}))
            print("kicks per drift window:", self.drift_dbg.get("kicks_per_window", []))

        print(f"audio_start≈{audio_start:.3f}s  approx_onset≈{float(approx_onset):.3f}s  kick_attack≈{float(kick_attack):.6f}s")
        print(f"ANCHOR_MODE={ANCHOR_MODE}  SNAP_AFTER_AUDIO_START={SNAP_AFTER_AUDIO_START}  EARLIEST_EDGE_MODE={EARLIEST_EDGE_MODE}")
        print(f"t0 (grid origin) = {self.t0:.6f}s")
        print("Hotkeys: Space=play/pause, B=set 4-beat loop, L=toggle loop, ←/→ move loop bar, P=save debug BPM plots")

        if DEBUG_PLOTS_AUTORUN and self.drift_dbg:
            generate_debug_plots(self.drift_dbg, self.t0, self.bpm_refined, self.bpm)

        # Playback
        self.player = LoopingPlayer(self.mono, self.sr)
        self.player.start(blocksize=1024)

        # UI
        cw = QtWidgets.QWidget()
        self.setCentralWidget(cw)
        layout = QtWidgets.QVBoxLayout(cw)

        self.plot = pg.PlotWidget()
        self.plot.setYRange(-PLOT_Y_RANGE, PLOT_Y_RANGE)
        self.plot.showGrid(x=True, y=True, alpha=0.2)
        self.plot.setBackground((18, 18, 18))
        self.plot.setLabel("left", "Amplitude")
        self.plot.setLabel("bottom", "Time (s) in window")
        layout.addWidget(self.plot)

        self.info = QtWidgets.QLabel("")
        self.info.setStyleSheet("color: black;")
        layout.addWidget(self.info)

        self.window_s = float(window_s)
        self.playhead_x = 0.2 * self.window_s

        # ===== "Color sound" layers (pos/neg filled) =====
        def mk_pair(r, g, b, a):
            pos = pg.PlotCurveItem(pen=None, brush=pg.mkBrush(r, g, b, a), fillLevel=0.0)
            neg = pg.PlotCurveItem(pen=None, brush=pg.mkBrush(r, g, b, a), fillLevel=0.0)
            return pos, neg

        # base + 3 bands (copied palette vibe)
        self.base_pos, self.base_neg = mk_pair(200, 200, 200, 20)
        self.low_pos,  self.low_neg  = mk_pair(0, 140, 255, 170)    # blue behind
        self.mid_pos,  self.mid_neg  = mk_pair(255, 170, 0, 170)    # orange
        self.high_pos, self.high_neg = mk_pair(255, 60, 140, 175)   # pink/red top

        for it in [self.base_pos, self.base_neg,
                   self.low_pos, self.low_neg,
                   self.mid_pos, self.mid_neg,
                   self.high_pos, self.high_neg]:
            self.plot.addItem(it)

        self.base_pos.setZValue(0); self.base_neg.setZValue(0)
        self.low_pos.setZValue(1);  self.low_neg.setZValue(1)
        self.mid_pos.setZValue(2);  self.mid_neg.setZValue(2)
        self.high_pos.setZValue(3); self.high_neg.setZValue(3)

        # Playhead (black)
        self.playhead = pg.InfiniteLine(pos=self.playhead_x, angle=90, pen=pg.mkPen('k', width=2))
        self.playhead.setZValue(10)
        self.plot.addItem(self.playhead)

        # Grid lines
        self.max_lines = int(np.ceil(self.window_s / max(self.T, 1e-6))) + 16 if self.T > 0 else 0
        self.grid_lines = []
        for _ in range(self.max_lines):
            ln = pg.InfiniteLine(pos=0.0, angle=90, movable=False)
            ln.hide()
            ln.setZValue(20)
            self.plot.addItem(ln)
            self.grid_lines.append(ln)

        # Loop boundary markers
        self.loop_start_ln = pg.InfiniteLine(pos=0.0, angle=90, movable=False, pen=pg.mkPen((0, 200, 0), width=2))
        self.loop_end_ln   = pg.InfiniteLine(pos=0.0, angle=90, movable=False, pen=pg.mkPen((0, 200, 0), width=2))
        self.loop_start_ln.setZValue(30)
        self.loop_end_ln.setZValue(30)
        self.loop_start_ln.hide()
        self.loop_end_ln.hide()
        self.plot.addItem(self.loop_start_ln)
        self.plot.addItem(self.loop_end_ln)

        # Timer
        self.timer = QtCore.QTimer()
        self.timer.timeout.connect(self.update_frame)
        self.timer.start(int(1000 / int(fps)))

    def closeEvent(self, event):
        self.timer.stop()
        self.player.stop()
        return super().closeEvent(event)

    # ---- Loop controls ----
    def set_loop_to_current_bar(self):
        if self.T <= 0:
            return
        cur_sample = self.player.get_pos()
        cur_t = cur_sample / self.sr

        k = int(np.floor((cur_t - self.t0) / self.T))
        bar_k = k - (k % self.beats_per_bar)

        loop_start_t = self.t0 + bar_k * self.T
        loop_end_t = loop_start_t + self.beats_per_bar * self.T

        ls = int(np.clip(loop_start_t * self.sr, 0, len(self.mono) - 1))
        le = int(np.clip(loop_end_t * self.sr, 0, len(self.mono)))
        if le <= ls + 64:
            return
        self.player.set_loop(True, ls, le)

    def toggle_loop(self):
        with self.player.lock:
            self.player.state.loop_on = not self.player.state.loop_on

    def shift_loop_one_bar(self, direction: int):
        if self.T <= 0:
            return
        delta_s = direction * self.beats_per_bar * self.T
        self.player.shift_loop(int(delta_s * self.sr))

    def keyPressEvent(self, event):
        key = event.key()
        if key == QtCore.Qt.Key.Key_Space:
            self.player.toggle_play()
        elif key == QtCore.Qt.Key.Key_L:
            self.toggle_loop()
        elif key == QtCore.Qt.Key.Key_B:
            self.set_loop_to_current_bar()
        elif key == QtCore.Qt.Key.Key_Left:
            self.shift_loop_one_bar(-1)
        elif key == QtCore.Qt.Key.Key_Right:
            self.shift_loop_one_bar(+1)
        elif key == QtCore.Qt.Key.Key_P:
            if self.drift_dbg:
                generate_debug_plots(self.drift_dbg, self.t0, self.bpm_refined, self.bpm)
            else:
                print("[debug] No drift debug info (drift refinement disabled or failed).")
        else:
            super().keyPressEvent(event)

    @staticmethod
    def _set_signed(curve_pos, curve_neg, x, vmin, vmax):
        y_pos = np.maximum(vmax, 0.0)
        y_neg = np.minimum(vmin, 0.0)
        curve_pos.setData(x, y_pos)
        curve_neg.setData(x, y_neg)

    def update_frame(self):
        cur_sample = self.player.get_pos()
        cur_t = cur_sample / self.sr

        # window left edge in absolute time
        t_left = cur_t - self.playhead_x
        t_left = float(np.clip(t_left, 0.0, max(0.0, self.duration_s - self.window_s)))

        # Convert window to sample indices (native sr)
        i0 = int(t_left * self.sr)
        i1 = int(min(self.n_samples, i0 + int(self.window_s * self.sr)))

        # Convert to envelope block indices
        b0 = i0 // self.env_block
        b1 = int(np.ceil(i1 / self.env_block))
        b0 = max(0, b0)
        b1 = min(self.nb, b1)

        # x positions (seconds inside window)
        m = max(1, b1 - b0)
        x = (np.arange(m, dtype=np.float32) * (self.env_block / self.sr))
        x = np.clip(x, 0.0, self.window_s)

        # Draw waveform layers (copied style)
        self._set_signed(self.base_pos, self.base_neg, x,
                         self.base_min[b0:b1], self.base_max[b0:b1])
        self._set_signed(self.low_pos, self.low_neg, x,
                         self.low_min[b0:b1], self.low_max[b0:b1])
        self._set_signed(self.mid_pos, self.mid_neg, x,
                         self.mid_min[b0:b1], self.mid_max[b0:b1])
        self._set_signed(self.high_pos, self.high_neg, x,
                         self.high_min[b0:b1], self.high_max[b0:b1])

        # ===== Grid lines =====
        for ln in self.grid_lines:
            ln.hide()

        if self.T > 0:
            k0 = int(np.floor((t_left - self.t0) / self.T)) - 2
            k1 = int(np.ceil((t_left + self.window_s - self.t0) / self.T)) + 2

            j = 0
            for k in range(k0, k1 + 1):
                tg = self.t0 + k * self.T
                if not (t_left <= tg <= t_left + self.window_s):
                    continue
                xg = tg - t_left
                is_bar = (k % self.beats_per_bar) == 0
                ln = self.grid_lines[j]
                ln.setPos(xg)
                if is_bar:
                    ln.setPen(pg.mkPen('r', width=3))
                else:
                    ln.setPen(pg.mkPen((255, 165, 0), width=1, style=QtCore.Qt.PenStyle.DashLine))
                ln.show()
                j += 1
                if j >= len(self.grid_lines):
                    break

        # ===== Loop markers =====
        with self.player.lock:
            loop_on = self.player.state.loop_on
            ls = self.player.state.loop_start
            le = self.player.state.loop_end
            playing = self.player.state.playing

        if loop_on and le > ls:
            ls_t = ls / self.sr
            le_t = le / self.sr
            if t_left <= ls_t <= t_left + self.window_s:
                self.loop_start_ln.setPos(ls_t - t_left)
                self.loop_start_ln.show()
            else:
                self.loop_start_ln.hide()
            if t_left <= le_t <= t_left + self.window_s:
                self.loop_end_ln.setPos(le_t - t_left)
                self.loop_end_ln.show()
            else:
                self.loop_end_ln.hide()
        else:
            self.loop_start_ln.hide()
            self.loop_end_ln.hide()

        self.info.setText(
            f"t={cur_t:7.3f}s  sample={cur_sample:,}  "
            f"BPM={self.bpm:6.2f} (ref={self.bpm_refined:7.3f}, drift={self.bpm_drift_best:7.3f})  "
            f"t0={self.t0:9.6f}s  mode={ANCHOR_MODE}  snapKick={SNAP_AFTER_AUDIO_START}  earliestKick={EARLIEST_EDGE_MODE}  "
            f"loop={'ON' if loop_on else 'off'}  play={'ON' if playing else 'off'}  blocks={m}"
        )


def main():
    audio_path = r"C:\Users\winga\source\repos\waveOut\waveOut\Test\spectrum.wav"  # <-- change me
    app = QtWidgets.QApplication([])
    w = BeatGridViewer(audio_path, window_s=8.0, fps=120, beats_per_bar=4)
    w.resize(1300, 520)
    w.show()
    app.exec()


if __name__ == "__main__":
    main()
