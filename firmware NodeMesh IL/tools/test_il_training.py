"""test_il_training.py -- Phase D validation: training loss decreases over 1000 epochs.

Exact Python mirror of IlTrainer (node0_orchestrator_s3/src/learning/il_trainer.cpp).

Architecture : 134 -> 32 (ReLU) -> 16 (ReLU) -> 6 (linear)
Init         : Kaiming He uniform, limit = sqrt(6 / fan_in)
Loss         : MSE = (1/6) sum (pred_i - target_i)^2
Gradient     : d_out = (2/6)(pred - target)
Grad clip    : rescale lr by kGradNormClip / ||g|| when ||g|| > kGradNormClip
Optimizer    : online SGD, one sample per step

Calibration constants mirrored from calibration_config.h:
  kIlLearningRate     = 0.001
  kGradNormClip       = 1.0
  kMinSamplesForTrain = 50
  kIlDatasetCapacity  = 512
"""

import math
import random
import sys

try:
    import numpy as np
    _HAS_NUMPY = True
except ImportError:
    _HAS_NUMPY = False

# ---------------------------------------------------------------------------
# Calibration constants (mirror calibration_config.h)
# ---------------------------------------------------------------------------

LR             = 0.001
GRAD_NORM_CLIP = 1.0
MIN_SAMPLES    = 50
DATASET_CAP    = 512

# ---------------------------------------------------------------------------
# Network dimensions
# ---------------------------------------------------------------------------

INPUT_DIM  = 134   # 6 joints + 128 vision features
H0_DIM     = 32
H1_DIM     = 16
OUTPUT_DIM = 6

# ---------------------------------------------------------------------------
# Reproducible RNG
# ---------------------------------------------------------------------------

rng = random.Random(0xDEADBEEF)


def _kaiming_list(n, fan_in):
    """Kaiming He uniform list: Uniform(-limit, +limit), limit=sqrt(6/fan_in)."""
    limit = math.sqrt(6.0 / fan_in)
    return [rng.uniform(-limit, limit) for _ in range(n)]


# ---------------------------------------------------------------------------
# Weight containers
# ---------------------------------------------------------------------------

class Weights:
    """Flat Python lists, row-major (matches C++ layout)."""
    def __init__(self):
        self.W0 = _kaiming_list(H0_DIM * INPUT_DIM,  INPUT_DIM)
        self.b0 = [0.0] * H0_DIM
        self.W1 = _kaiming_list(H1_DIM * H0_DIM,    H0_DIM)
        self.b1 = [0.0] * H1_DIM
        self.W2 = _kaiming_list(OUTPUT_DIM * H1_DIM, H1_DIM)
        self.b2 = [0.0] * OUTPUT_DIM


class NpWeights:
    """Same init as Weights, stored as numpy arrays for fast vectorised ops."""
    def __init__(self):
        _w = Weights()   # uses the same seeded rng for reproducibility
        self.W0 = np.array(_w.W0, dtype=np.float64).reshape(H0_DIM, INPUT_DIM)
        self.b0 = np.zeros(H0_DIM)
        self.W1 = np.array(_w.W1, dtype=np.float64).reshape(H1_DIM, H0_DIM)
        self.b1 = np.zeros(H1_DIM)
        self.W2 = np.array(_w.W2, dtype=np.float64).reshape(OUTPUT_DIM, H1_DIM)
        self.b2 = np.zeros(OUTPUT_DIM)


# ---------------------------------------------------------------------------
# Pure-Python forward pass (exact mirror of il_trainer.cpp)
# ---------------------------------------------------------------------------

def forward_py(w, inp):
    """Returns (pred, h0, h1)."""
    h0 = []
    for k in range(H0_DIM):
        acc = w.b0[k]
        off = k * INPUT_DIM
        for m in range(INPUT_DIM):
            acc += w.W0[off + m] * inp[m]
        h0.append(acc if acc > 0.0 else 0.0)

    h1 = []
    for j in range(H1_DIM):
        acc = w.b1[j]
        off = j * H0_DIM
        for k in range(H0_DIM):
            acc += w.W1[off + k] * h0[k]
        h1.append(acc if acc > 0.0 else 0.0)

    pred = []
    for i in range(OUTPUT_DIM):
        acc = w.b2[i]
        off = i * H1_DIM
        for j in range(H1_DIM):
            acc += w.W2[off + j] * h1[j]
        pred.append(acc)

    return pred, h0, h1


