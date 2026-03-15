#!/usr/bin/env python3
"""
ostn15_download.py — Download and convert the Ordnance Survey OSTN15 datum
shift grid to the BANDPASS II binary format.

Usage:
    python3 tools/ostn15_download.py [--out PATH]

Primary download source (NTv2 binary from OrdnanceSurvey GitHub):
    https://github.com/OrdnanceSurvey/os-transform

Fallback (manual): Download the developer pack from
    https://www.ordnancesurvey.co.uk/geodesy-positioning/coordinate-transformations/resources
and pass the text data file with --input-csv.

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
"""

import argparse
import csv
import io
import math
import os
import struct
import sys
import urllib.request
import zipfile

# ---------------------------------------------------------------------------
# Primary source: NTv2 .gsb from the official OrdnanceSurvey GitHub repo.
# This is the same data as the OS developer pack text file, in a different
# container format.  The GitHub raw CDN is stable and publicly accessible.
# ---------------------------------------------------------------------------
OSTN15_NTV2_URL = (
    "https://github.com/OrdnanceSurvey/os-transform"
    "/raw/refs/heads/main/resources/OSTN15_NTv2_OSGBtoETRS.gsb"
)

# Legacy OS website URL (was removed from OS website, kept as documentation).
# If the OS re-publishes the zip at a new URL, add it here.
OSTN15_CSV_URLS = [
    # Old URL (no longer works as of 2025):
    # "https://www.ordnancesurvey.co.uk/documents/resources/ostn15-osgm15-bin.zip",
]

# The name of the text data file inside the developer pack zip archive
OSTN15_FILENAME_IN_ZIP = "OSTN15_OSGM15_DataFile.txt"

# Output grid parameters (constant for OSTN15)
NCOLS     = 701    # E: 0 .. 700 000 m at 1 km spacing
NROWS     = 1251   # N: 0 .. 1 250 000 m at 1 km spacing
ORIGIN_E  = 0.0    # metres
ORIGIN_N  = 0.0    # metres
CELL_SIZE = 1000.0 # metres

MAGIC   = b"OSTN1500"
VERSION = 1

# ---------------------------------------------------------------------------
# OSGB36 Transverse Mercator (Airy 1830 ellipsoid)
# Used to convert lat/lon shifts from NTv2 into E/N shifts for our grid.
# ---------------------------------------------------------------------------
_A   = 6377563.396   # Airy 1830 semi-major axis (m)
_B   = 6356256.909   # Airy 1830 semi-minor axis (m)
_F0  = 0.9996012717  # central-meridian scale factor
_PHI0 = math.radians(49.0)   # true-origin latitude
_LAM0 = math.radians(-2.0)   # true-origin longitude
_E0   = 400_000.0            # false easting (m)
_N0   = -100_000.0           # false northing (m)


def _meridian_arc(phi: float, phi0: float) -> float:
    n = (_A - _B) / (_A + _B)
    dp, sp = phi - phi0, phi + phi0
    return _B * _F0 * (
        (1 + n + 5/4*n**2 + 5/4*n**3) * dp
        - (3*n + 3*n**2 + 21/8*n**3) * math.sin(dp) * math.cos(sp)
        + (15/8*n**2 + 15/8*n**3) * math.sin(2*dp) * math.cos(2*sp)
        - 35/24 * n**3 * math.sin(3*dp) * math.cos(3*sp)
    )


def _latlon_to_en(lat_deg: float, lon_deg: float):
    """OSGB36 TM: degrees → (E, N) metres (treats input as Airy lat/lon)."""
    phi = math.radians(lat_deg)
    lam = math.radians(lon_deg)
    e2  = (_A**2 - _B**2) / _A**2
    sl, cl, tl = math.sin(phi), math.cos(phi), math.tan(phi)
    nu   = _A * _F0 / math.sqrt(1 - e2 * sl**2)
    rho  = _A * _F0 * (1 - e2) / (1 - e2 * sl**2)**1.5
    eta2 = nu / rho - 1
    M    = _meridian_arc(phi, _PHI0)
    dl   = lam - _LAM0
    I    = M + _N0
    II   = nu / 2 * sl * cl
    III  = nu / 24 * sl * cl**3 * (5 - tl**2 + 9*eta2)
    IIIA = nu / 720 * sl * cl**5 * (61 - 58*tl**2 + tl**4)
    IV   = nu * cl
    V    = nu / 6 * cl**3 * (nu/rho - tl**2)
    VI   = nu / 120 * cl**5 * (5 - 18*tl**2 + tl**4 + 14*eta2 - 58*tl**2*eta2)
    N = I + II * dl**2 + III * dl**4 + IIIA * dl**6
    E = _E0 + IV * dl + V * dl**3 + VI * dl**5
    return E, N


