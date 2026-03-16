#!/usr/bin/env python3
"""
itu_p832_import.py — Convert ITU-R P.832 / IDWM ground conductivity data to a
2-band GeoTIFF suitable for loading by BANDPASS II's GdalConductivityMap.

Band 1: electrical conductivity  [S/m]
Band 2: relative permittivity    [dimensionless]

Data sources
============

The recommended source is the ITU Digitized World Map (IDWM) dataset,
available as GeoJSON downloads from the ITU-R Geospatial Catalogue:

    https://www.itu.int/ITU-R/BR-GeoCatalogue/BR-GeoApi/collections/idwm

Download either or both of:
  * idwm_GC_VLF — Ground Conductivity Zones (World ATLAS VLF)
  * idwm_GC_MF  — Ground Conductivity Zones (World ATLAS MF)

These are polygon zone datasets with conductivity values in mS/m.
This script rasterizes them into a regular grid GeoTIFF.

The script also accepts legacy formats:
  * A folder or ZIP containing "ESRT.txt" and "EPRT.txt" ASCII grids
    (conductivity and permittivity at 1.5 x 1 deg resolution).
  * A CSV with columns: lon, lat, conductivity_s_m, eps_r

Permittivity handling
=====================

The IDWM GeoJSON provides conductivity zones only.  If no permittivity
attribute is found, permittivity is estimated from conductivity using the
standard pairs from ITU-R P.527-3 / P.368 Table 1 (nearest-match lookup).
Use --epsr-property to override if your GeoJSON does include permittivity.

Output: a GeoTIFF with WGS84 CRS, band1 = conductivity [S/m],
band2 = relative permittivity.
Requires: GDAL Python bindings (python3-gdal / pip install GDAL).

Usage:
    python3 tools/itu_p832_import.py --input PATH [--out PATH]

    PATH may be:
      - A GeoJSON file (.geojson / .json) from the IDWM GeoCatalogue
      - A directory containing ESRT.txt and EPRT.txt
      - A zip file containing those files
      - A CSV with lon,lat,sigma,eps_r columns

Examples:
    python3 tools/itu_p832_import.py --input idwm_GC_VLF.geojson
    python3 tools/itu_p832_import.py --input idwm_GC_MF.geojson --resolution 0.5
    python3 tools/itu_p832_import.py --input p832_data/ --out conductivity.tif
    python3 tools/itu_p832_import.py --input conductivity.csv
"""

import argparse
import csv
import json
import os
import sys
import zipfile
from pathlib import Path

# Grid dimensions for the standard ITU-R P.832 raster
# Longitude: -180 to +180 at 1.5 deg spacing -> 241 columns
# Latitude:  -90  to +90  at 1.0 deg spacing -> 181 rows
DEFAULT_NCOLS   = 241
DEFAULT_NROWS   = 181
DEFAULT_LONMIN  = -180.0
DEFAULT_LATMIN  = -90.0
DEFAULT_DLON    = 1.5
DEFAULT_DLAT    = 1.0

# Fallback values used when a cell is missing or marked as no-data
FALLBACK_SIGMA  = 0.005   # S/m  (temperate land, ITU Table 1)
FALLBACK_EPS_R  = 15.0    # dimensionless

# ITU-R P.527-3 / P.368 standard conductivity-permittivity pairs.
# Conductivity in S/m, relative permittivity (dimensionless).
# Used to estimate permittivity when only conductivity is available.
# Sorted by conductivity ascending for lookup.
_P527_PAIRS = [
    # (sigma_s_m,  eps_r,  description)
    (0.00001,      3.0),   # Very dry ground / polar ice
    (0.0001,       3.0),   # Very dry ground
    (0.0003,       7.0),   # Dry ground
    (0.001,        15.0),  # Medium dry ground
    (0.003,        15.0),  # Average land
    (0.005,        15.0),  # Temperate land (fallback)
    (0.01,         30.0),  # Wet ground / fresh water
    (0.03,         30.0),  # Wet ground
    (0.1,          40.0),  # Marshy land
    (0.3,          40.0),  # Marshy / brackish water
    (1.0,          80.0),  # Brackish / low-salinity sea
    (5.0,          70.0),  # Sea water
]


