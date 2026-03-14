# BANDPASS II — Claude Code Development Brief

## What this project is

BANDPASS II is a C++17/wxWidgets desktop application for planning and analysing
Datatrak-type LF radio navigation networks. It predicts coverage and positioning
accuracy using the propagation physics model from:

  Williams (2004), "Prediction of the Coverage and Performance of the Datatrak
  Low-Frequency Tracking System", University of Wales Bangor PhD thesis.

It is also a teaching tool: map layers can be toggled to show how each physics
stage contributes to the final result.

F1 and F2 carrier frequencies are fully configurable per-scenario, enabling
modelling of alternative LF navigation networks (e.g. amateur radio allocations).
Datatrak standard frequencies (146.4375 kHz / 131.25 kHz) are the defaults.

There is a related signal generator (datatrak_gen.c / datatrak_gen.h, existing
C code) that generates the raw 1ms-sample-per-slot phase/amplitude buffers a
Datatrak receiver sees. BANDPASS II computes the per-slot phase values that feed
into that generator.

## Licence

GPLv3.

---

## Technology decisions (all final, do not re-litigate)

| Concern | Choice |
|---|---|
| Language | C++17; C++20 only where it clearly improves readability (std::span, designated initialisers, std::format) |
| UI | wxWidgets ≥ 3.2, native C++ |
| Map | wxWebView embedding Leaflet.js + OpenStreetMap tiles (vendored, no CDN) |
| Map tile cache | SQLite MBTiles, 30-day TTL, local .mbtiles fallback |
| Build | CMake 3.20+; system packages on Linux/macOS; vcpkg on Windows |
| Config | TOML (toml++ single-header) |
| Licence | GPLv3 |
| Coordinates | WGS84 LatLon + OSGB36 National Grid (primary Datatrak space) |
| Threading | Single dedicated worker thread + message queue (NOT std::async) |
| Testing | Catch2 v3 |

### Why not std::async

`std::async` has destructor-blocks-on-scope-exit semantics that cause hard-to-
debug UI freezes when a future goes out of scope. The worker thread pattern with
explicit ownership transfer is used instead. See §Threading below.

---

## Repository layout

```
bandpass2/
├── CMakeLists.txt
├── cmake/
│   ├── deps_linux.cmake
│   ├── deps_macos.cmake
│   └── deps_windows.cmake
├── vcpkg.json                  # tomlplusplus, nlohmann-json, geographiclib, catch2
├── LICENSE                     # GPLv3
├── .github/workflows/
│   ├── build_linux.yml
│   ├── build_macos.yml
│   └── build_windows.yml
├── Jenkinsfile
├── docker/
│   └── Dockerfile.build        # ubuntu:24.04 + system deps
├── data/
│   └── zones/
│       └── uk_32zone.geojson   # 32-zone UK polygon grid (DTM-170 Fig 7.3)
├── docs/
│   ├── physics/                # Annotated Williams equation derivations
│   └── user_guide/
│       ├── receiver_modelling.md
│       ├── coordinate_systems.md
│       └── monitor_calibration.md
├── src/
│   ├── main.cpp
│   ├── ui/
│   │   ├── MainFrame.*
│   │   ├── MapPanel.*          # wxWebView host + JS bridge
│   │   ├── NetworkConfigPanel.* # F1/F2 freq, mode, grid res, datum — whole-network settings
│   │   ├── ParamEditor.*       # Per-transmitter / per-receiver forms
│   │   ├── LayerPanel.*        # Map layer toggles
│   │   ├── ReceiverPanel.*     # Per-slot phase table + simulator export
│   │   └── ResultsPanel.*      # Field-strength vs range plots
│   ├── engine/
│   │   ├── grid.*              # ComputeGrid, WGS84/Airy utils
│   │   ├── groundwave.*        # ITU P.368, Millington, Monteath
│   │   ├── skywave.*           # ITU P.684
│   │   ├── noise.*             # ITU P.372
│   │   ├── snr.*               # SNR, GDR
│   │   ├── phase_error.*       # Williams Eq. 9.7-9.8
│   │   ├── station_select.*    # Nearest-N, slot rules, weight matrix
│   │   ├── whdop.*             # Directional cosines, WHDOP
│   │   ├── repeatable.*        # sigma_p = sigma_d * WHDOP
│   │   ├── asf.*               # Monteath SF delay, Virtual Locator LS, per-point phase
│   │   └── compute_manager.*   # Worker thread, queue, cancellation
│   ├── model/
│   │   ├── Scenario.*          # Network scenario struct
│   │   ├── Transmitter.*
│   │   ├── ReceiverModel.*
│   │   ├── SlotPhaseResult.*   # Output of per-slot phase computation
│   │   ├── MonitorStation.*    # Surveyed monitor station + correction data
│   │   └── toml_io.*
│   ├── almanac/
│   │   ├── AlmanacExport.*     # Sg/Stxs/Zp/Po text command generation
│   │   ├── ZonePatterns.*      # 32-zone UK grid, WHDOP-based Zp computation
│   │   └── MonitorCalib.*      # Correction import, consistency check, diagnostics
│   ├── coords/
│   │   ├── CoordSystem.*       # Enum + format auto-detection
│   │   ├── Osgb.*              # WGS84 <-> OSGB36 (Helmert + OSTN15)
│   │   └── NationalGrid.*      # Easting/Northing <-> LatLon, grid ref formatting
│   └── web/
│       ├── map.html
│       ├── leaflet/            # Vendored Leaflet.js ≥ 1.9
│       └── bridge.js
├── tests/
│   ├── engine/
│   ├── coords/                 # OSGB regression tests vs known OS points
│   └── almanac/
└── tools/
    ├── itu_p832_import.py
    ├── ostn15_download.py
    └── srtm_download.py
```

