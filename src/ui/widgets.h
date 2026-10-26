// SpaceRescue UI - SPDX-License-Identifier: GPL-3.0-or-later
//
// Small immediate-mode toolkit on plain GDI: a scrollable, double-buffered, keyboard
// accessible "Canvas" window plus a Painter with text/card/bar/ring/button helpers.
// Pages derive from Canvas and draw themselves in OnPaint(); buttons drawn through
// Painter::Button() are registered as hit regions (mouse, Tab, Enter/Space).

#pragma once

#include "theme.h"

namespace ui
{
class Canvas;
void RegisterCanvasClass();

enum class BtnStyle { Normal, Primary, Danger, Link, Tab, TabSelected };

struct Hit
{
    RECT r;
    int id;
    bool enabled;
};

COLORREF Blend(COLORREF a, COLORREF b, int percentB);
std::wstring ShortenPath(const std::wstring& path, size_t maxChars);

class Painter
{
public:
    Painter(HDC dc, int width, Canvas* owner);

    HDC dc;
    int width;      // content width in pixels
    Theme& t;

    int S(int v) const { return t.S(v); }
    void Fill(const RECT& r, COLORREF c);
    void Round(const RECT& r, int radius, COLORREF fill, COLORREF border = CLR_NONE);
    // Draws text; w > 0 wraps (unless DT_SINGLELINE is given) and clips to w. Returns height used.
    int Text(int x, int y, int w, const std::wstring& s, HFONT f, COLORREF c, UINT flags = 0);
    // Multi-line text clipped to `box` with a trailing ellipsis.
    void TextBox(const RECT& box, const std::wstring& s, HFONT f, COLORREF c, UINT flags = DT_WORDBREAK);
    int TextWidth(const std::wstring& s, HFONT f);
    int TextHeight(int w, const std::wstring& s, HFONT f);
    // Right-aligned single line ending at `right`; returns its left edge.
    int TextRight(int right, int y, const std::wstring& s, HFONT f, COLORREF c);
    void Bar(const RECT& r, double frac, COLORREF fill);
    void Ring(int cx, int cy, int radius, int thickness, double frac, COLORREF fill, COLORREF track, COLORREF hole);
    int Chip(int x, int y, const std::wstring& s, COLORREF c); // returns width
    int ButtonWidth(const std::wstring& s, BtnStyle style = BtnStyle::Normal);
    int ButtonHeight() const { return S(32); }
    RECT Button(int id, int x, int y, const std::wstring& s, BtnStyle style = BtnStyle::Normal,
                bool enabled = true, int fixedWidth = 0);

private:
    Canvas* m_cv;
};

class Canvas
{
public:
    friend void RegisterCanvasClass();
    Canvas() = default;
    virtual ~Canvas();
    Canvas(const Canvas&) = delete;
    Canvas& operator=(const Canvas&) = delete;

    HWND Create(HWND parent, bool scrollable, bool tabStop);
    HWND Hwnd() const { return m_hwnd; }
    void Redraw();
    // Height the content needs at `width` (runs OnPaint into a scratch DC).
    int Measure(int width);
    void ScrollTo(int y);
    void ScrollToTop() { ScrollTo(0); }
    int ContentHeight() const { return m_contentH; }

    // Internal (used by Painter).
    void AddHit(const RECT& r, int id, bool enabled) { m_hits.push_back({ r, id, enabled }); }
    int FocusId() const { return m_focusId; }
    int HoverId() const { return m_hoverId; }
    int PressedId() const { return m_pressedId; }
    bool HasFocus() const { return m_hasFocus && m_focusVisible; }

protected:
    virtual int OnPaint(Painter& p) = 0;     // draw; returns content height
    virtual void OnButton(int id) {}
    virtual COLORREF Background() const { return T().bg; }
    // Left/Right move the focus between buttons (tab strips).
    bool m_arrowsNavigate = false;
    HWND m_hwnd = nullptr;

private:
    static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
    LRESULT Handle(UINT, WPARAM, LPARAM);
    void Paint();
    void UpdateThumb();
    bool MoveFocus(bool backwards);
    int HitAt(int x, int y) const;
    void EnsureVisible(const RECT& r);
    int MaxScroll() const;
    RECT ThumbRect() const;

    bool m_scrollable = false;
    bool m_hasFocus = false;
    bool m_focusVisible = false;
    bool m_tracking = false;
    bool m_dragThumb = false;
    int m_dragOffset = 0;
    int m_scroll = 0;
    int m_contentH = 0;
    int m_hoverId = -1, m_pressedId = -1, m_focusId = -1;
    std::vector<Hit> m_hits;
};

// Registers the Canvas window class (idempotent).
void RegisterCanvasClass();
}

namespace ui
{
// "2024-08-12" from FILETIME ticks.
std::wstring FormatDate(sr::u64 ticks);
// "12.4 GB {ndash} 18% of C:" (label may be empty; total 0 omits the share).
std::wstring SizeShare(sr::u64 bytes, sr::u64 total, const std::wstring& label);
// Opens Explorer with the item selected.
void RevealInExplorer(HWND owner, const std::wstring& path);
// Opens a folder (or the folder containing a file) in Explorer.
void OpenFolderInExplorer(HWND owner, const std::wstring& path);
}

namespace ui
{
// "12.3 s" / "2 min 5 s" from milliseconds.
std::wstring FormatDuration(sr::u64 millis);
}
