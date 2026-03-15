# BANDPASS II — Propagation Physics Reference

## Overview

BANDPASS II models radio-wave propagation for Datatrak-type LF navigation
networks using the theoretical framework from:

> Williams, P.E.D. (2004), *Prediction of the Coverage and Performance of the
> Datatrak Low-Frequency Tracking System*, PhD thesis, University of Wales,
> Bangor.

The pipeline has eleven stages.  Each stage produces a GridArray (a 2D array
of values at every WGS84 lat/lon grid point).  Stages are run in sequence on
a dedicated worker thread; the UI receives the completed GridData when all
stages are finished.

---

## Stage 1 — Groundwave field strength (ITU-R P.368)

**Module:** `engine/groundwave.cpp`, `engine/grwave.cpp`
**Key functions:** `groundwave_for_model()`, `computeGroundwave()`

BANDPASS II provides three groundwave propagation models, selectable in the
Network Configuration panel (View → Network Configuration):

### Homogeneous (fast)

Uses a single conductivity lookup at the path midpoint and applies the
ITU-R P.368 empirical polynomial:

```
A_db = 0.0438 × d_km^0.832 × (f / 100 kHz)^0.5 × (0.005 / σ)^0.3
E [dBµV/m] = E_tx - A_db
```

where E_tx is the unattenuated field strength at 1 km (from transmitter power
and antenna height), σ is ground conductivity [S/m], and the reference
frequency is 100 kHz.

Ignores land/sea transitions entirely.  Fastest option (~20x faster than
Millington).  Useful for quick initial planning.

### Millington mixed-path (default)

Implements the Millington (1949) forward/backward averaging method for paths
that cross conductivity boundaries (e.g. land/sea transitions):

1. Sample *n* segments along the great-circle path TX→RX (n=20 for grid
   computation, n=50 for single-point virtual receiver).
2. Look up ground conductivity at each segment midpoint via ConductivityMap.
3. Forward pass: compute field using segment conductivities in TX→RX order.
4. Backward pass: compute field using segment conductivities in RX→TX order.
5. Return the linear average: `20 log10(0.5 × (lin(E_fwd) + lin(E_back)))`.

Each segment uses the same P.368 empirical polynomial as Homogeneous mode.
This is the recommended default for most planning work.

### GRWAVE (accurate)

Uses the Millington mixed-path averaging (same forward/backward method as
above), but replaces the empirical polynomial with the full Sommerfeld/Wait/
Norton residue series for each segment evaluation:

- **Flat-earth region** (short distances): Norton surface-wave formula with
  Earth-curvature correction.
- **Diffraction region** (long distances): Spherical-earth residue series
  using Airy functions and the Faddeeva function.
- **Transition distance**: d_test = 80 / cbrt(f_MHz) [SG3 Handbook Eq. 15].

Implementation follows NTIA Report 99-368 (DeMinco 1999) and the ITU
Handbook on Ground Wave Propagation (2014).  Vertical polarisation only;
ground-level antennas (no height gain).

GRWAVE is significantly slower (~100x per grid point) but provides better
accuracy at range extremes (< 10 km and > 500 km) and for paths over
varying ground conductivity.  The status bar shows "Groundwave (GRWAVE)"
during computation and the progress bar is scaled to reflect the longer
compute time.

### Model comparison

At typical Datatrak distances (100–300 km) over land, the polynomial fit
and GRWAVE agree to within ~5–8 dB.  The difference is more pronounced:
- At long range (> 300 km) where diffraction effects dominate
- Over sea paths (high conductivity increases the residue series accuracy)
- At frequencies away from the 100 kHz polynomial reference point

In the TOML file, the model is stored in the `[propagation]` section:

```toml
[propagation]
model = "millington"   # "homogeneous", "millington", or "grwave"
```

---

## Stage 2 — Skywave field strength (ITU-R P.684)

**Module:** `engine/skywave.cpp`

Median night-time ionospheric skywave field strength using the ITU-R P.684-6
method.  Key corrections applied:

- **Sea gain**: +6 dB for sea paths (lower absorption).
- **Geomagnetic latitude**: ITU Table 2 absorption correction.
- **Frequency scaling**: ITU P.684 specifies 100 kHz; BANDPASS II interpolates
  using configured F1 and F2 frequencies.

Skywave is modelled as an additive disturbance (worst-case night-time median);
it degrades SNR and contributes to GDR (groundwave-to-disturbance ratio).

---

## Stage 3 — Atmospheric noise (ITU-R P.372)

