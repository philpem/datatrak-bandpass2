#!/usr/bin/env python3
"""
ostn15_download.py — Download and convert the Ordnance Survey OSTN15 datum
shift grid to the BANDPASS II binary format.

Usage:
    python3 tools/ostn15_download.py [--out PATH]

The script downloads the OSTN15/OSGM15 data file from the Ordnance Survey
website, parses the CSV records, and writes a compact binary grid file that
src/coords/Osgb.cpp can load at runtime.

Output format (OSTN15 binary, magic "OSTN1500"):
  [0-7]   magic: b"OSTN1500"
  [8-11]  version: uint32 LE = 1
  [12-15] ncols:   uint32 LE (typically 701)
  [16-19] nrows:   uint32 LE (typically 1251)
  [20-27] origin_e: float64 LE (typically 0.0 m)
  [28-35] origin_n: float64 LE (typically 0.0 m)
  [36-43] cell_size: float64 LE (typically 1000.0 m)
  [44..]  pairs of float32 LE: (se, sn) per cell, row-major
          row 0 = northing = origin_n, row 1 = origin_n + cell_size, ...
          Zero se/sn indicates outside GB coverage.

The resulting file is ~6.7 MB.

The OS data file ("OSTN15_OSGM15_DataFile.txt") is published under the
OS OpenData licence.  Download it from:
  https://www.ordnancesurvey.co.uk/documents/resources/ostn15-osgm15-bin.zip
"""

import argparse
import csv
import io
import os
import struct
import sys
import urllib.request
import zipfile

# OS OpenData download URL for the combined OSTN15/OSGM15 data file.
# At time of writing this is the official distribution zip.
OSTN15_URL = (
    "https://www.ordnancesurvey.co.uk/documents/resources/ostn15-osgm15-bin.zip"
)

# The name of the text data file inside the zip archive
OSTN15_FILENAME_IN_ZIP = "OSTN15_OSGM15_DataFile.txt"

# Grid parameters (constant for OSTN15)
NCOLS      = 701    # E: 0 .. 700 000 m at 1 km spacing
NROWS      = 1251   # N: 0 .. 1 250 000 m at 1 km spacing
ORIGIN_E   = 0.0    # metres
ORIGIN_N   = 0.0    # metres
CELL_SIZE  = 1000.0 # metres

MAGIC   = b"OSTN1500"
VERSION = 1


def download_data(url: str, verbose: bool) -> bytes:
    if verbose:
        print(f"Downloading {url} ...", flush=True)
    with urllib.request.urlopen(url, timeout=120) as resp:
        data = resp.read()
    if verbose:
        print(f"  Downloaded {len(data):,} bytes", flush=True)
    return data


def extract_csv_from_zip(zip_bytes: bytes, filename: str) -> str:
    with zipfile.ZipFile(io.BytesIO(zip_bytes)) as zf:
        # Find the file case-insensitively
        names = {n.lower(): n for n in zf.namelist()}
        key = filename.lower()
        if key not in names:
            raise RuntimeError(
                f"'{filename}' not found in zip archive.\n"
                f"Files present: {list(zf.namelist())}"
            )
        with zf.open(names[key]) as f:
            return f.read().decode("ascii", errors="replace")