def _en_to_latlon(E: float, N: float):
    """OSGB36 TM: (E, N) metres → (lat_deg, lon_deg)."""
    e2 = (_A**2 - _B**2) / _A**2
    phi_p = _PHI0
    M = 0.0
    for _ in range(20):
        phi_p = (N - _N0 - M) / (_A * _F0) + phi_p
        M = _meridian_arc(phi_p, _PHI0)
        if abs(N - _N0 - M) < 0.001:
            break
    sl, cl, tl = math.sin(phi_p), math.cos(phi_p), math.tan(phi_p)
    nu   = _A * _F0 / math.sqrt(1 - e2 * sl**2)
    rho  = _A * _F0 * (1 - e2) / (1 - e2 * sl**2)**1.5
    eta2 = nu / rho - 1
    VII  = tl / (2 * rho * nu)
    VIII = tl / (24 * rho * nu**3) * (5 + 3*tl**2 + eta2 - 9*tl**2*eta2)
    IX   = tl / (720 * rho * nu**5) * (61 + 90*tl**2 + 45*tl**4)
    X    = 1 / (cl * nu)
    XI   = 1 / (cl * 6 * nu**3) * (nu/rho + 2*tl**2)
    XII  = 1 / (cl * 120 * nu**5) * (5 + 28*tl**2 + 24*tl**4)
    XIIA = 1 / (cl * 5040 * nu**7) * (61 + 662*tl**2 + 1320*tl**4 + 720*tl**6)
    dE   = E - _E0
    phi  = phi_p - VII*dE**2 + VIII*dE**4 - IX*dE**6
    lam  = _LAM0 + X*dE - XI*dE**3 + XII*dE**5 - XIIA*dE**7
    return math.degrees(phi), math.degrees(lam)


# ---------------------------------------------------------------------------
# NTv2 binary parser
# ---------------------------------------------------------------------------

def _detect_ntv2_endian(data: bytes) -> str:
    """Return '>' or '<' based on the NUM_OREC value in the first record."""
    # First record: 8-byte key + 8-byte value.  KEY should be "NUM_OREC".
    # NTv2 keys are always stored as plain ASCII regardless of endianness,
    # so we detect endianness from the integer value, not the key orientation.
    key = data[0:8].rstrip(b'\x00').rstrip(b' ')
    if key != b'NUM_OREC':
        raise RuntimeError(
            f"Expected NUM_OREC as first NTv2 key, got {data[0:8]!r}"
        )
    # NUM_OREC value should be 11 (standard) — check which byte order gives
    # a sane small integer
    le_val = struct.unpack_from('<I', data, 8)[0]
    be_val = struct.unpack_from('>I', data, 8)[0]
    if le_val == 11:
        return '<'
    if be_val == 11:
        return '>'
    # Accept any small value as a hint
    if 1 <= le_val <= 100:
        return '<'
    if 1 <= be_val <= 100:
        return '>'
    raise RuntimeError(
        f"Cannot determine NTv2 endianness "
        f"(first 8 bytes: {data[0:8]!r}, values: BE={be_val} LE={le_val})"
    )


