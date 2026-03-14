# BANDPASS II — Receiver Modelling Guide

## Overview

BANDPASS II models the Datatrak Mk4 Locator receiver.  The receiver
determines its position by measuring the phase of groundwave signals from
up to eight navigation transmitters on two carrier frequencies (F1 and F2).

Two receiver modes are available:

| Mode | Description |
|---|---|
| **Simple** | Fixed noise floor; no skywave contribution to noise |
| **Advanced** | ITU atmospheric noise + vehicle noise + skywave disturbance |

The mode is selected in the **Network Configuration** panel (View →
Network Configuration) and applies globally to the scenario.

---

## Receiver parameters

All receiver parameters are set via the **Parameter Editor** panel
(View → Parameter Editor), receiver tab.

| Parameter | Unit | Default | Description |
|---|---|---|---|
| Noise floor | dBµV/m | 14.0 | Minimum signal level for acquisition |
| Vehicle noise | dBµV/m | 27.0 | In-cab electrical noise contribution |
| Max range | km | 350.0 | Stations beyond this are excluded |
| Min stations | count | 4 | Minimum stations needed for a fix |
| Phase velocity | m/s | 299,892,718 | Speed used for range calculation |
| Ellipsoid | — | Airy 1830 | Range calculation ellipsoid |

### Noise floor

The noise floor is the effective minimum field strength [dBµV/m] that the
receiver can detect.  In **simple** mode this is the only noise contribution.
In **advanced** mode it is used as a floor below which ITU atmospheric noise
is not allowed to fall (i.e., it represents the receiver's internal noise).

### Vehicle noise

Empirical ignition and alternator noise level inside a typical vehicle cab.
Added in quadrature with atmospheric noise:

```
N_total = 10 log10(10^(N_atm/10) + 10^(N_veh/10)) dBµV/m
```

### Phase velocity

The Mk4 Locator uses a fixed phase velocity of 299,892,718 m/s (slightly
less than c in vacuum) to convert phase measurements to ranges.  This
value accounts for the average reduction in wave speed over typical ground
paths.  BANDPASS II uses this as the default; it can be changed if modelling
alternative receiver firmware.

### Ellipsoid

The Mk4 firmware computes ranges on the **Airy 1830** ellipsoid using the
Andoyer-Lambert formula.  BANDPASS II replicates this in the Virtual Locator
fix.  Changing to WGS84 shows the small absolute position bias (~10–50 m at
UK latitudes) introduced by the firmware's use of Airy rather than WGS84.

---

## Station selection

At each grid point (and at the placed receiver marker), BANDPASS II selects
the transmitters to use in the position fix according to the following rules
(DTM-170 Appendix K):

1. Compute SNR from each transmitter.
2. Exclude transmitters below the noise floor.
3. Exclude transmitters beyond `max_range_km`.
4. Sort remaining stations by SNR (descending).
5. Take the top `min_stations` to `8` stations, ensuring each pattern pair
   (slave + master) is either fully included or fully excluded.
6. If fewer than `min_stations` remain, the grid point is marked as
   outside coverage.

---

## Per-slot phase output

When a receiver marker is placed on the map, BANDPASS II computes:

| Output | Description |
|---|---|
| **Pseudorange** [m] | Geometric range + Monteath ASF delay + SPO + station delay |
| **Fractional phase** [0, 1) | Pseudorange mod one wavelength |
| **Lane number** [integer] | Complete wavelengths in pseudorange |
| **SNR** [dB] | Groundwave field strength vs. noise floor |
| **GDR** [dB] | Groundwave vs. disturbance (skywave + noise) |

All four signal components are shown: **F1+, F1−, F2+, F2−**.

The phase values update in real time as the receiver marker is dragged on
the map.  The Receiver Phase Table panel at the bottom of the window shows
the per-slot results.

---

## Simulator export

The **Export for Simulator** button (Receiver Phase Table panel) writes a
text block in the format expected by `datatrak_gen.h`:

```
# BANDPASS II receiver phase export
# Receiver: TL 27100 70700  (52.3247N, 0.1848W)
# Format: slot  f1+  f1-  f2+  f2-  snr_db  gdr_db
# Phases: integer millilanes 0-999 (slotPhaseOffset scale)
1   324  324  618  618   42.1  41.3
2   781  781  102  102   38.6  37.9
```

Phase values are in the range 0–999, representing thousandths of a lane.
These map directly to `DATATRAK_LF_CTX.slotPhaseOffset[]` in the signal
generator.

---

## Coverage criteria

A grid point is considered to have coverage when all of the following hold:

- At least `min_stations` transmitters are above the noise floor and within
  range.
- WHDOP ≤ some configurable threshold (default: 6.0; lower is better
  geometry).
- Repeatable accuracy ≤ some configurable threshold (default: 100 m RMS).

The **Coverage** layer (binary 0/1) shows the area satisfying all criteria.

---

## Accuracy layers

| Layer | Meaning |
|---|---|
| `repeatable` | RMS position uncertainty from phase noise alone |
| `absolute_accuracy` | RMS position error including ASF (uncorrected) |
| `absolute_accuracy_corrected` | ASF corrected using monitor-derived pattern offsets |
| `absolute_accuracy_delta` | Improvement from corrections (corrected − uncorrected, positive = better) |
| `whdop` | Weighted HDOP (geometric dilution factor) |
| `confidence` | VL fix convergence quality (0–1) |

---

## Typical expected accuracy

At UK Datatrak standard frequencies and geometry:

| Accuracy type | Typical value |
|---|---|
| Repeatable (1σ) | 10–40 m in good coverage |
| Absolute (uncorrected) | 50–500 m depending on conductivity path |
| Absolute (corrected, monitor calibrated) | 20–100 m |
| WHDOP (good geometry) | 1.5–4.0 |
