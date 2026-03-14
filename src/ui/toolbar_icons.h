// SPDX-License-Identifier: GPL-3.0-or-later
// Embedded XPM toolbar icons for BANDPASS II.
// TX mast: radio tower with signal arcs.
// RX car:  vehicle side-profile.

#pragma once

// 16x16 transmitting-mast icon (tower + blue signal arcs).
// Colours: ' '=transparent, '.'=dark (#1A1A1A), 'o'=blue (#2060C0)
static const char* const tx_mast_xpm[] = {
    "16 16 3 1",
    "  c None",
    ". c #1A1A1A",
    "o c #2060C0",
    "       .        ",   /*  0  antenna tip           */
    "      ...       ",   /*  1  cross-piece            */
    "       .        ",   /*  2  stem                   */
    "  o    .    o   ",   /*  3  inner signal arc       */
    " o     .     o  ",   /*  4  inner signal arc       */
    "o      .      o ",   /*  5  outer signal arc       */
    "       .        ",   /*  6  stem                   */
    "      ...       ",   /*  7  tower top              */
    "      ...       ",   /*  8                         */
    "     .....      ",   /*  9                         */
    "     .....      ",   /* 10                         */
    "    .......     ",   /* 11                         */
    "    .......     ",   /* 12                         */
    "   .........    ",   /* 13                         */
    "  ...........   ",   /* 14                         */
    " .............. "    /* 15  base                   */
};

// 16x16 vehicle (car) icon (side profile).
// Colours: ' '=transparent, '.'=dark (#1A1A1A), 'o'=grey (#505050)
static const char* const rx_car_xpm[] = {
    "16 16 3 1",
    "  c None",
    ". c #1A1A1A",
    "o c #505050",
    "                ",   /*  0  blank                  */
    "    .......     ",   /*  1  roof                   */
    "   .........    ",   /*  2  cabin                  */
    "   .........    ",   /*  3  cabin                  */
    " .............. ",   /*  4  body                   */
    " .............. ",   /*  5  body                   */
    " .............. ",   /*  6  body                   */
    " .............. ",   /*  7  body                   */
    " .ooo.....ooo.  ",   /*  8  body + wheel arches    */
    "  .ooo....ooo.  ",   /*  9  wheels                 */
    "  ..........    ",   /* 10  undercarriage          */
    "                ",   /* 11  blank                  */
    "                ",   /* 12  blank                  */
    "                ",   /* 13  blank                  */
    "                ",   /* 14  blank                  */
    "                "    /* 15  blank                  */
};
