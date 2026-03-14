// SPDX-License-Identifier: GPL-3.0-or-later
// Toolbar icon SVG strings, kept in sync with the map marker artwork in
// src/web/map.html.  The Leaflet anchor-crosshair lines present in the map
// versions are omitted here — they are Leaflet positioning artefacts and
// are not part of the icon artwork.
//
// Use with wxBitmapBundle::FromSVG() (requires wxWidgets >= 3.1.6).

#pragma once

// Red broadcast-tower icon (matches txIcon in map.html).
// viewBox 0 0 24 30  — crops just below the base platform at y=30.
static constexpr const char tx_mast_svg[] =
    R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 30">
  <circle cx="12" cy="2" r="2.5" fill="#cc2200"/>
  <line x1="12" y1="0" x2="12" y2="8" stroke="#cc2200" stroke-width="2.5" stroke-linecap="round"/>
  <path d="M8 5 A5 6 0 0 1 16 5" fill="none" stroke="#cc2200" stroke-width="1.5" opacity="0.8"/>
  <path d="M5 7 A8 9 0 0 1 19 7" fill="none" stroke="#cc2200" stroke-width="1.1" opacity="0.5"/>
  <path d="M12 8 L4 30 L20 30 Z" fill="none" stroke="#cc2200" stroke-width="2" stroke-linejoin="round"/>
  <line x1="8"   y1="18" x2="16"   y2="18" stroke="#cc2200" stroke-width="1.5"/>
  <line x1="6.5" y1="24" x2="17.5" y2="24" stroke="#cc2200" stroke-width="1.5"/>
  <line x1="2"   y1="30" x2="22"   y2="30" stroke="#cc2200" stroke-width="2.5" stroke-linecap="round"/>
</svg>)svg";

// Blue vehicle/car icon (matches rxIcon in map.html).
// viewBox 0 0 36 22  — crops just below the wheel tops at y=22.
static constexpr const char rx_car_svg[] =
    R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 36 22">
  <rect x="1"  y="9"  width="34" height="9"  rx="3" fill="#1a5fb4"/>
  <path d="M9 9 L11 3 L25 3 L27 9 Z" fill="#2970c8"/>
  <rect x="12" y="4" width="5" height="4" rx="1" fill="#c8e0fa" opacity="0.9"/>
  <rect x="19" y="4" width="5" height="4" rx="1" fill="#c8e0fa" opacity="0.9"/>
  <rect x="30" y="10" width="4" height="3" rx="1" fill="#f8d820" opacity="0.95"/>
  <rect x="2"  y="10" width="4" height="3" rx="1" fill="#f02828" opacity="0.85"/>
  <circle cx="10" cy="18" r="4" fill="#222"/>
  <circle cx="10" cy="18" r="2" fill="#888"/>
  <circle cx="26" cy="18" r="4" fill="#222"/>
  <circle cx="26" cy="18" r="2" fill="#888"/>
</svg>)svg";