def parse_ntv2(data: bytes, verbose: bool):
    """
    Parse an NTv2 binary (.gsb) file produced by Ordnance Survey for OSTN15.

    The file converts FROM OSGB36 geodetic TO ETRS89 geodetic.
    Longitude values follow the NTv2 positive-west convention.
    Shifts are in arc-seconds.

    Returns (se_grid, sn_grid) dicts keyed by (col, row) in the BANDPASS II
    1-km OSGB36 E/N grid, with SE/SN shifts in metres.

    Algorithm for each NTv2 node at OSGB36 (phi_o, lam_o):
      ETRS89 ≈ WGS84 = (phi_o + dlat/3600, lam_o - dlon/3600)
                        [dlon positive west → subtract in degrees east]
      SE = latlon_to_en(phi_o, lam_o).E - latlon_to_en(phi_w, lam_w).E
      SN = latlon_to_en(phi_o, lam_o).N - latlon_to_en(phi_w, lam_w).N
    """
    endian = _detect_ntv2_endian(data)
    if verbose:
        print(f"  NTv2 endianness: {'big' if endian == '>' else 'little'}-endian",
              flush=True)

    pos = 0

    def read_key_val():
        nonlocal pos
        raw_key = data[pos:pos+8]
        raw_val = data[pos+8:pos+16]
        pos += 16
        key = raw_key.rstrip(b'\x00').rstrip(b' ').decode('ascii', errors='replace')
        return key, raw_val

    def as_int(v):
        return struct.unpack(endian + 'i', v[:4])[0]

    def as_double(v):
        return struct.unpack(endian + 'd', v)[0]

    # ---- overview header ----
    # Standard NTv2: NUM_OREC records in overview, NUM_SREC records per subgrid.
    num_orec = 11
    num_srec = 11
    num_file = 1
    records_read = 0
    while pos + 16 <= len(data) and records_read < 20:
        key, val = read_key_val()
        records_read += 1
        if key == 'NUM_OREC':
            num_orec = as_int(val)
        elif key == 'NUM_SREC':
            num_srec = as_int(val)
        elif key == 'NUM_FILE':
            num_file = as_int(val)
        # Stop after reading exactly num_orec records
        if records_read >= num_orec:
            break

    # Ensure position is exactly at end of overview header
    pos = num_orec * 16

    if verbose:
        print(f"  NTv2: {num_file} subgrid(s), "
              f"overview={num_orec} records, subgrid_hdr={num_srec} records",
              flush=True)

    se_grid: dict = {}
    sn_grid: dict = {}

    # ---- subgrids ----
    for sg_idx in range(num_file):
        s_lat = n_lat = e_long = w_long = lat_inc = long_inc = 0.0
        gs_count = 0
        sub_name = ''

        # Read exactly num_srec records for the subgrid header
        sg_header_start = pos
        for rec_i in range(num_srec):
            if pos + 16 > len(data):
                break
            key, val = read_key_val()
            if key == 'SUB_NAME':
                sub_name = val.rstrip(b'\x00').rstrip(b' ').decode('ascii', errors='replace')
            elif key == 'S_LAT':
                s_lat = as_double(val)
            elif key == 'N_LAT':
                n_lat = as_double(val)
            elif key == 'E_LONG':
                e_long = as_double(val)
            elif key == 'W_LONG':
                w_long = as_double(val)
            elif key == 'LAT_INC':
                lat_inc = as_double(val)
            elif key == 'LONG_INC':
                long_inc = as_double(val)
            elif key == 'GS_COUNT':
                gs_count = as_int(val)
        # Ensure position is exactly at end of subgrid header
        pos = sg_header_start + num_srec * 16

        if lat_inc <= 0 or long_inc <= 0 or gs_count <= 0:
            if verbose:
                print(f"  Skipping subgrid '{sub_name}': invalid header", flush=True)
            pos += gs_count * 16
            continue

        n_rows = round((n_lat - s_lat) / lat_inc) + 1
        n_cols = round((w_long - e_long) / long_inc) + 1

        if verbose:
            print(
                f"  Subgrid '{sub_name}': {n_cols}x{n_rows} nodes, "
                f"lat {s_lat/3600:.2f}N-{n_lat/3600:.2f}N, "
                f"lon {-w_long/3600:.2f}E-{-e_long/3600:.2f}E",
                flush=True,
            )

        # ---- grid data ----
        # Row-major: row 0 = S_LAT (south), col 0 = W_LONG (west).
        # Longitude stored positive-west: col 0 has highest positive-west value.
        fmt4f = endian + '4f'
        nodes_parsed = 0
        for i in range(gs_count):
            if pos + 16 > len(data):
                break
            lat_shift, lon_shift, _lat_acc, _lon_acc = struct.unpack_from(fmt4f, data, pos)
            pos += 16
            nodes_parsed += 1

            row = i // n_cols   # south-to-north
            col = i % n_cols    # west-to-east (col 0 = W_LONG)

            # OSGB36 geodetic (degrees) of this node
            lat_osgb_arcsec   = s_lat + row * lat_inc
            lon_osgb_west_arcsec = w_long - col * long_inc   # positive west
            phi_o = lat_osgb_arcsec / 3600.0
            lam_o = -lon_osgb_west_arcsec / 3600.0  # convert to degrees east

            # Skip nodes clearly outside reasonable GB bounds
            if not (49.0 <= phi_o <= 61.5 and -9.5 <= lam_o <= 2.5):
                continue

            # WGS84 ≈ ETRS89 position corresponding to this OSGB36 node.
            # NTv2 lat_shift: positive means ETRS89 is further north.
            # NTv2 lon_shift (positive west): positive means ETRS89 is further west,
            # i.e., more negative in degrees-east convention.
            phi_w = phi_o + lat_shift / 3600.0
            lam_w = lam_o - lon_shift / 3600.0

            # OSGB36 E/N of this node (correct, via Airy TM)
            E_osgb, N_osgb = _latlon_to_en(phi_o, lam_o)

            # Provisional OSGB36 E/N from WGS84 input (treating WGS84 as Airy)
            E_prov, N_prov = _latlon_to_en(phi_w, lam_w)

            # Shifts: SE/SN to add to provisional E/N → corrected OSGB36 E/N
            se = E_osgb - E_prov
            sn = N_osgb - N_prov

            # Bin into the 1 km E/N output grid
            osgb_col = round(E_osgb / CELL_SIZE)
            osgb_row = round(N_osgb / CELL_SIZE)
            if 0 <= osgb_col < NCOLS and 0 <= osgb_row < NROWS:
                se_grid[(osgb_col, osgb_row)] = se
                sn_grid[(osgb_col, osgb_row)] = sn

        if verbose:
            print(f"  Parsed {nodes_parsed:,} NTv2 nodes -> {len(se_grid):,} grid cells",
                  flush=True)

    # Consume optional END record at the very end of the file
    if pos + 16 <= len(data):
        key, _val = read_key_val()
        if key != 'END' and verbose:
            print(f"  Note: expected END record, got '{key}'", flush=True)

    return se_grid, sn_grid