def _sigma_to_epsr(sigma_s_m: float) -> float:
    """Estimate relative permittivity from conductivity using P.527 pairs."""
    if sigma_s_m <= _P527_PAIRS[0][0]:
        return _P527_PAIRS[0][1]
    if sigma_s_m >= _P527_PAIRS[-1][0]:
        return _P527_PAIRS[-1][1]
    # Find the nearest pair by log-distance
    import math
    log_s = math.log10(max(sigma_s_m, 1e-10))
    best_eps = FALLBACK_EPS_R
    best_dist = float("inf")
    for s, e in _P527_PAIRS:
        d = abs(math.log10(max(s, 1e-10)) - log_s)
        if d < best_dist:
            best_dist = d
            best_eps = e
    return best_eps


def _require_gdal():
    try:
        from osgeo import gdal, osr
        gdal.UseExceptions()
        return gdal, osr
    except ImportError:
        print("ERROR: GDAL Python bindings not found.", file=sys.stderr)
        print("Install with: sudo apt-get install python3-gdal  (or pip install GDAL)",
              file=sys.stderr)
        sys.exit(1)


def _require_ogr():
    try:
        from osgeo import ogr
        return ogr
    except ImportError:
        print("ERROR: GDAL/OGR Python bindings not found.", file=sys.stderr)
        print("Install with: sudo apt-get install python3-gdal  (or pip install GDAL)",
              file=sys.stderr)
        sys.exit(1)


# ---------------------------------------------------------------------------
# GeoJSON (IDWM) loader
# ---------------------------------------------------------------------------

# Candidate property names for conductivity (case-insensitive matching)
_SIGMA_CANDIDATES = [
    "conductivity", "sigma", "cond", "gc", "ground_conductivity",
    "conductivity_ms_m", "sigma_ms_m", "value", "val",
]

# Candidate property names for permittivity
_EPSR_CANDIDATES = [
    "permittivity", "eps_r", "epsr", "epsilon", "relative_permittivity",
    "eps", "dielectric",
]


def _find_property(field_names, candidates, label):
    """
    Find a feature property name matching one of the candidates.
    Returns (property_name, found) or (None, False).
    """
    lower_fields = {f.lower(): f for f in field_names}
    for cand in candidates:
        if cand in lower_fields:
            return lower_fields[cand], True
    # Substring match as fallback
    for cand in candidates:
        for lf, orig in lower_fields.items():
            if cand in lf:
                return orig, True
    return None, False