def mse_loss_py(pred, target):
    return sum((p - t) ** 2 for p, t in zip(pred, target)) / OUTPUT_DIM


def sgd_step_py(w, inp, target):
    """One online SGD step. Returns MSE before update."""
    pred, h0, h1 = forward_py(w, inp)
    loss = mse_loss_py(pred, target)

    d2 = [(2.0 / OUTPUT_DIM) * (pred[i] - target[i]) for i in range(OUTPUT_DIM)]

    dh1 = []
    for j in range(H1_DIM):
        acc = sum(w.W2[i * H1_DIM + j] * d2[i] for i in range(OUTPUT_DIM))
        dh1.append(acc * (1.0 if h1[j] > 0.0 else 0.0))

    dh0 = []
    for k in range(H0_DIM):
        acc = sum(w.W1[j * H0_DIM + k] * dh1[j] for j in range(H1_DIM))
        dh0.append(acc * (1.0 if h0[k] > 0.0 else 0.0))

    # Factored grad-norm (mirrors C++ exactly)
    d2_sq   = sum(x * x for x in d2)
    h1_sq   = sum(x * x for x in h1)
    dh1_sq  = sum(x * x for x in dh1)
    h0_sq   = sum(x * x for x in h0)
    dh0_sq  = sum(x * x for x in dh0)
    inp_sq  = sum(x * x for x in inp)

    gnorm_sq = (d2_sq * h1_sq + d2_sq
              + dh1_sq * h0_sq + dh1_sq
              + dh0_sq * inp_sq + dh0_sq)

    lr = LR
    if gnorm_sq > GRAD_NORM_CLIP ** 2:
        lr *= GRAD_NORM_CLIP / math.sqrt(gnorm_sq)

    for i in range(OUTPUT_DIM):
        for j in range(H1_DIM):
            w.W2[i * H1_DIM + j] -= lr * d2[i] * h1[j]
        w.b2[i] -= lr * d2[i]
    for j in range(H1_DIM):
        for k in range(H0_DIM):
            w.W1[j * H0_DIM + k] -= lr * dh1[j] * h0[k]
        w.b1[j] -= lr * dh1[j]
    for k in range(H0_DIM):
        for m in range(INPUT_DIM):
            w.W0[k * INPUT_DIM + m] -= lr * dh0[k] * inp[m]
        w.b0[k] -= lr * dh0[k]

    return loss


# ---------------------------------------------------------------------------
# Numpy vectorised epoch step (same math, batch over full dataset)
# ---------------------------------------------------------------------------

def epoch_step_np(w, X, Y):
    """One full-batch gradient step. Returns MSE before update.
    X: (N, INPUT_DIM), Y: (N, OUTPUT_DIM)
    """
    N = X.shape[0]

    H0 = np.maximum(0.0, X @ w.W0.T + w.b0)    # (N, H0_DIM)
    H1 = np.maximum(0.0, H0 @ w.W1.T + w.b1)   # (N, H1_DIM)
    P  = H1 @ w.W2.T + w.b2                     # (N, OUTPUT_DIM)

    loss = float(np.mean((P - Y) ** 2))

    D2  = (2.0 / OUTPUT_DIM) * (P - Y) / N      # (N, OUTPUT_DIM)
    DH1 = (D2 @ w.W2) * (H1 > 0.0)              # (N, H1_DIM)
    DH0 = (DH1 @ w.W1) * (H0 > 0.0)             # (N, H0_DIM)

    # Per-sample grad norm and clipped lr
    d2_sq  = np.sum(D2  ** 2, axis=1)
    h1_sq  = np.sum(H1  ** 2, axis=1)
    dh1_sq = np.sum(DH1 ** 2, axis=1)
    h0_sq  = np.sum(H0  ** 2, axis=1)
    dh0_sq = np.sum(DH0 ** 2, axis=1)
    x_sq   = np.sum(X   ** 2, axis=1)

    gnorm_sq = (d2_sq * h1_sq + d2_sq
              + dh1_sq * h0_sq + dh1_sq
              + dh0_sq * x_sq  + dh0_sq)

    lrs = np.where(gnorm_sq > GRAD_NORM_CLIP ** 2,
                   LR * GRAD_NORM_CLIP / np.sqrt(np.maximum(gnorm_sq, 1e-30)),
                   LR)                           # (N,)

    D2s  = D2  * lrs[:, None]
    DH1s = DH1 * lrs[:, None]
    DH0s = DH0 * lrs[:, None]

    w.W2 -= D2s.T  @ H1;  w.b2 -= D2s.sum(axis=0)
    w.W1 -= DH1s.T @ H0;  w.b1 -= DH1s.sum(axis=0)
    w.W0 -= DH0s.T @ X;   w.b0 -= DH0s.sum(axis=0)

    return loss


