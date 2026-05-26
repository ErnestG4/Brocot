"""Differentiable, batched FM renderer on the GPU (torch).

This is the heavy-precompute engine. Two payoffs over the CPU/numpy path:

  1. Gradient-based matching — optimise FM parameters *through* a
     differentiable render→spectrum, with hundreds of parallel restarts on
     the GPU. Gradients escape the local-optima traps that limited the
     coordinate-descent search; matching a target is milliseconds, not
     seconds.

  2. Massive real-spectrum banks — batch-render and fingerprint millions of
     configs with the *actual* time-domain render (folding included, no
     analytic-prune gap), for a denser, more faithful retrieval table.

Parameterisation: each of K operator slots has a continuous log-ratio
(ratio = exp(log_ratio), snapped to a Stern-Brocot path afterward), a
non-negative depth (via softplus), and a detune in cents. The carrier is the
sine FM base x(t) = sin(2π f_c t + Σ_k I_k · sin(2π r_k f_c t)) — exactly the
engine's model.

Run only under the GPU venv (phase3/.venv-gpu/bin/python).
"""

from __future__ import annotations

import numpy as np
import torch

DEVICE = "cuda" if torch.cuda.is_available() else "cpu"
F_CARRIER = 220.0
SAMPLE_RATE = 48_000


# ---------------------------------------------------------------------------
# Integer-grid projection matrix (fixed, differentiable linear map)
# ---------------------------------------------------------------------------

def interp_matrix(n_fft: int, sr: int, q_max: int, f_carrier: float,
                  device=DEVICE) -> torch.Tensor:
    """(q_max, F) integer-grid *binning* matrix: every rfft bin's magnitude is
    split linearly between its two bracketing integer partials (position
    x = f/f_carrier → floor(x) and floor(x)+1). This conserves energy and
    captures off-grid (fractional-ratio) partials — matching the semantics of
    audio_spectrum.audio_to_spectrum and family_distance.quantize_to_integer_grid.

    (An earlier version *sampled* the magnitude at exactly n·f_carrier, which
    silently dropped off-grid partial energy and made fractional-ratio targets
    un-matchable — fixed here.)"""
    freqs = np.fft.rfftfreq(n_fft, 1.0 / sr)
    F = freqs.size
    W = np.zeros((q_max, F), dtype=np.float32)
    for j, f in enumerate(freqs):
        if f <= 0.0:
            continue
        x = f / f_carrier
        lo = int(np.floor(x))
        frac = x - lo
        if 1 <= lo <= q_max:
            W[lo - 1, j] += 1.0 - frac
        if 1 <= lo + 1 <= q_max:
            W[lo, j] += frac
    return torch.from_numpy(W).to(device)


# ---------------------------------------------------------------------------
# Differentiable render + fingerprint
# ---------------------------------------------------------------------------

def render_batch(log_ratio: torch.Tensor, depth: torch.Tensor,
                 detune_cents: torch.Tensor, t: torch.Tensor,
                 f_carrier: float = F_CARRIER) -> torch.Tensor:
    """Batched sine-FM render. Shapes: params (B, K), t (T,) → (B, T).
    K-loop accumulation keeps memory at O(B·T) (no B·K·T tensor)."""
    B, K = log_ratio.shape
    two_pi = 2.0 * np.pi
    ratio = torch.exp(log_ratio) * torch.pow(2.0, detune_cents / 1200.0)  # (B,K)
    phase = two_pi * f_carrier * t.unsqueeze(0).expand(B, -1).clone()      # (B,T)
    for k in range(K):
        fmod = (f_carrier * ratio[:, k]).unsqueeze(1)                      # (B,1)
        phase = phase + depth[:, k].unsqueeze(1) * torch.sin(two_pi * fmod * t)
    return torch.sin(phase)


def fingerprint_batch(x: torch.Tensor, W: torch.Tensor,
                      window: torch.Tensor) -> torch.Tensor:
    """(B, T) waveforms → (B, q_max) L1-normalised integer-grid fingerprints."""
    mag = torch.fft.rfft(x * window.unsqueeze(0)).abs()       # (B, F)
    amps = mag @ W.t()                                        # (B, q_max)
    amps = torch.clamp(amps, min=0.0)
    s = amps.sum(dim=1, keepdim=True)
    return amps / torch.clamp(s, min=1e-12)


# ---------------------------------------------------------------------------
# Gradient-based matching (parallel restarts)
# ---------------------------------------------------------------------------