def _load_geojson(geojson_path: Path, verbose: bool,
                  sigma_prop_override: str = None,
                  epsr_prop_override: str = None,
                  resolution: float = None):
    """
    Load an IDWM GeoJSON file and rasterize conductivity zones.

    The GeoJSON contains polygon features with a conductivity attribute
    (in mS/m).  We rasterize to a regular lat/lon grid.

    Returns (sigma_flat, eps_flat, ncols, nrows, lonmin, latmax, dlon, dlat).
    All conductivity values are converted to S/m in the output.
    """
    gdal, osr = _require_gdal()
    ogr = _require_ogr()

    ds = ogr.Open(str(geojson_path))
    if ds is None:
        raise RuntimeError(f"Could not open {geojson_path} with OGR")

    layer = ds.GetLayer(0)
    if layer is None:
        raise RuntimeError(f"No layers found in {geojson_path}")

    feat_count = layer.GetFeatureCount()
    if verbose:
        print(f"  GeoJSON: {feat_count} features in layer '{layer.GetName()}'")

    # Discover field names
    layer_defn = layer.GetLayerDefn()
    field_names = [layer_defn.GetFieldDefn(i).GetName()
                   for i in range(layer_defn.GetFieldCount())]
    if verbose:
        print(f"  Fields: {field_names}")

    # Find conductivity property
    if sigma_prop_override:
        sigma_prop = sigma_prop_override
    else:
        sigma_prop, found = _find_property(field_names, _SIGMA_CANDIDATES,
                                           "conductivity")
        if not found:
            # If there's only one numeric field, use it
            numeric_fields = []
            for i in range(layer_defn.GetFieldCount()):
                fd = layer_defn.GetFieldDefn(i)
                if fd.GetType() in (ogr.OFTInteger, ogr.OFTReal,
                                    ogr.OFTInteger64):
                    numeric_fields.append(fd.GetName())
            if len(numeric_fields) == 1:
                sigma_prop = numeric_fields[0]
                if verbose:
                    print(f"  Using sole numeric field '{sigma_prop}' "
                          "as conductivity")
            else:
                raise RuntimeError(
                    f"Could not auto-detect conductivity property in "
                    f"{field_names}.  Use --sigma-property to specify it.")

    if verbose:
        print(f"  Conductivity property: '{sigma_prop}'")

    # Find permittivity property (optional)
    has_epsr = False
    if epsr_prop_override:
        epsr_prop = epsr_prop_override
        has_epsr = True
    else:
        epsr_prop, has_epsr = _find_property(field_names, _EPSR_CANDIDATES,
                                             "permittivity")
    if has_epsr and verbose:
        print(f"  Permittivity property: '{epsr_prop}'")
    elif verbose:
        print("  No permittivity property found; will estimate from "
              "conductivity using ITU-R P.527")

    # Determine layer extent
    extent = layer.GetExtent()  # (minX, maxX, minY, maxY)
    lonmin = extent[0]
    lonmax = extent[1]
    latmin = extent[2]
    latmax = extent[3]

    if verbose:
        print(f"  Extent: lon [{lonmin:.2f}, {lonmax:.2f}], "
              f"lat [{latmin:.2f}, {latmax:.2f}]")

    # Determine raster resolution
    dlon = resolution if resolution else DEFAULT_DLON
    dlat = resolution if resolution else DEFAULT_DLAT

    ncols = max(1, int(round((lonmax - lonmin) / dlon)))
    nrows = max(1, int(round((latmax - latmin) / dlat)))
    # Adjust lonmax/latmax to align to grid
    lonmax_aligned = lonmin + ncols * dlon
    latmax_aligned = latmin + nrows * dlat

    if verbose:
        print(f"  Raster: {nrows} x {ncols} cells at "
              f"{dlon} x {dlat} deg resolution")

    # Detect conductivity units by sampling values
    sigma_values = []
    layer.ResetReading()
    for feat in layer:
        val = feat.GetField(sigma_prop)
        if val is not None:
            sigma_values.append(float(val))
    layer.ResetReading()

    if not sigma_values:
        raise RuntimeError(
            f"No valid values found in property '{sigma_prop}'")

    max_sigma = max(sigma_values)
    min_sigma = min(v for v in sigma_values if v > 0) if any(
        v > 0 for v in sigma_values) else 0

    # IDWM uses mS/m.  If max value > 100, almost certainly mS/m.
    # Typical sea water = 5000 mS/m = 5 S/m.
    is_ms_per_m = max_sigma > 100
    if verbose:
        unit = "mS/m" if is_ms_per_m else "S/m"
        print(f"  Conductivity range: {min_sigma:.4g} - {max_sigma:.4g} "
              f"(detected as {unit})")

    # Create in-memory raster for conductivity
    mem_driver = gdal.GetDriverByName("MEM")
    sigma_ds = mem_driver.Create("", ncols, nrows, 1, gdal.GDT_Float32)
    sigma_ds.SetGeoTransform([lonmin, dlon, 0.0, latmax_aligned, 0.0, -dlat])
    srs = osr.SpatialReference()
    srs.ImportFromEPSG(4326)
    sigma_ds.SetProjection(srs.ExportToWkt())

    band = sigma_ds.GetRasterBand(1)
    band.SetNoDataValue(-9999.0)
    band.Fill(-9999.0)

    # Rasterize conductivity
    err = gdal.RasterizeLayer(sigma_ds, [1], layer,
                              options=[f"ATTRIBUTE={sigma_prop}"])
    if err != 0:
        raise RuntimeError(f"RasterizeLayer failed with error code {err}")

    # Read back conductivity raster
    sigma_flat = list(band.ReadRaster(0, 0, ncols, nrows,
                                      buf_type=gdal.GDT_Float32))
    # ReadRaster returns bytes; unpack
    import struct
    sigma_flat = list(struct.unpack(f"{ncols * nrows}f",
                      band.ReadRaster(0, 0, ncols, nrows,
                                      buf_type=gdal.GDT_Float32)))

    sigma_ds = None  # close

    # Rasterize permittivity if available
    if has_epsr:
        epsr_ds = mem_driver.Create("", ncols, nrows, 1, gdal.GDT_Float32)
        epsr_ds.SetGeoTransform([lonmin, dlon, 0.0, latmax_aligned, 0.0,
                                 -dlat])
        epsr_ds.SetProjection(srs.ExportToWkt())
        eband = epsr_ds.GetRasterBand(1)
        eband.SetNoDataValue(-9999.0)
        eband.Fill(-9999.0)

        layer.ResetReading()
        err = gdal.RasterizeLayer(epsr_ds, [1], layer,
                                  options=[f"ATTRIBUTE={epsr_prop}"])
        if err != 0:
            raise RuntimeError(
                f"RasterizeLayer (permittivity) failed with error {err}")

        eps_flat = list(struct.unpack(
            f"{ncols * nrows}f",
            eband.ReadRaster(0, 0, ncols, nrows, buf_type=gdal.GDT_Float32)))
        epsr_ds = None
    else:
        eps_flat = [0.0] * (ncols * nrows)

    ds = None  # close OGR dataset

    # Post-process: convert units, fill no-data, estimate permittivity
    nodata_count = 0
    for i in range(ncols * nrows):
        s = sigma_flat[i]
        if s <= 0 or s == -9999.0:
            sigma_flat[i] = FALLBACK_SIGMA
            eps_flat[i] = FALLBACK_EPS_R
            nodata_count += 1
        else:
            # Convert mS/m to S/m if needed
            if is_ms_per_m:
                sigma_flat[i] = s / 1000.0
            if has_epsr:
                e = eps_flat[i]
                if e <= 0 or e == -9999.0:
                    eps_flat[i] = _sigma_to_epsr(sigma_flat[i])
            else:
                eps_flat[i] = _sigma_to_epsr(sigma_flat[i])

    if verbose:
        filled = ncols * nrows - nodata_count
        print(f"  Rasterized {filled} cells with data, "
              f"{nodata_count} filled with fallback")

    return (sigma_flat, eps_flat, ncols, nrows,
            lonmin, latmax_aligned, dlon, dlat)


