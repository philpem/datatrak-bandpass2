#!/usr/bin/env python3
"""
srtm_download.py — Download SRTM3 elevation tiles for a given bounding box
and optionally merge them into a single GeoTIFF for use by BANDPASS II's
GdalTerrainMap.

SRTM tiles are 1°×1° HGT files at ~90 m resolution (SRTM3, 3 arc-second).
The primary public data source is NASA Earthdata (requires free registration)
or the CGIAR-CSI processed tiles (no registration, void-filled).

Usage:
    python3 tools/srtm_download.py [options]

    --bbox LAT_MIN LON_MIN LAT_MAX LON_MAX
        Bounding box to download (WGS84 degrees).
        Default: UK + surrounding waters (48 70 -12 5).

    --source {cgiar|nasa|local}
        Data source (default: cgiar).
        cgiar  — CGIAR-CSI Processed SRTM v4.1 (void-filled, no login needed)
        nasa   — NASA Earthdata SRTM3 (requires EARTHDATA_TOKEN env variable)
        local  — Use HGT files already in --tile-dir; only merge/convert them

    --tile-dir DIR
        Directory to store downloaded HGT tiles (default: ./srtm_tiles).

    --out FILE
        Output merged GeoTIFF path (default: terrain_srtm.tif).
        If omitted when using --source local, only converts existing tiles.

    --no-merge
        Download tiles only; do not merge into a GeoTIFF.

    --quiet
        Suppress progress messages.

Examples:
    # UK coverage (CGIAR, no login):
    python3 tools/srtm_download.py --bbox 49.5 -8.0 61.0 2.5 --out terrain_uk.tif

    # Custom bounding box with NASA source (requires EARTHDATA_TOKEN):
    export EARTHDATA_TOKEN=<your-bearer-token>
    python3 tools/srtm_download.py --bbox 49 -8 61 3 --source nasa --out terrain.tif

    # Merge already-downloaded HGT files:
    python3 tools/srtm_download.py --source local --tile-dir ./srtm_tiles --out terrain.tif

Output:
    A WGS84 GeoTIFF with elevations in metres (Int16 or Float32).
    Set terrain.source in your scenario TOML to the output file path.
"""

import argparse
import math
import os
import sys
import time
import urllib.request
import zipfile
from pathlib import Path


# ---------------------------------------------------------------------------
# Tile name helpers
# ---------------------------------------------------------------------------

def tile_name(lat: int, lon: int) -> str:
    """Return the standard SRTM3 HGT filename for the lower-left corner."""
    ns = "N" if lat >= 0 else "S"
    ew = "E" if lon >= 0 else "W"
    return f"{ns}{abs(lat):02d}{ew}{abs(lon):03d}.hgt"


def tiles_for_bbox(lat_min: float, lon_min: float,
                   lat_max: float, lon_max: float) -> list:
    """Return sorted list of (lat, lon) lower-left corners for the bbox."""
    tiles = []
    for lat in range(math.floor(lat_min), math.ceil(lat_max)):
        for lon in range(math.floor(lon_min), math.ceil(lon_max)):
            tiles.append((lat, lon))
    return sorted(tiles)


# ---------------------------------------------------------------------------
# CGIAR-CSI download
# ---------------------------------------------------------------------------

def _cgiar_url(lat: int, lon: int) -> str | None:
    """
    Return the CGIAR-CSI SRTM v4.1 download URL for the tile.
    CGIAR uses a 5°×5° tile numbering scheme.
    """
    # CGIAR numbering: lon tile 1–72 (−180 to +180), lat tile 1–24 (60 to −60)
    lon_tile = (lon + 180) // 5 + 1
    lat_tile = (60  - lat ) // 5
    if lat_tile < 1 or lat_tile > 24 or lon_tile < 1 or lon_tile > 72:
        return None  # outside SRTM coverage
    fname = f"srtm_{lon_tile:02d}_{lat_tile:02d}.zip"
    return (
        "https://srtm.csi.cgiar.org/wp-content/uploads/files/srtm_5x5/TIFF/"
        + fname
    )