def _normalise_target(target_amps, q_max):
    tgt = torch.from_numpy(np.asarray(target_amps, np.float32)[:q_max]).to(DEVICE)
    if tgt.numel() < q_max:
        tgt = torch.nn.functional.pad(tgt, (0, q_max - tgt.numel()))
    return tgt / torch.clamp(tgt.sum(), min=1e-12)


def match_target(target_amps: np.ndarray, *, ratio_values: np.ndarray,
                 n_ops: int = 6, n_candidates: int = 400_000, top_m: int = 96,
                 polish_steps: int = 250, q_max: int = 64, n_fft: int = 4096,
                 f_carrier: float = F_CARRIER, sr: int = SAMPLE_RATE,
                 chunk: int = 25_000, lr: float = 0.04, seed: int = 0):
    """Invert a target spectrum to FM params, GPU hybrid:

      Phase 1 — discrete ratio search: render+score `n_candidates` random
      configs whose ratios are drawn from `ratio_values` (the curated
      Stern-Brocot set) with random depths, in chunks; keep the best `top_m`.
      This is the part gradients can't do (frequency loss is non-convex), so
      we brute-force it on the GPU.

      Phase 2 — gradient polish: Adam-refine the depths & detune of those
      top_m (ratios fixed), which gradients *do* nail (smooth amplitude loss).

    Returns (best dict {ratio_idx, ratios, depths, detune}, best_loss).
    """
    g = torch.Generator(device=DEVICE).manual_seed(seed)
    t = torch.arange(n_fft, device=DEVICE, dtype=torch.float32) / sr
    window = torch.hann_window(n_fft, device=DEVICE)
    W = interp_matrix(n_fft, sr, q_max, f_carrier)
    tgt = _normalise_target(target_amps, q_max)
    rv = torch.from_numpy(np.asarray(ratio_values, np.float32)).to(DEVICE)
    R, K = rv.numel(), n_ops

    # ---- Phase 1: discrete-ratio brute force ----
    best_loss = torch.full((top_m,), 1e9, device=DEVICE)
    best_ridx = torch.zeros((top_m, K), dtype=torch.long, device=DEVICE)
    best_depth = torch.zeros((top_m, K), device=DEVICE)
    done = 0
    while done < n_candidates:
        b = min(chunk, n_candidates - done)
        ridx = torch.randint(0, R, (b, K), generator=g, device=DEVICE)
        depth = torch.empty(b, K, device=DEVICE).uniform_(0.0, 5.0, generator=g)
        log_ratio = torch.log(rv[ridx])
        amps = fingerprint_batch(
            render_batch(log_ratio, depth, torch.zeros(b, K, device=DEVICE), t,
                         f_carrier), W, window)
        loss = (amps - tgt.unsqueeze(0)).abs().sum(dim=1)
        # Merge this chunk into the running top_m.
        cat_loss = torch.cat([best_loss, loss])
        cat_ridx = torch.cat([best_ridx, ridx])
        cat_depth = torch.cat([best_depth, depth])
        keep = torch.argsort(cat_loss)[:top_m]
        best_loss, best_ridx, best_depth = cat_loss[keep], cat_ridx[keep], cat_depth[keep]
        done += b

    # ---- Phase 2: gradient polish (ratios fixed) ----
    log_ratio = torch.log(rv[best_ridx])                       # (top_m, K) fixed
    depth_raw = torch.log(torch.expm1(torch.clamp(best_depth, min=1e-3)))  # inverse softplus
    depth_raw = depth_raw.detach().requires_grad_(True)
    detune = torch.zeros(top_m, K, device=DEVICE).requires_grad_(True)
    opt = torch.optim.Adam([depth_raw, detune], lr=lr)
    for _ in range(polish_steps):
        opt.zero_grad()
        depth = torch.nn.functional.softplus(depth_raw)
        amps = fingerprint_batch(render_batch(log_ratio, depth, detune, t, f_carrier),
                                 W, window)
        loss = (amps - tgt.unsqueeze(0)).abs().sum(dim=1)
        loss.sum().backward()
        opt.step()
        with torch.no_grad():
            detune.clamp_(-100.0, 100.0)

    with torch.no_grad():
        depth = torch.nn.functional.softplus(depth_raw)
        amps = fingerprint_batch(render_batch(log_ratio, depth, detune, t, f_carrier),
                                 W, window)
        final = (amps - tgt.unsqueeze(0)).abs().sum(dim=1)
        bi = int(torch.argmin(final))
        best = {
            "ratio_idx": best_ridx[bi].cpu().numpy(),
            "ratios": rv[best_ridx[bi]].cpu().numpy(),
            "depths": depth[bi].cpu().numpy(),
            "detune": detune[bi].cpu().numpy(),
        }
        return best, float(final[bi])