# ---------------------------------------------------------------------------
# Legacy loaders (ESRT/EPRT text tables, CSV)
# ---------------------------------------------------------------------------

def _read_itu_table(text: str) -> list:
    """
    Parse an ITU-R P.832 ASCII table.  Rows are latitude bands from +90 deg
    (first row) to -90 deg (last row); columns are longitude from -180 deg to
    +180 deg at 1.5 deg steps.  Values may be separated by commas, spaces, or
    tabs.
    """
    rows = []
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith("#") or line.startswith("%"):
            continue
        # Try comma then whitespace splitting
        if "," in line:
            parts = [x.strip() for x in line.split(",")]
        else:
            parts = line.split()
        try:
            row = [float(x) for x in parts if x]
        except ValueError:
            continue
        if row:
            rows.append(row)
    return rows


def _load_itu_tables_from_dir(dirpath: Path, verbose: bool):
    """Load ESRT.txt and EPRT.txt from a directory."""
    esrt_path = None
    eprt_path = None
    for name in os.listdir(dirpath):
        lower = name.lower()
        if "esrt" in lower or "sigma" in lower or "conductiv" in lower:
            esrt_path = dirpath / name
        elif "eprt" in lower or "eps" in lower or "permit" in lower:
            eprt_path = dirpath / name

    if not esrt_path:
        raise RuntimeError(
            f"Could not find conductivity table (ESRT.txt) in {dirpath}")
    if not eprt_path:
        raise RuntimeError(
            f"Could not find permittivity table (EPRT.txt) in {dirpath}")

    if verbose:
        print(f"  Reading conductivity: {esrt_path}")
        print(f"  Reading permittivity: {eprt_path}")

    esrt = _read_itu_table(esrt_path.read_text(errors="replace"))
    eprt = _read_itu_table(eprt_path.read_text(errors="replace"))
    return esrt, eprt