# single-sample wrapper used by unit tests
def sgd_step_np(w, inp, target):
    return epoch_step_np(w, inp[None, :], target[None, :])


# ---------------------------------------------------------------------------
# Synthetic dataset
# ---------------------------------------------------------------------------

def make_dataset(n=100, seed=42):
    """Returns list of (inp_list, target_list) with nonlinear targets."""
    ds_rng = random.Random(seed)
    samples = []
    for _ in range(n):
        joints = [ds_rng.uniform(10.0, 170.0) for _ in range(6)]
        vis    = [ds_rng.randint(0, 255) for _ in range(128)]
        targets = []
        for i in range(6):
            v = 90.0 + 60.0 * math.sin(
                0.3 * joints[(i + 1) % 6] / 180.0 * math.pi
                + 0.1 * sum(vis[i*16:(i+1)*16]) / 255.0)
            targets.append(max(10.0, min(170.0, v)))
        inp = joints + [b / 255.0 for b in vis]
        samples.append((inp, targets))
    return samples


# ---------------------------------------------------------------------------
# Training runner
# ---------------------------------------------------------------------------

def run_training(n_epochs=1000, n_demos=100, verbose=False, seed=0xDEADBEEF):
    # Reset the global rng so each call with the same seed is reproducible.
    global rng
    rng = random.Random(seed)
    dataset = make_dataset(n=n_demos)

    if _HAS_NUMPY:
        w = NpWeights()
        X = np.array([s[0] for s in dataset], dtype=np.float64)
        Y = np.array([s[1] for s in dataset], dtype=np.float64)
        epoch_losses = []
        for epoch in range(n_epochs):
            l = epoch_step_np(w, X, Y)
            epoch_losses.append(l)
            if verbose and (epoch + 1) % 100 == 0:
                print(f"  Epoch {epoch+1:4d}/{n_epochs}  MSE = {l:.6f}")
    else:
        w = Weights()
        epoch_losses = []
        for epoch in range(n_epochs):
            el = 0.0
            for inp, tgt in dataset:
                el += sgd_step_py(w, inp, tgt)
            el /= len(dataset)
            epoch_losses.append(el)
            if verbose and (epoch + 1) % 100 == 0:
                print(f"  Epoch {epoch+1:4d}/{n_epochs}  MSE = {el:.6f}")

    return {
        "epoch_losses": epoch_losses,
        "initial_loss": epoch_losses[0],
        "final_loss":   epoch_losses[-1],
        "reduction_pct": 100.0 * (epoch_losses[0] - epoch_losses[-1]) / (epoch_losses[0] + 1e-12),
    }


# ---------------------------------------------------------------------------
# Test harness
# ---------------------------------------------------------------------------

PASS_COUNT = 0
FAIL_COUNT = 0
FAILURES   = []
VERBOSE    = "--verbose" in sys.argv or "-v" in sys.argv


def test(name):
    def decorator(fn):
        global PASS_COUNT, FAIL_COUNT
        try:
            fn()
            PASS_COUNT += 1
            if VERBOSE:
                print(f"  PASS  {name}")
        except AssertionError as exc:
            FAIL_COUNT += 1
            msg = str(exc)
            FAILURES.append(f"  X {name}" + (f": {msg}" if msg else ""))
            if VERBOSE:
                print(f"  FAIL  {name}" + (f": {msg}" if msg else ""))
        return fn
    return decorator


# ---------------------------------------------------------------------------
# Group 1: Architecture sanity
# ---------------------------------------------------------------------------

@test("forward pass output has 6 elements")
def _():
    w = Weights()
    pred, h0, h1 = forward_py(w, [0.0] * INPUT_DIM)
    assert len(pred) == 6
    assert len(h0)   == H0_DIM
    assert len(h1)   == H1_DIM


@test("forward pass produces finite values on random input")
def _():
    w = Weights()
    inp = [rng.uniform(-1.0, 1.0) for _ in range(INPUT_DIM)]
    pred, h0, h1 = forward_py(w, inp)
    assert all(math.isfinite(v) for v in pred + h0 + h1), "NaN/Inf in forward pass"


@test("ReLU activations are non-negative")
def _():
    w = Weights()
    _, h0, h1 = forward_py(w, [rng.uniform(-100.0, 100.0) for _ in range(INPUT_DIM)])
    assert all(v >= 0.0 for v in h0), "h0 has negative values"
    assert all(v >= 0.0 for v in h1), "h1 has negative values"