# ---------------------------------------------------------------------------
# CSV parser (OS developer pack text file)
# ---------------------------------------------------------------------------

def parse_csv(csv_text: str, verbose: bool):
    """
    Parse the OSTN15 CSV (OSTN15_OSGM15_DataFile.txt).

    Columns: POINT_ID, EASTING, NORTHING, EAST_SHIFT, NORTH_SHIFT, GEOID_HEIGHT
    Easting and northing are in METRES (integer values 0..700000 / 0..1250000).
    East and north shifts are in metres (floating-point).

    Returns (se_grid, sn_grid) dicts keyed by (col, row).
    """
    se_grid: dict = {}
    sn_grid: dict = {}

    reader = csv.reader(csv_text.splitlines())
    for row in reader:
        if not row:
            continue
        # Skip header / comment lines (first field must look like an integer)
        first = row[0].strip()
        if not first.lstrip('-').isdigit():
            continue

        try:
            easting  = float(row[1])
            northing = float(row[2])
            se       = float(row[3])
            sn       = float(row[4])
        except (ValueError, IndexError):
            continue

        # The data file uses metres. Sanity check: easting must be in [0, 700000].
        # If easting looks like km (max ~700 instead of ~700000), scale it up.
        # We check the first plausible non-zero easting value.
        # A definitive km value would be at most 700, so if easting > 1000 it
        # is already in metres.
        if easting != 0.0 and easting < 1001.0:
            # Looks like km — scale to metres
            easting  *= 1000.0
            northing *= 1000.0

        col     = round(easting  / CELL_SIZE)
        row_idx = round(northing / CELL_SIZE)
        if 0 <= col < NCOLS and 0 <= row_idx < NROWS:
            se_grid[(col, row_idx)] = se
            sn_grid[(col, row_idx)] = sn

    if verbose:
        print(f"  Parsed {len(se_grid):,} shift nodes from CSV", flush=True)

    return se_grid, sn_grid


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def download_bytes(url: str, verbose: bool) -> bytes:
    if verbose:
        print(f"Downloading {url} ...", flush=True)
    req = urllib.request.Request(url, headers={"User-Agent": "ostn15_download/2"})
    with urllib.request.urlopen(req, timeout=120) as resp:
        data = resp.read()
    if verbose:
        print(f"  {len(data):,} bytes", flush=True)
    return data


def extract_csv_from_zip(zip_bytes: bytes, filename: str) -> str:
    with zipfile.ZipFile(io.BytesIO(zip_bytes)) as zf:
        names = {n.lower(): n for n in zf.namelist()}
        key   = filename.lower()
        if key not in names:
            raise RuntimeError(
                f"'{filename}' not found in zip.\nFiles present: {list(zf.namelist())}"
            )
        with zf.open(names[key]) as f:
            return f.read().decode("ascii", errors="replace")