def _load_itu_tables_from_zip(zip_path: Path, verbose: bool):
    """Load ESRT.txt and EPRT.txt from a zip archive."""
    esrt_text = eprt_text = None
    with zipfile.ZipFile(zip_path) as zf:
        names_lower = {n.lower(): n for n in zf.namelist()}
        for key, val in names_lower.items():
            if "esrt" in key or ("sigma" in key and key.endswith(".txt")):
                esrt_text = zf.read(val).decode("ascii", errors="replace")
            elif "eprt" in key or ("eps" in key and key.endswith(".txt")):
                eprt_text = zf.read(val).decode("ascii", errors="replace")

    if not esrt_text:
        raise RuntimeError(
            f"Could not find conductivity table (ESRT.txt) in {zip_path}")
    if not eprt_text:
        raise RuntimeError(
            f"Could not find permittivity table (EPRT.txt) in {zip_path}")

    esrt = _read_itu_table(esrt_text)
    eprt = _read_itu_table(eprt_text)
    return esrt, eprt


def _tables_to_arrays(esrt_rows, eprt_rows, nrows, ncols, verbose):
    """
    Convert row lists to flat (row-major) lists.
    ITU tables are ordered North-to-South (row 0 = lat +90).
    GeoTIFF top-left origin convention matches this.
    """
    sigma_flat = [FALLBACK_SIGMA] * (nrows * ncols)
    eps_flat   = [FALLBACK_EPS_R] * (nrows * ncols)

    def fill(rows, flat, fallback):
        for r, row_vals in enumerate(rows[:nrows]):
            for c, val in enumerate(row_vals[:ncols]):
                if val > 0:
                    flat[r * ncols + c] = val
                else:
                    flat[r * ncols + c] = fallback

    fill(esrt_rows, sigma_flat, FALLBACK_SIGMA)
    fill(eprt_rows,  eps_flat,  FALLBACK_EPS_R)

    if verbose:
        non_default = sum(1 for v in sigma_flat if v != FALLBACK_SIGMA)
        print(f"  {non_default} non-default conductivity cells "
              f"out of {nrows*ncols}")

    return sigma_flat, eps_flat


