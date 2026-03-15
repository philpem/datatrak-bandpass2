# Third-Party Components

BANDPASS II incorporates or links against the following third-party software.
Each component's licence is reproduced in full below.

---

## Vendored in this repository

### toml++ v3.4.0

- **Location:** `third_party/tomlplusplus/toml++/toml.hpp`
- **Use:** TOML scenario file parsing and serialisation
- **Homepage:** https://github.com/marzer/tomlplusplus
- **SPDX-License-Identifier:** MIT

```
MIT License

Copyright (c) Mark Gillard <mark.gillard@outlook.com.au>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

### Leaflet.js (stub — replace with 1.9.4)

- **Location:** `src/web/leaflet/leaflet.js`, `src/web/leaflet/leaflet.css`
- **Use:** Interactive map rendering in the wxWebView panel
- **Homepage:** https://leafletjs.com / https://github.com/Leaflet/Leaflet
- **SPDX-License-Identifier:** BSD-2-Clause

> **Note:** The files currently in `src/web/leaflet/` are stubs for Phase 1
> development. Replace them with the full Leaflet 1.9.4 distribution before
> shipping. Download from https://unpkg.com/leaflet@1.9.4/dist/ or via npm.

```
Copyright (c) 2010-2023, Vladimir Agafonkin
Copyright (c) 2010-2011, CloudMade
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
```

---

## Linked at build time (not vendored)

### wxWidgets ≥ 3.2

- **Use:** Cross-platform GUI, wxWebView host, file dialogs, clipboard
- **Homepage:** https://wxwidgets.org
- **SPDX-License-Identifier:** LicenseRef-wxWindows

wxWidgets is distributed under the **wxWindows Library Licence**, a modified
LGPL which adds an explicit exception permitting distribution of applications
that link against wxWidgets without triggering the LGPL's "share-alike"
requirement for the application's own source code. Full licence text:
https://www.wxwidgets.org/about/licence/

---

### nlohmann/json ≥ 3.11

- **Use:** JSON serialisation for the wxWebView JS bridge
- **Homepage:** https://github.com/nlohmann/json
- **SPDX-License-Identifier:** MIT

```
MIT License

Copyright (c) 2013-2022 Niels Lohmann

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

### GeographicLib ≥ 2.0

- **Use:** Geodesic computations (WGS84 distances, ellipsoid maths)
- **Homepage:** https://geographiclib.sourceforge.io
- **SPDX-License-Identifier:** MIT

```
Copyright (c) Charles Karney (2008-2023) <karney@alum.mit.edu>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

### SQLite3

- **Use:** MBTiles map tile cache
- **Homepage:** https://sqlite.org
- **Licence:** Public Domain

SQLite is in the public domain. The authors disclaim all copyright.
See https://sqlite.org/copyright.html for the full statement.

---

### libcurl

- **Use:** HTTP tile fetching from OpenStreetMap tile servers
- **Homepage:** https://curl.se/libcurl/
- **SPDX-License-Identifier:** curl (MIT-style)

```
COPYRIGHT AND PERMISSION NOTICE

Copyright (c) 1996-2024, Daniel Stenberg, <daniel@haxx.se>, and many
contributors, see the THANKS file.

All rights reserved.

Permission to use, copy, modify, and distribute this software for any purpose
with or without fee is hereby granted, provided that the above copyright
notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY RIGHTS. IN
NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of a copyright holder shall not
be used in advertising or otherwise to promote the sale, use or other dealings
in this Software without prior written authorization of the copyright holder.
```

---

### Catch2 v3

- **Use:** Unit test framework (test builds only — not linked into the application binary)
- **Homepage:** https://github.com/catchorg/Catch2
- **SPDX-License-Identifier:** BSL-1.0

```
Boost Software License - Version 1.0 - August 17th, 2003

Permission is hereby granted, free of charge, to any person or organization
obtaining a copy of the software and accompanying documentation covered by
this license (the "Software") to use, reproduce, display, distribute,
execute, and transmit the Software, and to prepare derivative works of the
Software, and to permit third-parties to whom the Software is furnished to
do so, all subject to the following:

The copyright notices in the Software and this entire statement, including
the above license grant, this restriction and the following disclaimer,
must be included in all copies of the Software, in whole or in part, and
all derivative works of the Software, unless such copies or derivative
works are solely in the form of machine-executable object code generated by
a source language processor.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF TITLE,
MERCHANTABILITY, AGAINST INFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE.
```

---

## OSTN15 datum shift grid (optional, downloaded separately)

- **File:** `OSTN15.dat` (generated by `tools/ostn15_download.py`)
- **Source data:** `OSTN15_NTv2_OSGBtoETRS.gsb` from
  https://github.com/OrdnanceSurvey/os-transform
- **Use:** High-accuracy (±0.1 m) WGS84 ↔ OSGB36 datum transform
- **Licence:** Open Government Licence v3.0

The `OSTN15_NTv2_OSGBtoETRS.gsb` file is published by Ordnance Survey
under the [Open Government Licence v3.0](https://www.nationalarchives.gov.uk/doc/open-government-licence/version/3/).
The OGL permits free use, reproduction, and redistribution provided the
following attribution is included:

> Contains OS data © Crown copyright and database right 2016

**OSTN15.dat is not bundled with BANDPASS II** — it is generated locally
by running `tools/ostn15_download.py`.  The OGL attribution above must
be displayed wherever the OSTN15 transform is described or used (e.g.
the About box and any printed output noting coordinate accuracy).

---

## Map tile data

When online tile fetching is enabled, map tiles are retrieved from
**OpenStreetMap** tile servers.

- **Data:** © OpenStreetMap contributors, licenced under the
  [Open Database Licence (ODbL) v1.0](https://opendatacommons.org/licenses/odbl/)
- **Cartography:** licenced under
  [CC BY-SA 2.0](https://creativecommons.org/licenses/by-sa/2.0/)
- **Usage policy:** https://operations.osmfoundation.org/policies/tiles/

Applications displaying OSM tiles must include the attribution
**"© OpenStreetMap contributors"** wherever the map is visible. BANDPASS II
displays this attribution in the map panel footer.

---

## Propagation model

The physics model implemented in the `src/engine/` modules is derived from:

> Williams, P. (2004). *Prediction of the Coverage and Performance of the
> Datatrak Low-Frequency Tracking System.* PhD thesis, University of Wales
> Bangor.

The thesis is not a software component and carries no open-source licence.
BANDPASS II implements the equations described therein; it does not
redistribute any text or figures from the thesis.
