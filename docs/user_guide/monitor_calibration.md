# BANDPASS II — Monitor Station Calibration Guide

## Overview

Datatrak navigation accuracy depends critically on the **pattern offsets** (`po`)
stored in each locator's almanac.  These values are the ASF (Additional Secondary
Factor) corrections that compensate for the excess propagation delay caused by
imperfect ground conductivity and terrain.

BANDPASS II predicts the `po` values from the propagation model.  For maximum
accuracy, these predictions should be validated and refined using observations
from **monitor stations** — fixed receivers at precisely-surveyed positions that
continuously measure their own lane readings.

---

## Monitor station calibration workflow

```
1. Plan monitor siting (ASF gradient map)
2. Install monitor at surveyed position
3. Collect lane observations → monitor log CSV
4. Import log (File → Import → Monitor Station Log)
5. Review consistency diagnostic
6. Export corrected almanac (File → Export → Almanac Commands)
7. Deploy almanac to all transmitters
```

---

## Step 1: Plan monitor siting

Use the **asf_gradient** map layer (ml/km) to choose monitor locations:

- **High ASF gradient** → a small position error at the monitor maps to a
  large ASF correction.  Monitors here are most sensitive to propagation drift.
- **Low ASF** (absolute) → the monitor can serve as a reference point for
  absolute accuracy measurements.

For a single monitor, choose a location with high gradient on the path segment
most affected by mixed land/sea propagation or terrain variations.  For multiple
monitors, distribute them to cover different pattern pairs and conductivity zones.

---

## Step 2: Monitor log format

The monitor station exports a CSV log file with one row per observation period:

```csv
# Station: Huntingdon Monitor
# Lat: 52.3247
# Lon: -0.1848
# Format: slot1,slot2,f1plus_ml,f1minus_ml,f2plus_ml,f2minus_ml
2,1,+12,+9,+7,+11
3,1,-3,-5,+2,-1
```

- `slot1,slot2` — the pattern pair (slave slot, master slot)
- `f1plus_ml` — measured correction delta [ml] for F1+ channel
- `f1minus_ml` — measured correction delta for F1− channel
- `f2plus_ml` — measured correction delta for F2+ channel
- `f2minus_ml` — measured correction delta for F2− channel

Values are **deltas** (measured − predicted), not absolute po values.
Positive values mean the signal arrived later than predicted (more ASF
than the model computed).

Header comment lines beginning with `#` are optional but recommended.
Blank lines are ignored.  Malformed lines are skipped with a warning.

---

## Step 3: Import a monitor log

**File → Import → Monitor Station Log**

1. Select the CSV log file.
2. Choose which monitor station to attach the log to (or create a new one).
3. BANDPASS II merges the corrections into the scenario, applies them to the
   pattern offsets, and triggers a recompute.

After import:

- `scenario.pattern_offsets` are updated: `po_new = po_current + mean_delta`.
- The `absolute_accuracy_corrected` and `absolute_accuracy_delta` map layers
  are recomputed.
- If two or more monitors exist, a **consistency check** runs automatically.

---

## Step 4: Consistency diagnostic

When two or more monitor stations have corrections for the same pattern pair,
BANDPASS II checks for inconsistencies:

**Inconsistency threshold** (default 20 ml, configurable):
If two monitors disagree by more than this value on any pattern pair, a warning
is shown.  Inconsistency usually indicates one of:

- A survey position error at one monitor.
- A hardware fault at one monitor.
- A spatially-localised propagation anomaly between the two monitors.

**Diagnostic patterns:**

| Pattern | Likely cause |
|---|---|
| Uniform offset across all pattern pairs at all monitors | Wrong conductivity data (uniform scaling error) |
| Large correction for one pattern pair only | Localised path anomaly for that transmitter pair |
| Temporal variation in corrections | Atmospheric / seasonal change; model is a median |
| Single monitor diverging from all others | Survey error or hardware fault at that monitor |

The diagnostic summary is shown in the status bar and (for inconsistencies)
as a message box.  Detailed results are accessible via the export log.

---

## Step 5: Export corrected almanac

**File → Export → Almanac Commands (V7)** or **(V16)**

The exported `po` commands include the monitor-corrected values.  The export
header shows the source of each correction:

```
# Po commands — pattern offsets (millilanes)
# Corrections applied from 2 monitor station(s)
po 2,1 +549 +510 +610 +580
po 3,1 +482 +451 +588 +548
```

---

## Pattern offset reference modes

When pattern offsets have not yet been calibrated by monitors, BANDPASS II
can compute initial estimates from the propagation model alone.

**File → Export → Compute Pattern Offsets...**

Three modes:

### Mode 1 — Baseline midpoint (quick estimate)

Computes `po` at the geometric midpoint between each slave station and its
master.  No reference position needed.  Gives a physically reasonable starting
point that reduces commissioning time.

### Mode 2 — User-defined reference marker (primary method)

User enters one or more reference positions (surveyed locations or map clicks).
BANDPASS II computes the Monteath ASF at each marker and averages the results.
The RMS spread across markers is shown as a quality indicator — high spread
means the propagation conditions vary significantly across the markers and a
single `po` value is a poor representation.

**Primary use case:** a single monitor station at a surveyed position before
sufficient operational data has been collected.  Mode 2 gives a model-based
initial `po` using the known monitor position; the monitor's measured deltas
then refine it over time via the import workflow above.

### Mode 3 — ASF analysis map (planning aid only)

Not a direct `po` computation.  Produces two map layers:

- **ASF value map**: absolute ASF at each grid point for each pattern pair.
  The minimum-ASF location is the most stable reference point.
- **ASF gradient map**: spatial rate of change [ml/km].  High gradient zones
  are the most sensitive positions for monitor siting.

Mode 3 is accessed via the map layer selector, not the pattern offset dialog.

---

## Direct vs offset patterns

**Offset patterns** (slave/master pairs) are corrected by `po` values.

**Direct patterns** (master-only slots) are corrected by transmitter phase
adjustment during commissioning — not by `po`.  BANDPASS II does not generate
transmitter phase commands.  The absolute accuracy map will show residual errors
for direct patterns until the transmitters are physically calibrated.

Do not apply monitor-derived `po` corrections to direct patterns.

---

## Multi-monitor consistency matrix example

For a network with monitors at A, B, and C measuring pattern pair 2,1:

| Monitor | Correction F1+ | Correction F2+ |
|---|---|---|
| A (Huntingdon) | +12 ml | +7 ml |
| B (Bedford) | +14 ml | +8 ml |
| C (Cambridge) | +11 ml | +6 ml |

Mean applied: F1+ = +12, F2+ = +7.  Max disagreement: 3 ml (< 20 ml threshold).
Consistent — apply mean correction.

If monitor B reported +35 ml (F1+), the threshold would be exceeded and
BANDPASS II would flag B as a suspect monitor for this pattern pair.
