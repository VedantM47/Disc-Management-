// SpaceRescue - SPDX-License-Identifier: GPL-3.0-or-later
//
// Direct2D + DirectWrite rendering layer and the design tokens from DESIGN.md.
// All coordinates are DIPs (1/96 inch); the render target maps them to pixels for the window's DPI.

#pragma once

#include "../core/common.h"

#include <d2d1.h>
#include <dwrite.h>

#include <array>
#include <map>

namespace sr::gfx
{
// --- Tokens -------------------------------------------------------------------

struct Color
{
    float r = 0, g = 0, b = 0, a = 1;
    constexpr Color() = default;
    constexpr Color(unsigned rgb, float alpha = 1.0f) :
        r(((rgb >> 16) & 0xFF) / 255.0f), g(((rgb >> 8) & 0xFF) / 255.0f), b((rgb & 0xFF) / 255.0f), a(alpha) {}
    Color WithAlpha(float alpha) const { Color c = *this; c.a = alpha; return c; }
    Color Mix(const Color& o, float t) const
    {
        Color c;
        c.r = r + (o.r - r) * t; c.g = g + (o.g - g) * t; c.b = b + (o.b - b) * t; c.a = a + (o.a - a) * t;
        return c;
    }
    D2D1_COLOR_F D2D() const { return { r, g, b, a }; }
};

enum class FileKind
{
    Video, Images, Audio, Documents, Archives, Code, Programs, DiskImages, System, Other, Count
};

struct Palette
{
    bool dark = true;
    Color base, surface1, surface2, surface3, stroke;
    Color text, textDim, textFaint;
    Color accent, accentInk;
    Color safe, review, caution, protectedC, freeSpace;
    std::array<Color, static_cast<size_t>(FileKind::Count)> kinds;
    Color Kind(FileKind k) const { return kinds[static_cast<size_t>(k)]; }
};

const Palette& Dark();
const Palette& Light();
// Follows HKCU ...\Personalize AppsUseLightTheme unless overridden.
bool SystemPrefersDark();

// Classifies a file name by extension (the same classes everywhere: treemap, sunburst, legends).
FileKind KindOf(const wchar_t* name, size_t len);
const wchar_t* KindLabel(FileKind k);

// Type scale from DESIGN.md §3.
enum class Text
{
    Hero, Title, Section, Number, Body, BodyStrong, Caption, CaptionStrong, Overline, Icon, IconLarge, Count
};

enum class Align { Left, Center, Right };

// --- Renderer -------------------------------------------------------------------

class Canvas
{
public:
    // Creates device resources for `hwnd` on first use; call inside WM_PAINT.
    bool Begin(HWND hwnd, const Palette& palette);
    // Renders into a GDI DC instead (WM_PRINTCLIENT / PrintWindow). End() switches back.
    bool BeginPrint(HWND hwnd, HDC hdc, const Palette& palette);
    // Returns false if the device was lost (the caller just repaints).
    bool End();
    void Resize(UINT width, UINT height);
    void Discard();

    float Width() const { return m_size.width; }
    float Height() const { return m_size.height; }
    float Dpi() const { return m_dpi; }
    const Palette& P() const { return *m_palette; }
    ID2D1RenderTarget* Target() const { return m_target; }

    void Clear(const Color& c);
    void Fill(const D2D1_RECT_F& r, const Color& c);
    void FillRound(const D2D1_RECT_F& r, float radius, const Color& c);
    void StrokeRound(const D2D1_RECT_F& r, float radius, const Color& c, float width = 1.0f);
    void Line(float x1, float y1, float x2, float y2, const Color& c, float width = 1.0f);
    void Circle(float cx, float cy, float radius, const Color& c);
    // Soft drop shadow under a rounded rectangle (layered alpha; cheap and good enough at E1–E3).
    void Shadow(const D2D1_RECT_F& r, float radius, float blur, float offsetY, float alpha);
    // Card per DESIGN.md: surface + stroke (+ shadow when raised).
    void Card(const D2D1_RECT_F& r, float radius = 12.0f, bool raised = false);
    // Annular sector from angle a0 to a1 (radians, 0 = 12 o'clock, clockwise).
    void Arc(float cx, float cy, float r0, float r1, float a0, float a1, const Color& fill, const Color* stroke = nullptr);
    // Rounded-cap arc stroke (meters, progress rings).
    void ArcStroke(float cx, float cy, float radius, float a0, float a1, float width, const Color& c);

    void Text(const std::wstring& s, const D2D1_RECT_F& r, gfx::Text style, const Color& c, Align align = Align::Left,
        bool vcenter = false);
    D2D1_SIZE_F Measure(const std::wstring& s, gfx::Text style, float maxWidth = 10000.0f);
    // Glyph from Segoe Fluent Icons / MDL2 Assets.
    void Icon(wchar_t glyph, float x, float y, float size, const Color& c);

    // BGRA premultiplied pixels (e.g. the cushion treemap) drawn into `dest`.
    void Pixels(const std::vector<UINT32>& bgra, UINT width, UINT height, const D2D1_RECT_F& dest);

    void PushClip(const D2D1_RECT_F& r);
    void PopClip();

private:
    ID2D1SolidColorBrush* Brush(const Color& c);
    IDWriteTextFormat* Format(gfx::Text style);

    HWND m_hwnd = nullptr;
    ID2D1HwndRenderTarget* m_hwndTarget = nullptr;
    ID2D1DCRenderTarget* m_dcTarget = nullptr;
    ID2D1RenderTarget* m_target = nullptr;   // the one being drawn to
    ID2D1SolidColorBrush* m_brush = nullptr;
    const Palette* m_palette = nullptr;
    D2D1_SIZE_F m_size = { 0, 0 };
    float m_dpi = 96.0f;
    std::map<int, IDWriteTextFormat*> m_formats;
};

ID2D1Factory* Factory();
IDWriteFactory* Write();
// "Animation effects" setting.
bool AnimationsEnabled();
// 0..1 eased progress for a transition started at `startTick` lasting `ms`.
float Ease(DWORD startTick, DWORD ms, bool emphasized = false);

inline D2D1_RECT_F Rect(float x, float y, float w, float h) { return { x, y, x + w, y + h }; }
inline D2D1_RECT_F Inset(const D2D1_RECT_F& r, float d) { return { r.left + d, r.top + d, r.right - d, r.bottom - d }; }
inline bool Contains(const D2D1_RECT_F& r, float x, float y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }
}
