# BANDPASS II — Claude Code Development Brief

## What this project is

BANDPASS II is a C++17/wxWidgets desktop application for planning and analysing
Datatrak-type LF radio navigation networks. It predicts coverage and positioning
accuracy using the propagation physics model from:

  Williams (2004), "Prediction of the Coverage and Performance of the Datatrak
  Low-Frequency Tracking System", University of Wales Bangor PhD thesis.

It is also a teaching tool: map layers can be toggled to show how each physics
stage contributes to the final result.

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
│   │   ├── ParamEditor.*       # Transmitter / receiver forms
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
| 4 | Vehicle noise | `noise.cpp` | `vehicleNoise()` | Empirical |
| 5 | SNR per station | `snr.cpp` | `computeSNR()` | — |
| 6 | SGR / GDR | `snr.cpp` | `computeGDR()` | — |
| 7 | Phase/range uncertainty | `phase_error.cpp` | `phaseUncertainty()` | Eq. 9.7–9.8 |
| 8 | Station selection + WHDOP | `station_select.cpp` + `whdop.cpp` | `selectStations()`, `computeWHDOP()` | Eq. 9.12–9.16, App. K |
| 9 | Repeatable accuracy | `repeatable.cpp` | `computeRepeatable()` | σ_p = σ_d × WHDOP |
| 10 | ASF / absolute accuracy | `asf.cpp` | `computeASF()`, `virtualLocatorFix()` | Eq. 11.1–11.10 |
| 11 | Confidence factor | `asf.cpp` | `computeConfidence()` | Residues from VL fix |
| 12 | Per-slot phase at receiver | `asf.cpp` | `computeAtPoint()` | Stages 1+10 at placed point |

### Key Williams equations → functions