def _load_csv(csv_path: Path, verbose: bool):
    """
    Load a CSV with columns: lon, lat, sigma [S/m], eps_r.
    Column order is auto-detected from header if present.
    Returns (sigma_flat, eps_flat, ncols, nrows, lonmin, latmin, dlon, dlat).
    """
    rows = []
    with open(csv_path, newline="") as f:
        sample = f.read(1024)
        f.seek(0)
        has_header = not sample.lstrip()[0].lstrip("-").replace(".", ""
                                                                ).isdigit()
        reader = csv.reader(f)
        if has_header:
            header = [h.strip().lower() for h in next(reader)]
            lon_i = next((i for i, h in enumerate(header) if "lon" in h), 0)
            lat_i = next((i for i, h in enumerate(header) if "lat" in h), 1)
            sig_i = next((i for i, h in enumerate(header)
                          if "sigma" in h or "conduct" in h), 2)
            eps_i = next((i for i, h in enumerate(header)
                          if "eps" in h or "permit" in h), 3)
        else:
            lon_i, lat_i, sig_i, eps_i = 0, 1, 2, 3
        for row in reader:
            try:
                rows.append((float(row[lon_i]), float(row[lat_i]),
                             float(row[sig_i]), float(row[eps_i])))
            except (ValueError, IndexError):
                continue

    if not rows:
        raise RuntimeError(f"No data rows found in {csv_path}")

    # Determine grid bounds
    lons = sorted(set(r[0] for r in rows))
    lats = sorted(set(r[1] for r in rows))
    dlon = round(lons[1] - lons[0], 6) if len(lons) > 1 else DEFAULT_DLON
    dlat = round(lats[1] - lats[0], 6) if len(lats) > 1 else DEFAULT_DLAT
    lonmin = min(lons)
    latmin = min(lats)
    ncols  = len(lons)
    nrows  = len(lats)

    sigma_flat = [FALLBACK_SIGMA] * (nrows * ncols)
    eps_flat   = [FALLBACK_EPS_R] * (nrows * ncols)

    for lon, lat, sigma, eps_r in rows:
        c = round((lon - lonmin) / dlon)
        # Lat rows: GeoTIFF top = latmax (last in ascending lats)
        r = nrows - 1 - round((lat - latmin) / dlat)
        if 0 <= c < ncols and 0 <= r < nrows:
            sigma_flat[r * ncols + c] = sigma
            eps_flat  [r * ncols + c] = eps_r

    if verbose:
        print(f"  CSV: {nrows} x {ncols} grid, "
              f"lon [{lonmin}..{lonmin+ncols*dlon}], "
              f"lat [{latmin}..{latmin+nrows*dlat}]")

    return (sigma_flat, eps_flat, ncols, nrows,
            lonmin, latmin + nrows * dlat, dlon, dlat)


# ---------------------------------------------------------------------------
# GeoTIFF writer
# ---------------------------------------------------------------------------

def write_geotiff(out_path: str,
                  sigma_flat, eps_flat,
                  ncols, nrows,
                  lonmin, latmax,
                  dlon, dlat,
                  verbose: bool) -> None:
    gdal, osr = _require_gdal()

    driver = gdal.GetDriverByName("GTiff")
    ds = driver.Create(out_path, ncols, nrows, 2, gdal.GDT_Float32,
                       ["COMPRESS=DEFLATE", "TILED=YES"])
    if ds is None:
        raise RuntimeError(f"Could not create {out_path}")

    # GeoTransform: (top-left-lon, dlon, 0, top-left-lat, 0, -dlat)
    ds.SetGeoTransform([lonmin, dlon, 0.0, latmax, 0.0, -dlat])

    srs = osr.SpatialReference()
    srs.ImportFromEPSG(4326)  # WGS84
    ds.SetProjection(srs.ExportToWkt())

    import array as _array

    band1 = ds.GetRasterBand(1)
    band1.SetDescription("Conductivity [S/m]")
    band1.SetNoDataValue(-9999.0)
    arr1 = _array.array("f", sigma_flat)
    band1.WriteRaster(0, 0, ncols, nrows, arr1.tobytes())

    band2 = ds.GetRasterBand(2)
    band2.SetDescription("Relative permittivity")
    band2.SetNoDataValue(-9999.0)
    arr2 = _array.array("f", eps_flat)
    band2.WriteRaster(0, 0, ncols, nrows, arr2.tobytes())

    ds.FlushCache()
    ds = None

    size_kb = os.path.getsize(out_path) / 1024
    if verbose:
        print(f"  Written {out_path}  ({size_kb:.0f} KB, "
              f"{nrows} x {ncols} cells)")


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def _is_geojson(path: Path) -> bool:
    """Check if path looks like a GeoJSON file."""
    suffix = path.suffix.lower()
    if suffix in (".geojson", ".json"):
        return True
    # Peek at first bytes for GeoJSON signature
    if path.is_file():
        try:
            with open(path) as f:
                head = f.read(256).lstrip()
                if head.startswith("{") and '"type"' in head:
                    return True
        except (OSError, UnicodeDecodeError):
            pass
    return False


