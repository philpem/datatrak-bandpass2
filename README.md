# BANDPASS II

Coverage and positioning accuracy planner for Datatrak-type LF radio navigation
networks, based on the propagation physics model from Williams (2004).

---

## Building

### Linux (recommended path)

All dependencies are available as system packages on Ubuntu 22.04 / 24.04, Debian
12+, and Linux Mint 21+. **vcpkg is not needed on Linux.**

toml++ is vendored in `third_party/` as a single header — no package needed on
any platform.

```bash
# Ubuntu 22.04+ / Debian 12+ / Mint 21+
sudo apt-get install -y \
    build-essential cmake ninja-build \
    libwxgtk3.2-dev libwxgtk-webview3.2-dev \
    libsqlite3-dev libcurl4-gnutls-dev \
    libgeographiclib-dev nlohmann-json3-dev \
    catch2

git clone https://github.com/philpem/datatrak-bandpass2.git
cd datatrak-bandpass2
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The resulting binary is `build/src/bandpass2`.

#### Docker (any Linux distro / CI)

If your distro doesn't have the right package versions, use the provided
Dockerfile which gives a clean Ubuntu 24.04 build environment:

```bash
docker build -t bandpass2-builder -f docker/Dockerfile.build .
docker run --rm -v "$PWD":/workspace bandpass2-builder \
    bash -c "cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
             && cmake --build build --parallel \
             && ctest --test-dir build --output-on-failure"
```

The binary appears in `build/src/bandpass2` on your host.

---

### macOS

```bash
brew install wxwidgets sqlite curl geographiclib \
             nlohmann-json catch2 cmake ninja

git clone https://github.com/philpem/datatrak-bandpass2.git
cd datatrak-bandpass2
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The resulting binary is `build/src/bandpass2`.

---

### Windows

Windows uses [vcpkg](https://vcpkg.io) to install all dependencies, including
wxWidgets with WebView2 support.

**Prerequisites**

- Visual Studio 2022 (Community edition is fine) with the "Desktop development
  with C++" workload
- CMake 3.20+ (bundled with VS, or from cmake.org)
- Git for Windows

**Steps**

```powershell
# 1. Clone vcpkg alongside the project (or anywhere you like)
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat -disableMetrics

# 2. Clone and build BANDPASS II
git clone https://github.com/philpem/datatrak-bandpass2.git
cd datatrak-bandpass2

cmake -B build -G "Visual Studio 17 2022" -A x64 `
      -DCMAKE_BUILD_TYPE=Release `
      -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake

cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

vcpkg will download and compile all dependencies on first run; this takes
10–30 minutes depending on your machine. Subsequent builds are fast.

The resulting binary is `build\src\Release\bandpass2.exe` together with a
`web\` folder that must stay alongside it.

---

## Running

```
./bandpass2
```

The map panel requires network access for tile fetching on first use; tiles
are cached locally in `~/.cache/bandpass2/tiles.mbtiles` (Linux/macOS) or
`%LOCALAPPDATA%\bandpass2\tiles.mbtiles` (Windows) with a 30-day TTL.

An offline `.mbtiles` fallback file can be placed at the same path before
first run.

---

## Licence

GPLv3 — see [LICENSE](LICENSE).

Third-party component licences (toml++, wxWidgets, nlohmann/json,
GeographicLib, SQLite, libcurl, Catch2, Leaflet, OSM tile data) are
documented in [THIRD_PARTY.md](THIRD_PARTY.md).