def download_cgiar(lat: int, lon: int, tile_dir: Path,
                   verbose: bool) -> Path | None:
    """
    Download a single CGIAR-CSI tile, extract the HGT, return its path.
    Returns None if the tile is outside SRTM coverage (polar or ocean-only).
    """
    url = _cgiar_url(lat, lon)
    if url is None:
        if verbose:
            print(f"  Skipping {tile_name(lat, lon)} (outside CGIAR coverage)")
        return None

    hgt_path = tile_dir / tile_name(lat, lon)
    if hgt_path.exists():
        if verbose:
            print(f"  Already have {hgt_path.name}")
        return hgt_path

    if verbose:
        print(f"  Downloading {url} ...", end=" ", flush=True)
    try:
        with urllib.request.urlopen(url, timeout=60) as resp:
            data = resp.read()
    except Exception as e:
        if verbose:
            print(f"FAILED ({e})")
        return None

    if verbose:
        print(f"{len(data)//1024} KB")

    # The zip contains a GeoTIFF (.tif), not an HGT.  We convert via GDAL
    # later during merge.  Save as-is with .zip extension; merge step handles it.
    zip_path = tile_dir / (tile_name(lat, lon) + ".cgiar.zip")
    zip_path.write_bytes(data)

    # Extract .tif from the zip
    try:
        with zipfile.ZipFile(zip_path) as zf:
            tif_names = [n for n in zf.namelist()
                         if n.lower().endswith(".tif") or n.lower().endswith(".tiff")]
            if not tif_names:
                # Fallback: might contain HGT
                hgt_names = [n for n in zf.namelist() if n.lower().endswith(".hgt")]
                if hgt_names:
                    with zf.open(hgt_names[0]) as src:
                        hgt_path.write_bytes(src.read())
                    zip_path.unlink(missing_ok=True)
                    return hgt_path
                if verbose:
                    print(f"  WARNING: no .tif or .hgt in {zip_path.name}")
                return None
            tif_name = tif_names[0]
            tif_path = tile_dir / Path(tif_name).name
            with zf.open(tif_name) as src:
                tif_path.write_bytes(src.read())
        zip_path.unlink(missing_ok=True)
        return tif_path  # return .tif; merge step accepts both
    except zipfile.BadZipFile:
        if verbose:
            print(f"  WARNING: bad zip for {tile_name(lat, lon)}")
        zip_path.unlink(missing_ok=True)
        return None


# ---------------------------------------------------------------------------
# NASA Earthdata download
# ---------------------------------------------------------------------------

NASA_BASE = "https://e4ftl01.cr.usgs.gov/MEASURES/SRTMGL3.003/2000.02.11/"


def download_nasa(lat: int, lon: int, tile_dir: Path,
                  token: str, verbose: bool) -> Path | None:
    """Download a single NASA SRTM3 HGT tile (requires Earthdata token)."""
    name  = tile_name(lat, lon)
    hgt_path = tile_dir / name
    if hgt_path.exists():
        if verbose:
            print(f"  Already have {name}")
        return hgt_path

    # NASA distributes .hgt.zip files
    url = NASA_BASE + name + ".SRTMGL3.hgt.zip"
    req = urllib.request.Request(url,
                                 headers={"Authorization": f"Bearer {token}"})
    if verbose:
        print(f"  Downloading {name} from NASA ...", end=" ", flush=True)
    try:
        with urllib.request.urlopen(req, timeout=120) as resp:
            data = resp.read()
    except urllib.error.HTTPError as e:
        if e.code == 404:
            if verbose:
                print("not found (ocean tile?)")
            return None
        if verbose:
            print(f"HTTP {e.code}")
        return None
    except Exception as e:
        if verbose:
            print(f"FAILED ({e})")
        return None

    if verbose:
        print(f"{len(data)//1024} KB")

    # Extract HGT from zip
    try:
        with zipfile.ZipFile(io.BytesIO(data)) as zf:
            hgt_names = [n for n in zf.namelist() if n.lower().endswith(".hgt")]
            if not hgt_names:
                return None
            with zf.open(hgt_names[0]) as src:
                hgt_path.write_bytes(src.read())
    except zipfile.BadZipFile:
        return None
    return hgt_path


# ---------------------------------------------------------------------------
# Merge tiles with GDAL
# ---------------------------------------------------------------------------