---

## Dependencies

### System packages (Linux)
```
libwxgtk3.2-dev libwxgtk-webview3.2-dev libgdal-dev libsqlite3-dev
libcurl4-openssl-dev
```

### System packages (macOS)
```
brew install wxwidgets gdal sqlite curl
```

### vcpkg (all platforms — vcpkg.json manifest)
- `tomlplusplus`
- `nlohmann-json`
- `geographiclib`
- `catch2`

### Windows only (via vcpkg instead of system)
- `wxwidgets[webview]`
- `gdal`
- `sqlite3`
- `curl`

### Vendored in-repo
- `src/web/leaflet/` — Leaflet.js ≥ 1.9 + Leaflet.MBTiles plugin

---

## Frequency configuration

F1 and F2 are stored in `Scenario::frequencies` as `double f1_hz` and
`double f2_hz` (Hz, not kHz). Two derived fields are computed (never stored in
TOML) on load and on any frequency change:

```cpp
struct Frequencies {
    double f1_hz = 146437.5;   // Datatrak default
    double f2_hz = 131250.0;   // Datatrak default

    // Derived — recomputed whenever f1_hz or f2_hz changes
    double lane_width_f1_m = 0.0;   // c / f1_hz
    double lane_width_f2_m = 0.0;   // c / f2_hz

    void recompute() {
        constexpr double C = 299'792'458.0;
        lane_width_f1_m = C / f1_hz;
        lane_width_f2_m = C / f2_hz;
    }
};
```

**Rule: every pipeline function that converts phase to distance or computes a
lane count must take `const Frequencies&` as a parameter, not hardcode any
frequency or lane width constant.**

TOML representation (kHz for human readability; converted to Hz on load):
```toml
[frequencies]
f1_khz = 146.4375
f2_khz = 131.2500
```

Validation: 30 kHz ≤ f ≤ 300 kHz (hard limits, rejected with error). f1 == f2
is allowed with a warning. Changing either frequency in the UI triggers a full
pipeline recompute (no partial recompute path — frequency affects all 11 stages).

### UI location

Frequency is set in a dedicated **Network Configuration panel** (not the
transmitter/receiver parameter editor). This panel covers the settings that
apply to the network as a whole rather than to individual stations:

- F1 frequency (kHz) — floating-point field, 4 decimal places, step 0.0001
- F2 frequency (kHz) — as above
- Mode (8-slot / interlaced)
- Grid resolution (km)
- Receiver model (simple / advanced)
- Datum transform (Helmert / OSTN15)

The Network Configuration panel is a separate dockable panel accessible via
View → Network Configuration. It is distinct from `ParamEditor` (which handles
per-transmitter and per-receiver parameters). Frequency is **not** accessible
from `ParamEditor` or any command-line interface — UI and TOML only.

On change, frequency fields validate immediately (red highlight if out of range).
A full pipeline recompute is triggered after a 500 ms debounce. The status bar
millilane readout updates immediately without waiting for recompute.

### Almanac export at non-standard frequencies

The export header shows computed lane widths and a non-standard warning:

```
# F1: 137.0000 kHz  lane width: 2187.54 m  (1 ml = 2.188 m)
# F2: 137.0000 kHz  lane width: 2187.54 m  (1 ml = 2.188 m)
# WARNING: Non-standard frequencies. po values are in millilanes of the
#          configured frequencies, not Datatrak-standard millilanes.
```

The po values are in millilanes of the *configured* frequencies. This is correct
for a receiver running firmware at those frequencies; it is noted in the header.

---

## Threading architecture

**Rule: no shared mutable state crosses the thread boundary.**

```cpp
// The UI thread posts a snapshot — immutable, ref-counted, safe
struct ComputeRequest {
    std::shared_ptr<const Scenario> scenario;
    uint64_t                        request_id;  // monotonically increasing
};

// The worker posts results back via wxQueueEvent
struct ComputeResult {
    uint64_t                        request_id;
    std::shared_ptr<const GridData> data;
    std::string                     error;       // empty on success
};
```

Worker loop pattern:
```cpp
void WorkerThread::run() {
    while (true) {
        auto req = queue_.wait_pop();
        if (req.is_shutdown()) break;
        if (req.request_id < current_id_) continue;  // superseded, discard
        auto result = pipeline_.compute(*req.scenario, cancel_flag_);
        if (!cancel_flag_.load())
            wxQueueEvent(frame_, new ComputeResultEvent(result));
    }
}
```

UI thread triggers recompute:
```cpp
void MainFrame::onParameterChanged() {
    cancel_flag_.store(true);
    auto snap = std::make_shared<const Scenario>(current_scenario_);  // copy
    cancel_flag_.store(false);
    worker_.post(ComputeRequest{ snap, ++request_counter_ });
}
```

**Every top-level pipeline function must accept `const std::atomic<bool>& cancel`
as its last parameter and check it at each transmitter loop boundary (not per
grid point — that is too frequent).**

---

## Coordinate systems

Three systems are supported. Internal storage is always WGS84.

| System | Description |
|---|---|
| `WGS84_LatLon` | Decimal degrees. Used for all internal computation and Leaflet map. |
| `OSGB36_LatLon` | Airy 1830 ellipsoid. Intermediate form only; not entered directly. |
| `OSGB36_NationalGrid` | Easting/Northing in metres. **Primary Datatrak engineering coordinate space.** |

### Datum transforms
- **Helmert 7-parameter** (built-in, ±5 m): no data files, always available.
- **OSTN15** (±0.1 m): requires `tools/ostn15_download.py` to fetch the OS grid
  file. Falls back to Helmert if absent.

