#!/usr/bin/env python3
"""
itu_p832_import.py — Convert the ITU-R P.832 world conductivity map to a
2-band GeoTIFF suitable for loading by BANDPASS II's GdalConductivityMap.

Band 1: electrical conductivity  [S/m]
Band 2: relative permittivity    [dimensionless]

ITU-R P.832 provides ground conductivity and permittivity in a global
gridded dataset at 1.5° × 1° resolution (longitude × latitude).  The
data are available from the ITU in digital form.  Two versions are in
common use:

  * ITU-R P.832-4 (2015): "Characteristics of the surface of the Earth"
    — distributed as a pair of text tables or a Matlab .mat file.
  * The R package "itur" includes the dataset in CSV form.

This script accepts:
  a) A folder or ZIP containing the official ITU tables "ESRT.txt" and
     "EPRT.txt" (conductivity and permittivity 1.5°×1° ASCII grids).
  b) A single CSV with columns: lon, lat, conductivity_s_m, eps_r
     (e.g. from the itur R package or manual export).

Output: a GeoTIFF with WGS84 CRS, cell size 1.5° (lon) × 1.0° (lat),
origin at (-180, -90), band1 = conductivity, band2 = permittivity.
Requires: GDAL Python bindings (python3-gdal / gdal-python).

Usage:
    python3 tools/itu_p832_import.py --input PATH [--out PATH]

    PATH may be:
      - A directory containing ESRT.txt and EPRT.txt
      - A zip file containing those files
      - A CSV with lon,lat,sigma,eps_r columns

Examples:
    python3 tools/itu_p832_import.py --input ~/itu-p832/ --out conductivity.tif
    python3 tools/itu_p832_import.py --input p832.csv
"""

import argparse
import csv
import io
import os
import struct
import sys
import zipfile
from pathlib import Path

# Grid dimensions for the standard ITU-R P.832 raster
# Longitude: -180 to +180 at 1.5° spacing → 241 columns
# Latitude:  -90  to +90  at 1.0° spacing → 181 rows
DEFAULT_NCOLS   = 241
DEFAULT_NROWS   = 181
DEFAULT_LONMIN  = -180.0
DEFAULT_LATMIN  = -90.0
DEFAULT_DLON    = 1.5
DEFAULT_DLAT    = 1.0

# Fallback values used when a cell is missing or marked as no-data
FALLBACK_SIGMA  = 0.005   # S/m  (temperate land, ITU Table 1)
FALLBACK_EPS_R  = 15.0    # dimensionless


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


def _read_itu_table(text: str) -> list:
    """
    Parse an ITU-R P.832 ASCII table.  Rows are latitude bands from +90°
    (first row) to -90° (last row); columns are longitude from -180° to
    +180° at 1.5° steps.  Values may be separated by commas, spaces, or
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
        raise RuntimeError(f"Could not find conductivity table (ESRT.txt) in {dirpath}")
    if not eprt_path:
        raise RuntimeError(f"Could not find permittivity table (EPRT.txt) in {dirpath}")

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
        raise RuntimeError(f"Could not find conductivity table (ESRT.txt) in {zip_path}")
    if not eprt_text:
        raise RuntimeError(f"Could not find permittivity table (EPRT.txt) in {zip_path}")

    esrt = _read_itu_table(esrt_text)
    eprt = _read_itu_table(eprt_text)
    return esrt, eprt


def _tables_to_arrays(esrt_rows, eprt_rows, nrows, ncols, verbose):
    """
    Convert row lists to flat (row-major) numpy-style lists.
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
        print(f"  {non_default} non-default conductivity cells out of {nrows*ncols}")

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
        has_header = not sample.lstrip()[0].lstrip("-").replace(".","").isdigit()
        reader = csv.reader(f)
        if has_header:
            header = [h.strip().lower() for h in next(reader)]
            lon_i   = next((i for i, h in enumerate(header) if "lon" in h), 0)
            lat_i   = next((i for i, h in enumerate(header) if "lat" in h), 1)
            sig_i   = next((i for i, h in enumerate(header)
                            if "sigma" in h or "conduct" in h), 2)
            eps_i   = next((i for i, h in enumerate(header)
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
        print(f"  CSV: {nrows}×{ncols} grid, lon [{lonmin}..{lonmin+ncols*dlon}],"
              f" lat [{latmin}..{latmin+nrows*dlat}]")

    return sigma_flat, eps_flat, ncols, nrows, lonmin, latmin + nrows * dlat, dlon, dlat


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
        print(f"  Written {out_path}  ({size_kb:.0f} KB, {nrows}×{ncols} cells)")


def main():
    parser = argparse.ArgumentParser(
        description="Convert ITU-R P.832 conductivity data to a BANDPASS II GeoTIFF."
    )
    parser.add_argument(
        "--input", "-i", required=True, metavar="PATH",
        help="Directory containing ESRT.txt/EPRT.txt, a zip archive, or a CSV file"
    )
    parser.add_argument(
        "--out", "-o", default="conductivity_itu_p832.tif", metavar="FILE",
        help="Output GeoTIFF path (default: conductivity_itu_p832.tif)"
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

    if inp.suffix.lower() == ".csv":
        sigma_flat, eps_flat, ncols, nrows, lonmin, latmax, dlon, dlat = \
            _load_csv(inp, verbose)
    elif inp.suffix.lower() == ".zip":
        esrt, eprt = _load_itu_tables_from_zip(inp, verbose)
        ncols, nrows = DEFAULT_NCOLS, DEFAULT_NROWS
        lonmin = DEFAULT_LONMIN
        latmax = DEFAULT_LATMIN + nrows * DEFAULT_DLAT
        dlon, dlat = DEFAULT_DLON, DEFAULT_DLAT
        sigma_flat, eps_flat = _tables_to_arrays(esrt, eprt, nrows, ncols, verbose)
    elif inp.is_dir():
        esrt, eprt = _load_itu_tables_from_dir(inp, verbose)
        ncols, nrows = DEFAULT_NCOLS, DEFAULT_NROWS
        lonmin = DEFAULT_LONMIN
        latmax = DEFAULT_LATMIN + nrows * DEFAULT_DLAT
        dlon, dlat = DEFAULT_DLON, DEFAULT_DLAT
        sigma_flat, eps_flat = _tables_to_arrays(esrt, eprt, nrows, ncols, verbose)
    else:
        print(f"ERROR: Unrecognised input format: {inp}", file=sys.stderr)
        print("Expected: a directory with ESRT.txt/EPRT.txt, a .zip, or a .csv",
              file=sys.stderr)
        sys.exit(1)

    if verbose:
        print(f"Writing GeoTIFF to {args.out} ...")
    write_geotiff(args.out, sigma_flat, eps_flat,
                  ncols, nrows, lonmin, latmax, dlon, dlat, verbose)

    if verbose:
        print("Done.")
        print()
        print("To use in BANDPASS II, set conductivity.source in your scenario TOML to:")
        print(f"  source = \"{args.out}\"")


if __name__ == "__main__":
    main()
