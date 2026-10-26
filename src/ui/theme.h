// SpaceRescue UI - SPDX-License-Identifier: GPL-3.0-or-later
// Colors, fonts, DPI scaling and light/dark handling.

#pragma once

#include "ui_common.h"

namespace ui
{
struct Theme
{
    bool dark = false;
    int dpi = 96;
    COLORREF bg = 0, card = 0, border = 0, text = 0, dim = 0, accent = 0, onAccent = 0;
    COLORREF good = 0, warn = 0, danger = 0, track = 0, hover = 0, banner = 0, bannerText = 0;
    HFONT font = nullptr;   // body
    HFONT bold = nullptr;   // semibold body
    HFONT caption = nullptr;  // captions
    HFONT title = nullptr;  // section / page titles
    HFONT big = nullptr;    // hero numbers
    HBRUSH bgBrush = nullptr;
    HBRUSH cardBrush = nullptr;

    int S(int v) const { return MulDiv(v, dpi, 96); }
};

Theme& T();

// Detects light/dark and DPI for `hwnd` and (re)builds colors, fonts and brushes.
void ThemeRefresh(HWND hwnd);
bool SystemUsesDarkMode();
int DpiForWindow(HWND hwnd);

// Dark title bar for a top-level window.
void StyleTopLevel(HWND hwnd);
// Explorer-style (or dark) visual style for standard controls, and matching colors.
void StyleControl(HWND ctl);
void StyleListView(HWND lv);
void StyleTreeView(HWND tv);
}