Use **GeographicLib** for ellipsoid math.

### Important: Locator firmware uses Airy 1830
The Mk4 Locator firmware computes ranges using the Airy 1830 ellipsoid with the
Andoyer-Lambert formula (not WGS84). The Virtual Locator in `asf.cpp` must
replicate this: range computations use Airy ellipsoid, display uses WGS84, datum
shift is applied transparently at the boundary.

### Coordinate entry auto-detection
All coordinate entry fields accept either system. Format is auto-detected:
- `TL 271 707` → OSGB National Grid (grid reference)
- `271000 707000` → OSGB National Grid (raw E/N)
- `52.3247 -0.1848` → WGS84 decimal degrees

The map status bar shows cursor position in both systems simultaneously.

---

## Physics pipeline

Eleven stages produce successive GridArrays; each feeds the next. The pipeline
runs on the worker thread. Stage 12 is a single-point computation for the
virtual receiver, not a grid operation.

| # | Stage | C++ module | Key function | Williams ref |
|---|---|---|---|---|
| 1 | Groundwave field strength | `groundwave.cpp` | `computeGroundwave()` | ITU P.368 + Millington + Monteath |
| 2 | Skywave field strength | `skywave.cpp` | `computeSkywave()` | ITU P.684 |
| 3 | Atmospheric noise | `noise.cpp` | `computeAtmNoise()` | ITU P.372 |
| 4 | Vehicle noise | `noise.cpp` | `vehicle_noise_dbuvm()` | Empirical |
| 5 | SNR per station | `snr.cpp` | `computeSNR()` | — |
| 6 | SGR / GDR | `snr.cpp` | `computeSNR()` | — |
| 7 | Phase/range uncertainty | `snr.cpp` | `phase_uncertainty_ml()` | Eq. 9.7–9.8 |
| 8 | Station selection + WHDOP | `whdop.cpp` | `compute_whdop()`, `computeWHDOP()` | Eq. 9.12–9.16, App. K |
| 9 | Repeatable accuracy | `whdop.cpp` | `computeWHDOP()` | σ_p = σ_d × WHDOP |
| 10 | ASF / absolute accuracy | `asf.cpp` | `computeASF()`, `virtual_locator_error_m()` | Eq. 11.1–11.10 |
| 11 | Confidence factor | `asf.cpp` | `computeASF()` (inline) | Residues from VL fix |
| 12 | Per-slot phase at receiver | `asf.cpp` | `computeAtPoint()` | Stages 1+10 at placed point |

### Key Williams equations → actual functions

```
Eq. 9.7–9.8   phase/range uncertainty      → snr.cpp::phase_uncertainty_ml()
Eq. 9.9       weighted least-squares fix   → asf.cpp::virtual_locator_error_m()  ← simplified; P4-02 to replace
Eq. 9.12      directional cosines matrix A → whdop.cpp::compute_whdop() (inline)
Eq. 9.13      weight matrix W              → whdop.cpp::compute_whdop() (inline)
Eq. 9.14      SNR-based weighting          → whdop.cpp::compute_whdop() (inline)
Eq. 9.16      weighted HDOP               → whdop.cpp::compute_whdop()
Eq. 11.1      additional delay             → asf.cpp::asf_single_ml()           ← approximation; P4-01 to replace
Eq. 11.5–11.10 link bias chain            → asf.cpp::computeASF() (partial)
Appendix J    Helmert transform           → coords/Osgb.cpp::wgs84_to_osgb36()
Appendix K    slot selection rules        → whdop.cpp::compute_whdop()
```

---

## Virtual receiver — per-slot phase output

A receiver marker can be placed on the map. For each active transmission slot,
`asf.cpp::computeAtPoint()` computes:

- **Pseudorange** [m]: primary propagation delay + Monteath SF delay
- **Fractional phase** [cycles 0.0–1.0]: pseudorange mod one wavelength
- **Lane number** [integer]: complete wavelengths in pseudorange
- **SNR** [dB]: groundwave field strength vs noise floor at receiver point
- **GDR** [dB]: including skywave

All four signal components: **f1+, f1−, f2+, f2−**.

This runs on the worker thread as a `computeAtPoint()` call (not a grid
operation). It completes fast enough to update in real time as the receiver
marker is dragged.

### Simulator export

The "Export for simulator" button writes a text block for serial input to the
signal generator. The format matches the `DATATRAK_LF_CTX.slotPhaseOffset[]`
field in `datatrak_gen.h`:

```
# BANDPASS II receiver phase export
# Receiver: TL 27100 70700  (52.3247N, 0.1848W)
# Format: slot  f1+_phase  f1-_phase  f2+_phase  f2-_phase  snr_db  gdr_db
# Phases: fractional cycles * 1000 (0-999), same scale as datatrak_gen slotPhaseOffset
1   324  324  618  618   42.1  41.3
2   781  781  102  102   38.6  37.9
3   153  153  ...  ...   35.2  34.6
```

The integer phase values (0–999) map directly to `slotPhaseOffset[]` in the
existing signal generator.

---

## Almanac configuration export

BANDPASS II can derive and export the four almanac tables as text commands for
direct serial entry into a station or locator terminal (V7 or V16 firmware
format). It does **not** generate binary almanac streams — the receiving
firmware encodes text commands internally.

### The four tables