```
Eq. 9.7–9.8   phase/range uncertainty      → phase_error.cpp::phaseUncertainty()
Eq. 9.9       weighted least-squares fix   → asf.cpp::virtualLocatorFix()
Eq. 9.12      directional cosines matrix A → whdop.cpp::buildDirectionalCosines()
Eq. 9.13      weight matrix W              → station_select.cpp::buildWeightMatrix()
Eq. 9.14      SNR-based weighting          → station_select.cpp::snrWeight()
Eq. 9.16      weighted HDOP               → whdop.cpp::computeWHDOP()
Eq. 11.1      additional delay             → asf.cpp::additionalDelay()
Eq. 11.3      chord distance WGS84        → grid.cpp::chordDistance()
Eq. 11.5–11.10 link bias chain            → asf.cpp::linkBiasError()
Appendix J    Helmert transform           → coords/Osgb.cpp::helmertTransform()
Appendix K    slot selection rules        → station_select.cpp::selectStations()
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

### Phase 1 — Skeleton (start here)

| Task | Description |
|---|---|
| P1-01 | CMake skeleton, vcpkg manifest, `cmake/deps_*.cmake`, Catch2 harness, GitHub Actions ×3, Jenkinsfile, Dockerfile.build |
| P1-02 | wxWidgets `MainFrame` + menus (File/Edit/View/Help), toolbar stubs, status bar |
| P1-03 | `MapPanel` (wxWebView + Leaflet + OSM tiles), JS bridge skeleton, round-trip smoke test |
| P1-04 | Tile cache: libcurl fetch → SQLite MBTiles store, 30-day TTL, read-only .mbtiles fallback |
| P1-05 | TOML scenario I/O: load/save `Scenario`/`Transmitter`/`ReceiverModel` with round-trip unit tests |
| P1-06 | `coords/` module: WGS84↔OSGB36 (Helmert), National Grid E/N↔lat/lon, grid ref formatting, OSTN15 loader stub. Regression tests vs known OS benchmark points. |
| P1-07 | Transmitter placement UI: click map → add/drag transmitters, coordinate display in both systems, sync to Scenario |
| P1-08 | Parameter editor panel: transmitter + receiver forms, validation, simple/advanced receiver mode toggle |
| P1-09 | `ComputeGrid` + worker thread: grid definition, worker thread, message queue, cancellation flag, `wxQueueEvent` dispatch, ownership-transfer model. Unit tests. |

### Phase 2 — Core propagation

| Task | Description |
|---|---|
| P2-01 | ITU P.368 groundwave attenuation curves (polynomial, parameterised by conductivity + frequency). Validate against ITU published values. |
| P2-02 | Millington mixed-path extension over land/sea boundaries |
| P2-03 | Conductivity raster: GDAL GeoTIFF load, bilinear interpolation at lat/lon. Built-in synthetic test values. ITU P.832 importer. |
| P2-04 | Terrain raster: SRTM tile load, mosaic, height profile along path |
| P2-05 | **Monteath terrain method** — most complex sub-module. Surface impedance integration along terrain profile. Phase delay + field strength correction. Validate against Williams Ch.10. |
| P2-06 | ITU P.684 skywave: median night-time, sea gain, geomagnetic latitude correction (Eq. 5.3–5.5). Validate against Williams figures. |
| P2-07 | ITU P.372 atmospheric noise: Fam table interpolation, annual median |
| P2-08 | Groundwave layer render: first visible map layer, GeoJSON colour ramp to Leaflet |

### Phase 3 — SNR, geometry, repeatable, virtual receiver

| Task | Description |
|---|---|
| P3-01 | SNR / GDR computation from groundwave + noise + skywave arrays |
| P3-02 | Phase/range uncertainty: Williams Eq. 9.7–9.8 |
| P3-03 | Station selection + WHDOP: nearest-N, range limit, slot-clash rules (App. K), weight matrix (Eq. 9.14), WHDOP (Eq. 9.16). Validate against Williams Fig. 9.9. |
| P3-04 | Repeatable accuracy: σ_p = σ_d × WHDOP, user-configurable filter factor |
| P3-05 | SNR / WHDOP / repeatable layer renders; layer toggle panel wired up |
| P3-06 | Virtual receiver marker: drag on map, real-time coordinate display in both systems |
| P3-07 | Per-slot phase computation (`asf.cpp::computeAtPoint()`): pseudorange, fractional phase, lane number, SNR, GDR at receiver point for all four components (f1+/f1−/f2+/f2−). Updates ReceiverPanel. |
| P3-08 | Simulator export: text block with phase values 0–999 matching `slotPhaseOffset[]` in `datatrak_gen.h`. Copy to clipboard + save file. |
| P3-09 | Field-strength vs range plot in ResultsPanel |

### Phase 4 — ASF, absolute accuracy, confidence factor

| Task | Description |
|---|---|
| P4-01 | Monteath SF phase delay (ASF): per station pair, link bias chain (Eq. 11.5–11.10), SPO and station delay from scenario |
| P4-02 | Virtual Locator least-squares (Eq. 9.9–9.12): iterated weighted LS, Airy ellipsoid, convergence < 1 m, per grid point |
| P4-03 | Absolute accuracy + confidence factor map layers |
| P4-04 | Airy ellipsoid + OSGB in Virtual Locator: Andoyer-Lambert range with Airy ellipsoid, matches firmware behaviour |
| P4-05 | OSTN15 full datum transform: load grid (GDAL), bicubic interpolation, fallback to Helmert |

### Phase 5 — Outputs, almanac, monitor calibration, polish

| Task | Description |
|---|---|
| P5-01 | PNG / SVG / GeoTIFF / CSV export. GeoTIFF via GDAL, verify import into QGIS. |
| P5-02 | HTML report: self-contained, scenario params + layer images + statistics tables |
| P5-03 | Scenario comparison mode: load two scenarios, diff selected layers side by side |
| P5-04 | Data import helpers: `tools/` Python scripts for ITU P.832, SRTM, OSTN15 |
| P5-05 | Physics documentation: `docs/physics/` annotated Williams equation derivations |
| P5-06 | Receiver modelling guide + coordinate systems guide |
| P5-07 | Linux AppImage packaging (linuxdeploy) |
| P5-08 | **UK 32-zone polygon dataset**: digitise DTM-170 Fig 7.3 zone boundaries as GeoJSON. Bundle as `data/zones/uk_32zone.geojson`. Include zone centroids. |
| P5-09 | **Zone pattern computation (Zp)**: per zone centroid, SNR-filter pattern pairs, rank by WHDOP, assign top 3. Flag zones with <3 viable patterns. |
| P5-10 | **Pattern offset reference UI**: Mode 1/2/3 selector, reference marker placement (map click or OSGB entry), multiple markers → least-squares composite, RMS residual display. |
| P5-11 | **ASF gradient map layer**: spatial gradient magnitude of ASF grid per pattern pair [ml/km]. Monitor siting planning layer. |
| P5-12 | **Pattern offset computation (Po)**: ASF at reference point(s) in millilanes, clip negative→0 + flag, store in scenario. |
| P5-13 | **Almanac text export (V7 + V16)**: Sg, Stxs, Zp, Po commands in both formats. Header block with generation params, RMS residuals, warnings (negative clips, coverage gaps). Plain ASCII. |
| P5-14 | **Monitor station calibration import**: manual delta entry UI + CSV/JSON import. Apply corrections to `po` store. Multi-monitor consistency check (default threshold 20 ml). Delta-error map overlay. |
| P5-15 | **Model vs measurement diagnostic**: corrected vs predicted absolute accuracy comparison, pattern-pair divergence table, divergence classification (uniform / specific pair / temporal / single-monitor). |

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
- f1 frequency: **146.4375 kHz** (lane width ≈ 2047 m)
- f2 frequency: **131.25 kHz** (lane width ≈ 2286 m)
- Pattern offset unit: **millilane (ml)** = 1/1000 lane
  - f1: 1 ml ≈ 2.0 m
  - f2: 1 ml ≈ 2.3 m
- Almanac radiated every **third** cycle (shared with FUT and specific vehicle data)
- Full almanac broadcast time: **> 1 hour**
- Zone count: **32** zones over UK coverage area
- Max stations: **24** (slots 1–24)
- OSGB National Grid: Airy 1830 ellipsoid, Transverse Mercator projection,
  false origin 400 km W, 100 km N of true origin (49°N, 2°W)

---

## Things to avoid

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

---

## Where to start

**Start with Phase 1, task P1-01**: create the CMake skeleton, vcpkg manifest,
platform cmake files, Catch2 test harness, and the three GitHub Actions
workflows. Build it, make sure all three CI workflows pass with an empty
(hello-world) binary, before writing any application code.

Then P1-02 (MainFrame), P1-03 (MapPanel + bridge), in order. The milestone for
Phase 1 is: a window with a working Leaflet map, a transmitter that can be
placed by clicking the map, and the TOML scenario round-tripping correctly in
unit tests.