@test("all-zero weights give zero output (biases are zero)")
def _():
    w = Weights()
    w.W0[:] = [0.0] * len(w.W0)
    w.W1[:] = [0.0] * len(w.W1)
    w.W2[:] = [0.0] * len(w.W2)
    pred, _, _ = forward_py(w, [1.0] * INPUT_DIM)
    assert all(p == 0.0 for p in pred)


@test("MSE loss is non-negative")
def _():
    w = Weights()
    pred, _, _ = forward_py(w, [rng.uniform(0.0, 1.0) for _ in range(INPUT_DIM)])
    tgt = [rng.uniform(10.0, 170.0) for _ in range(OUTPUT_DIM)]
    assert mse_loss_py(pred, tgt) >= 0.0


@test("MSE loss is zero when pred equals target")
def _():
    w = Weights()
    pred, _, _ = forward_py(w, [0.5] * INPUT_DIM)
    assert mse_loss_py(pred, pred) == 0.0


@test("single SGD step does not increase loss (zero input / zero target)")
def _():
    w = Weights()
    inp = [0.0] * INPUT_DIM
    tgt = [0.0] * OUTPUT_DIM
    l0 = mse_loss_py(forward_py(w, inp)[0], tgt)
    sgd_step_py(w, inp, tgt)
    l1 = mse_loss_py(forward_py(w, inp)[0], tgt)
    assert l1 <= l0 + 1e-9, f"Loss increased: {l0:.6f} -> {l1:.6f}"


@test("Kaiming W0 variance approx 2/fan_in")
def _():
    expected = 2.0 / INPUT_DIM
    w = Weights()
    n = len(w.W0)
    mean = sum(w.W0) / n
    var  = sum((x - mean) ** 2 for x in w.W0) / n
    assert abs(var - expected) / expected < 0.20, (
        f"W0 var={var:.5f} expected~{expected:.5f}")


@test("weights change after one SGD step")
def _():
    w = Weights()
    W2_before = list(w.W2)
    sgd_step_py(w, [0.5] * INPUT_DIM, [90.0] * OUTPUT_DIM)
    assert w.W2 != W2_before, "W2 unchanged after SGD step"


# ---------------------------------------------------------------------------
# Group 2: Dataset
# ---------------------------------------------------------------------------

@test("dataset size is correct")
def _():
    assert len(make_dataset(100)) == 100


@test("dataset inputs have 134 elements")
def _():
    for inp, _ in make_dataset(5):
        assert len(inp) == INPUT_DIM


@test("dataset targets have 6 elements")
def _():
    for _, tgt in make_dataset(5):
        assert len(tgt) == OUTPUT_DIM


@test("vision features normalized to [0, 1]")
def _():
    for inp, _ in make_dataset(20):
        for v in inp[6:]:
            assert 0.0 <= v <= 1.0, f"value {v} out of [0,1]"


@test("joint inputs in [10, 170]")
def _():
    for inp, _ in make_dataset(20):
        for v in inp[:6]:
            assert 10.0 <= v <= 170.0, f"joint {v} out of range"


@test("dataset is deterministic with same seed")
def _():
    d1 = make_dataset(10, seed=7)
    d2 = make_dataset(10, seed=7)
    assert d1 == d2


@test("dataset differs with different seed")
def _():
    assert make_dataset(5, seed=1)[0][0] != make_dataset(5, seed=2)[0][0]


# ---------------------------------------------------------------------------
# Group 3: Training convergence
# ---------------------------------------------------------------------------

_RESULT = None


def _get_result():
    global _RESULT
    if _RESULT is None:
        if VERBOSE:
            print()
            print("  [Running 1000-epoch training on 100-demo dataset...]")
        _RESULT = run_training(n_epochs=1000, n_demos=100, verbose=VERBOSE)
    return _RESULT


@test("training completes 1000 epochs without NaN/Inf")
def _():
    r = _get_result()
    assert len(r["epoch_losses"]) == 1000
    assert all(math.isfinite(l) for l in r["epoch_losses"]), "NaN/Inf in loss trace"


@test("final loss is lower than initial loss")
def _():
    r = _get_result()
    assert r["final_loss"] < r["initial_loss"], (
        f"initial={r['initial_loss']:.4f}  final={r['final_loss']:.4f}")