**Module:** `engine/noise.cpp`
**Key function:** `computeAtmNoise(freq_hz, lat, lon, hour)`

Atmospheric noise figure F_am is taken from the ITU-R P.372-17 world maps
(annual median, pre-dawn worst-case).  BANDPASS II stores the ITU tabulated
values as a built-in lookup table interpolated bilinearly at the receiver
position and the configured frequency.

---

## Stage 4 — Vehicle noise

**Module:** `engine/noise.cpp`
**Key function:** `vehicle_noise_dbuvm(dist_km)`

Empirical vehicle noise model.  Vehicle ignition and electrical system noise
forms a spatially-uniform noise floor that is added in quadrature to
atmospheric noise at the receiver point.  The level is constant across the
coverage area and set by `ReceiverModel::vehicle_noise_dbuvpm`.

---

## Stage 5 — SNR per station

**Module:** `engine/snr.cpp`
**Key function:** `computeSNR()`

Signal-to-noise ratio at each grid point for each transmitter:

```
SNR [dB] = E_gw [dBµV/m] − N_total [dBµV/m]
```

where N_total combines atmospheric noise, vehicle noise, and (in GDR)
the skywave field.  The result is clipped to a minimum of −40 dB
(receiver noise floor) and a maximum of +60 dB.

---

## Stage 6 — SGR / GDR

**Module:** `engine/snr.cpp`

- **SGR** (Skywave-to-Groundwave Ratio): E_sky / E_gw [dB].
- **GDR** (Groundwave-to-Disturbance Ratio): E_gw / (E_sky + N_atm) [dB].
  GDR drives the effective phase uncertainty under ionospheric conditions.

---

## Stage 7 — Phase / range uncertainty (Williams Eq. 9.7–9.8)

**Module:** `engine/snr.cpp`
**Key function:** `phase_uncertainty_ml(snr_db, freq_hz)`

Williams Eq. 9.7 gives RMS phase uncertainty σ_φ in radians as a function
of SNR:

```
σ_φ = 1 / sqrt(2 × SNR_linear)               [Eq. 9.7]
σ_d = σ_φ × (lane_width / 2π)                [Eq. 9.8]
```

where lane_width = c / f_hz.  The result σ_d [m] is the RMS range
uncertainty for one station.

In millilanes: σ_d [ml] = σ_φ × 1000 / (2π).

---

## Stage 8 — Station selection + WHDOP (Williams Eq. 9.12–9.16, Appendix K)

**Module:** `engine/whdop.cpp`
**Key functions:** `compute_whdop()`, `computeWHDOP()`

At each grid point, up to N stations are selected (N from `ReceiverModel::
min_stations`, typically 4–8) using slot-number rules from DTM-170 Appendix K:

1. Sort by SNR (descending).
2. Include master if any slave is selected.
3. Exclude stations below SNR threshold or beyond max_range_km.

The **Weighted HDOP (WHDOP)** then quantifies geometric dilution:

```
A = directional cosines matrix (from bearings to each station)
W = diag(SNR_i / sum(SNR))         [Eq. 9.13–9.14, normalised]
WHDOP = sqrt(trace((A W Aᵀ)⁻¹))   [Eq. 9.16, simplified 2D]
```

**Expected values:** For a four-transmitter network where all stations present
similar SNR, the normalised weights are approximately equal and the four
azimuths span the compass.  In this case `AWAᵀ ≈ ½I`, so
`WHDOP = √(trace(2I)) = 2.0`.  This is the theoretical ideal; WHDOP rises
above ~2 only where geometry or SNR balance degrades (fringe coverage, shadowing).

**Transmitter-foot singularity:** At a grid point coincident with a transmitter,
the nearby transmitter's SNR dominates by many orders of magnitude, collapsing
`AWAᵀ` to a near-rank-1 matrix whose determinant approaches zero and whose
inverse diverges.  The resulting very large WHDOP is mathematically correct;
no real receiver is located at a transmitter mast.

---

## Stage 9 — Repeatable accuracy

**Module:** `engine/whdop.cpp`

```
σ_p [m] = σ_d [m] × WHDOP
```

This is the RMS repeatable position uncertainty — the accuracy achievable
by a receiver that has been calibrated at the measurement point (i.e., the
ASF errors are known and corrected).

---

## Stage 10 — ASF / Absolute accuracy (Williams Eq. 11.1–11.10)

**Module:** `engine/asf.cpp`, `engine/monteath.cpp`

### Monteath terrain method (P2-05 / P4-01)