def merge_tiles(tile_paths: list, out_path: str, verbose: bool) -> None:
    try:
        from osgeo import gdal
        gdal.UseExceptions()
    except ImportError:
        print("ERROR: GDAL Python bindings required for merge.", file=sys.stderr)
        print("Install: sudo apt-get install python3-gdal", file=sys.stderr)
        sys.exit(1)

    str_paths = [str(p) for p in tile_paths if p and Path(p).exists()]
    if not str_paths:
        print("ERROR: No valid tiles to merge.", file=sys.stderr)
        sys.exit(1)

    if verbose:
        print(f"  Merging {len(str_paths)} tile(s) into {out_path} ...")

    vrt = gdal.BuildVRT("/vsimem/merged.vrt", str_paths)
    if vrt is None:
        raise RuntimeError("gdal.BuildVRT failed")

    gdal.Translate(out_path, vrt,
                   format="GTiff",
                   creationOptions=["COMPRESS=DEFLATE", "TILED=YES",
                                    "BIGTIFF=IF_NEEDED"])
    vrt = None

    size_mb = os.path.getsize(out_path) / 1_048_576
    if verbose:
        print(f"  Written {out_path}  ({size_mb:.1f} MB)")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    import io  # needed for NASA path

    parser = argparse.ArgumentParser(
        description="Download SRTM3 tiles and merge into a BANDPASS II terrain GeoTIFF."
    )
    parser.add_argument(
        "--bbox", nargs=4, type=float,
        metavar=("LAT_MIN", "LON_MIN", "LAT_MAX", "LON_MAX"),
        default=[49.5, -12.0, 61.0, 5.0],
        help="Bounding box (default: UK + surrounding waters)"
    )
    parser.add_argument(
        "--source", choices=["cgiar", "nasa", "local"], default="cgiar",
        help="Data source (default: cgiar)"
    )
    parser.add_argument(
        "--tile-dir", default="srtm_tiles", metavar="DIR",
        help="Directory to store/read HGT tiles (default: ./srtm_tiles)"
    )
    parser.add_argument(
        "--out", default="terrain_srtm.tif", metavar="FILE",
        help="Output merged GeoTIFF (default: terrain_srtm.tif)"
    )
    parser.add_argument(
        "--no-merge", action="store_true",
        help="Download tiles only; skip merging into a GeoTIFF"
    )
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args()
    verbose = not args.quiet

    lat_min, lon_min, lat_max, lon_max = args.bbox
    tile_dir = Path(args.tile_dir)
    tile_dir.mkdir(parents=True, exist_ok=True)

    tiles = tiles_for_bbox(lat_min, lon_min, lat_max, lon_max)
    if verbose:
        print(f"Bounding box: lat [{lat_min}..{lat_max}], lon [{lon_min}..{lon_max}]")
        print(f"Tiles: {len(tiles)}, source: {args.source}, dir: {tile_dir}")

    downloaded = []

    if args.source == "local":
        # Collect existing files
        for ext in ("*.hgt", "*.HGT", "*.tif", "*.tiff"):
            downloaded.extend(tile_dir.glob(ext))
        if verbose:
            print(f"Found {len(downloaded)} local tile(s) in {tile_dir}")

    elif args.source == "cgiar":
        seen_cgiar = set()  # CGIAR tiles cover 5°×5°; avoid duplicate downloads
        for lat, lon in tiles:
            # CGIAR tile key (5°×5° block)
            ckey = (_cgiar_url(lat, lon) or "")
            if ckey and ckey in seen_cgiar:
                continue
            seen_cgiar.add(ckey)
            path = download_cgiar(lat, lon, tile_dir, verbose)
            if path:
                downloaded.append(path)
            time.sleep(0.2)  # polite rate limiting

    elif args.source == "nasa":
        token = os.environ.get("EARTHDATA_TOKEN", "")
        if not token:
            print("ERROR: Set EARTHDATA_TOKEN environment variable for NASA source.",
                  file=sys.stderr)
            print("  Register at https://urs.earthdata.nasa.gov/ (free).", file=sys.stderr)
            sys.exit(1)
        import io as _io
        for lat, lon in tiles:
            path = download_nasa(lat, lon, tile_dir, token, verbose)
            if path:
                downloaded.append(path)
            time.sleep(0.5)

    if args.no_merge:
        if verbose:
            print(f"Download complete. {len(downloaded)} tile(s) in {tile_dir}")
            print("Skipping merge (--no-merge specified).")
        return

    if not downloaded:
        print("No tiles downloaded or found — nothing to merge.", file=sys.stderr)
        sys.exit(1)

    merge_tiles(downloaded, args.out, verbose)

    if verbose:
        print("Done.")
        print()
        print("To use in BANDPASS II, set terrain.source in your scenario TOML to:")
        print(f'  source = "{args.out}"')


if __name__ == "__main__":
    main()
