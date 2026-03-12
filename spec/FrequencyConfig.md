# Feature Spec: Configurable F1/F2 Frequencies

## Purpose

Allow F1 and F2 carrier frequencies to be configured per-scenario, enabling
BANDPASS II to model LF navigation networks on frequency pairs other than the
Datatrak standard (e.g. amateur radio LF allocations at 135.7 kHz, 137 kHz,
or other LF band allocations between 30 and 300 kHz).

## Defaults

| Parameter | Default value |
|---|---|
| F1 | 146.4375 kHz (Datatrak standard) |
| F2 | 131.2500 kHz (Datatrak standard) |

## Constraints

- F1 and F2 are **fully independent** — no enforced ratio or offset.
- Both must be within **30–300 kHz** (full LF band). Values outside this range
  are rejected with a validation error; the field is highlighted red and the
  scenario cannot be saved or computed until corrected.
- F1 may equal F2 — this produces a single-frequency network (Mk1-style);
  BANDPASS II models it correctly though the interlaced mode output will be
  degenerate (both grids identical). A warning is shown but it is not an error.
- No lower-bound constraint between F1 and F2 — either may be higher.

## Where frequency is set

1. **Network Configuration panel** (primary UI) — a dedicated dockable panel
   accessible via View → Network Configuration. Contains the settings that apply
   to the network as a whole: F1/F2 frequencies, mode, grid resolution, receiver
   model, datum transform. Frequency is **not** in the per-transmitter/receiver
   `ParamEditor` panel and is **not** available from any command-line interface.

   Field details: "F1 (kHz)" and "F2 (kHz)", floating-point, 4 decimal places,
   step 0.0001 kHz. Validation fires immediately on input (red highlight on
   invalid). Tooltip: "F1 carrier frequency. Default: 146.4375 kHz (Datatrak
   standard)". On valid change: marks scenario dirty, triggers full recompute
   after 500 ms debounce. The status bar ml→m readout updates immediately.

2. **TOML scenario file** — `[frequencies]` table:
   ```toml
   [frequencies]
   f1_khz = 146.4375
   f2_khz = 131.2500
   ```
   If absent, defaults are used and written on next save.

Changing either frequency in the UI marks the scenario dirty and triggers a
full recompute (same path as any other parameter change).

## Effects on the propagation model

All four propagation effects recalculate from the new frequencies:

### 1. Lane widths
Computed directly from the speed of light and the configured frequency.

```
lane_width_m = c / f_hz
```

where `c = 299,792,458 m/s` (exact, SI definition).

- Default f1: 299792458 / 146437.5 = **2047.14 m**
- Default f2: 299792458 / 131250.0 = **2284.59 m**

Lane widths are recomputed on every scenario load and frequency change. All
phase-to-distance conversions, millilane-to-metres display values, and per-slot
phase calculations use the computed values, never hardcoded constants.

### 2. Groundwave field strength (ITU P.368)

The ITU P.368 groundwave attenuation function is frequency-dependent.
Conductivity loss, skin depth, and the numerical distance parameter `p` all
vary with frequency. The groundwave curves must be re-evaluated at the actual
configured frequency for each of F1 and F2 separately.

Implementation note: the P.368 numerical distance is:

```
p = (pi * d) / (lambda * sqrt(epsilon_r_eff))
```

where `lambda = c / f`. A change in frequency changes `p` and therefore the
entire attenuation curve shape. There is no approximation that can be borrowed
from the Datatrak-standard curves.

### 3. Skywave field strength (ITU P.684)

The P.684 median night-time sky-wave field strength is frequency-dependent
through:
- The reference field strength formula (includes a `20 log10(f)` term)
- The geomagnetic coupling factor (varies with frequency through the ionospheric
  reflection coefficient)

Both ITU P.684 computations must use the configured frequency for each carrier.

### 4. Atmospheric noise floor (ITU P.372)

The Fam (atmospheric noise figure) from ITU P.372 is a function of frequency.
The published tables cover the LF/MF band with sufficient resolution. Bilinear
interpolation at the configured frequency is already required for mid-table
values; no special handling is needed here beyond ensuring the interpolation
receives the configured frequency rather than a hardcoded constant.

## Effects on almanac export

Lane widths are frequency-dependent. The almanac export header must reflect
the configured frequencies and their derived lane widths.

### Export header changes

The export header block (see §16.6 of the architecture document) is updated to
include:

```
# F1: 146.4375 kHz  lane width: 2047.14 m  (1 ml = 2.047 m)
# F2: 131.2500 kHz  lane width: 2284.59 m  (1 ml = 2.285 m)
```