| Command | What it is | Computability |
|---|---|---|
| `Sg N E N` | Station grid: OSGB easting/northing per station | Exact — direct from scenario |
| `Stxs slot addr` | Slot→station assignment | Exact — direct from scenario |
| `Zp zone set pat,pat` | Zone patterns: best pattern pairs per zone (Mk1 locators) | High confidence — WHDOP-ranked |
| `po pat,pat ml ml ml ml` | Pattern offsets: ASF correction per pattern pair per component | Model prediction — must be refined by calibration |

### Units

**Millilanes (ml)**: 1 ml = 1/1000 lane.  
- 1 lane (f1, 146.4375 kHz) ≈ 2047 m → 1 ml ≈ 2.0 m  
- 1 lane (f2, 131.25 kHz) ≈ 2286 m → 1 ml ≈ 2.3 m

### Pattern offset reference modes

The `po` value is the modelled ASF at a chosen reference point, in ml. Three
modes (UI: export dialogue):

1. **Baseline midpoint** — geometric midpoint between the two station positions.
   Quick estimate, no user input.

2. **User-defined marker** (primary) — user places one or more reference markers
   (OSGB coordinates or map click). Multiple markers → least-squares composite.
   Primary use: surveyed monitor station position. RMS residual shown per marker.

3. **ASF analysis / monitor siting** — produces two map layers:
   - *ASF value map*: absolute ASF per grid point per pattern pair. Minimum ASF
     point = most stable accuracy reference location for a monitor station.
   - *ASF gradient magnitude map*: spatial rate of change (ml/km). High-gradient
     zones = most sensitive location for a monitor station to detect propagation
     drift early.
   Mode 3 does not directly set `po` values — it is a planning aid for choosing
   monitor station locations.

### V7 and V16 text formats

```
# V7 format (DTM-170, unsigned millilanes 0-999):
po 8,2 537 501 603 569

# V16 format (signed):
po 8,2 +537 +501 +603 +569
```

BANDPASS II always generates absolute values. The V16 incremental form
(`+N` adds to current value) is not generated. If a computed po is negative
(rare — sea paths), clip to 0 and flag in export log.

### Zone pattern computation

Uses the bundled 32-zone UK polygon grid (`data/zones/uk_32zone.geojson`),
derived from DTM-170 Figure 7.3. For each zone centroid:
1. Filter pattern pairs by SNR threshold (both stations must be above threshold)
2. Rank by WHDOP (lowest = best geometry)
3. Assign top three as sets 1–3; set 4 = `0,0` if fewer than four viable pairs
4. Flag zones with fewer than three viable patterns as coverage gaps

### Monitor station calibration loop

Monitor stations at surveyed positions measure observed lane readings and report
correction deltas. BANDPASS II imports this data to:

1. Update stored `po` values: `po_new = po_current + delta_measured`
2. Re-run absolute accuracy with corrected values
3. Overlay corrected vs predicted as a delta-error map

**Multi-monitor consistency check**: if two monitors give corrections for the
same pattern pair differing by > 20 ml (configurable), flag as inconsistent.

**Model vs measurement diagnostic** — patterns of divergence:
- Uniform offset across all pairs → wrong conductivity data
- Specific pair only → localised path anomaly between those stations
- Temporal variation → atmospheric/seasonal; model is a median prediction
- Single-monitor divergence → suspect that monitor's survey position or hardware

### Direct vs offset patterns

Direct patterns (master-slave pair) are corrected by **transmitter phase
adjustment**, not by `po`. BANDPASS II does not generate transmitter phase
commands. The absolute accuracy map will show non-zero residuals for direct
patterns until the transmitters are physically calibrated during commissioning.

---

## TOML scenario file format

Internal storage is always WGS84. `display_crs` controls which coordinate
system is shown in the UI.

```toml
[scenario]
name         = "UK Datatrak baseline"
created      = 2025-01-01
display_crs  = "osgb_ng"    # wgs84 | osgb_ng

[grid]
lat_min      = 49.5
lat_max      = 59.5
lon_min      = -7.0
lon_max      = 2.5
resolution_km = 1.0

[frequencies]
f1_khz       = 146.4375
f2_khz       = 131.2500

[receiver]
mode                  = "advanced"    # simple | advanced
noise_floor_dbuvpm    = 14.0
vehicle_noise_dbuvpm  = 27.0
max_range_km          = 350.0
min_stations          = 4
vp_ms                 = 299892718
ellipsoid             = "airy1830"    # airy1830 | wgs84

[[transmitters]]
name             = "Huntingdon"
lat              = 52.3247
lon              = -0.1848
osgb_easting     = 513054    # informational — computed from lat/lon
osgb_northing    = 262453
power_w          = 40.0
height_m         = 50.0
slot             = 1
is_master        = true
spo_us           = 0.0
station_delay_us = 0.0

[[transmitters]]
name             = "Selsey"
lat              = 50.7300
lon              = -0.7900
power_w          = 40.0
height_m         = 50.0
slot             = 2
master_slot      = 1
spo_us           = 0.0
station_delay_us = 0.19

[[monitor_stations]]
name             = "Huntingdon Monitor"
lat              = 52.3247
lon              = -0.1848
osgb_easting     = 513054
osgb_northing    = 262453
# Per-pattern-pair correction deltas (millilanes)
# Populated by File -> Import -> Monitor Station Log
# [[monitor_stations.corrections]]
# pattern = "3,1"
# f1plus_ml = 12
# f1minus_ml = 9
# f2plus_ml = 7
# f2minus_ml = 11

[[pattern_offsets]]
# Populated by almanac export / monitor calibration
# pattern = "8,2"
# f1plus_ml  = 537
# f1minus_ml = 501
# f2plus_ml  = 603
# f2minus_ml = 569

[conductivity]
source = "itu_p832"    # builtin | itu_p832 | bgs | /path/to/file.tif

[terrain]
source = "srtm"        # flat | srtm | /path/to/file.tif

[datum]
transform = "helmert"  # helmert | ostn15

[output]
layers = ["groundwave","snr","gdr","whdop","repeatable","asf","confidence"]
```