def parse_csv(csv_text: str, verbose: bool):
    """
    Parse the OSTN15 CSV.  Returns a dict (col, row) -> (se, sn).

    The OS CSV has columns (some variants use different separators):
        POINT_ID, EASTING, NORTHING, EAST_SHIFT, NORTH_SHIFT, GEOID_HEIGHT
    Easting/northing are in km (some releases) or m (check header).
    """
    se_grid = {}
    sn_grid = {}

    reader = csv.reader(csv_text.splitlines())
    first_row = True
    e_scale = 1.0  # will be determined from first data row

    for row in reader:
        if not row:
            continue
        # Skip header / comment lines
        if not row[0].strip().lstrip("-").isdigit():
            continue

        try:
            # POINT_ID, EASTING(km), NORTHING(km), SE(m), SN(m), SG(m)
            easting  = float(row[1]) * 1000.0  # km → m
            northing = float(row[2]) * 1000.0  # km → m
            se       = float(row[3])
            sn       = float(row[4])
        except (ValueError, IndexError):
            continue

        # Detect whether easting is already in metres (values > 1000 suggest metres)
        if first_row:
            if float(row[1]) > 1000.0:
                # Probably already in metres
                easting  = float(row[1])
                northing = float(row[2])
            first_row = False
            # Re-parse with detected scale
            if float(row[1]) > 1000.0:
                e_scale = 1.0  # already metres
            else:
                e_scale = 1000.0  # km → m

        easting  = float(row[1]) * e_scale
        northing = float(row[2]) * e_scale
        se       = float(row[3])
        sn       = float(row[4])

        col = round(easting  / CELL_SIZE)
        row_idx = round(northing / CELL_SIZE)

        if 0 <= col < NCOLS and 0 <= row_idx < NROWS:
            se_grid[(col, row_idx)] = se
            sn_grid[(col, row_idx)] = sn

    if verbose:
        print(f"  Parsed {len(se_grid):,} shift nodes", flush=True)

    return se_grid, sn_grid


def write_binary(out_path: str,
                 se_grid: dict, sn_grid: dict,
                 verbose: bool) -> None:
    n_cells = NCOLS * NROWS
    # Build flat arrays (row-major: row 0 = N=0, row 1 = N=1000, ...)
    se_flat = [0.0] * n_cells
    sn_flat = [0.0] * n_cells
    for (col, row_idx), val in se_grid.items():
        se_flat[row_idx * NCOLS + col] = val
    for (col, row_idx), val in sn_grid.items():
        sn_flat[row_idx * NCOLS + col] = val

    with open(out_path, "wb") as f:
        f.write(MAGIC)
        f.write(struct.pack("<I", VERSION))
        f.write(struct.pack("<I", NCOLS))
        f.write(struct.pack("<I", NROWS))
        f.write(struct.pack("<d", ORIGIN_E))
        f.write(struct.pack("<d", ORIGIN_N))
        f.write(struct.pack("<d", CELL_SIZE))
        for i in range(n_cells):
            f.write(struct.pack("<ff", se_flat[i], sn_flat[i]))

    size_mb = os.path.getsize(out_path) / 1_048_576
    if verbose:
        print(f"  Written {out_path}  ({size_mb:.1f} MB)", flush=True)


def main():
    parser = argparse.ArgumentParser(
        description="Download and convert the OSTN15 datum shift grid."
    )
    parser.add_argument(
        "--out",
        default="OSTN15.dat",
        help="Output binary file path (default: OSTN15.dat)",
    )
    parser.add_argument(
        "--input-csv",
        default=None,
        metavar="FILE",
        help=(
            "Use a locally-downloaded CSV instead of fetching from the OS website. "
            "Accepts either the raw .txt file or a zip containing it."
        ),
    )
    parser.add_argument(
        "--quiet", action="store_true", help="Suppress progress messages"
    )
    args = parser.parse_args()
    verbose = not args.quiet

    if args.input_csv:
        path = args.input_csv
        if verbose:
            print(f"Reading {path} ...", flush=True)
        with open(path, "rb") as f:
            raw = f.read()
        if raw[:2] == b"PK":
            csv_text = extract_csv_from_zip(raw, OSTN15_FILENAME_IN_ZIP)
        else:
            csv_text = raw.decode("ascii", errors="replace")
    else:
        raw = download_data(OSTN15_URL, verbose)
        csv_text = extract_csv_from_zip(raw, OSTN15_FILENAME_IN_ZIP)

    if verbose:
        print("Parsing shift data ...", flush=True)
    se_grid, sn_grid = parse_csv(csv_text, verbose)

    if not se_grid:
        print("ERROR: No shift nodes were parsed.  Check the input file format.",
              file=sys.stderr)
        sys.exit(1)

    if verbose:
        print(f"Writing binary grid to {args.out} ...", flush=True)
    write_binary(args.out, se_grid, sn_grid, verbose)

    if verbose:
        print("Done.  Place the .dat file where BANDPASS II can find it,")
        print("then call bp::osgb::load_ostn15(path) at application startup.")


if __name__ == "__main__":
    main()