def write_binary(out_path: str, se_grid: dict, sn_grid: dict, verbose: bool) -> None:
    n_cells  = NCOLS * NROWS
    se_flat  = [0.0] * n_cells
    sn_flat  = [0.0] * n_cells
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


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

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
            "Use a locally-downloaded CSV instead of fetching automatically. "
            "Accepts either the raw OSTN15_OSGM15_DataFile.txt or a zip containing it."
        ),
    )
    parser.add_argument(
        "--input-ntv2",
        default=None,
        metavar="FILE",
        help=(
            "Use a locally-downloaded NTv2 .gsb file instead of fetching automatically. "
            "Download OSTN15_NTv2_OSGBtoETRS.gsb from "
            "https://github.com/OrdnanceSurvey/os-transform"
        ),
    )
    parser.add_argument(
        "--quiet", action="store_true", help="Suppress progress messages"
    )
    args = parser.parse_args()
    verbose = not args.quiet

    se_grid = {}
    sn_grid = {}

    # ---- Local NTv2 input ----
    if args.input_ntv2:
        if verbose:
            print(f"Reading NTv2 from {args.input_ntv2} ...", flush=True)
        with open(args.input_ntv2, "rb") as f:
            ntv2_data = f.read()
        se_grid, sn_grid = parse_ntv2(ntv2_data, verbose)

    # ---- Local CSV input ----
    elif args.input_csv:
        if verbose:
            print(f"Reading CSV from {args.input_csv} ...", flush=True)
        with open(args.input_csv, "rb") as f:
            raw = f.read()
        if raw[:2] == b"PK":
            csv_text = extract_csv_from_zip(raw, OSTN15_FILENAME_IN_ZIP)
        else:
            csv_text = raw.decode("ascii", errors="replace")
        se_grid, sn_grid = parse_csv(csv_text, verbose)

    # ---- Automatic download: NTv2 from OrdnanceSurvey GitHub (primary) ----
    else:
        try:
            ntv2_data = download_bytes(OSTN15_NTV2_URL, verbose)
            se_grid, sn_grid = parse_ntv2(ntv2_data, verbose)
        except Exception as exc:
            print(f"WARNING: NTv2 download failed: {exc}", file=sys.stderr)
            print("Trying CSV fallback URLs ...", file=sys.stderr)

            for url in OSTN15_CSV_URLS:
                try:
                    raw      = download_bytes(url, verbose)
                    csv_text = extract_csv_from_zip(raw, OSTN15_FILENAME_IN_ZIP)
                    se_grid, sn_grid = parse_csv(csv_text, verbose)
                    break
                except Exception as exc2:
                    print(f"  {url}: {exc2}", file=sys.stderr)
            else:
                print(
                    "\nERROR: All automatic downloads failed.\n"
                    "\nTo download manually:\n"
                    "  Option A (NTv2):\n"
                    "    1. Download OSTN15_NTv2_OSGBtoETRS.gsb from:\n"
                    "         https://github.com/OrdnanceSurvey/os-transform\n"
                    "    2. Run:  python3 tools/ostn15_download.py --input-ntv2 "
                    "OSTN15_NTv2_OSGBtoETRS.gsb\n"
                    "\n"
                    "  Option B (CSV developer pack):\n"
                    "    1. Download the developer pack from:\n"
                    "         https://www.ordnancesurvey.co.uk/geodesy-positioning/"
                    "coordinate-transformations/resources\n"
                    "    2. Extract OSTN15_OSGM15_DataFile.txt from the zip\n"
                    "    3. Run:  python3 tools/ostn15_download.py --input-csv "
                    "OSTN15_OSGM15_DataFile.txt\n",
                    file=sys.stderr,
                )
                sys.exit(1)

    if not se_grid:
        print(
            "ERROR: No shift nodes were parsed.  "
            "Check the input file format.",
            file=sys.stderr,
        )
        sys.exit(1)

    if verbose:
        print(f"Writing binary grid to {args.out} ...", flush=True)
    write_binary(args.out, se_grid, sn_grid, verbose)

    if verbose:
        print(
            "Done.  BANDPASS II searches for OSTN15.dat in this order:\n"
            "  1. Next to the executable\n"
            "  2. data/ subdirectory next to the executable\n"
            "  3. User data directory (~/.local/share/bandpass2 on Linux)\n"
            "  4. data/ in the parent of the executable directory (dev builds)\n"
            "Copy the .dat file to one of these locations."
        )


if __name__ == "__main__":
    main()