---

## Map layers

Each layer is a GridArray pushed to Leaflet as a GeoJSON colour ramp.

| Layer key | Description |
|---|---|
| `groundwave` | Groundwave field strength [dBµV/m] per transmitter |
| `skywave` | Median night-time skywave field strength |
| `atm_noise` | Atmospheric noise floor |
| `snr` | SNR per transmitter |
| `sgr` | Skywave-to-groundwave ratio |
| `gdr` | Groundwave-to-disturbance ratio |
| `whdop` | Weighted HDOP |
| `repeatable` | Repeatable accuracy [m, 1-sigma] |
| `asf` | ASF systematic delay |
| `asf_gradient` | ASF spatial gradient magnitude [ml/km] — monitor siting aid |
| `absolute_accuracy` | Absolute position error [m] |
| `absolute_accuracy_corrected` | Absolute accuracy with monitor corrections applied |
| `absolute_accuracy_delta` | Difference between corrected and predicted |
| `confidence` | Confidence factor |
| `coverage` | Binary: all criteria met |

---

## wxWebView JS bridge

**C++ → JS** (push layer data, update receiver marker):
```cpp
panel_->RunScript(wxString::Format(
    "updateLayer('%s', %s);", layerName, geojsonString));
```

**JS → C++** (transmitter placement, receiver drag):
```js
window.chrome.webview.postMessage(JSON.stringify({
    type: 'transmitter_placed',
    lat: latlng.lat,
    lon: latlng.lng
}));
```

The bridge uses `wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED`. JSON encoded via
nlohmann/json. The bridge must be smoke-tested on all three platforms in CI —
the backend (webkit2gtk / WebKit / WebView2) differs but the wxWidgets API is
identical.

---

## Output formats

| Format | Trigger |
|---|---|
| PNG / SVG | File → Export Map |
| GeoTIFF | File → Export Layers (GDAL, WGS84, single-band per layer) |
| CSV | Any GridArray as lat, lon, value |
| HTML report | File → Export Report (self-contained, scenario params + layer images + stats) |
| Almanac text (V7/V16) | File → Export → Almanac Commands |
| Simulator phase export | ReceiverPanel → Export for Simulator |

---

## Build system

### CMakeLists.txt structure

Platform detection delegates to `cmake/deps_*.cmake`:

```cmake
cmake_minimum_required(VERSION 3.20)
project(bandpass2 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    include(cmake/deps_linux.cmake)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    include(cmake/deps_macos.cmake)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    include(cmake/deps_windows.cmake)
endif()

find_package(tomlplusplus  CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(GeographicLib CONFIG REQUIRED)
find_package(Catch2        CONFIG REQUIRED)
```

### GitHub Actions

Three workflow files: `build_linux.yml`, `build_macos.yml`, `build_windows.yml`.
Each: checkout → install deps → configure (with vcpkg toolchain) → build → ctest.

### Docker / Jenkins (Linux)

`docker/Dockerfile.build` is `FROM ubuntu:24.04` with all system packages
pre-installed. vcpkg is cloned and bootstrapped. Jenkins pipeline: configure →
build → test → archive artifact.

---

## Implementation phases

### Phase 1 — Skeleton ✓ COMPLETE

| Task | Status | Description |
|---|---|---|
| P1-01 | ✓ | CMake skeleton, vcpkg manifest, `cmake/deps_*.cmake`, Catch2 harness, GitHub Actions ×3, Jenkinsfile, Dockerfile.build |
| P1-02 | ✓ | wxWidgets `MainFrame` + menus (File/Edit/View/Help), toolbar stubs, status bar |
| P1-03 | ✓ | `MapPanel` (wxWebView + Leaflet + OSM tiles), JS bridge, round-trip smoke test |
| P1-04 | ✓ | Tile cache: libcurl fetch → SQLite MBTiles store, 30-day TTL, read-only .mbtiles fallback |
| P1-05 | ✓ | TOML scenario I/O: load/save `Scenario`/`Transmitter`/`ReceiverModel` with round-trip unit tests |
| P1-06 | ✓ | `coords/` module: WGS84↔OSGB36 (Helmert), National Grid E/N↔lat/lon, grid ref formatting, OSTN15 loader stub. Regression tests vs known OS benchmark points. |
| P1-07 | ✓ | Transmitter placement UI: click map → add/drag transmitters, coordinate display in both systems, sync to Scenario |
| P1-08 | ✓ | Parameter editor panel (`ParamEditor`): per-transmitter and per-receiver forms, validation, simple/advanced receiver mode toggle |
| P1-08b | ✓ | Network Configuration panel (`NetworkConfigPanel`): F1/F2 frequency fields, mode, grid resolution, receiver model, datum. 500 ms debounce → full recompute. |
| P1-09 | ✓ | `ComputeManager` + worker thread: grid definition, message queue, cancellation flag, `wxQueueEvent` dispatch. Unit tests. |

### Phase 2 — Core propagation (partial)

