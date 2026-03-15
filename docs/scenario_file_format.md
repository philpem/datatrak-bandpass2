# BANDPASS II Scenario File Format (.toml)

Scenario files use [TOML](https://toml.io/) format.  All coordinates are stored
as WGS84 decimal degrees.  Frequencies are stored in kHz for human readability
and converted to Hz on load.

## Sections

### `[scenario]`

| Field | Type | Description |
|---|---|---|
| `name` | string | Human-readable scenario name |
| `created` | string | Date string, e.g. `"2004-01-01"` |

### `[grid]`

Rectangular computation grid in WGS84.

| Field | Type | Description |
|---|---|---|
| `lat_min` | float | Southern boundary (degrees N). Use `49.5` for UK. |
| `lat_max` | float | Northern boundary (degrees N). Adjust to cover the northernmost station + margin. |
| `lon_min` | float | Western boundary (degrees E, negative = west). Use `-7.0` for UK. |
| `lon_max` | float | Eastern boundary (degrees E). Use `2.5` for UK. |
| `resolution_km` | float | Grid spacing in km. `2.0` is moderate detail; `1.0` is high (slow). |

### `[frequencies]`

Datatrak standard carrier frequencies.  Only change these if modelling a
non-standard network.

| Field | Type | Default | Description |
|---|---|---|---|
| `f1_khz` | float | `146.4375` | F1 carrier frequency in kHz |
| `f2_khz` | float | `131.2500` | F2 carrier frequency in kHz |

Valid range: 30-300 kHz (full LF band).  Values outside this range are
rejected on load.

### `[receiver]`

| Field | Type | Default | Description |
|---|---|---|---|
| `mode` | string | `"simple"` | `"simple"` or `"advanced"` |
| `noise_floor_dbuvpm` | float | `14.0` | Receiver noise floor (dBuV/m) |
| `vehicle_noise_dbuvpm` | float | `27.0` | Vehicle electrical noise (dBuV/m) |
| `max_range_km` | float | `350.0` | Maximum usable range from any transmitter (km) |
| `min_stations` | int | `4` | Minimum stations required for a position fix |
| `vp_ms` | float | `299892718` | Velocity of propagation (m/s) |
| `ellipsoid` | string | `"airy1830"` | `"airy1830"` (matches Mk4 Locator firmware) or `"wgs84"` |

### `[[transmitters]]`

Array of tables.  One block per transmitter.  Multi-slot stations (same
physical site transmitting on two slots with different masters) appear as
separate entries with the same coordinates.  There must be at least one
master station.

| Field | Type | Default | Description |
|---|---|---|---|
| `name` | string | required | Station name, e.g. `"Huntingdon"` |
| `lat` | float | required | WGS84 latitude (decimal degrees, positive = north) |
| `lon` | float | required | WGS84 longitude (decimal degrees, negative = west) |
| `power_w` | float | `40.0` | Transmitter power (watts) |
| `height_m` | float | `50.0` | Antenna height (metres) |
| `slot` | int | required | Transmission slot number (1-8 standard, 1-24 interlaced) |
| `is_master` | bool | `false` | `true` if this is a master station |
| `master_slot` | int | `0` | Slot number of this station's master. `0` if this station is itself a master. |
| `spo_us` | float | `0.0` | Synchronisation pulse offset (microseconds) |
| `station_delay_us` | float | `0.0` | Station delay (microseconds) |

### `[[monitor_stations]]`

Array of tables.  Optional.  Surveyed monitor stations for calibration.

| Field | Type | Description |
|---|---|---|
| `name` | string | Monitor station name |
| `lat` | float | WGS84 latitude |
| `lon` | float | WGS84 longitude |

Each monitor station can have a `[[monitor_stations.corrections]]` sub-array
with per-pattern-pair correction deltas (populated by File -> Import -> Monitor
Station Log):

| Field | Type | Description |
|---|---|---|
| `pattern` | string | Pattern pair, e.g. `"3,1"` (slave_slot,master_slot) |
| `f1plus_ml` | int | F1+ correction delta (millilanes) |
| `f1minus_ml` | int | F1- correction delta (millilanes) |
| `f2plus_ml` | int | F2+ correction delta (millilanes) |
| `f2minus_ml` | int | F2- correction delta (millilanes) |

### `[[pattern_offsets]]`

Array of tables.  Optional.  Populated by almanac export or monitor
calibration.

| Field | Type | Description |
|---|---|---|
| `pattern` | string | Pattern pair, e.g. `"8,2"` (slave_slot,master_slot) |
| `f1plus_ml` | int | F1+ ASF correction (millilanes, 0-999 for V7 format) |
| `f1minus_ml` | int | F1- ASF correction (millilanes) |
| `f2plus_ml` | int | F2+ ASF correction (millilanes) |
| `f2minus_ml` | int | F2- ASF correction (millilanes) |

### `[conductivity]`

| Field | Value | Description |
|---|---|---|
| `source` | `"builtin"` | Built-in land/sea heuristic conductivity map |
| | `"itu_p832"` | ITU-R P.832 conductivity raster (requires import via `tools/itu_p832_import.py`) |
| | `"bgs"` | British Geological Survey conductivity data |
| | `/path/to/file.tif` | GeoTIFF conductivity raster (absolute path) |

Both conductivity and terrain source can be set in the Network Configuration
panel or in the TOML file.  See [Obtaining data files](#obtaining-data-files)
below for instructions on preparing the data.

### `[terrain]`

| Field | Value | Description |
|---|---|---|
| `source` | `"flat"` | Flat earth (no terrain data) |
| | `"srtm"` | SRTM terrain data (requires download via `tools/srtm_download.py`) |
| | `/path/to/file.tif` | GeoTIFF terrain raster (absolute path) |

### `[datum]`

| Field | Value | Description |
|---|---|---|
| `transform` | `"helmert"` | Helmert 7-parameter transform (+-5 m accuracy, no data files needed) |
| | `"ostn15"` | OSTN15 transform (+-0.1 m, requires `tools/ostn15_download.py`) |

## Example

```toml
[scenario]
name        = "UK Datatrak baseline"
created     = "2025-01-01"

[grid]
lat_min       = 49.5
lat_max       = 57.5
lon_min       = -7.0
lon_max       = 2.5
resolution_km = 2.0

[frequencies]
f1_khz = 146.4375
f2_khz = 131.2500

[receiver]
mode                 = "advanced"
noise_floor_dbuvpm   = 14.0
vehicle_noise_dbuvpm = 27.0
max_range_km         = 350.0
min_stations         = 4
vp_ms                = 299892718
ellipsoid            = "airy1830"

# Master station
[[transmitters]]
name             = "Huntingdon"
lat              = 52.248652
lon              = -0.345416
power_w          = 29.5
height_m         = 50.0
slot             = 1
is_master        = true
master_slot      = 0
spo_us           = 0.0
station_delay_us = 0.0

# Slave station
[[transmitters]]
name             = "Selsey"
lat              = 50.762753
lon              = -0.792227
power_w          = 30.0
height_m         = 50.0
slot             = 2
is_master        = false
master_slot      = 1
spo_us           = 0.0
station_delay_us = 0.22

[conductivity]
source = "builtin"

[terrain]
source = "flat"

[datum]
transform = "helmert"

```

## Notes

- All coordinates must be WGS84.  If converting from OSGB National Grid,
  use EPSG:27700 to EPSG:4326.  Provide at least 4 decimal places.
- Multi-slot stations (same physical site, two slot assignments with different
  masters) are represented as two `[[transmitters]]` entries with identical
  coordinates but different `slot` and `master_slot` values.
- The `is_master` field and `master_slot` field must be consistent: a master
  has `is_master = true` and `master_slot = 0`; a slave has `is_master = false`
  and `master_slot` set to the slot number of its master.
- Slot numbers can exceed 8 for interlaced-mode networks (up to 24).
- Pattern strings use the format `"slave_slot,master_slot"`, e.g. `"6,4"`
  means slave station on slot 6 with master on slot 4.

## Obtaining data files

### Ground conductivity (ITU-R P.832)

The built-in conductivity map uses a simple land/sea heuristic.  For more
accurate modelling, import the ITU-R P.832 world ground conductivity dataset:

1. Obtain the ITU-R P.832-4 data.  It is distributed as a pair of ASCII grid
   files (`ESRT.txt` for conductivity, `EPRT.txt` for permittivity) or as a
   CSV export.  The data can also be obtained from the `itur` R package.

2. Run the import tool to convert it to a GeoTIFF:

   ```
   python3 tools/itu_p832_import.py --input /path/to/itu-p832/ --out conductivity.tif
   ```

   The input may be a directory containing `ESRT.txt` and `EPRT.txt`, a ZIP
   file containing those files, or a CSV with `lon,lat,sigma,eps_r` columns.

3. In the Network Configuration panel, set Conductivity Source to "File" and
   browse to the output GeoTIFF.  Alternatively, set `conductivity.source` in
   the TOML file to the absolute path of the GeoTIFF.

### Terrain (SRTM)

Terrain data improves the Monteath ASF computation (slope correction along the
propagation path).  For flat terrain or initial planning, the "Flat" setting is
adequate.

1. Run the download tool for your area of interest:

   ```
   python3 tools/srtm_download.py --bbox 49.5 -8.0 61.0 2.5 --out terrain_uk.tif
   ```

   This downloads SRTM3 tiles (3 arc-second, ~90 m resolution) from CGIAR-CSI
   (no login required) and merges them into a single GeoTIFF.

   For NASA Earthdata tiles (requires free registration):

   ```
   export EARTHDATA_TOKEN=<your-bearer-token>
   python3 tools/srtm_download.py --bbox 49.5 -8.0 61.0 2.5 --source nasa --out terrain_uk.tif
   ```

2. In the Network Configuration panel, set Terrain Source to "File" and browse
   to the output GeoTIFF.  Alternatively, set `terrain.source` in the TOML file
   to the absolute path of the GeoTIFF.

### OSTN15 datum grid

The OSTN15 datum transform provides sub-metre accuracy for WGS84 to OSGB36
conversion.  It is optional -- Helmert (+-5 m) is used as a fallback.

```
python3 tools/ostn15_download.py
```

This downloads the OS OSTN15 shift grid and converts it to the binary format
expected by BANDPASS II.  The datum transform setting is in the Network
Configuration panel.