The Additional Secondary Factor (ASF) is the excess electrical path length
caused by imperfect ground conductivity and terrain undulations.  BANDPASS II
uses the **Monteath terrain method** (Monteath 1978):

1. Sample the terrain height profile along the great-circle path from
   transmitter to grid point at *n* equal intervals.
2. For each profile segment, compute the surface impedance Z_s from the
   ground constants (σ, ε_r) and the skin depth at the carrier frequency.
3. Integrate the surface-impedance perturbation to yield the ASF in
   metres; divide by lane_width/1000 to convert to millilanes.

The Monteath function is:

```
monteath_asf_ml(freq_hz, tx_lat, tx_lon, rx_lat, rx_lon,
                terrain_map, cond_map, nsamples)
```

The terrain profile is sampled via `TerrainMap::profile()`, which uses
GeographicLib's WGS84 geodesic to interpolate along the great-circle arc.

### Virtual Locator fix (Williams Eq. 9.9–9.12)

The Virtual Locator (VL) simulates the Mk4 Locator's position fix algorithm:

1. Compute the pseudorange from each selected transmitter to the grid point,
   including the Monteath ASF delay.
2. Solve the iterated 2D weighted least-squares (WLS) fix:

```
Δ(E, N) = (Aᵀ W A)⁻¹ Aᵀ W Δρ      [Eq. 9.9]
```

   Iterate up to 8 times or until convergence < 0.1 m.
3. The absolute accuracy is the Airy 1830 geodesic distance from the VL fix
   to the true grid point.

**Important:** The Mk4 Locator uses the Airy 1830 ellipsoid with the
Andoyer-Lambert formula.  BANDPASS II replicates this using GeographicLib
with `Geodesic(6377563.396, 1/299.3249646)` in all VL range computations.

### Pattern offset correction

When `scenario.pattern_offsets` is populated, a corrected absolute accuracy
layer is also computed:

```
corrected_asf = asf − po_correction_for_pattern
corrected_abs_accuracy = VL_fix(corrected_asf)
```

The delta layer shows improvement: `delta = abs_accuracy − corrected_abs_accuracy`.

---

## Stage 11 — Confidence factor

**Module:** `engine/asf.cpp`

A dimensionless confidence factor (0.0–1.0) is derived from the residuals
of the VL fix:

```
confidence = 1 / (1 + residual_rms / reference_residual)
```

High confidence (> 0.9) indicates the VL fix converged well with small
residuals.  Low confidence (< 0.5) indicates geometric instability or large
ASF variation across the selected stations.

---

## Stage 12 — Per-slot phase at receiver (single point)

**Module:** `engine/asf.cpp`
**Key function:** `computeAtPoint(lat, lon, scenario)`

This is not a grid operation.  For a placed receiver marker, BANDPASS II
computes four signal components from each active transmitter:

| Component | Description |
|---|---|
| f1+ | F1 upper sideband pseudorange, fractional phase, lane number, SNR |
| f1− | F1 lower sideband (same path; same ASF; opposite Doppler sign) |
| f2+ | F2 upper sideband |
| f2− | F2 lower sideband |

Pseudorange includes: geometric range (Airy ellipsoid) + Monteath ASF delay
+ SPO (synchronisation pulse offset, µs × c) + station delay (µs × c).

Phase is fractional cycles in [0, 1).  Lane number is integer wavelengths.

---

## Key constants and conventions

| Quantity | Value | Notes |
|---|---|---|
| c | 299,792,458 m/s | Speed of light in vacuum |
| F1 default | 146,437.5 Hz | Datatrak standard; configurable |
| F2 default | 131,250.0 Hz | Datatrak standard; configurable |
| Lane width F1 (default) | ≈ 2047.24 m | c / f1_hz |
| Lane width F2 (default) | ≈ 2284.13 m | c / f2_hz |
| 1 ml | 1/1000 lane | 1 ml(F1) ≈ 2.047 m at defaults |
| Ellipsoid (VL) | Airy 1830 | a = 6,377,563.396 m, 1/f = 299.3249646 |
| Ellipsoid (display/map) | WGS84 | GeographicLib default |

---

## References

- Williams (2004) — PhD thesis (primary reference for all equations)
- ITU-R P.368-9 — Ground-wave propagation curves
- ITU-R P.684-6 — Ionospheric propagation at LF/MF
- ITU-R P.372-17 — Radio noise
- Monteath (1978) — *Applications of the Electromagnetic Reciprocity Principle*,
  Pergamon Press
- DTM-170 — Datatrak Technical Manual, Rev. 170 (transmitter/locator firmware
  documentation; not publicly available)