# ---------------------------------------------------------------------------
# Convenience: target spectrum → Config (snapped to Stern-Brocot paths)
# ---------------------------------------------------------------------------

def match_to_config(target_amps, *, min_depth: float = 0.04, **kw):
    """GPU-match a target spectrum and return a (Config, loss). Ratios come
    from the curated Stern-Brocot set, so each maps straight back to a path;
    operators below `min_depth` are dropped.

    IMPORTANT — coherence: `target_amps` must be extracted in the *same* space
    the matcher scores in (the integer-grid binning fingerprint at this q_max).
    For audio input use gpu_fm.audio_to_fingerprint, not the Welch-based
    audio_spectrum.audio_to_spectrum, or the match won't transfer.
    """
    from phase3.config import Config, ModConfig, path_ratio
    from phase3.coverage_bank import curated_ratios
    paths = curated_ratios(5)
    rv = np.array([path_ratio(p) for p in paths], np.float32)
    best, loss = match_target(target_amps, ratio_values=rv, **kw)
    mods = [ModConfig(path=paths[int(i)], depth=float(d), enabled=True,
                      detune_cents=float(c))
            for i, d, c in zip(best["ratio_idx"], best["depths"], best["detune"])
            if d > min_depth]
    n = len(mods)
    return Config(mods=mods + [ModConfig() for _ in range(max(0, 64 - n))]), loss


def audio_to_fingerprint(samples, q_max: int = 64, f_carrier: float = F_CARRIER,
                         sr: int = SAMPLE_RATE, n_fft: int = 16384,
                         n_segs: int = 8):
    """Integer-grid binning fingerprint of audio, in the matcher's space
    (segment-averaged for stability). Use this to extract a Mode B target so
    it's coherent with match_to_config / the GPU bank."""
    x = torch.as_tensor(np.asarray(samples, np.float32), device=DEVICE)
    if x.ndim > 1:
        x = x.mean(dim=-1)
    window = torch.hann_window(n_fft, device=DEVICE)
    W = interp_matrix(n_fft, sr, q_max, f_carrier)
    if x.numel() < n_fft:
        x = torch.nn.functional.pad(x, (0, n_fft - x.numel()))
    hop = max(1, (x.numel() - n_fft) // max(1, n_segs - 1)) if x.numel() > n_fft else n_fft
    segs = [x[i:i + n_fft] for i in range(0, x.numel() - n_fft + 1, hop)][:n_segs]
    if not segs:
        segs = [x[:n_fft]]
    batch = torch.stack(segs)
    amps = fingerprint_batch(batch, W, window).mean(dim=0)
    return amps.cpu().numpy()


if __name__ == "__main__":   # quick self-test (run under the GPU venv)
    import time
    from phase3.coverage_bank import curated_ratios
    from phase3.config import path_ratio
    paths = curated_ratios(5)
    rv = np.array([path_ratio(p) for p in paths], np.float32)
    n_fft, q_max = 16384, 64
    t = torch.arange(n_fft, device=DEVICE, dtype=torch.float32) / SAMPLE_RATE
    win = torch.hann_window(n_fft, device=DEVICE)
    W = interp_matrix(n_fft, SAMPLE_RATE, q_max, F_CARRIER)
    rt = torch.log(torch.tensor([[2.0, 1.5]], device=DEVICE))
    tgt = fingerprint_batch(render_batch(rt, torch.tensor([[1.0, 1.5]], device=DEVICE),
                                         torch.zeros(1, 2, device=DEVICE), t), W, win)[0].cpu().numpy()
    t0 = time.time()
    cfg, loss = match_to_config(tgt, n_ops=6, n_candidates=400_000, q_max=q_max, n_fft=n_fft)
    print(f"self-test 2-op: loss={loss:.4f} in {time.time()-t0:.1f}s, "
          f"recovered {cfg.num_enabled()} ops, ratios "
          f"{sorted(round(m.ratio(),2) for m in cfg.mods if m.enabled)}")