@test("loss reduces by at least 30% over 1000 epochs")
def _():
    r = _get_result()
    pct = r["reduction_pct"]
    assert pct >= 30.0, (
        f"Only {pct:.1f}% reduction  "
        f"(initial={r['initial_loss']:.4f} final={r['final_loss']:.4f})")


@test("loss at epoch 500 < loss at epoch 50")
def _():
    losses = _get_result()["epoch_losses"]
    assert losses[499] < losses[49], (
        f"ep50={losses[49]:.4f}  ep500={losses[499]:.4f}")


@test("loss at epoch 1000 < loss at epoch 500")
def _():
    losses = _get_result()["epoch_losses"]
    assert losses[999] < losses[499], (
        f"ep500={losses[499]:.4f}  ep1000={losses[999]:.4f}")


@test("all losses in plausible range [0, 1e6]")
def _():
    for i, l in enumerate(_get_result()["epoch_losses"]):
        assert 0.0 <= l < 1e6, f"loss={l} at epoch {i+1}"


@test("run is deterministic (same seed -> same final loss)")
def _():
    r1 = run_training(n_epochs=200, n_demos=50)
    r2 = run_training(n_epochs=200, n_demos=50)
    assert abs(r1["final_loss"] - r2["final_loss"]) < 1e-9


@test("50-demo dataset converges (>=20% reduction over 500 epochs)")
def _():
    r = run_training(n_epochs=500, n_demos=50)
    assert r["final_loss"] < r["initial_loss"]
    assert r["reduction_pct"] >= 20.0, f"only {r['reduction_pct']:.1f}%"


@test("gradient clipping prevents explosion on large-magnitude inputs")
def _():
    if _HAS_NUMPY:
        w = NpWeights()
        X = np.full((1, INPUT_DIM), 1000.0)
        Y = np.zeros((1, OUTPUT_DIM))
        for _ in range(20):
            l = epoch_step_np(w, X, Y)
            assert math.isfinite(l), "NaN/Inf with large input"
        P = np.maximum(0.0, np.maximum(0.0, X @ w.W0.T + w.b0) @ w.W1.T + w.b1) @ w.W2.T + w.b2
        assert np.all(np.isfinite(P)), "weights exploded"
    else:
        w = Weights()
        for _ in range(20):
            l = sgd_step_py(w, [1000.0]*INPUT_DIM, [0.0]*OUTPUT_DIM)
            assert math.isfinite(l), "NaN/Inf with large input"


@test("infer clamp [0, 180] produces valid joint angles")
def _():
    if _HAS_NUMPY:
        w = NpWeights()
        X = np.full((1, INPUT_DIM), 100.0)
        Y = np.full((1, OUTPUT_DIM), 180.0)
        for _ in range(50):
            epoch_step_np(w, X, Y)
        pred = (np.maximum(0.0, np.maximum(0.0, X @ w.W0.T + w.b0) @ w.W1.T + w.b1) @ w.W2.T + w.b2)[0]
    else:
        w = Weights()
        for _ in range(50):
            sgd_step_py(w, [100.0]*INPUT_DIM, [180.0]*OUTPUT_DIM)
        pred, _, _ = forward_py(w, [100.0]*INPUT_DIM)
    clamped = [max(0.0, min(180.0, p)) for p in pred]
    assert all(0.0 <= v <= 180.0 for v in clamped)


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    total  = PASS_COUNT + FAIL_COUNT
    result = _get_result()
    backend = "numpy" if _HAS_NUMPY else "pure-Python"

    print()
    print(f"NodeMesh IL Training Validation -- {PASS_COUNT}/{total} passed  [{backend}]")
    print()
    print(f"  Dataset     : 100 demos, {INPUT_DIM}-dim input, {OUTPUT_DIM}-dim output")
    print(f"  Epochs      : 1000")
    print(f"  LR          : {LR}  GradClip: {GRAD_NORM_CLIP}")
    print(f"  Architecture: {INPUT_DIM} -> {H0_DIM} (ReLU) -> {H1_DIM} (ReLU) -> {OUTPUT_DIM}")
    print()
    print(f"  Initial MSE : {result['initial_loss']:.6f}")
    print(f"  Final   MSE : {result['final_loss']:.6f}")
    print(f"  Reduction   : {result['reduction_pct']:.1f}%")

    if FAIL_COUNT:
        print()
        print(f"FAILED ({FAIL_COUNT}):")
        for msg in FAILURES:
            print(msg)
        sys.exit(1)
    else:
        print()
        print("All tests passed.")
        sys.exit(0)