| Task | Status | Description |
|---|---|---|
| P2-01 | ✓ | ITU P.368 groundwave: empirical polynomial fit parameterised by conductivity + configured frequency. Uses `scenario.frequencies.f1_hz`. **Note: polynomial approximation, not full Sommerfeld/Wait/GRWAVE.** |
| P2-02 | ✓ | Millington mixed-path extension over land/sea boundaries. `millington_field_dbuvm()` implements the Millington (1949) forward/backward averaging method over N great-circle segments with per-segment `ConductivityMap` lookup. Used in `computeGroundwave()` (20 segments), `computeASF()` (20 segments), and `computeAtPoint()` (50 segments). |
| P2-03 | ✓ | Conductivity raster: `BuiltInConductivityMap` (land/sea heuristic, British Isles region) + `GdalConductivityMap` (GeoTIFF, `#ifdef USE_GDAL`). Factory: `make_conductivity_map()`. |
| P2-04 | ✓ | Terrain raster: `FlatTerrainMap` + `GdalTerrainMap` (GeoTIFF single-file and SRTM HGT directory). `TerrainMap::profile()` samples great-circle path via GeographicLib. Factory: `make_terrain_map()`. |
| P2-05 | ✓ | Monteath terrain method: `engine/monteath.cpp::monteath_asf_ml()` — surface-impedance path integration along great-circle profile; conductivity looked up at segment midpoints; terrain slope correction included. Used by P4-01 ASF computation and P5-10 po reference. |
| P2-06 | ✓ | ITU P.684 skywave: median night-time, sea gain, geomagnetic latitude correction. Uses configured frequencies. |
| P2-07 | ✓ | ITU P.372 atmospheric noise: Fam table interpolation at configured frequencies, annual median. |
| P2-08 | ✓ | All computed layers rendered as GeoJSON colour ramps to Leaflet. Layer toggle panel with wxChoice + opacity slider. |

### Phase 3 — SNR, geometry, repeatable, virtual receiver ✓ COMPLETE

| Task | Status | Description |
|---|---|---|
| P3-01 | ✓ | SNR / GDR / SGR computation (`snr.cpp::computeSNR()`). |
| P3-02 | ✓ | Phase/range uncertainty: Williams Eq. 9.7–9.8 → `snr.cpp::phase_uncertainty_ml()`. |
| P3-03 | ✓ | Station selection + WHDOP: nearest-N, range limit, slot-clash rules, weight matrix, WHDOP (Eq. 9.16) → `whdop.cpp::compute_whdop()` + `computeWHDOP()`. |
| P3-04 | ✓ | Repeatable accuracy: σ_p = σ_d × WHDOP, computed inside `computeWHDOP()`. |
| P3-05 | ✓ | SNR / WHDOP / repeatable layer renders; layer toggle panel wired up. |
| P3-06 | ✓ | Virtual receiver marker: drag on map, real-time coordinate display. |
| P3-07 | ✓ | Per-slot phase computation (`asf.cpp::computeAtPoint()`): pseudorange, fractional phase, lane number, SNR, GDR. **See approximations in §Current implementation state.** |
| P3-08 | ✓ | Simulator export: text block with phase values 0–999 matching `slotPhaseOffset[]`. Copy to clipboard + save file. |
| P3-09 | ✓ | Field-strength vs range plot in ResultsPanel. |

### Phase 4 — ASF, absolute accuracy, confidence factor (partial — **START HERE**)

A first-pass skeleton for Phase 4 exists in `asf.cpp`, but the core physics is
approximated and must be replaced. See **§Current implementation state** for the
precise gaps. The map layers (`asf`, `absolute_accuracy`, `confidence`) are
computed and rendered; `asf_gradient` and `absolute_accuracy_corrected` are
allocated but zero everywhere.

| Task | Status | Description |
|---|---|---|
| P4-01 | ✓ | Monteath SF phase delay (ASF): `engine/monteath.cpp::monteath_asf_ml()` — surface-impedance path integration with terrain profile, conductivity raster lookup, slope correction. SPO and station delay applied in `computeAtPoint()`. |
| P4-02 | ✓ | Virtual Locator least-squares (Eq. 9.9–9.12): `asf.cpp::virtual_locator_error_m()` — iterated 2D WLS fix (max 8 iter, convergence < 0.1 m). Returns Airy distance from VL fix to true grid point. |
| P4-03 | ✓ | Absolute accuracy + confidence factor map layers computed and rendered. `absolute_accuracy_corrected` and `absolute_accuracy_delta` also computed when pattern_offsets are present (P5-14). |
| P4-04 | ✓ | Airy ellipsoid + OSGB in Virtual Locator: GeographicLib `Geodesic(6377563.396, 1/299.3249646)` used in all distance computations inside `computeASF()` and `computeAtPoint()`, matching Mk4 firmware. |
| P4-05 | ✓ | OSTN15 full datum transform: `coords/Osgb.cpp` — bilinear interpolation from binary shift grid produced by `tools/ostn15_download.py`. Falls back to Helmert when grid absent or out of coverage. |

### Phase 5 — Outputs, almanac, monitor calibration, polish (partial)

