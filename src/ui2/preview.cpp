// SpaceRescue - SPDX-License-Identifier: GPL-3.0-or-later

#include "preview.h"

#include "../gfx/gfx.h"
#include "../insights/insights.h"
#include "../insights/quickwins.h"
#include "../internals/internals.h"
#include "../notify/tray.h"
#include "../rescue/rescue.h"
#include "../viz/viz.h"

#include <dwmapi.h>

#include <cmath>
#include <memory>
#include <thread>

namespace sr::ui2
{
namespace
{
using gfx::Align;
using gfx::Canvas;
using gfx::Color;
using gfx::Rect;
using gfx::Text;

constexpr UINT WM_SCAN_DONE = WM_APP + 60;
constexpr UINT_PTR TimerProgress = 1, TimerAnim = 2;
constexpr float Rail = 84, Top = 60, Margin = 28;
constexpr float Pi = 3.14159265358979f;

const wchar_t* PageNames[] = { L"Overview", L"Explore", L"Map", L"Clean up", L"System", L"Disk internals", L"History", L"Settings" };
const wchar_t PageIcons[] = { 0xE80F, 0xE8B7, 0xE9D9, 0xE74D, 0xE770, 0xEDA2, 0xE81C, 0xE713 };

enum class Safety { Safe, Review, Caution, Protected };

struct App
{
    PreviewOptions opt;
    HWND hwnd = nullptr;
    Canvas canvas;
    const gfx::Palette* pal = &gfx::Dark();
    int page = 0;
    int mapMode = 0;
    int hoverRail = -1;
    int hoverSeg = -1;
    u32 hoverTile = NoNode;
    float mouseX = 0, mouseY = 0;
    DWORD pageStart = 0, unfoldStart = 0;

    // Data
    std::wstring root;
    std::wstring volume;
    u64 volTotal = 0, volFree = 0;
    std::unique_ptr<Scanner> scanner;
    std::unique_ptr<QuickWins> quick;
    DWORD scanStart = 0;
    std::unique_ptr<ScanTree> tree;
    std::unique_ptr<InsightsReport> report;
    std::unique_ptr<viz::KindModel> kinds;
    viz::Sunburst sun;
    u32 sunRoot = 0;
    viz::Treemap treemap;
    int treemapW = 0, treemapH = 0;
    u32 treemapRoot = NoNode;
    viz::FlameGraph flame;
    VolumeInternals vol;
    FileInternals bigFile;
    SlackSummary slack;
    bool scanning = true;
    std::thread analysis;