If either frequency differs from the Datatrak standard (146.4375 / 131.25 kHz),
the header also includes:

```
# WARNING: Non-standard frequencies. Almanac po values are in millilanes of the
#          configured frequencies, not Datatrak-standard millilanes. A receiver
#          running Datatrak-standard firmware will misinterpret these values.
```

### Pattern offset computation

The po values (millilanes) are computed as:

```
po_ml = round(asf_lanes * 1000)
```

where `asf_lanes = asf_metres / lane_width_m`. Since `lane_width_m` is
frequency-dependent, po values computed at non-standard frequencies are in
millilanes of those frequencies. This is correct for a receiver running firmware
configured for those frequencies, but not for standard Datatrak firmware.

The millilane-to-metres display in the UI (status bar, export header, monitor
calibration panel) always shows the current frequency's lane width:

```
1 ml (f1) = 2.047 m    [updates live when frequency is changed]
1 ml (f2) = 2.285 m
```

## Effects on simulator phase export

The simulator export (per-slot phase values for `datatrak_gen.h`) is unaffected
in format — phase values are 0–999 representing fractional phase within one
cycle of the carrier, which is dimensionless. However the header comment in
the export block is updated to show the configured frequency:

```
# F1: 146.4375 kHz  F2: 131.2500 kHz
# Phase values: fractional cycle * 1000 (0-999)
```

The signal generator (`datatrak_gen.c`) operates on whatever carrier frequency
its hardware is configured for; BANDPASS II does not need to pass frequency
information to it.

## UI details

### Network Configuration panel

The frequency fields are in the Network Configuration panel (View → Network
Configuration), not in `ParamEditor`. The panel groups all whole-network
settings together: F1/F2 frequencies, mode, grid resolution, receiver model,
datum transform.

- Input type: floating-point, 4 decimal places shown, step 0.0001 kHz
- Validation: 30.0000 ≤ value ≤ 300.0000 kHz; field turns red on invalid input
- Tooltip: "F1 carrier frequency. Default: 146.4375 kHz (Datatrak standard)"
- On change: marks scenario dirty, enqueues recompute after 500 ms debounce
  (same debounce used for all parameter changes)

The map status bar millilane readout updates immediately on frequency change
(does not wait for recompute).

### Scenario dirty / recompute

Changing either frequency clears all computed grid arrays and triggers a full
pipeline recompute. There is no partial recompute path — frequency affects all
eleven pipeline stages.

## Implementation location

All frequency values in the codebase must come from `Scenario::frequencies.f1_hz`
and `Scenario::frequencies.f2_hz` (stored internally in Hz, displayed in kHz).
No pipeline function or calculation may use a hardcoded frequency value.

Specifically, the following existing references in `datatrak_gen.h` (lane width
comments) and in any existing physics stubs must be updated:

- Any comment or constant referencing "146 kHz", "131 kHz", "2047 m", "2286 m"
  must become a computed value or a reference to the scenario frequencies.
- `Scenario::frequencies.lane_width_f1_m` and `lane_width_f2_m` are computed
  fields (not stored in TOML, derived on load and on frequency change).

## Validation at compute time

Before the pipeline runs, the compute manager validates:

```cpp
if (scenario.frequencies.f1_hz < 30e3 || scenario.frequencies.f1_hz > 300e3)
    return error("F1 frequency out of range (30–300 kHz)");
if (scenario.frequencies.f2_hz < 30e3 || scenario.frequencies.f2_hz > 300e3)
    return error("F2 frequency out of range (30–300 kHz)");
```

The parameter editor prevents out-of-range values from being entered, but the
compute-time check is a safety net for scenarios loaded from hand-edited TOML.

## Test cases

| Test | Expected |
|---|---|
| Default frequencies → lane widths | f1: 2047.14 m, f2: 2284.59 m (±0.01 m) |
| f1 = 137.0 kHz → lane width | 299792458 / 137000 = 2187.54 m |
| f1 = 29.9999 kHz → validation | Rejected, error message |
| f1 = 300.0001 kHz → validation | Rejected, error message |
| f1 = f2 = 137.0 kHz → mode | Accepted with warning, not error |
| TOML round-trip at non-standard frequency | Loaded frequency matches saved frequency exactly |
| Almanac export at non-standard frequency | Header shows correct lane width + non-standard warning |
| Groundwave at 137 kHz vs 146 kHz | Different attenuation curves, numerically validated against ITU P.368 |