| Task | Status | Description |
|---|---|---|
| P5-01 | ✓ | PNG / GeoTIFF / CSV export: `src/ui/ExportManager.{h,cpp}`. File → Export → Active Layer as CSV/PNG/GeoTIFF. |
| P5-02 | ✓ | HTML report: `ExportManager::export_html()`. File → Export → HTML Report. Self-contained HTML with scenario params, layer stats, pattern offset table. |
| P5-03 | ✗ | Scenario comparison mode. |
| P5-04 | ✓ | Data import helpers: `tools/ostn15_download.py` (OSTN15 datum grid), `tools/itu_p832_import.py` (ITU-R P.832 conductivity → 2-band GeoTIFF), `tools/srtm_download.py` (SRTM3 tile download + merge via CGIAR or NASA Earthdata). |
| P5-05 | ✓ | Physics documentation: `docs/physics/propagation_model.md` — annotated derivations for all 11 pipeline stages with Williams equation references. |
| P5-06 | ✓ | User guides: `docs/user_guide/receiver_modelling.md`, `docs/user_guide/coordinate_systems.md`, `docs/user_guide/monitor_calibration.md`. |
| P5-07 | ✓ | Linux AppImage packaging: `tools/build_appimage.sh` + `data/bandpass2.desktop`. CMake install rules for binary, web assets, zones, and desktop file. |
| P5-08 | ✓ | UK 32-zone polygon dataset: `data/zones/uk_32zone.geojson` — 32 rectangular zones aligned to OSGB National Grid, derived from DTM-170 Fig 7.3. |
| P5-09 | ✓ | Zone pattern computation: `almanac/ZonePatterns.{h,cpp}` — SNR-filtered, WHDOP-ranked sets 1-4, coverage gap flagging. `generate_zp()` produces Zp commands. |
| P5-10 | ✓ | Pattern offset reference UI: `PoRefDialog` (Mode 1 baseline midpoint, Mode 2 user-defined markers with RMS spread). File → Export → Compute Pattern Offsets... Modes 1+2 fully implemented; Mode 3 (ASF map planning aid) is the asf_gradient/asf layers already in the map — no separate dialog needed. |
| P5-11 | ✓ | ASF gradient map layer: computed in `computeASF()` by central-difference ∇ASF [ml/km] on 2D grid after main loop. |
| P5-12 | ✓ | Pattern offset computation (Po): `almanac/AlmanacExport.cpp`. |
| P5-13 | ✓ | Almanac text export (V7 + V16): Sg, Stxs, Po, Zp commands. Zp included when geojson_path given to `generate_almanac()`. |
| P5-14 | ✓ | Monitor station calibration import: `almanac/MonitorCalib.{h,cpp}` — `import_monitor_log()`, `apply_monitor_corrections()`. File → Import → Monitor Station Log. `absolute_accuracy_corrected` and `absolute_accuracy_delta` layers computed with corrected po values. |
| P5-15 | ✓ | Model vs measurement diagnostic: `almanac/MonitorCalib.cpp::check_consistency()` — multi-monitor cross-check, uniform offset / localised anomaly / suspect monitor pattern analysis. |

---

## Current implementation state (Phase 5 handover)

Phases 1–4 and partial Phase 5 are complete. 137 tests pass. Build is clean on Linux.

### Remaining approximation

**`groundwave.cpp::groundwave_field_dbuvm()`** — empirical polynomial fit:
```
A_db = 0.0438 × d_km^0.832 × (f/100kHz)^0.5 × (0.005/σ)^0.3
```
Adequate for Phases 3–5. The full Sommerfeld/Wait/GRWAVE residue series would
give better accuracy at short and very long ranges, but ASF dominates absolute
accuracy so this approximation is acceptable for typical Datatrak geometry.

### Remaining Phase 5 work

- **P5-03** — Scenario comparison mode: not yet designed.
- **P5-04 / P5-05 / P5-06 / P5-10** — All complete (see table above).

### All major Phase 5 items complete

143 tests pass. Build clean on Linux. The only remaining item is:
- P5-03 scenario comparison mode (not designed — requires future planning).

### Monitor calibration (P5-14 / P5-15) — implemented

`almanac/MonitorCalib.{h,cpp}` provides:
- `import_monitor_log(path)` — parses CSV log files (# header comments for
  station name, lat, lon; 6-column data rows: slot1,slot2,f1p,f1m,f2p,f2m).
- `apply_monitor_corrections(scenario)` — returns updated `pattern_offsets`
  with mean correction deltas merged in (po_new = po_current + mean_delta).
- `check_consistency(scenario, threshold_ml=20)` — multi-monitor cross-check
  returning `ConsistencyIssue` list and `DiagnosticItem` list (UniformOffset /
  LocalisedAnomaly / SuspectMonitor flags).

File → Import → Monitor Station Log triggers this flow.

### Corrected accuracy layers — implemented

`computeASF()` now computes two additional layers when `scenario.pattern_offsets`
is non-empty:
- **`absolute_accuracy_corrected`** — VL fix with `corrected_asf = asf - po_correction`
  for each slave station (pattern "slave_slot,master_slot").
- **`absolute_accuracy_delta`** — `absolute_accuracy - absolute_accuracy_corrected`
  (positive = corrections improved the fix).

---

## Existing related code

`datatrak_gen.c` / `datatrak_gen.h` are an existing C signal generator that
runs independently of BANDPASS II. The key interface point:

```c
typedef struct {
    int16_t slotPhaseOffset[24];  // Phase offsets fed from BANDPASS II virtual receiver
    uint8_t slotPower[24];        // Signal amplitude fed from BANDPASS II SNR computation
    // ... internal state ...
} DATATRAK_LF_CTX;
```

BANDPASS II's simulator export (P3-08) generates values that populate these
fields. No direct code dependency between BANDPASS II and the generator —
the interface is a text/serial protocol.

---

## Key domain facts

- Timing cycle: **1.68 seconds** per cycle
- Goldcode: **64 bits**, repeating. Cycle = one goldcode step.
- Clock: **16-bit** value, increments every 64 cycles (~107.5 s)
- Nav slots: **8 per frequency per cycle** in single-chain mode; **24 total** in
  interlaced mode (8 per cycle from each of three groups on alternating cycles)
- Preamble: AA1 (0–40 ms), trigger (45–85 ms), clock (95–115 ms),
  station data (120–185 ms), vehicle data (185–300 ms), AA2 (300–340 ms)