def main():
    parser = argparse.ArgumentParser(
        description="Convert ITU-R P.832 / IDWM conductivity data to a "
                    "BANDPASS II GeoTIFF."
    )
    parser.add_argument(
        "--input", "-i", required=True, metavar="PATH",
        help="GeoJSON from IDWM GeoCatalogue, directory with ESRT.txt/"
             "EPRT.txt, a zip archive, or a CSV file"
    )
    parser.add_argument(
        "--out", "-o", default="conductivity_itu_p832.tif", metavar="FILE",
        help="Output GeoTIFF path (default: conductivity_itu_p832.tif)"
    )
    parser.add_argument(
        "--resolution", "-r", type=float, default=None, metavar="DEG",
        help="Output grid cell size in degrees (default: 1.5 x 1.0 for "
             "legacy, auto for GeoJSON)"
    )
    parser.add_argument(
        "--sigma-property", default=None, metavar="NAME",
        help="GeoJSON property name for conductivity (auto-detected if "
             "not specified)"
    )
    parser.add_argument(
        "--epsr-property", default=None, metavar="NAME",
        help="GeoJSON property name for relative permittivity (estimated "
             "from conductivity if not specified)"
    )
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args()
    verbose = not args.quiet

    inp = Path(args.input)
    if not inp.exists():
        print(f"ERROR: Input path not found: {inp}", file=sys.stderr)
        sys.exit(1)

    if verbose:
        print(f"Loading conductivity data from {inp} ...")

    if _is_geojson(inp):
        sigma_flat, eps_flat, ncols, nrows, lonmin, latmax, dlon, dlat = \
            _load_geojson(inp, verbose,
                          sigma_prop_override=args.sigma_property,
                          epsr_prop_override=args.epsr_property,
                          resolution=args.resolution)
    elif inp.suffix.lower() == ".csv":
        sigma_flat, eps_flat, ncols, nrows, lonmin, latmax, dlon, dlat = \
            _load_csv(inp, verbose)
    elif inp.suffix.lower() == ".zip":
        esrt, eprt = _load_itu_tables_from_zip(inp, verbose)
        ncols, nrows = DEFAULT_NCOLS, DEFAULT_NROWS
        lonmin = DEFAULT_LONMIN
        latmax = DEFAULT_LATMIN + nrows * DEFAULT_DLAT
        dlon, dlat = DEFAULT_DLON, DEFAULT_DLAT
        sigma_flat, eps_flat = _tables_to_arrays(esrt, eprt, nrows, ncols,
                                                 verbose)
    elif inp.is_dir():
        esrt, eprt = _load_itu_tables_from_dir(inp, verbose)
        ncols, nrows = DEFAULT_NCOLS, DEFAULT_NROWS
        lonmin = DEFAULT_LONMIN
        latmax = DEFAULT_LATMIN + nrows * DEFAULT_DLAT
        dlon, dlat = DEFAULT_DLON, DEFAULT_DLAT
        sigma_flat, eps_flat = _tables_to_arrays(esrt, eprt, nrows, ncols,
                                                 verbose)
    else:
        print(f"ERROR: Unrecognised input format: {inp}", file=sys.stderr)
        print("Expected: a .geojson/.json file, a directory with "
              "ESRT.txt/EPRT.txt, a .zip, or a .csv", file=sys.stderr)
        sys.exit(1)

    if verbose:
        print(f"Writing GeoTIFF to {args.out} ...")
    write_geotiff(args.out, sigma_flat, eps_flat,
                  ncols, nrows, lonmin, latmax, dlon, dlat, verbose)

    if verbose:
        print("Done.")
        print()
        print("To use in BANDPASS II, set conductivity.source in your "
              "scenario TOML to:")
        print(f"  source = \"{args.out}\"")


if __name__ == "__main__":
    main()
