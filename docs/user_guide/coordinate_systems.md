# BANDPASS II — Coordinate Systems Guide

## Overview

BANDPASS II supports three coordinate systems.  All internal storage and
computation uses **WGS84** (decimal degrees).  OSGB36 National Grid is used
for display and for almanac export, because the Datatrak transmitter grid
commands use OSGB easting/northing.

| System | Code | Description |
|---|---|---|
| WGS84 | `wgs84` | Decimal degrees lat/lon; used internally |
| OSGB36 LatLon | — | Airy 1830 ellipsoid degrees; intermediate only |
| OSGB36 National Grid | `osgb_ng` | Easting/northing in metres; TOML display and almanac |

---

## Entering coordinates

All coordinate entry fields in the UI accept either WGS84 or OSGB36 National
Grid format.  The format is auto-detected:

| Input example | Interpreted as |
|---|---|
| `52.3247 -0.1848` | WGS84 decimal degrees (lat lon) |
| `TL 271 707` | OSGB36 National Grid (letter pair + 3+3 digits) |
| `TL 27100 70700` | OSGB36 National Grid (full easting/northing) |
| `513054 262453` | OSGB36 raw easting/northing (no letter pair) |

National Grid references use the standard two-letter prefix covering
100 km squares.  Both 6-figure (TL 271 707, precision ≈ 100 m) and
10-figure (TL 27100 70700, precision 1 m) forms are accepted.

---

## Map status bar

The status bar shows the cursor position in both systems simultaneously:

```
52.32470, -0.18480    TL 271 707
```

---

## TOML scenario files

Scenario files always store coordinates in WGS84:

```toml
[[transmitters]]
lat = 52.3247
lon = -0.1848
```

The `osgb_easting` and `osgb_northing` fields are informational only;
they are recomputed from lat/lon and never read back.  The status bar
always shows cursor position in both WGS84 and OSGB National Grid
simultaneously.

---

## Datum transforms

### Helmert 7-parameter (built-in, always available)

A 7-parameter Helmert transform between WGS84 and OSGB36 (Airy 1830),
using the standard OS parameters:

| Parameter | Value |
|---|---|
| Δx | −446.448 m |
| Δy | +125.157 m |
| Δz | −542.060 m |
| s | +20.4894 × 10⁻⁶ |
| Rx | −0.1502″ |
| Ry | −0.2470″ |
| Rz | −0.8421″ |

Accuracy: ±5 m over GB.  Adequate for scenario planning.

### OSTN15 (requires data download, ±0.1 m)

The Ordnance Survey OSTN15 datum shift grid gives sub-metre accuracy
between WGS84 and OSGB36 National Grid.  It is not bundled with
BANDPASS II and must be downloaded separately.

#### Automatic download

```bash
python3 tools/ostn15_download.py --out OSTN15.dat
```

This fetches the NTv2 grid file from the official
[OrdnanceSurvey/os-transform](https://github.com/OrdnanceSurvey/os-transform)
GitHub repository and converts it to BANDPASS II's compact binary format.

#### Manual download

If the automatic download fails (network restrictions, etc.):

1. Download `OSTN15_NTv2_OSGBtoETRS.gsb` from
   https://github.com/OrdnanceSurvey/os-transform
2. Run:
   ```bash
   python3 tools/ostn15_download.py --input-ntv2 OSTN15_NTv2_OSGBtoETRS.gsb
   ```

Alternatively, download the developer pack from
https://www.ordnancesurvey.co.uk/geodesy-positioning/coordinate-transformations/resources,
extract `OSTN15_OSGM15_DataFile.txt`, and run:
```bash
python3 tools/ostn15_download.py --input-csv OSTN15_OSGM15_DataFile.txt
```

#### Installation

Place `OSTN15.dat` in any of these locations (searched in order):

1. Same directory as the BANDPASS II executable (recommended)
2. User data directory (`~/.local/share/bandpass2/` on Linux)
3. `data/` subdirectory relative to the executable (development tree)

BANDPASS II loads the file at startup automatically.  Outside the GB
coverage area, or when the file is absent, it falls back to Helmert.

The Network Configuration panel shows **"OSTN15 grid loaded (±0.1 m)"**
when the grid is active, or a red warning if it is not loaded.

#### Activation

Select OSTN15 in the Network Configuration panel, or set in the TOML:

```toml
[datum]
transform = "ostn15"
```

#### What OSTN15 affects

The datum selection controls coordinate *display and export* only —
it does not affect the physics pipeline.  Specifically it changes:

- Status bar OSGB36 grid reference as the cursor moves
- Receiver position display in the Receiver panel
- Easting/northing values in almanac `Sg` commands
- Parsing of OSGB36 coordinates typed into reference-point dialogs

The Virtual Locator range computations use the Airy 1830 ellipsoid
directly (matching Mk4 firmware) regardless of datum setting.

---

## Airy 1830 ellipsoid in the Virtual Locator

The Mk4 Datatrak Locator firmware computes ranges on the **Airy 1830**
ellipsoid (semi-major axis 6,377,563.396 m, inverse flattening 299.3249646)
using the Andoyer-Lambert formula.  This is a deliberate design choice in
the firmware; it is not a bug.

BANDPASS II replicates this in the Virtual Locator fix so that the simulated
absolute position error matches what a physical locator would produce.  All
display and map operations use WGS84.  The datum shift is applied
transparently at the boundary.

---

## Coordinate transformations in code

Key functions in `src/coords/`:

| Function | File | Description |
|---|---|---|
| `osgb::wgs84_to_osgb36(LatLon)` | `Osgb.cpp` | WGS84 → OSGB36 LatLon |
| `osgb::osgb36_to_wgs84(LatLon)` | `Osgb.cpp` | OSGB36 → WGS84 |
| `osgb::load_ostn15(path)` | `Osgb.cpp` | Load OSTN15 grid; enables high-accuracy mode |
| `national_grid::latlon_to_en(LatLon)` | `NationalGrid.cpp` | OSGB36 LatLon → Easting/Northing |
| `national_grid::en_to_latlon(EastNorth)` | `NationalGrid.cpp` | Easting/Northing → OSGB36 LatLon |
| `national_grid::en_to_gridref(EastNorth, n)` | `NationalGrid.cpp` | E/N → grid reference string |
| `national_grid::parse_coordinate(text)` | `NationalGrid.cpp` | Auto-detect and parse any format → WGS84 |

The `parse_coordinate()` function is used throughout the UI for all
coordinate entry fields.  It returns WGS84 LatLon regardless of input format.

---

## National Grid reference formatting

```
TL 271 707        ← 6-figure (100 m precision)
TL 27100 70700    ← 10-figure (1 m precision)
TL271707          ← compact form (no spaces)
```

BANDPASS II always displays 8-figure references (10 m precision) in the
status bar.  The almanac export uses full easting/northing integers.

---

## Projection

The OSGB36 National Grid uses a Transverse Mercator projection:

| Parameter | Value |
|---|---|
| Central meridian | 2°W |
| True origin | 49°N, 2°W |
| Scale factor at origin | 0.9996012717 |
| False easting | 400,000 m |
| False northing | −100,000 m |
| Ellipsoid | Airy 1830 |

Coordinates within GB typically range:

- Easting: 0 – 700,000 m
- Northing: 0 – 1,300,000 m