- F1 navslots: 340 ms onwards, 80 ms each (40 ms F+, 40 ms F−)
- G1 guard: 40 ms
- F2 navslots: after G1, same structure
- G2 guard: 20 ms
- f1 frequency: **configurable, default 146.4375 kHz**; lane width = c / f1_hz
- f2 frequency: **configurable, default 131.2500 kHz**; lane width = c / f2_hz
- Frequency range: **30–300 kHz** (full LF band). Hard limits — rejected outside
  this range. Warning (not error) if f1 == f2.
- Lane widths at defaults: f1 ≈ 2047.24 m (= 299792458/146437.5), f2 ≈ 2284.13 m (= 299792458/131250)
- Pattern offset unit: **millilane (ml)** = 1/1000 lane
  - At defaults: f1: 1 ml ≈ 2.047 m; f2: 1 ml ≈ 2.284 m
  - At custom frequencies: recomputed from lane_width = c / f
- **All pipeline code uses `scenario.frequencies.f1_hz` / `f2_hz`. No hardcoded
  frequency values anywhere.**
- Almanac radiated every **third** cycle (shared with FUT and specific vehicle data)
- Full almanac broadcast time: **> 1 hour**
- Zone count: **32** zones over UK coverage area
- Max stations: **24** (slots 1–24)
- OSGB National Grid: Airy 1830 ellipsoid, Transverse Mercator projection,
  false origin 400 km W, 100 km N of true origin (49°N, 2°W)

---

## Things to avoid

- **Do not hardcode any frequency value or lane width constant in pipeline code.**
  Every calculation involving carrier frequency, wavelength, or lane width must
  use `scenario.frequencies.f1_hz` / `f2_hz` / `lane_width_f1_m` /
  `lane_width_f2_m`. This includes ITU P.368, P.684, P.372, phase-to-distance
  conversions, millilane computations, and the almanac export header.
- Do not use `std::async` for the compute pipeline (see Threading section).
- Do not fetch map tiles from a CDN — Leaflet.js must be vendored.
- Do not store WGS84 coordinates as OSGB or vice versa without explicit
  conversion. TOML files always store WGS84; OSGB is display/entry only.
- Do not use `int` for millilane values that may be accumulated — use `int32_t`
  explicitly to avoid platform-dependent width.
- Pattern offset `po` values in V7 format are **unsigned** (0–999). Values
  computed as negative must be clipped to 0 and flagged, not wrapped.
- The Monteath terrain method (P2-05) and Virtual Locator least-squares (P4-02)
  are the two most complex sub-modules. Implement them incrementally with unit
  tests at each stage. Do not attempt to implement either in a single pass.
- Direct patterns are corrected by transmitter phase adjustment, not by `po`.
  Do not conflate the two correction mechanisms.
- **Always explicitly capture `constexpr` locals in lambda capture lists.**
  MSVC requires this (`[DEG_TO_RAD](...)`), while GCC/Clang silently allow
  implicit use of `constexpr` variables inside lambdas. Using `[]` with a
  `constexpr` variable will compile on Linux/macOS but fail on Windows with
  `error C3493`.
- **Do not use raw hex escape sequences (`\x..`) for Unicode characters in UI
  strings.** Use the named constants in `src/ui/UiConstants.h` (`bp::ui::DEGREE`,
  `bp::ui::MICRO`, `bp::ui::SIGMA`, `bp::ui::DBUVM`, `bp::ui::MICROSEC`).
  For characters that are purely decorative (em dash, en dash, curly quotes),
  use plain ASCII instead. Raw hex sequences cause build/display issues on
  macOS and Windows.
- **Never use hardcoded `/tmp/` paths in tests.** Use
  `std::filesystem::temp_directory_path()` for cross-platform temp file
  handling. `/tmp` does not exist on Windows — tests that write temp files
  will silently fail to open the file, producing empty output and cascading
  assertion failures.
- **Never pass `wxString` or `std::string` directly to `wxString::Format` for
  `%s`.** Both are non-POD objects; passing them as varargs is undefined
  behaviour and causes a crash in `wxFormatString::AsWChar()` at runtime.
  Always call `.c_str()` on the argument:
  ```cpp
  // Wrong — crashes at runtime:
  wxString::Format("Hello %s", some_wx_string)
  wxString::Format("Hello %s", some_std_string)
  // Correct:
  wxString::Format("Hello %s", some_wx_string.c_str())
  wxString::Format("Hello %s", some_std_string.c_str())
  ```
  This bug is silent on some compilers and only manifests when the affected
  code path is first exercised, making it hard to trace. Four instances were
  found and fixed in `ParamEditor.cpp` and `MainFrame.cpp`.

---

## Where to resume — Phase 4

Phases 1, 2 (partial), and 3 are complete. 58 tests pass. The application
builds and runs with a working Leaflet map, transmitter/receiver placement,
full propagation pipeline (groundwave → SNR → WHDOP → repeatable → ASF →
absolute accuracy → confidence), ReceiverPanel, simulator export, and almanac
export (Sg/Stxs/Po in V7/V16 formats).

**Read §Current implementation state before writing any Phase 4 code.** It
documents precisely which functions are approximations, which layers are
uncomputed, and why.

**Recommended starting point: P2-05 Monteath terrain method.** This is the
most complex sub-module and a prerequisite for a proper P4-01 ASF
implementation. Implement it incrementally in `engine/` — one unit test per
step. The terrain height profile API (`terrain.h::TerrainMap::profile()`) is
already in place.

The Phase 4 milestone is: absolute accuracy map that reflects real Monteath
ASF delays rather than the surface-impedance approximation, computed via a
proper iterated WLS Virtual Locator fix using Airy ellipsoid ranges, with
the `asf_gradient` layer correctly computed and the f1−/f2− phase polarity
fixed in `computeAtPoint()`.