    // Hit regions for this frame.
    struct Hot { D2D1_RECT_F r; int id; };
    std::vector<Hot> hots;
};
App g;

const gfx::Palette& P() { return *g.pal; }

std::wstring Share(u64 bytes)
{
    if (!g.volTotal) return FormatBytes(bytes);
    return FormatBytes(bytes) + L" · " + FormatPercent(static_cast<double>(bytes) / static_cast<double>(g.volTotal)) +
        L" of " + g.volume.substr(0, 2);
}

Color SafetyColor(Safety s)
{
    switch (s)
    {
    case Safety::Safe: return P().safe;
    case Safety::Review: return P().review;
    case Safety::Caution: return P().caution;
    default: return P().protectedC;
    }
}

const wchar_t* SafetyLabel(Safety s)
{
    static const wchar_t* labels[] = { L"Safe", L"Review", L"Caution", L"Protected" };
    return labels[static_cast<int>(s)];
}

// --- Small components ---------------------------------------------------------

void Badge(Canvas& c, float x, float y, Safety s)
{
    const std::wstring label = SafetyLabel(s);
    const float w = c.Measure(label, Text::CaptionStrong).width + 22;
    const Color col = SafetyColor(s);
    c.FillRound(Rect(x, y, w, 22), 11, col.WithAlpha(P().dark ? 0.16f : 0.12f));
    c.Circle(x + 9, y + 11, 3, col);
    c.Text(label, Rect(x + 15, y, w - 15, 22), Text::CaptionStrong, col, Align::Left, true);
}

float BadgeWidth(Canvas& c, Safety s) { return c.Measure(SafetyLabel(s), Text::CaptionStrong).width + 22; }

void Button(Canvas& c, const D2D1_RECT_F& r, const std::wstring& label, bool primary, int id = -1)
{
    if (primary)
    {
        c.FillRound(r, 8, P().accent);
        c.Text(label, r, Text::BodyStrong, P().accentInk, Align::Center, true);
    }
    else
    {
        c.FillRound(r, 8, P().surface2);
        c.StrokeRound(r, 8, P().stroke);
        c.Text(label, r, Text::BodyStrong, P().text, Align::Center, true);
    }
    if (id >= 0) g.hots.push_back({ r, id });
}

void Overline(Canvas& c, const std::wstring& s, float x, float y, float w)
{
    std::wstring up = s;
    CharUpperBuffW(&up[0], static_cast<DWORD>(up.size()));
    c.Text(up, Rect(x, y, w, 16), Text::Overline, P().textFaint);
}

void Bar(Canvas& c, const D2D1_RECT_F& r, double fraction, const Color& col)
{
    c.FillRound(r, (r.bottom - r.top) / 2, P().freeSpace);
    const float w = static_cast<float>(std::clamp(fraction, 0.0, 1.0)) * (r.right - r.left);
    if (w > 1) c.FillRound({ r.left, r.top, r.left + std::max(w, r.bottom - r.top), r.bottom }, (r.bottom - r.top) / 2, col);
}

void EmptyState(Canvas& c, const D2D1_RECT_F& r, wchar_t glyph, const std::wstring& title, const std::wstring& body)
{
    const float cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2 - 30;
    c.Circle(cx, cy - 10, 30, P().surface2);
    c.Icon(glyph, cx - 14, cy - 24, 28, P().textDim);
    c.Text(title, { r.left, cy + 30, r.right, cy + 56 }, Text::BodyStrong, P().text, Align::Center);
    c.Text(body, { r.left + 40, cy + 56, r.right - 40, cy + 100 }, Text::Caption, P().textDim, Align::Center);
}

// --- Frame: rail + top bar ------------------------------------------------------

void DrawRail(Canvas& c)
{
    c.Fill(Rect(0, 0, Rail, c.Height()), P().base);
    // Brand mark
    c.FillRound(Rect(22, 14, 40, 40), 12, P().accent);
    c.ArcStroke(42, 34, 11, 0.6f, 2 * Pi - 0.2f, 4, P().accentInk);
    c.Circle(42, 34, 3.5f, P().accentInk);
    for (int i = 0; i < 8; ++i)
    {
        const float y = 80 + static_cast<float>(i) * 66;
        const D2D1_RECT_F r = Rect(8, y, Rail - 16, 60);
        const bool sel = i == g.page;
        if (sel) c.FillRound(r, 10, P().surface2);
        else if (i == g.hoverRail) c.FillRound(r, 10, P().surface2.WithAlpha(0.5f));
        if (sel) c.FillRound(Rect(4, y + 18, 3, 24), 1.5f, P().accent);
        c.Icon(PageIcons[i], (Rail - 22) / 2, y + 8, 22, sel ? P().accent : P().textDim);
        c.Text(PageNames[i], Rect(8, y + 34, Rail - 16, 18), Text::Caption, sel ? P().text : P().textDim, Align::Center);
        g.hots.push_back({ r, 100 + i });
    }
}

void DrawTopBar(Canvas& c)
{
    const float x = Rail + Margin;
    c.Text(PageNames[g.page], Rect(x, 14, 400, 36), Text::Title, P().text, Align::Left, true);
    // Drive chip
    const std::wstring chip = g.volume.substr(0, 2) + L"  " + FormatBytes(g.volFree) + L" free of " + FormatBytes(g.volTotal);
    const float cw = c.Measure(chip, Text::BodyStrong).width + 56;
    const float cx = c.Width() - Margin - 300 - 12 - cw;
    const D2D1_RECT_F cr = Rect(cx, 16, cw, 32);
    c.FillRound(cr, 16, P().surface1);
    c.StrokeRound(cr, 16, P().stroke);
    const double used = g.volTotal ? 1.0 - static_cast<double>(g.volFree) / static_cast<double>(g.volTotal) : 0;
    const bool low = g.volTotal && IsLow(g.volFree, g.volTotal, RescueConfig());
    c.ArcStroke(cx + 18, 32, 7, 0, static_cast<float>(used) * 2 * Pi, 3, low ? P().caution : P().accent);
    c.Text(chip, Rect(cx + 34, 16, cw - 40, 32), Text::BodyStrong, P().text, Align::Left, true);
    // Command palette field
    const D2D1_RECT_F sr = Rect(c.Width() - Margin - 300, 16, 300, 32);
    c.FillRound(sr, 8, P().surface1);
    c.StrokeRound(sr, 8, P().stroke);
    c.Icon(0xE721, sr.left + 10, 23, 16, P().textFaint);
    c.Text(L"Search or run a command", Rect(sr.left + 34, 16, 200, 32), Text::Body, P().textFaint, Align::Left, true);
    c.FillRound(Rect(sr.right - 52, 22, 44, 20), 5, P().surface2);
    c.Text(L"Ctrl K", Rect(sr.right - 52, 22, 44, 20), Text::Caption, P().textDim, Align::Center, true);
}

// --- Pages ------------------------------------------------------------------------

struct Reclaim { u64 safe = 0, review = 0, caution = 0; };

Reclaim ReclaimSplit()
{
    Reclaim r;
    if (!g.report) return r;
    for (const CategoryResult& cr : g.report->categories)
    {
        switch (cr.id)
        {
        case Category::TempFiles: case Category::BrowserCaches: case Category::WindowsUpdate: case Category::RecycleBin:
            r.safe += cr.bytes; break;
        case Category::DevCaches: case Category::OldDownloads: case Category::Installers: case Category::Duplicates:
            r.review += cr.bytes; break;
        case Category::Videos:
            r.caution += cr.bytes; break;
        default: break;
        }
    }
    return r;
}

void DrawSunburstHero(Canvas& c, const D2D1_RECT_F& card)
{
    c.Card(card, 16);
    const float h = card.bottom - card.top, w = card.right - card.left;
    const float legendH = 64;
    const float outer = std::min(w - 48, h - legendH - 40) / 2;
    const float cx = (card.left + card.right) / 2, cy = card.top + 20 + outer;
    const float inner = outer * 0.40f;
    const float t = gfx::Ease(g.unfoldStart, 700, true);
    g.sun.Draw(c, *g.kinds, cx, cy, inner, outer, t, g.page == 0 ? g.hoverSeg : -1);
    // Centre: free space
    const bool low = IsLow(g.volFree, g.volTotal, RescueConfig());
    c.Text(g.volume.substr(0, 2), Rect(cx - inner, cy - 44, inner * 2, 18), Text::Overline, P().textFaint, Align::Center);
    c.Text(FormatBytes(g.volFree), Rect(cx - inner, cy - 26, inner * 2, 40), Text::Number, low ? P().caution : P().text, Align::Center);
    c.Text(L"free of " + FormatBytes(g.volTotal), Rect(cx - inner, cy + 14, inner * 2, 18), Text::Caption, P().textDim, Align::Center);
    // Legend: top kinds
    std::vector<std::pair<u64, int>> order;
    for (size_t k = 0; k < g.kinds->totals.size(); ++k) order.push_back({ g.kinds->totals[k], static_cast<int>(k) });
    std::sort(order.rbegin(), order.rend());
    float lx = card.left + 24, ly = card.bottom - legendH + 6;
    for (int i = 0; i < 6 && i < static_cast<int>(order.size()); ++i)
    {
        if (i == 3) { lx = card.left + 24; ly += 26; }
        const auto kind = static_cast<gfx::FileKind>(order[static_cast<size_t>(i)].second);
        c.FillRound(Rect(lx, ly + 5, 10, 10), 3, P().Kind(kind));
        const std::wstring s = std::wstring(gfx::KindLabel(kind)) + L"  " + FormatBytes(order[static_cast<size_t>(i)].first);
        c.Text(s, Rect(lx + 16, ly, (w - 48) / 3 - 18, 20), Text::Caption, P().textDim);
        lx += (w - 48) / 3;
    }
    g.hots.push_back({ card, 300 });
}

void PageOverview(Canvas& c, const D2D1_RECT_F& area)
{
    const float w = area.right - area.left;
    const float heroW = std::min(w * 0.52f, area.bottom - area.top);
    DrawSunburstHero(c, { area.left, area.top, area.left + heroW, area.bottom });

    const float x = area.left + heroW + 20, rw = area.right - x;
    // Reclaim meter
    const Reclaim rc = ReclaimSplit();
    const u64 total = rc.safe + rc.review + rc.caution;
    const D2D1_RECT_F mc = { x, area.top, area.right, area.top + 218 };
    c.Card(mc, 16);
    Overline(c, L"You can safely free", x + 24, mc.top + 22, rw);
    c.Text(FormatBytes(rc.safe), Rect(x + 24, mc.top + 40, rw - 48, 56), Text::Hero, P().text);
    c.Text(L"right now, plus " + FormatBytes(rc.review) + L" to review", Rect(x + 24, mc.top + 96, rw - 48, 20), Text::Body, P().textDim);
    const D2D1_RECT_F bar = Rect(x + 24, mc.top + 128, rw - 48, 10);
    c.FillRound(bar, 5, P().freeSpace);
    float bx = bar.left;
    const Safety order[] = { Safety::Safe, Safety::Review, Safety::Caution };
    const u64 parts[] = { rc.safe, rc.review, rc.caution };
    for (int i = 0; i < 3; ++i)
    {
        if (!total || !parts[i]) continue;
        const float pw = static_cast<float>(parts[i]) / static_cast<float>(total) * (bar.right - bar.left);
        c.Fill({ bx, bar.top, bx + pw, bar.bottom }, SafetyColor(order[i]));
        bx += pw;
    }
    float lx = x + 24;
    for (int i = 0; i < 3; ++i)
    {
        c.Circle(lx + 4, mc.top + 156, 4, SafetyColor(order[i]));
        const std::wstring s = std::wstring(SafetyLabel(order[i])) + L" " + FormatBytes(parts[i]);
        c.Text(s, Rect(lx + 12, mc.top + 147, 150, 18), Text::Caption, P().textDim);
        lx += c.Measure(s, Text::Caption).width + 32;
    }
    Button(c, Rect(x + 24, mc.bottom - 50, 200, 36), L"Review and free space", true, 400);
    Button(c, Rect(x + 236, mc.bottom - 50, 120, 36), L"Explore", false, 101);

    // Top 5 hogs
    float y = mc.bottom + 24;
    Overline(c, L"Biggest space users", x, y, rw);
    y += 24;
    std::vector<const Hog*> hogs;
    if (g.report)
    {
        for (const Hog& hgt : g.report->topFolders) hogs.push_back(&hgt);
        for (const Hog& hgt : g.report->topFiles) hogs.push_back(&hgt);
        std::sort(hogs.begin(), hogs.end(), [](const Hog* a, const Hog* b) { return g.tree->At(a->node).size > g.tree->At(b->node).size; });
    }
    const float rowH = 62;
    for (size_t i = 0; i < hogs.size() && i < 5 && y + rowH <= area.bottom; ++i)
    {
        const Hog& hg = *hogs[i];
        const D2D1_RECT_F r = { x, y, area.right, y + rowH - 8 };
        c.Card(r, 12);
        const Color kc = P().Kind(g.kinds->Of(hg.node));
        c.FillRound(Rect(r.left + 14, r.top + 13, 28, 28), 8, kc.WithAlpha(0.18f));
        c.Icon(g.tree->IsDir(hg.node) ? 0xE8B7 : 0xE7C3, r.left + 20, r.top + 19, 16, kc);
        c.Text(g.tree->Name(hg.node), Rect(r.left + 54, r.top + 8, rw - 260, 20), Text::BodyStrong, P().text);
        c.Text(hg.explanation, Rect(r.left + 54, r.top + 28, rw - 260, 18), Text::Caption, P().textDim);
        c.Text(FormatBytes(g.tree->At(hg.node).size), Rect(r.right - 190, r.top + 8, 172, 20), Text::BodyStrong, P().text, Align::Right);
        c.Text(FormatPercent(static_cast<double>(g.tree->At(hg.node).size) / static_cast<double>(std::max<u64>(1, g.volTotal))) +
            L" of " + g.volume.substr(0, 2), Rect(r.right - 190, r.top + 28, 172, 18), Text::Caption, P().textDim, Align::Right);
        y += rowH;
    }
}

void PageExplore(Canvas& c, const D2D1_RECT_F& area)
{
    const ScanTree& t = *g.tree;
    const float split = area.top + (area.bottom - area.top) * 0.56f;
    // Breadcrumb
    c.Text(g.root + L"  ›  biggest first", Rect(area.left, area.top, 600, 20), Text::Caption, P().textDim);
    // Tree list
    const float listW = (area.right - area.left) * 0.64f;
    const D2D1_RECT_F lc = { area.left, area.top + 26, area.left + listW, split - 12 };
    c.Card(lc, 12);
    const float cols[] = { lc.left + 16, lc.right - 360, lc.right - 250, lc.right - 160, lc.right - 90 };
    const wchar_t* heads[] = { L"Name", L"Share", L"Size", L"Files", L"Changed" };
    // Header cells use the same boxes as the row cells below, so right-aligned numbers line up.
    const D2D1_RECT_F headBoxes[] = { Rect(cols[0], lc.top + 10, 300, 18), Rect(cols[1], lc.top + 10, 90, 18),
        Rect(cols[2] - 10, lc.top + 10, 100, 18), Rect(cols[3] - 10, lc.top + 10, 80, 18), Rect(cols[4] - 30, lc.top + 10, 104, 18) };
    for (int i = 0; i < 5; ++i)
        c.Text(heads[i], headBoxes[i], Text::CaptionStrong, P().textFaint, i >= 2 ? Align::Right : Align::Left);
    c.Line(lc.left + 12, lc.top + 34, lc.right - 12, lc.top + 34, P().stroke);
    std::vector<std::pair<u32, int>> rows;
    const std::vector<u32> top = t.Children(t.Root());
    for (size_t i = 0; i < top.size(); ++i)
    {
        rows.push_back({ top[i], 0 });
        if (i == 0 && t.IsDir(top[i]))
            for (u32 ch : t.Children(top[i])) { rows.push_back({ ch, 1 }); if (rows.size() > 7) break; }
    }
    float y = lc.top + 40;
    const u64 rootSize = std::max<u64>(1, t.At(t.Root()).size);
    for (size_t i = 0; i < rows.size() && y + 26 <= lc.bottom - 6; ++i)
    {
        const u32 n = rows[i].first;
        const float indent = 18.0f * static_cast<float>(rows[i].second);
        if (i == 1) c.FillRound({ lc.left + 6, y - 2, lc.right - 6, y + 26 }, 6, P().surface2);
        if (t.IsDir(n)) c.Icon(i == 0 ? 0xE70D : 0xE76C, cols[0] + indent - 4, y + 4, 12, P().textFaint);
        c.FillRound(Rect(cols[0] + indent + 14, y + 7, 10, 10), 3, P().Kind(g.kinds->Of(n)));
        c.Text(t.Name(n), Rect(cols[0] + indent + 30, y, cols[1] - cols[0] - indent - 40, 24), Text::Body, P().text, Align::Left, true);
        const double share = static_cast<double>(t.At(n).size) / static_cast<double>(rootSize);
        Bar(c, Rect(cols[1], y + 9, 64, 6), share, P().Kind(g.kinds->Of(n)));
        c.Text(FormatPercent(share), Rect(cols[1] + 68, y, 40, 24), Text::Caption, P().textDim, Align::Left, true);
        c.Text(FormatBytes(t.At(n).size), Rect(cols[2] - 10, y, 100, 24), Text::Body, P().text, Align::Right, true);
        c.Text(FormatCount(t.At(n).files), Rect(cols[3] - 10, y, 80, 24), Text::Caption, P().textDim, Align::Right, true);
        c.Text(FormatAge(t.At(n).writeTime), Rect(cols[4] - 30, y, 104, 24), Text::Caption, P().textDim, Align::Right, true);
        y += 28;
    }
    // File types
    const D2D1_RECT_F fc = { lc.right + 16, lc.top, area.right, split - 12 };
    c.Card(fc, 12);
    c.Text(L"File types", Rect(fc.left + 16, fc.top + 10, 200, 20), Text::BodyStrong, P().text);
    std::vector<std::pair<u64, int>> order;
    u64 sum = 0;
    for (size_t k = 0; k < g.kinds->totals.size(); ++k) { order.push_back({ g.kinds->totals[k], static_cast<int>(k) }); sum += g.kinds->totals[k]; }
    std::sort(order.rbegin(), order.rend());
    y = fc.top + 40;
    for (const auto& o : order)
    {
        if (y + 26 > fc.bottom - 6) break;
        const auto kind = static_cast<gfx::FileKind>(o.second);
        c.FillRound(Rect(fc.left + 16, y + 6, 10, 10), 3, P().Kind(kind));
        c.Text(gfx::KindLabel(kind), Rect(fc.left + 34, y, 150, 22), Text::Body, P().text, Align::Left, true);
        c.Text(FormatBytes(o.first), Rect(fc.right - 96, y, 80, 22), Text::Body, P().textDim, Align::Right, true);
        Bar(c, Rect(fc.left + 34, y + 22, fc.right - fc.left - 50, 3), sum ? static_cast<double>(o.first) / static_cast<double>(sum) : 0, P().Kind(kind));
        y += 32;
    }
    // Treemap docked at the bottom, linked by colour
    const D2D1_RECT_F tc = { area.left, split, area.right, area.bottom };
    c.Card(tc, 12);
    const D2D1_RECT_F inner = gfx::Inset(tc, 8);
    const int pw = static_cast<int>((inner.right - inner.left) * c.Dpi() / 96.0f);
    const int ph = static_cast<int>((inner.bottom - inner.top) * c.Dpi() / 96.0f);
    if (pw != g.treemapW || ph != g.treemapH || g.treemapRoot != t.Root())
    {
        g.treemap.Render(t, *g.kinds, P(), t.Root(), pw, ph);
        g.treemapW = pw; g.treemapH = ph; g.treemapRoot = t.Root();
    }
    c.PushClip(inner);
    c.Pixels(g.treemap.Pixels(), static_cast<UINT>(pw), static_cast<UINT>(ph), inner);
    c.PopClip();
}

void Tooltip(Canvas& c, float x, float y, u32 node, u64 bytes, const std::wstring& name)
{
    const ScanTree& t = *g.tree;
    const float w = 260, h = 112;
    x = std::min(x + 16, c.Width() - w - 12);
    y = std::min(y + 16, c.Height() - h - 12);
    const D2D1_RECT_F r = Rect(x, y, w, h);
    c.Shadow(r, 12, 24, 8, P().dark ? 0.4f : 0.16f);
    c.FillRound(r, 12, P().surface2);
    c.StrokeRound(r, 12, P().stroke);
    c.Text(name, Rect(x + 16, y + 12, w - 32, 20), Text::BodyStrong, P().text);
    c.Text(FormatBytes(bytes), Rect(x + 16, y + 34, w - 32, 28), Text::Number, P().text);
    std::wstring line = Share(bytes);
    if (node != NoNode)
    {
        const u32 parent = t.At(node).parent;
        if (parent != NoNode && t.At(parent).size)
            line = FormatPercent(static_cast<double>(bytes) / static_cast<double>(t.At(parent).size)) + L" of " + t.Name(parent);
        c.Text(line, Rect(x + 16, y + 64, w - 32, 18), Text::Caption, P().textDim);
        c.Text(FormatCount(t.At(node).files) + L" files · changed " + FormatAge(t.At(node).writeTime),
            Rect(x + 16, y + 84, w - 32, 18), Text::Caption, P().textDim);
    }
    else c.Text(line, Rect(x + 16, y + 64, w - 32, 18), Text::Caption, P().textDim);
}

void PageMap(Canvas& c, const D2D1_RECT_F& area)
{
    const ScanTree& t = *g.tree;
    // Segmented toggle
    const wchar_t* modes[] = { L"Sunburst", L"Treemap", L"Flame graph" };
    const D2D1_RECT_F seg = Rect(area.left, area.top, 330, 34);
    c.FillRound(seg, 9, P().surface1);
    c.StrokeRound(seg, 9, P().stroke);
    for (int i = 0; i < 3; ++i)
    {
        const D2D1_RECT_F r = Rect(seg.left + 3 + 108.0f * static_cast<float>(i), seg.top + 3, 106, 28);
        if (i == g.mapMode) c.FillRound(r, 7, P().surface3);
        c.Text(modes[i], r, Text::BodyStrong, i == g.mapMode ? P().text : P().textDim, Align::Center, true);
        g.hots.push_back({ r, 200 + i });
    }
    c.Text(L"Click a ring to drill in, the centre to go up. Colours show what kind of files fill each folder.",
        Rect(seg.right + 20, seg.top, 640, 34), Text::Caption, P().textDim, Align::Left, true);
    const D2D1_RECT_F vr = { area.left, area.top + 50, area.right, area.bottom };
    c.Card(vr, 16);
    if (g.mapMode == 0)
    {
        const float outer = std::min(vr.right - vr.left, vr.bottom - vr.top) / 2 - 28;
        const float cx = (vr.left + vr.right) / 2, cy = (vr.top + vr.bottom) / 2;
        g.sun.Draw(c, *g.kinds, cx, cy, outer * 0.32f, outer, gfx::Ease(g.unfoldStart, 700, true), g.hoverSeg);
        c.Text(t.Name(g.sunRoot), Rect(cx - 90, cy - 26, 180, 22), Text::BodyStrong, P().text, Align::Center);
        c.Text(FormatBytes(t.At(g.sunRoot).size), Rect(cx - 90, cy - 4, 180, 30), Text::Number, P().text, Align::Center);
        int seg2 = g.hoverSeg;
        if (seg2 < 0 && !g.sun.Segments().empty()) seg2 = 0; // static demo: tooltip on the largest slice
        if (seg2 >= 0)
        {
            const viz::Segment& s = g.sun.Segments()[static_cast<size_t>(seg2)];
            const float mid = (s.a0 + s.a1) / 2;
            const float rr = outer * 0.32f + (outer * 0.68f) * (static_cast<float>(s.depth) - 0.5f) / static_cast<float>(g.sun.MaxDepth());
            const float tx = g.hoverSeg >= 0 ? g.mouseX : cx + rr * std::sin(mid), ty = g.hoverSeg >= 0 ? g.mouseY : cy - rr * std::cos(mid);
            Tooltip(c, tx, ty, s.node, s.bytes, s.freeSpace ? L"Free space" : s.other ? L"Smaller items" : t.Name(s.node));
        }
    }
    else if (g.mapMode == 1)
    {
        const D2D1_RECT_F inner = gfx::Inset(vr, 10);
        const int pw = static_cast<int>((inner.right - inner.left) * c.Dpi() / 96.0f);
        const int ph = static_cast<int>((inner.bottom - inner.top) * c.Dpi() / 96.0f);
        if (pw != g.treemapW || ph != g.treemapH)
        {
            g.treemap.Render(t, *g.kinds, P(), t.Root(), pw, ph);
            g.treemapW = pw; g.treemapH = ph;
        }
        c.Pixels(g.treemap.Pixels(), static_cast<UINT>(pw), static_cast<UINT>(ph), inner);
        c.Text(Fmt(L"Layout + shading %.0f ms", g.treemap.LayoutMillis()), Rect(vr.right - 220, vr.bottom - 26, 200, 18),
            Text::Caption, Color(0xFFFFFF, 0.8f), Align::Right);
        if (g.hoverTile != NoNode) Tooltip(c, g.mouseX, g.mouseY, g.hoverTile, t.At(g.hoverTile).size, t.Name(g.hoverTile));
    }
    else
        g.flame.Draw(c, t, *g.kinds, t.Root(), gfx::Inset(vr, 16), 24, 14);
}

struct Section
{
    const wchar_t* title;
    wchar_t icon;
    Safety safety;
    std::vector<Category> cats;
    const wchar_t* note;
};

void PageCleanup(Canvas& c, const D2D1_RECT_F& area)
{
    const Section sections[] = {
        { L"Windows temp", 0xE7B8, Safety::Safe, { Category::TempFiles, Category::WindowsUpdate, Category::RecycleBin },
          L"Scratch files Windows and apps leave behind. Removed files are recreated when needed." },
        { L"App caches", 0xE774, Safety::Safe, { Category::BrowserCaches },
          L"Browser and app caches rebuild themselves. Sign-ins and history are kept." },
        { L"Developer", 0xE943, Safety::Review, { Category::DevCaches },
          L"node_modules, Gradle, Maven, pip and NuGet caches. Restore with your package manager." },
        { L"Old and unused", 0xE81C, Safety::Review, { Category::OldDownloads, Category::Installers, Category::Duplicates, Category::Videos },
          L"Old downloads, installers you already ran, duplicates and large videos." },
    };
    u64 grand = 0;
    for (const Section& s : sections)
        for (Category ct : s.cats) grand += g.report->categories[static_cast<size_t>(ct)].bytes;
    c.Text(L"4 groups · " + FormatBytes(grand) + L" could be freed. Pick a group to review it item by item.",
        Rect(area.left, area.top, 900, 22), Text::Body, P().textDim);
    const float gw = (area.right - area.left - 20) / 2, gh = (area.bottom - area.top - 40 - 20) / 2;
    for (int i = 0; i < 4; ++i)
    {
        const Section& s = sections[i];
        const float x = area.left + static_cast<float>(i % 2) * (gw + 20);
        const float y = area.top + 36 + static_cast<float>(i / 2) * (gh + 20);
        const D2D1_RECT_F r = Rect(x, y, gw, gh);
        c.Card(r, 16, i == 0);
        u64 total = 0;
        for (Category ct : s.cats) total += g.report->categories[static_cast<size_t>(ct)].bytes;
        c.FillRound(Rect(x + 20, y + 20, 36, 36), 10, SafetyColor(s.safety).WithAlpha(0.15f));
        c.Icon(s.icon, x + 28, y + 28, 20, SafetyColor(s.safety));
        c.Text(s.title, Rect(x + 68, y + 18, gw - 200, 22), Text::BodyStrong, P().text);
        c.Text(FormatBytes(total), Rect(x + 68, y + 38, gw - 200, 30), Text::Number, P().text);
        Badge(c, r.right - 20 - BadgeWidth(c, s.safety), y + 22, s.safety);
        c.Text(s.note, Rect(x + 20, y + 76, gw - 40, 36), Text::Caption, P().textDim);
        float ry = y + 116;
        for (Category ct : s.cats)
        {
            const CategoryResult& cr = g.report->categories[static_cast<size_t>(ct)];
            if (ry + 26 > r.bottom - 52) break;
            c.Text(cr.title, Rect(x + 20, ry, gw - 160, 22), Text::Body, P().text, Align::Left, true);
            c.Text(cr.bytes ? FormatBytes(cr.bytes) : std::wstring(L"—"), Rect(r.right - 140, ry, 120, 22), Text::Body,
                P().textDim, Align::Right, true);
            ry += 28;
        }
        Button(c, Rect(x + 20, r.bottom - 50, 130, 34), L"Review ›", i == 0, 500 + i);
    }
}

struct SysItem { const wchar_t* path; const wchar_t* label; const wchar_t* why; const wchar_t* fix; double reducible; };

void PageSystem(Canvas& c, const D2D1_RECT_F& area)
{
    const ScanTree& t = *g.tree;
    const std::wstring win = KnownFolder(CSIDL_WINDOWS);
    const std::wstring sysRoot = VolumeOf(win);
    if (_wcsicmp(VolumeOf(g.root).c_str(), sysRoot.c_str()) != 0 || g.root.size() > 3)
    {
        EmptyState(c, area, 0xE770, L"Scan your system drive to see what Windows uses",
            L"This view needs a whole-drive scan of " + sysRoot.substr(0, 2) + L". Folder scans don't include Windows files.");
        return;
    }
    const SysItem items[] = {
        { L"hiberfil.sys", L"Hibernation file", L"Holds memory contents for hibernate and fast startup.", L"Turn off hibernation", 1.0 },
        { L"pagefile.sys", L"Page file", L"Virtual memory. Windows sizes it automatically.", L"Adjust virtual memory", 0.0 },
        { L"swapfile.sys", L"Swap file", L"Used by Store apps when memory is tight.", L"Managed by Windows", 0.0 },
        { L"Windows\\WinSxS", L"Component store (WinSxS)", L"Every Windows update keeps older components here.", L"Clean up components (DISM)", 0.25 },
        { L"Windows\\System32\\DriverStore", L"Driver store", L"Copies of every installed driver.", L"Managed by Windows", 0.0 },
        { L"Windows\\SoftwareDistribution", L"Windows Update cache", L"Downloaded update packages.", L"Disk Cleanup", 0.9 },
        { L"System Volume Information", L"Restore points", L"Snapshots for System Restore (needs admin to measure).", L"Shrink restore points", 0.0 },
        { L"Program Files", L"Installed apps", L"Programs you installed.", L"Apps & features", 0.0 },
    };
    u64 total = 0;
    double reducible = 0;
    std::vector<std::pair<const SysItem*, u64>> found;
    for (const SysItem& it : items)
    {
        const u32 n = t.Find(JoinPath(g.root, it.path));
        const u64 b = n == NoNode ? 0 : t.At(n).size;
        found.push_back({ &it, b });
        total += b;
        reducible += static_cast<double>(b) * it.reducible;
    }
    const D2D1_RECT_F hero = { area.left, area.top, area.right, area.top + 110 };
    c.Card(hero, 16);
    c.FillRound(Rect(hero.left + 24, hero.top + 26, 56, 56), 16, P().protectedC.WithAlpha(0.16f));
    c.Icon(0xE72E, hero.left + 38, hero.top + 40, 28, P().protectedC);
    c.Text(L"Windows itself uses " + FormatBytes(total), Rect(hero.left + 100, hero.top + 22, 700, 36), Text::Title, P().text);
    c.Text(L"About " + FormatBytes(static_cast<u64>(reducible)) +
        L" of that can be reduced safely with Windows' own tools. Never delete these files by hand.",
        Rect(hero.left + 100, hero.top + 62, 900, 22), Text::Body, P().textDim);
    float y = hero.bottom + 16;
    const float rowH = 58;
    for (const auto& f : found)
    {
        if (y + rowH > area.bottom) break;
        const D2D1_RECT_F r = { area.left, y, area.right, y + rowH - 8 };
        c.Card(r, 12);
        c.Icon(0xE72E, r.left + 18, r.top + 17, 16, P().protectedC);
        c.Text(f.first->label, Rect(r.left + 48, r.top + 7, 340, 20), Text::BodyStrong, P().text);
        c.Text(f.first->why, Rect(r.left + 48, r.top + 27, 520, 18), Text::Caption, P().textDim);
        c.Text(f.second ? FormatBytes(f.second) : std::wstring(L"not measured"), Rect(r.right - 470, r.top, 120, rowH - 8),
            Text::BodyStrong, P().text, Align::Right, true);
        const std::wstring fix = f.first->fix;
        const float fw = c.Measure(fix, Text::CaptionStrong).width + 28;
        const D2D1_RECT_F chip = Rect(r.right - 20 - fw, r.top + 11, fw, 28);
        const bool action = f.first->reducible > 0 || wcscmp(f.first->fix, L"Managed by Windows") != 0;
        c.FillRound(chip, 8, action ? P().accent.WithAlpha(0.14f) : P().surface2);
        c.Text(fix, chip, Text::CaptionStrong, action ? P().accent : P().textDim, Align::Center, true);
        Badge(c, r.right - 330, r.top + 14, Safety::Protected);
        y += rowH;
    }
}

void PageInternals(Canvas& c, const D2D1_RECT_F& area)
{
    const float colW = (area.right - area.left - 20) / 2;
    // Cluster map (illustration of the used:free ratio; the exact bitmap needs admin).
    const D2D1_RECT_F cm = Rect(area.left, area.top, colW, 300);
    c.Card(cm, 16);
    c.Text(L"Cluster map", Rect(cm.left + 20, cm.top + 16, 300, 22), Text::BodyStrong, P().text);
    const u64 clusters = g.vol.totalClusters, freeC = g.vol.freeClusters;
    c.Text(FormatCount(clusters) + L" clusters of " + FormatBytes(g.vol.bytesPerCluster) + L" · " +
        FormatCount(freeC) + L" free", Rect(cm.left + 20, cm.top + 38, colW - 40, 18), Text::Caption, P().textDim);
    const int cols = 48, rows = 12;
    const float cell = std::min((colW - 40) / cols, 13.0f);
    const int used = clusters ? static_cast<int>(std::lround(static_cast<double>(clusters - freeC) / clusters * cols * rows)) : 0;
    unsigned seed = 7;
    for (int i = 0; i < cols * rows; ++i)
    {
        seed = seed * 1103515245u + 12345u;
        // Mostly-front-loaded allocation with scattered holes, like a real NTFS volume.
        const bool isUsed = i < used ? ((seed >> 16) % 23 != 0) : ((seed >> 16) % 31 == 0);
        const float x = cm.left + 20 + static_cast<float>(i % cols) * cell, y = cm.top + 68 + static_cast<float>(i / cols) * cell;
        c.FillRound(Rect(x, y, cell - 2, cell - 2), 2, isUsed ? P().accent.WithAlpha(0.75f) : P().freeSpace);
    }
    c.Text(L"Each square is about " + FormatBytes(clusters ? clusters * g.vol.bytesPerCluster / (cols * rows) : 0) +
        L". Teal is in use, grey is free. The exact layout needs administrator rights; this shows the real ratio.",
        Rect(cm.left + 20, cm.bottom - 52, colW - 40, 40), Text::Caption, P().textDim);

    // File layout: extents of the largest file
    const D2D1_RECT_F fl = Rect(area.left + colW + 20, area.top, colW, 300);
    c.Card(fl, 16);
    c.Text(L"How a file sits on disk", Rect(fl.left + 20, fl.top + 16, colW - 40, 22), Text::BodyStrong, P().text);
    const FileInternals& f = g.bigFile;
    const size_t slash = f.path.find_last_of(L'\\');
    c.Text(slash == std::wstring::npos ? f.path : f.path.substr(slash + 1), Rect(fl.left + 20, fl.top + 38, colW - 40, 18),
        Text::Caption, P().textDim);
    const D2D1_RECT_F strip = Rect(fl.left + 20, fl.top + 70, colW - 40, 34);
    c.FillRound(strip, 6, P().surface2);
    u64 totalClusters = 0;
    for (const Extent& e : f.extents) totalClusters += e.clusters;
    float sx = strip.left;
    for (size_t i = 0; i < f.extents.size(); ++i)
    {
        const Extent& e = f.extents[i];
        const float w = totalClusters ? static_cast<float>(e.clusters) / static_cast<float>(totalClusters) * (strip.right - strip.left) : 0;
        if (!e.hole) c.Fill({ sx, strip.top + 3, sx + std::max(1.0f, w - 1.5f), strip.bottom - 3 },
            i % 2 ? P().Kind(gfx::FileKind::DiskImages) : P().Kind(gfx::FileKind::Programs));
        sx += w;
    }
    const std::wstring verdict = f.fragments <= 1 ? L"Contiguous: stored in one piece." :
        Fmt(L"Fragmented: stored in %llu pieces across the disk.", f.fragments);
    c.Text(verdict, Rect(fl.left + 20, strip.bottom + 10, colW - 40, 20), Text::BodyStrong, f.fragments <= 1 ? P().safe : P().review);
    const wchar_t* labels[] = { L"Logical size", L"Size on disk", L"Slack" };
    const u64 vals[] = { f.logicalSize, f.allocatedSize, f.slack };
    for (int i = 0; i < 3; ++i)
    {
        const float x = fl.left + 20 + static_cast<float>(i) * (colW - 40) / 3;
        Overline(c, labels[i], x, strip.bottom + 46, 140);
        c.Text(FormatBytes(vals[i]), Rect(x, strip.bottom + 62, 160, 28), Text::Number, P().text);
    }
    c.Text(Explain(Metric::Extents), Rect(fl.left + 20, fl.bottom - 70, colW - 40, 60), Text::Caption, P().textDim);

    // Explanation cards
    struct Card { const wchar_t* title; std::wstring value; Metric m; };
    const Card cards[] = {
        { L"Cluster size", FormatBytes(g.vol.bytesPerCluster), Metric::ClusterSize },
        { L"Slack space", FormatBytes(g.slack.totalSlack), Metric::Slack },
        { L"Master File Table", g.vol.ntfsData ? FormatCount(g.vol.mftRecordCount) + L" records" : std::wstring(L"Needs admin"), Metric::Mft },
    };
    const float cw = (area.right - area.left - 40) / 3;
    for (int i = 0; i < 3; ++i)
    {
        const D2D1_RECT_F r = Rect(area.left + static_cast<float>(i) * (cw + 20), area.top + 320, cw, area.bottom - area.top - 320);
        c.Card(r, 16);
        Overline(c, cards[i].title, r.left + 20, r.top + 18, cw - 40);
        c.Text(cards[i].value, Rect(r.left + 20, r.top + 36, cw - 40, 32), Text::Number, P().text);
        c.Text(L"What does this mean?", Rect(r.left + 20, r.top + 76, cw - 40, 18), Text::CaptionStrong, P().accent);
        c.Text(Explain(cards[i].m), Rect(r.left + 20, r.top + 96, cw - 40, r.bottom - r.top - 104), Text::Caption, P().textDim);
    }
}

void PageHistory(Canvas& c, const D2D1_RECT_F& area)
{
    const float h1 = (area.bottom - area.top) * 0.6f;
    const D2D1_RECT_F gc = { area.left, area.top, area.right, area.top + h1 };
    c.Card(gc, 16);
    c.Text(L"Growth since the last scan", Rect(gc.left + 20, gc.top + 16, 500, 22), Text::BodyStrong, P().text);
    if (g.report && g.report->hasSnapshot && !g.report->growth.empty())
    {
        long long maxDelta = 1;
        for (const GrowthEntry& e : g.report->growth) maxDelta = std::max(maxDelta, std::llabs(e.delta));
        float y = gc.top + 54;
        for (const GrowthEntry& e : g.report->growth)
        {
            if (y + 30 > gc.bottom - 10) break;
            c.Text(e.path, Rect(gc.left + 20, y, 420, 22), Text::Body, P().text, Align::Left, true);
            const float bw = static_cast<float>(std::llabs(e.delta)) / static_cast<float>(maxDelta) * (gc.right - gc.left - 640);
            c.FillRound(Rect(gc.left + 460, y + 6, std::max(3.0f, bw), 10), 5, e.delta > 0 ? P().review : P().safe);
            c.Text((e.delta > 0 ? L"+" : L"−") + FormatBytes(static_cast<u64>(std::llabs(e.delta))), Rect(gc.right - 160, y, 140, 22),
                Text::Body, P().textDim, Align::Right, true);
            y += 32;
        }
    }
    else
    {
        // Skeleton bars hint at what will appear.
        for (int i = 0; i < 5; ++i)
            c.FillRound(Rect(gc.left + 40, gc.top + 70 + static_cast<float>(i) * 30, 120.0f + static_cast<float>((i * 97) % 260), 12), 6,
                P().surface2);
        EmptyState(c, { gc.left + 300, gc.top + 20, gc.right, gc.bottom }, 0xE81C, L"First scan of " + g.volume.substr(0, 2),
            L"Growth appears after your next scan. Snapshots are saved on another drive, never on a full one.");
    }
    const D2D1_RECT_F nc = { area.left, gc.bottom + 16, area.right, area.bottom };
    c.Card(nc, 16);
    c.Text(L"Low-space alerts", Rect(nc.left + 20, nc.top + 16, 500, 22), Text::BodyStrong, P().text);
    const bool on = IsStartWithWindows();
    Badge(c, nc.left + 180, nc.top + 16, on ? Safety::Safe : Safety::Review);
    c.Text(on ? L"Alerts are on. You'll get a quiet notification when a drive crosses 50, 20, 10, 5, 3, 2, 1 GB or 512 MB." :
        L"Alerts are off. Turn them on in Settings to get a quiet notification before a drive fills up.",
        Rect(nc.left + 20, nc.top + 46, nc.right - nc.left - 40, 20), Text::Body, P().textDim);
    c.Text(L"No alerts yet.", Rect(nc.left + 20, nc.top + 76, 400, 20), Text::Caption, P().textFaint);
}

void Toggle(Canvas& c, float x, float y, bool on)
{
    c.FillRound(Rect(x, y, 40, 20), 10, on ? P().accent : P().surface3);
    c.Circle(on ? x + 30 : x + 10, y + 10, 7, on ? P().accentInk : P().textDim);
}

void Segmented(Canvas& c, float x, float y, const std::vector<std::wstring>& items, int sel)
{
    float w = 0;
    for (const auto& s : items) w += c.Measure(s, Text::BodyStrong).width + 28;
    c.FillRound(Rect(x, y, w + 6, 32), 8, P().surface2);
    float cx = x + 3;
    for (size_t i = 0; i < items.size(); ++i)
    {
        const float iw = c.Measure(items[i], Text::BodyStrong).width + 28;
        if (static_cast<int>(i) == sel) c.FillRound(Rect(cx, y + 3, iw, 26), 6, P().surface1);
        c.Text(items[i], Rect(cx, y + 3, iw, 26), Text::BodyStrong, static_cast<int>(i) == sel ? P().text : P().textDim, Align::Center, true);
        cx += iw;
    }
}

void PageSettings(Canvas& c, const D2D1_RECT_F& area)
{
    Settings& st = Settings::Get();
    const std::wstring work = PickWorkVolume(VolumeOf(g.root), st.rescue);
    struct Row { const wchar_t* title; const wchar_t* sub; };
    const Row rows[] = {
        { L"Low-space alerts", L"A quiet notification when a drive crosses a threshold. Runs in the tray using about 2 MB." },
        { L"Alert thresholds", L"Notify when free space drops below these levels." },
        { L"Reserve file", L"Space set aside on the system drive. Release it in Rescue Mode to get Windows breathing again." },
        { L"Work volume", L"Where quarantine, undo logs and snapshots go, so a full drive is never written to." },
        { L"Scan threads", L"More threads scan faster on SSDs. Rescue Mode uses at most 4." },
        { L"Theme", L"Follows Windows unless you choose one." },
    };
    const float rowH = 76;
    const D2D1_RECT_F card = { area.left, area.top, std::min(area.right, area.left + 980), area.top + rowH * 6 + 8 };
    c.Card(card, 16);
    for (int i = 0; i < 6; ++i)
    {
        const float y = card.top + 4 + static_cast<float>(i) * rowH;
        if (i) c.Line(card.left + 20, y, card.right - 20, y, P().stroke);
        c.Text(rows[i].title, Rect(card.left + 24, y + 14, 420, 22), Text::BodyStrong, P().text);
        c.Text(rows[i].sub, Rect(card.left + 24, y + 36, 520, 32), Text::Caption, P().textDim);
        const float rx = card.left + 580;
        switch (i)
        {
        case 0: Toggle(c, card.right - 64, y + 26, IsStartWithWindows()); break;
        case 1:
        {
            const wchar_t* th[] = { L"50", L"20", L"10", L"5", L"3", L"2", L"1 GB", L"512 MB" };
            float x = rx;
            for (const wchar_t* s : th)
            {
                const float w = c.Measure(s, Text::CaptionStrong).width + 18;
                c.FillRound(Rect(x, y + 24, w, 26), 13, P().accent.WithAlpha(0.14f));
                c.Text(s, Rect(x, y + 24, w, 26), Text::CaptionStrong, P().accent, Align::Center, true);
                x += w + 6;
            }
            break;
        }
        case 2: Segmented(c, rx, y + 22, { L"256 MB", L"512 MB", L"1 GB", L"2 GB" }, 1); break;
        case 3:
        {
            const std::wstring s = work.empty() ? std::wstring(L"No other drive — kept in memory") : work.substr(0, 2) + L"  ·  automatic";
            const D2D1_RECT_F r = Rect(rx, y + 22, 300, 32);
            c.FillRound(r, 8, P().surface2);
            c.StrokeRound(r, 8, P().stroke);
            c.Text(s, Rect(r.left + 12, r.top, 260, 32), Text::Body, P().text, Align::Left, true);
            c.Icon(0xE70D, r.right - 28, r.top + 9, 14, P().textDim);
            break;
        }
        case 4:
        {
            const D2D1_RECT_F r = Rect(rx, y + 22, 132, 32);
            c.FillRound(r, 8, P().surface2);
            c.Text(L"−", Rect(r.left, r.top, 36, 32), Text::BodyStrong, P().textDim, Align::Center, true);
            c.Text(Fmt(L"%d", g.tree ? g.tree->threadsUsed : 8), Rect(r.left + 36, r.top, 60, 32), Text::BodyStrong, P().text, Align::Center, true);
            c.Text(L"+", Rect(r.right - 36, r.top, 36, 32), Text::BodyStrong, P().textDim, Align::Center, true);
            break;
        }
        case 5: Segmented(c, rx, y + 22, { L"System", L"Light", L"Dark" }, g.opt.theme); break;
        }
    }
    c.Text(L"SpaceRescue · GPL-3.0 · built on ideas and code from WinDirStat (windirstat.net)",
        Rect(area.left, card.bottom + 16, 800, 18), Text::Caption, P().textFaint);
}

void DrawScanning(Canvas& c, const D2D1_RECT_F& area)
{
    const ScanProgress p = g.scanner ? g.scanner->Progress() : ScanProgress();
    // Quick wins (right column): sized in parallel, answer within seconds.
    const float qw = 320;
    const float w = std::min(700.0f, area.right - area.left - qw - 40);
    const float x = area.left + (area.right - area.left - w - qw - 40) / 2;
    if (g.quick)
    {
        const float qx = x + w + 40;
        const D2D1_RECT_F card = Rect(qx, area.top + 40, qw, 420);
        c.Card(card, 16);
        Overline(c, L"Quick wins found so far", qx + 20, card.top + 18, qw - 40);
        c.Text(FormatBytes(g.quick->SafeTotal()), Rect(qx + 20, card.top + 36, qw - 40, 40), Text::Number, P().safe);
        c.Text(Fmt(L"safe to free · %.1f s", (GetTickCount() - g.scanStart) / 1000.0), Rect(qx + 20, card.top + 70, qw - 40, 18),
            Text::Caption, P().textDim);
        float qy = card.top + 100;
        for (const QuickWin& q : g.quick->Snapshot())
        {
            if (qy + 26 > card.bottom - 8 || !q.bytes) continue;
            c.Circle(qx + 24, qy + 11, 3.5f, q.safe ? P().safe : P().review);
            c.Text(q.label, Rect(qx + 34, qy, qw - 150, 22), Text::Body, q.done ? P().text : P().textDim, Align::Left, true);
            c.Text(FormatBytes(q.bytes), Rect(qx + qw - 120, qy, 100, 22), Text::Body, P().textDim, Align::Right, true);
            qy += 28;
        }
    }
    float y = area.top + 40;
    Overline(c, L"Scanning " + g.root, x, y, w);
    c.Text(FormatCount(p.files) + L" files · " + FormatBytes(p.bytes), Rect(x, y + 20, w, 56), Text::Hero, P().text);
    c.Text(L"Biggest folders so far. This list fills in live, so you don't have to wait for the whole scan.",
        Rect(x, y + 80, w, 20), Text::Body, P().textDim);
    y += 120;
    u64 top = 1;
    for (const auto& t : p.topLevel) top = std::max(top, t.bytes);
    for (size_t i = 0; i < p.topLevel.size() && i < 10; ++i)
    {
        const auto& t = p.topLevel[i];
        c.Text(t.name, Rect(x, y, 260, 24), Text::Body, P().text, Align::Left, true);
        Bar(c, Rect(x + 270, y + 9, w - 400, 8), static_cast<double>(t.bytes) / static_cast<double>(top), P().accent);
        c.Text(FormatBytes(t.bytes), Rect(x + w - 120, y, 120, 24), Text::Body, P().textDim, Align::Right, true);
        y += 32;
    }
    if (p.topLevel.empty())
        for (int i = 0; i < 6; ++i) { c.FillRound(Rect(x, y + 8, w, 10), 5, P().surface2); y += 32; }
    c.Text(p.currentPath, Rect(x, area.bottom - 30, w, 18), Text::Caption, P().textFaint);
}

void Paint(HDC printDc = nullptr)
{
    Canvas& c = g.canvas;
    if (printDc ? !c.BeginPrint(g.hwnd, printDc, P()) : !c.Begin(g.hwnd, P())) return;
    g.hots.clear();
    c.Clear(P().base);
    DrawRail(c);
    DrawTopBar(c);
    // Page transition: fade + 8 px slide (motion "standard").
    const float t = gfx::Ease(g.pageStart, 200);
    const D2D1_RECT_F area = { Rail + Margin, Top + 16 + (1 - t) * 8, c.Width() - Margin, c.Height() - 24 };
    if (g.scanning) DrawScanning(c, area);
    else
    {
        switch (g.page)
        {
        case 0: PageOverview(c, area); break;
        case 1: PageExplore(c, area); break;
        case 2: PageMap(c, area); break;
        case 3: PageCleanup(c, area); break;
        case 4: PageSystem(c, area); break;
        case 5: PageInternals(c, area); break;
        case 6: PageHistory(c, area); break;
        default: PageSettings(c, area); break;
        }
        if (g.tree)
            c.Text(FormatCount(g.tree->fileCount) + L" files · scanned in " + Fmt(L"%.1f s", g.tree->scanMillis / 1000.0) +
                L" · " + FormatBytes(PeakWorkingSet()) + L" peak memory", Rect(c.Width() - 420, c.Height() - 20, 400, 16),
                Text::Caption, P().textFaint, Align::Right);
    }
    if (t < 1 || (g.unfoldStart && gfx::Ease(g.unfoldStart, 700, true) < 1)) SetTimer(g.hwnd, TimerAnim, 16, nullptr);
    else KillTimer(g.hwnd, TimerAnim);
    c.End();
}

void SetPage(int p)
{
    if (p == g.page) return;
    g.page = std::clamp(p, 0, 7);
    g.pageStart = GetTickCount();
    g.hoverSeg = -1;
    if (g.page == 0 || g.page == 2)
    {
        g.unfoldStart = GetTickCount();
        g.sunRoot = g.tree ? g.tree->Root() : 0;
        if (g.tree) g.sun.Build(*g.tree, g.sunRoot, g.page == 0 && g.root.size() <= 3 ? g.volFree : 0, g.page == 0 ? 3 : 5);
    }
    InvalidateRect(g.hwnd, nullptr, FALSE);
}

void StartScan()
{
    g.scanning = true;
    g.scanner = std::make_unique<Scanner>();
    ScanOptions o;
    o.root = g.root;
    g.scanner->Start(o, g.hwnd, WM_SCAN_DONE);
    g.scanStart = GetTickCount();
    if (g.root.size() <= 3)
    {
        g.quick = std::make_unique<QuickWins>();
        g.quick->Start(g.volume);
    }
    SetTimer(g.hwnd, TimerProgress, 100, nullptr);
}

void OnScanDone()
{
    g.tree = g.scanner->TakeResult();
    KillTimer(g.hwnd, TimerProgress);
    // Analysis runs here once; it's quick next to the scan.
    InsightsOptions io;
    g.report = std::make_unique<InsightsReport>(Analyze(*g.tree, io));
    g.kinds = std::make_unique<viz::KindModel>(viz::BuildKinds(*g.tree));
    g.vol = QueryVolume(g.volume);
    g.slack = EstimateSlack(*g.tree);
    // Largest file for the extents strip.
    u32 best = NoNode;
    for (u32 i = 1; i < g.tree->Count(); ++i)
    {
        const Node& n = g.tree->At(i);
        if (!(n.flags & (NF_DIR | NF_AGGREGATE)) && (best == NoNode || n.size > g.tree->At(best).size)) best = i;
    }
    if (best != NoNode) g.bigFile = QueryFile(g.tree->Path(best), g.vol.bytesPerCluster);
    g.scanning = false;
    const int p = g.page;
    g.page = -1;
    SetPage(p);
}

int HotAt(float x, float y)
{
    for (auto it = g.hots.rbegin(); it != g.hots.rend(); ++it)
        if (gfx::Contains(it->r, x, y)) return it->id;
    return -1;
}

LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        Paint();
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_PRINTCLIENT:
        Paint(reinterpret_cast<HDC>(wp));
        return 0;
    case WM_SIZE:
        g.canvas.Resize(LOWORD(lp), HIWORD(lp));
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_DPICHANGED:
    {
        const RECT* r = reinterpret_cast<const RECT*>(lp);
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        g.treemapW = 0;
        return 0;
    }
    case WM_TIMER:
        if (wp == 99) // SR_SNAPSHOT: render this frame into a BMP (docs screenshots without screen capture)
        {
            KillTimer(hwnd, 99);
            wchar_t spec[MAX_PATH] = {};
            GetEnvironmentVariableW(L"SR_SNAPSHOT", spec, MAX_PATH);
            std::wstring file = spec;
            file = file.substr(0, file.find(L';'));
            RECT rc;
            GetClientRect(hwnd, &rc);
            HDC screen = GetDC(hwnd);
            HDC mem = CreateCompatibleDC(screen);
            BITMAPINFO bi = {};
            bi.bmiHeader = { sizeof(BITMAPINFOHEADER), rc.right, -rc.bottom, 1, 32, BI_RGB };
            void* bits = nullptr;
            HBITMAP dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
            HGDIOBJ old = SelectObject(mem, dib);
            Paint(mem);
            GdiFlush();
            BITMAPFILEHEADER fh = { 0x4D42, 0, 0, 0, sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) };
            const DWORD bytes = static_cast<DWORD>(rc.right) * rc.bottom * 4;
            fh.bfSize = fh.bfOffBits + bytes;
            if (FILE* f = _wfopen(file.c_str(), L"wb"))
            {
                fwrite(&fh, sizeof fh, 1, f);
                fwrite(&bi.bmiHeader, sizeof bi.bmiHeader, 1, f);
                fwrite(bits, bytes, 1, f);
                fclose(f);
            }
            SelectObject(mem, old);
            DeleteObject(dib);
            DeleteDC(mem);
            ReleaseDC(hwnd, screen);
            return 0;
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_SCAN_DONE:
        OnScanDone();
        return 0;
    case WM_MOUSEMOVE:
    {
        const float s = 96.0f / static_cast<float>(GetDpiForWindow(hwnd));
        g.mouseX = static_cast<float>(static_cast<short>(LOWORD(lp))) * s;
        g.mouseY = static_cast<float>(static_cast<short>(HIWORD(lp))) * s;
        const int hot = HotAt(g.mouseX, g.mouseY);
        g.hoverRail = hot >= 100 && hot < 108 ? hot - 100 : -1;
        if (!g.scanning && (g.page == 0 || (g.page == 2 && g.mapMode == 0)) && g.canvas.Width() > 0)
        {
            // Recompute geometry the same way the page draws it.
            const D2D1_RECT_F area = { Rail + Margin, Top + 16, g.canvas.Width() - Margin, g.canvas.Height() - 24 };
            float cx, cy, inner, outer;
            if (g.page == 0)
            {
                const float heroW = std::min((area.right - area.left) * 0.52f, area.bottom - area.top);
                outer = std::min(heroW - 48, area.bottom - area.top - 64 - 40) / 2;
                cx = area.left + heroW / 2; cy = area.top + 20 + outer; inner = outer * 0.40f;
            }
            else
            {
                const D2D1_RECT_F vr = { area.left, area.top + 50, area.right, area.bottom };
                outer = std::min(vr.right - vr.left, vr.bottom - vr.top) / 2 - 28;
                cx = (vr.left + vr.right) / 2; cy = (vr.top + vr.bottom) / 2; inner = outer * 0.32f;
            }
            g.hoverSeg = g.sun.Hit(g.mouseX, g.mouseY, cx, cy, inner, outer);
        }
        if (!g.scanning && g.page == 2 && g.mapMode == 1)
        {
            const float px = (g.mouseX - (Rail + Margin + 10)) * GetDpiForWindow(hwnd) / 96.0f;
            const float py = (g.mouseY - (Top + 16 + 60)) * GetDpiForWindow(hwnd) / 96.0f;
            g.hoverTile = g.treemap.Hit(static_cast<int>(px), static_cast<int>(py));
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        TRACKMOUSEEVENT tme = { sizeof tme, TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }
    case WM_MOUSELEAVE:
        g.hoverRail = g.hoverSeg = -1;
        g.hoverTile = NoNode;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
    {
        const int hot = HotAt(g.mouseX, g.mouseY);
        if (hot >= 100 && hot < 108) SetPage(hot - 100);
        else if (hot >= 200 && hot < 203) { g.mapMode = hot - 200; g.unfoldStart = GetTickCount(); InvalidateRect(hwnd, nullptr, FALSE); }
        else if (hot == 400) SetPage(3);
        else if (g.page == 2 && g.mapMode == 0 && g.tree)
        {
            if (g.hoverSeg >= 0)
            {
                const viz::Segment& s = g.sun.Segments()[static_cast<size_t>(g.hoverSeg)];
                if (s.node != NoNode && g.tree->IsDir(s.node)) g.sunRoot = s.node;
            }
            else if (g.sunRoot != g.tree->Root()) g.sunRoot = g.tree->At(g.sunRoot).parent; // centre click: up
            g.sun.Build(*g.tree, g.sunRoot, 0, 5);
            g.unfoldStart = GetTickCount();
            g.hoverSeg = -1;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (GetKeyState(VK_CONTROL) < 0 && wp >= '1' && wp <= '8') SetPage(static_cast<int>(wp - '1'));
        else if (GetKeyState(VK_CONTROL) < 0 && wp == VK_TAB) SetPage((g.page + (GetKeyState(VK_SHIFT) < 0 ? 7 : 1)) % 8);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
}

bool ParsePreviewArgs(const int argc, wchar_t** argv, PreviewOptions* options)
{
    bool preview = false;
    for (int i = 1; i < argc; ++i)
    {
        const wchar_t* a = argv[i];
        const bool sw = a[0] == L'/' || a[0] == L'-';
        if (sw && _wcsicmp(a + 1, L"preview") == 0) preview = true;
        else if (sw && _wcsicmp(a + 1, L"light") == 0) options->theme = 1;
        else if (sw && _wcsicmp(a + 1, L"dark") == 0) options->theme = 2;
        else if (sw && _wcsicmp(a + 1, L"page") == 0 && i + 1 < argc) options->page = _wtoi(argv[++i]);
        else if (!sw)
        {
            // "C:\" on a command line arrives as C:" (the backslash escapes the quote).
            // "C:\" /dark even arrives as one argument: C:" /dark. Cut at the stray quote.
            std::wstring p = a;
            if (const size_t q = p.find(L'"'); q != std::wstring::npos)
            {
                const std::wstring rest = p.substr(q + 1);
                p.resize(q);
                if (rest.find(L"/light") != std::wstring::npos) options->theme = 1;
                if (rest.find(L"/dark") != std::wstring::npos) options->theme = 2;
            }
            if (p.size() == 2 && p[1] == L':') p += L'\\';
            options->path = p;
        }
    }
    return preview;
}

int RunPreview(const HINSTANCE instance, const int show, const PreviewOptions& options)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    g.opt = options;
    g.pal = options.theme == 1 ? &gfx::Light() : options.theme == 2 ? &gfx::Dark() :
        (gfx::SystemPrefersDark() ? &gfx::Dark() : &gfx::Light());
    g.page = std::clamp(options.page, 0, 7);
    g.root = options.path.empty() ? VolumeOf(KnownFolder(CSIDL_WINDOWS)) : options.path;
    if (g.root.size() == 2 && g.root[1] == L':') g.root += L'\\';
    g.volume = VolumeOf(g.root);
    ULARGE_INTEGER avail, total, freeAll;
    if (GetDiskFreeSpaceExW(g.volume.c_str(), &avail, &total, &freeAll)) { g.volFree = avail.QuadPart; g.volTotal = total.QuadPart; }

    WNDCLASSEXW wc = { sizeof wc };
    wc.lpfnWndProc = Proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    wc.lpszClassName = L"SpaceRescue.Preview";
    RegisterClassExW(&wc);
    g.hwnd = CreateWindowExW(0, wc.lpszClassName, L"SpaceRescue", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1500, 980,
        nullptr, nullptr, instance, nullptr);
    // Title bar blends into the app: dark mode caption + caption colour = base, Mica backdrop.
    const BOOL dark = P().dark ? TRUE : FALSE;
    DwmSetWindowAttribute(g.hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof dark);
    const COLORREF caption = RGB(static_cast<int>(P().base.r * 255), static_cast<int>(P().base.g * 255), static_cast<int>(P().base.b * 255));
    DwmSetWindowAttribute(g.hwnd, 35 /* DWMWA_CAPTION_COLOR */, &caption, sizeof caption);
    const int backdrop = 2; // DWMSBT_MAINWINDOW (Mica)
    DwmSetWindowAttribute(g.hwnd, 38 /* DWMWA_SYSTEMBACKDROP_TYPE */, &backdrop, sizeof backdrop);
    ShowWindow(g.hwnd, show);
    StartScan();
    if (wchar_t spec[MAX_PATH]; GetEnvironmentVariableW(L"SR_SNAPSHOT", spec, MAX_PATH))
    {
        const wchar_t* ms = wcschr(spec, L';');
        SetTimer(g.hwnd, 99, ms ? static_cast<UINT>(_wtoi(ms + 1)) : 3000, nullptr);
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    g.canvas.Discard();
    return 0;
}
}
