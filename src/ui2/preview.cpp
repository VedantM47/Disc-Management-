// SpaceRescue - SPDX-License-Identifier: GPL-3.0-or-later
//
// The SpaceRescue interface: left navigation rail, eight tabs drawn with the Direct2D design system
// (DESIGN.md), and every action wired to the engines - scanning, cleanup with confirm + undo, system
// tools, settings, alerts, search / command palette (Ctrl+K), drive and folder picking.

#include "preview.h"

#include "../gfx/gfx.h"
#include "../insights/cleanup.h"
#include "../insights/insights.h"
#include "../insights/quickwins.h"
#include "../internals/internals.h"
#include "../notify/tray.h"
#include "../rescue/actions.h"
#include "../rescue/rescue.h"
#include "../viz/viz.h"

#include <dwmapi.h>
#include <shobjidl.h>

#include <cmath>
#include <functional>
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

constexpr UINT WM_SCAN_DONE = WM_APP + 60, WM_ACTION_DONE = WM_APP + 61, WM_UNDO_DONE = WM_APP + 62;
constexpr UINT_PTR TimerProgress = 1, TimerAnim = 2, TimerToast = 3, TimerSnapshot = 99;
constexpr float Rail = 84, Top = 60, Margin = 28;
constexpr float Pi = 3.14159265358979f;
constexpr int PageReview = 8;

const wchar_t* PageNames[] = { L"Overview", L"Explore", L"Map", L"Clean up", L"System", L"Disk internals", L"History",
    L"Settings", L"Review" };
const wchar_t PageIcons[] = { 0xE80F, 0xE8B7, 0xE9D9, 0xE74D, 0xE770, 0xEDA2, 0xE81C, 0xE713 };

// Hit-test ids (one namespace per kind of control).
enum Hot : int
{
    HotRail = 100, HotMapMode = 200, HotSunCard = 300, HotReviewAll = 400, HotExploreBtn = 401,
    HotDriveChip = 600, HotSearch = 601, HotBannerRelease = 602, HotHog = 700, HotRow = 800, HotUp = 799,
    HotTreemap = 798, HotCleanReview = 500, HotSysTool = 900, HotAlerts = 1000, HotReserveSize = 1010,
    HotReserveAction = 1015, HotWorkVolume = 1020, HotThreadsMinus = 1030, HotThreadsPlus = 1031, HotTheme = 1040,
    HotBrowseFile = 1100, HotRevBack = 2000, HotRevAll = 2001, HotRevNone = 2002, HotRevRecycle = 2003,
    HotRevQuarantine = 2004, HotRevCompress = 2005, HotRevDelete = 2006, HotRevOpen = 2007, HotRevEmptyBin = 2008, HotRevItem = 2100,
    HotToastUndo = 3000, HotToastClose = 3001, HotPaletteItem = 4000, HotPaletteBox = 3999,
};

struct ReviewItem
{
    u32 node = NoNode;
    std::wstring group;
    Safety safety = Safety::Safe;
    bool checked = false;
    bool locked = false; // protected path: never deletable here
};

struct PaletteItem
{
    std::wstring title, detail;
    wchar_t icon = 0xE721;
    std::function<void()> run;
};

struct App
{
    PreviewOptions opt;
    HWND hwnd = nullptr;
    Canvas canvas;
    const gfx::Palette* pal = &gfx::Dark();
    int page = 0, backPage = 3;
    int mapMode = 0;
    int hoverRail = -1, hoverSeg = -1, hoverHot = -1;
    u32 hoverTile = NoNode;
    float mouseX = 0, mouseY = 0;
    DWORD pageStart = 0, unfoldStart = 0;
    int threads = 0;

    // Data
    std::wstring root, volume;
    u64 volTotal = 0, volFree = 0;
    std::unique_ptr<Scanner> scanner;
    std::unique_ptr<QuickWins> quick;
    DWORD scanStart = 0;
    std::unique_ptr<ScanTree> tree;
    std::unique_ptr<InsightsReport> report;
    std::unique_ptr<CleanupReport> cleanup;
    std::unique_ptr<SystemReport> system;
    std::unique_ptr<viz::KindModel> kinds;
    viz::Sunburst sun;
    u32 sunRoot = 0;
    viz::Treemap treemap;
    int treemapW = 0, treemapH = 0;
    u32 treemapRoot = NoNode;
    D2D1_RECT_F treemapRect = {};
    viz::FlameGraph flame;
    VolumeInternals vol;
    FileInternals bigFile;
    SlackSummary slack;
    bool scanning = true;

    // Explore
    u32 exploreRoot = 0;
    u32 highlight = NoNode;
    std::vector<u32> rowNodes;
    std::vector<u32> hogNodes;

    // Review list + actions
    std::wstring reviewTitle;
    std::vector<ReviewItem> review;
    float reviewScroll = 0;
    bool busy = false;
    std::thread worker;

    // Toast
    std::wstring toast;
    int toastUndo = -1;
    bool toastError = false;

    // Command palette
    bool palette = false;
    std::wstring query;
    int paletteSel = 0;
    std::vector<PaletteItem> items;

    struct HotRect { D2D1_RECT_F r; int id; };
    std::vector<HotRect> hots;
};
App g;

const gfx::Palette& P() { return *g.pal; }
void Invalidate() { InvalidateRect(g.hwnd, nullptr, FALSE); }
bool Low() { return g.volTotal && IsLow(g.volFree, g.volTotal, Settings::Get().rescue); }

void StartScan(const std::wstring& root);
void SetPage(int p);
void OpenReview(const std::wstring& title, std::vector<ReviewItem> items);
void ShowToast(const std::wstring& text, int undoId = -1, bool error = false);

std::wstring Share(u64 bytes)
{
    if (!g.volTotal) return FormatBytes(bytes);
    return FormatBytes(bytes) + L" \u00B7 " + FormatPercent(static_cast<double>(bytes) / static_cast<double>(g.volTotal)) +
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

void HotArea(const D2D1_RECT_F& r, int id) { g.hots.push_back({ r, id }); }
bool Hovered(int id) { return g.hoverHot == id; }

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

enum class Btn { Primary, Secondary, Danger, Disabled };

void Button(Canvas& c, const D2D1_RECT_F& r, const std::wstring& label, Btn kind, int id = -1)
{
    const bool hover = id >= 0 && Hovered(id) && kind != Btn::Disabled;
    switch (kind)
    {
    case Btn::Primary:
        c.FillRound(r, 8, hover ? P().accent.Mix(P().text, 0.15f) : P().accent);
        c.Text(label, r, Text::BodyStrong, P().accentInk, Align::Center, true);
        break;
    case Btn::Danger:
        c.FillRound(r, 8, P().caution.WithAlpha(hover ? 0.28f : 0.16f));
        c.Text(label, r, Text::BodyStrong, P().caution, Align::Center, true);
        break;
    case Btn::Disabled:
        c.FillRound(r, 8, P().surface2.WithAlpha(0.5f));
        c.Text(label, r, Text::BodyStrong, P().textFaint, Align::Center, true);
        break;
    default:
        c.FillRound(r, 8, hover ? P().surface3 : P().surface2);
        c.StrokeRound(r, 8, P().stroke);
        c.Text(label, r, Text::BodyStrong, P().text, Align::Center, true);
    }
    if (id >= 0 && kind != Btn::Disabled) HotArea(r, id);
}

void Overline(Canvas& c, const std::wstring& s, float x, float y, float w)
{
    std::wstring up = s;
    if (!up.empty()) CharUpperBuffW(&up[0], static_cast<DWORD>(up.size()));
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

void Toggle(Canvas& c, float x, float y, bool on, int id)
{
    c.FillRound(Rect(x, y, 40, 20), 10, on ? P().accent : P().surface3);
    c.Circle(on ? x + 30 : x + 10, y + 10, 7, on ? P().accentInk : P().textDim);
    HotArea(Rect(x - 4, y - 4, 48, 28), id);
}

void Segmented(Canvas& c, float x, float y, const std::vector<std::wstring>& labels, int sel, int baseId)
{
    float w = 0;
    for (const auto& s : labels) w += c.Measure(s, Text::BodyStrong).width + 28;
    c.FillRound(Rect(x, y, w + 6, 32), 8, P().surface2);
    float cx = x + 3;
    for (size_t i = 0; i < labels.size(); ++i)
    {
        const float iw = c.Measure(labels[i], Text::BodyStrong).width + 28;
        const D2D1_RECT_F r = Rect(cx, y + 3, iw, 26);
        if (static_cast<int>(i) == sel) c.FillRound(r, 6, P().surface1);
        else if (Hovered(baseId + static_cast<int>(i))) c.FillRound(r, 6, P().surface3);
        c.Text(labels[i], r, Text::BodyStrong, static_cast<int>(i) == sel ? P().text : P().textDim, Align::Center, true);
        HotArea(r, baseId + static_cast<int>(i));
        cx += iw;
    }
}

// --- Frame: rail, top bar, banner, toast ----------------------------------------

void DrawRail(Canvas& c)
{
    c.Fill(Rect(0, 0, Rail, c.Height()), P().base);
    c.FillRound(Rect(22, 14, 40, 40), 12, P().accent);
    c.ArcStroke(42, 34, 11, 0.6f, 2 * Pi - 0.2f, 4, P().accentInk);
    c.Circle(42, 34, 3.5f, P().accentInk);
    const int current = g.page == PageReview ? 3 : g.page;
    for (int i = 0; i < 8; ++i)
    {
        const float y = 80 + static_cast<float>(i) * 66;
        const D2D1_RECT_F r = Rect(8, y, Rail - 16, 60);
        const bool sel = i == current;
        if (sel) c.FillRound(r, 10, P().surface2);
        else if (i == g.hoverRail) c.FillRound(r, 10, P().surface2.WithAlpha(0.5f));
        if (sel) c.FillRound(Rect(4, y + 18, 3, 24), 1.5f, P().accent);
        c.Icon(PageIcons[i], (Rail - 22) / 2, y + 8, 22, sel ? P().accent : P().textDim);
        c.Text(PageNames[i], Rect(8, y + 34, Rail - 16, 18), Text::Caption, sel ? P().text : P().textDim, Align::Center);
        HotArea(r, HotRail + i);
    }
}

void DrawTopBar(Canvas& c)
{
    const float x = Rail + Margin;
    c.Text(PageNames[g.page], Rect(x, 14, 400, 36), Text::Title, P().text, Align::Left, true);
    // Drive chip: click to pick another drive or folder.
    const std::wstring chip = (g.root.size() <= 3 ? g.volume.substr(0, 2) : g.root) + L"  " + FormatBytes(g.volFree) +
        L" free of " + FormatBytes(g.volTotal);
    const float cw = std::min(c.Measure(chip, Text::BodyStrong).width + 74, 520.0f);
    const float cx = c.Width() - Margin - 300 - 12 - cw;
    const D2D1_RECT_F cr = Rect(cx, 16, cw, 32);
    c.FillRound(cr, 16, Hovered(HotDriveChip) ? P().surface2 : P().surface1);
    c.StrokeRound(cr, 16, P().stroke);
    const double used = g.volTotal ? 1.0 - static_cast<double>(g.volFree) / static_cast<double>(g.volTotal) : 0;
    c.ArcStroke(cx + 18, 32, 7, 0, static_cast<float>(used) * 2 * Pi, 3, Low() ? P().caution : P().accent);
    c.Text(chip, Rect(cx + 34, 16, cw - 64, 32), Text::BodyStrong, P().text, Align::Left, true);
    c.Icon(0xE70D, cr.right - 26, 25, 14, P().textDim);
    HotArea(cr, HotDriveChip);
    // Search field opens the command palette.
    const D2D1_RECT_F sr = Rect(c.Width() - Margin - 300, 16, 300, 32);
    c.FillRound(sr, 8, Hovered(HotSearch) ? P().surface2 : P().surface1);
    c.StrokeRound(sr, 8, P().stroke);
    c.Icon(0xE721, sr.left + 10, 23, 16, P().textFaint);
    c.Text(L"Search or run a command", Rect(sr.left + 34, 16, 200, 32), Text::Body, P().textFaint, Align::Left, true);
    c.FillRound(Rect(sr.right - 52, 22, 44, 20), 5, P().surface2);
    c.Text(L"Ctrl K", Rect(sr.right - 52, 22, 44, 20), Text::Caption, P().textDim, Align::Center, true);
    HotArea(sr, HotSearch);
}

// Rescue Mode banner (calm, caution accent). Returns its height.
float DrawBanner(Canvas& c, float y)
{
    if (!Low()) return 0;
    const D2D1_RECT_F r = Rect(Rail + Margin, y, c.Width() - Rail - 2 * Margin, 52);
    c.FillRound(r, 12, P().caution.WithAlpha(P().dark ? 0.16f : 0.10f));
    c.Icon(0xE7BA, r.left + 16, r.top + 16, 20, P().caution);
    const std::wstring work = PickWorkVolume(g.volume, Settings::Get().rescue);
    c.Text(g.volume.substr(0, 2) + L" is almost full (" + FormatBytes(g.volFree) + L" free). Rescue Mode is on: nothing is written to " +
        g.volume.substr(0, 2) + L"." + (work.empty() ? L" No other drive for quarantine." : L" Quarantine goes to " + work.substr(0, 2) + L"."),
        Rect(r.left + 48, r.top, r.right - r.left - 260, 52), Text::BodyStrong, P().text, Align::Left, true);
    const ReserveStatus rs = QueryReserve();
    if (rs.exists && _wcsicmp(VolumeOf(rs.path).c_str(), g.volume.c_str()) == 0)
        Button(c, Rect(r.right - 210, r.top + 10, 196, 32), L"Release reserve (+" + FormatBytes(rs.size) + L")", Btn::Primary, HotBannerRelease);
    return 64;
}

void DrawToast(Canvas& c)
{
    if (g.toast.empty()) return;
    const float w = std::min(760.0f, c.Width() - Rail - 80);
    const float x = Rail + (c.Width() - Rail - w) / 2, y = c.Height() - 84;
    const D2D1_RECT_F r = Rect(x, y, w, 56);
    c.Shadow(r, 12, 24, 8, P().dark ? 0.4f : 0.16f);
    c.FillRound(r, 12, P().surface2);
    c.StrokeRound(r, 12, g.toastError ? P().caution : P().stroke);
    c.Icon(g.toastError ? 0xE783 : 0xE73E, x + 16, y + 18, 20, g.toastError ? P().caution : P().safe);
    const float right = g.toastUndo >= 0 ? 170 : 60;
    c.Text(g.toast, Rect(x + 48, y + 4, w - 48 - right, 48), Text::Body, P().text, Align::Left, true);
    if (g.toastUndo >= 0) Button(c, Rect(r.right - 160, y + 12, 100, 32), L"Undo", Btn::Primary, HotToastUndo);
    c.Icon(0xE711, r.right - 44, y + 18, 16, P().textDim);
    HotArea(Rect(r.right - 52, y + 8, 40, 40), HotToastClose);
}

// --- Pages ------------------------------------------------------------------------

void DrawSunburstHero(Canvas& c, const D2D1_RECT_F& card)
{
    c.Card(card, 16);
    const float h = card.bottom - card.top, w = card.right - card.left;
    const float legendH = 64;
    const float outer = std::min(w - 48, h - legendH - 40) / 2;
    const float cx = (card.left + card.right) / 2, cy = card.top + 20 + outer;
    const float inner = outer * 0.40f;
    g.sun.Draw(c, *g.kinds, cx, cy, inner, outer, gfx::Ease(g.unfoldStart, 700, true), g.page == 0 ? g.hoverSeg : -1);
    c.Text(g.volume.substr(0, 2), Rect(cx - inner, cy - 44, inner * 2, 18), Text::Overline, P().textFaint, Align::Center);
    c.Text(FormatBytes(g.volFree), Rect(cx - inner, cy - 26, inner * 2, 40), Text::Number, Low() ? P().caution : P().text, Align::Center);
    c.Text(L"free of " + FormatBytes(g.volTotal), Rect(cx - inner, cy + 14, inner * 2, 18), Text::Caption, P().textDim, Align::Center);
    std::vector<std::pair<u64, int>> order;
    for (size_t k = 0; k < g.kinds->totals.size(); ++k) order.push_back({ g.kinds->totals[k], static_cast<int>(k) });
    std::sort(order.rbegin(), order.rend());
    float lx = card.left + 24, ly = card.bottom - legendH + 6;
    for (int i = 0; i < 6 && i < static_cast<int>(order.size()); ++i)
    {
        if (i == 3) { lx = card.left + 24; ly += 26; }
        const auto kind = static_cast<gfx::FileKind>(order[static_cast<size_t>(i)].second);
        c.FillRound(Rect(lx, ly + 5, 10, 10), 3, P().Kind(kind));
        c.Text(std::wstring(gfx::KindLabel(kind)) + L"  " + FormatBytes(order[static_cast<size_t>(i)].first),
            Rect(lx + 16, ly, (w - 48) / 3 - 18, 20), Text::Caption, P().textDim);
        lx += (w - 48) / 3;
    }
}

void PageOverview(Canvas& c, const D2D1_RECT_F& area)
{
    const float w = area.right - area.left;
    const float heroW = std::min(w * 0.52f, area.bottom - area.top);
    DrawSunburstHero(c, { area.left, area.top, area.left + heroW, area.bottom });
    const float x = area.left + heroW + 20, rw = area.right - x;

    const CleanupReport& cr = *g.cleanup;
    const u64 total = cr.safeBytes + cr.reviewBytes + cr.cautionBytes;
    const D2D1_RECT_F mc = { x, area.top, area.right, area.top + 218 };
    c.Card(mc, 16);
    Overline(c, L"You can safely free", x + 24, mc.top + 22, rw);
    c.Text(FormatBytes(cr.safeBytes), Rect(x + 24, mc.top + 40, rw - 48, 56), Text::Hero, P().text);
    c.Text(L"right now, plus " + FormatBytes(cr.reviewBytes) + L" to review", Rect(x + 24, mc.top + 96, rw - 48, 20), Text::Body, P().textDim);
    const D2D1_RECT_F bar = Rect(x + 24, mc.top + 128, rw - 48, 10);
    c.FillRound(bar, 5, P().freeSpace);
    float bx = bar.left;
    const Safety order[] = { Safety::Safe, Safety::Review, Safety::Caution };
    const u64 parts[] = { cr.safeBytes, cr.reviewBytes, cr.cautionBytes };
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
    Button(c, Rect(x + 24, mc.bottom - 50, 200, 36), L"Review and free space", Btn::Primary, HotReviewAll);
    Button(c, Rect(x + 236, mc.bottom - 50, 120, 36), L"Explore", Btn::Secondary, HotExploreBtn);

    float y = mc.bottom + 24;
    Overline(c, L"Biggest space users \u00B7 click to explore", x, y, rw);
    y += 24;
    std::vector<const Hog*> hogs;
    for (const Hog& hg : g.report->topFolders) hogs.push_back(&hg);
    for (const Hog& hg : g.report->topFiles) hogs.push_back(&hg);
    std::sort(hogs.begin(), hogs.end(), [](const Hog* a, const Hog* b) { return g.tree->At(a->node).size > g.tree->At(b->node).size; });
    g.hogNodes.clear();
    const float rowH = 62;
    for (size_t i = 0; i < hogs.size() && i < 5 && y + rowH <= area.bottom; ++i)
    {
        const Hog& hg = *hogs[i];
        const D2D1_RECT_F r = { x, y, area.right, y + rowH - 8 };
        c.Card(r, 12, Hovered(HotHog + static_cast<int>(i)));
        const Color kc = P().Kind(g.kinds->Of(hg.node));
        c.FillRound(Rect(r.left + 14, r.top + 13, 28, 28), 8, kc.WithAlpha(0.18f));
        c.Icon(g.tree->IsDir(hg.node) ? 0xE8B7 : 0xE7C3, r.left + 20, r.top + 19, 16, kc);
        c.Text(g.tree->Name(hg.node), Rect(r.left + 54, r.top + 8, rw - 260, 20), Text::BodyStrong, P().text);
        c.Text(hg.explanation, Rect(r.left + 54, r.top + 28, rw - 260, 18), Text::Caption, P().textDim);
        c.Text(FormatBytes(g.tree->At(hg.node).size), Rect(r.right - 190, r.top + 8, 172, 20), Text::BodyStrong, P().text, Align::Right);
        c.Text(FormatPercent(static_cast<double>(g.tree->At(hg.node).size) / static_cast<double>(std::max<u64>(1, g.volTotal))) +
            L" of " + g.volume.substr(0, 2), Rect(r.right - 190, r.top + 28, 172, 18), Text::Caption, P().textDim, Align::Right);
        HotArea(r, HotHog + static_cast<int>(i));
        g.hogNodes.push_back(hg.node);
        y += rowH;
    }
}

void EnsureTreemap(Canvas& c, const D2D1_RECT_F& inner, u32 root)
{
    const int pw = static_cast<int>((inner.right - inner.left) * c.Dpi() / 96.0f);
    const int ph = static_cast<int>((inner.bottom - inner.top) * c.Dpi() / 96.0f);
    if (pw != g.treemapW || ph != g.treemapH || g.treemapRoot != root)
    {
        g.treemap.Render(*g.tree, *g.kinds, P(), root, pw, ph);
        g.treemapW = pw; g.treemapH = ph; g.treemapRoot = root;
    }
    g.treemapRect = inner;
    c.PushClip(inner);
    c.Pixels(g.treemap.Pixels(), static_cast<UINT>(pw), static_cast<UINT>(ph), inner);
    c.PopClip();
    HotArea(inner, HotTreemap);
}

void PageExplore(Canvas& c, const D2D1_RECT_F& area)
{
    const ScanTree& t = *g.tree;
    const u32 here = g.exploreRoot;
    const float split = area.top + (area.bottom - area.top) * 0.56f;
    // Breadcrumb with Up.
    if (here != t.Root()) Button(c, Rect(area.left, area.top - 4, 70, 26), L"\u2190 Up", Btn::Secondary, HotUp);
    c.Text(t.Path(here) + L"  \u00B7  " + FormatBytes(t.At(here).size) + L"  \u00B7  click a folder to open it, right-click for actions",
        Rect(area.left + (here != t.Root() ? 82 : 0), area.top - 4, 900, 26), Text::Caption, P().textDim, Align::Left, true);
    const float listW = (area.right - area.left) * 0.64f;
    const D2D1_RECT_F lc = { area.left, area.top + 30, area.left + listW, split - 12 };
    c.Card(lc, 12);
    const float cols[] = { lc.left + 16, lc.right - 360, lc.right - 250, lc.right - 160, lc.right - 90 };
    const wchar_t* heads[] = { L"Name", L"Share", L"Size", L"Files", L"Changed" };
    const D2D1_RECT_F headBoxes[] = { Rect(cols[0], lc.top + 10, 300, 18), Rect(cols[1], lc.top + 10, 90, 18),
        Rect(cols[2] - 10, lc.top + 10, 100, 18), Rect(cols[3] - 10, lc.top + 10, 80, 18), Rect(cols[4] - 30, lc.top + 10, 104, 18) };
    for (int i = 0; i < 5; ++i) c.Text(heads[i], headBoxes[i], Text::CaptionStrong, P().textFaint, i >= 2 ? Align::Right : Align::Left);
    c.Line(lc.left + 12, lc.top + 34, lc.right - 12, lc.top + 34, P().stroke);
    g.rowNodes = t.Children(here);
    float y = lc.top + 40;
    const u64 parentSize = std::max<u64>(1, t.At(here).size);
    for (size_t i = 0; i < g.rowNodes.size() && y + 26 <= lc.bottom - 6; ++i)
    {
        const u32 n = g.rowNodes[i];
        const D2D1_RECT_F row = { lc.left + 6, y - 2, lc.right - 6, y + 26 };
        if (n == g.highlight) c.FillRound(row, 6, P().accent.WithAlpha(0.18f));
        else if (Hovered(HotRow + static_cast<int>(i))) c.FillRound(row, 6, P().surface2);
        if (t.IsDir(n)) c.Icon(0xE76C, cols[0] - 4, y + 4, 12, P().textFaint);
        c.FillRound(Rect(cols[0] + 14, y + 7, 10, 10), 3, P().Kind(g.kinds->Of(n)));
        c.Text(t.Name(n), Rect(cols[0] + 30, y, cols[1] - cols[0] - 40, 24), Text::Body, P().text, Align::Left, true);
        const double share = static_cast<double>(t.At(n).size) / static_cast<double>(parentSize);
        Bar(c, Rect(cols[1], y + 9, 64, 6), share, P().Kind(g.kinds->Of(n)));
        c.Text(FormatPercent(share), Rect(cols[1] + 68, y, 40, 24), Text::Caption, P().textDim, Align::Left, true);
        c.Text(FormatBytes(t.At(n).size), Rect(cols[2] - 10, y, 100, 24), Text::Body, P().text, Align::Right, true);
        c.Text(FormatCount(t.At(n).files), Rect(cols[3] - 10, y, 80, 24), Text::Caption, P().textDim, Align::Right, true);
        c.Text(FormatAge(t.At(n).writeTime), Rect(cols[4] - 30, y, 104, 24), Text::Caption, P().textDim, Align::Right, true);
        HotArea(row, HotRow + static_cast<int>(i));
        y += 28;
    }
    if (g.rowNodes.empty()) EmptyState(c, lc, 0xE8B7, L"This folder is empty", L"Nothing to show here.");
    // File types of the whole scan.
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
    // Treemap of the current folder (click a tile to open its folder).
    const D2D1_RECT_F tc = { area.left, split, area.right, area.bottom };
    c.Card(tc, 12);
    EnsureTreemap(c, gfx::Inset(tc, 8), here);
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
        c.Text(FormatCount(t.At(node).files) + L" files \u00B7 changed " + FormatAge(t.At(node).writeTime),
            Rect(x + 16, y + 84, w - 32, 18), Text::Caption, P().textDim);
    }
    else c.Text(line, Rect(x + 16, y + 64, w - 32, 18), Text::Caption, P().textDim);
}

struct SunGeom { float cx, cy, inner, outer; };

SunGeom MapGeometry(const D2D1_RECT_F& vr)
{
    const float outer = std::min(vr.right - vr.left, vr.bottom - vr.top) / 2 - 28;
    return { (vr.left + vr.right) / 2, (vr.top + vr.bottom) / 2, outer * 0.32f, outer };
}

void PageMap(Canvas& c, const D2D1_RECT_F& area)
{
    const ScanTree& t = *g.tree;
    const wchar_t* modes[] = { L"Sunburst", L"Treemap", L"Flame graph" };
    const D2D1_RECT_F seg = Rect(area.left, area.top, 330, 34);
    c.FillRound(seg, 9, P().surface1);
    c.StrokeRound(seg, 9, P().stroke);
    for (int i = 0; i < 3; ++i)
    {
        const D2D1_RECT_F r = Rect(seg.left + 3 + 108.0f * static_cast<float>(i), seg.top + 3, 106, 28);
        if (i == g.mapMode) c.FillRound(r, 7, P().surface3);
        c.Text(modes[i], r, Text::BodyStrong, i == g.mapMode ? P().text : P().textDim, Align::Center, true);
        HotArea(r, HotMapMode + i);
    }
    c.Text(g.mapMode == 0 ? L"Click a ring to open that folder, the centre to go back up." :
           g.mapMode == 1 ? L"Click a tile to open its folder in Explore." : L"Each row is one folder level; width is size.",
        Rect(seg.right + 20, seg.top, 640, 34), Text::Caption, P().textDim, Align::Left, true);
    const D2D1_RECT_F vr = { area.left, area.top + 50, area.right, area.bottom };
    c.Card(vr, 16);
    if (g.mapMode == 0)
    {
        const SunGeom s = MapGeometry(vr);
        g.sun.Draw(c, *g.kinds, s.cx, s.cy, s.inner, s.outer, gfx::Ease(g.unfoldStart, 700, true), g.hoverSeg);
        c.Text(t.Name(g.sunRoot), Rect(s.cx - 90, s.cy - 26, 180, 22), Text::BodyStrong, P().text, Align::Center);
        c.Text(FormatBytes(t.At(g.sunRoot).size), Rect(s.cx - 90, s.cy - 4, 180, 30), Text::Number, P().text, Align::Center);
        HotArea(vr, HotSunCard);
        if (g.hoverSeg >= 0)
        {
            const viz::Segment& sg = g.sun.Segments()[static_cast<size_t>(g.hoverSeg)];
            Tooltip(c, g.mouseX, g.mouseY, sg.node, sg.bytes, sg.freeSpace ? L"Free space" : sg.other ? L"Smaller items" : t.Name(sg.node));
        }
    }
    else if (g.mapMode == 1)
    {
        EnsureTreemap(c, gfx::Inset(vr, 10), t.Root());
        if (g.hoverTile != NoNode) Tooltip(c, g.mouseX, g.mouseY, g.hoverTile, t.At(g.hoverTile).size, t.Name(g.hoverTile));
    }
    else g.flame.Draw(c, t, *g.kinds, t.Root(), gfx::Inset(vr, 16), 24, 14);
}

Safety SectionSafety(CleanSection s)
{
    return s == CleanSection::WindowsTemp || s == CleanSection::AppCaches ? Safety::Safe : Safety::Review;
}

void PageCleanup(Canvas& c, const D2D1_RECT_F& area)
{
    const wchar_t icons[] = { 0xE7B8, 0xE774, 0xE943, 0xE81C };
    const wchar_t* notes[] = {
        L"Scratch files Windows and apps leave behind. They are recreated when needed.",
        L"Browser and app caches rebuild themselves. Sign-ins and history are kept.",
        L"Package caches and build output per project. Restore with your package manager.",
        L"Old downloads, installers, large videos and files you haven't used in a year." };
    u64 grand = 0;
    for (const auto& s : g.cleanup->sections) grand += s.bytes;
    c.Text(L"4 groups \u00B7 " + FormatBytes(grand) + L" could be freed. Click Review to pick items; nothing is removed without confirming.",
        Rect(area.left, area.top, 1000, 22), Text::Body, P().textDim);
    const float gw = (area.right - area.left - 20) / 2, gh = (area.bottom - area.top - 40 - 20) / 2;
    for (int i = 0; i < 4; ++i)
    {
        const CleanSectionResult& s = g.cleanup->sections[static_cast<size_t>(i)];
        const Safety safety = SectionSafety(s.id);
        const float x = area.left + static_cast<float>(i % 2) * (gw + 20);
        const float y = area.top + 36 + static_cast<float>(i / 2) * (gh + 20);
        const D2D1_RECT_F r = Rect(x, y, gw, gh);
        c.Card(r, 16, i == 0);
        c.FillRound(Rect(x + 20, y + 20, 36, 36), 10, SafetyColor(safety).WithAlpha(0.15f));
        c.Icon(icons[i], x + 28, y + 28, 20, SafetyColor(safety));
        c.Text(s.title, Rect(x + 68, y + 18, gw - 200, 22), Text::BodyStrong, P().text);
        c.Text(FormatBytes(s.bytes), Rect(x + 68, y + 38, gw - 200, 30), Text::Number, P().text);
        Badge(c, r.right - 20 - BadgeWidth(c, safety), y + 22, safety);
        c.Text(notes[i], Rect(x + 20, y + 76, gw - 40, 36), Text::Caption, P().textDim);
        float ry = y + 112;
        for (const CleanGroup& grp : s.groups)
        {
            if (ry + 26 > r.bottom - 52) break;
            c.Circle(x + 24, ry + 11, 3.5f, SafetyColor(grp.safety));
            std::wstring name = grp.name;
            if (grp.lastOpened) name += L"  \u00B7  opened " + FormatAge(grp.lastOpened);
            c.Text(name, Rect(x + 34, ry, gw - 170, 22), Text::Body, P().text, Align::Left, true);
            c.Text(FormatBytes(grp.bytes), Rect(r.right - 140, ry, 120, 22), Text::Body, P().textDim, Align::Right, true);
            ry += 26;
        }
        if (s.groups.empty()) c.Text(L"Nothing found here.", Rect(x + 20, ry, gw - 40, 22), Text::Body, P().textFaint);
        Button(c, Rect(x + 20, r.bottom - 50, 130, 34), L"Review \u203A", s.groups.empty() ? Btn::Disabled : i == 0 ? Btn::Primary : Btn::Secondary,
            HotCleanReview + i);
    }
}

void PageSystem(Canvas& c, const D2D1_RECT_F& area)
{
    const SystemReport& sys = *g.system;
    if (!sys.available)
    {
        EmptyState(c, area, 0xE770, L"Scan your system drive to see what Windows uses",
            L"This view needs a whole-drive scan of " + VolumeOf(KnownFolder(CSIDL_WINDOWS)).substr(0, 2) +
            L". Use the drive chip at the top to switch.");
        return;
    }
    const D2D1_RECT_F hero = { area.left, area.top, area.right, area.top + 110 };
    c.Card(hero, 16);
    c.FillRound(Rect(hero.left + 24, hero.top + 26, 56, 56), 16, P().protectedC.WithAlpha(0.16f));
    c.Icon(0xE72E, hero.left + 38, hero.top + 40, 28, P().protectedC);
    c.Text(L"Windows itself uses " + FormatBytes(sys.total), Rect(hero.left + 100, hero.top + 22, 800, 36), Text::Title, P().text);
    c.Text(L"About " + FormatBytes(sys.reducible) + L" of that can be reduced safely with Windows' own tools. Never delete these files by hand.",
        Rect(hero.left + 100, hero.top + 62, 1000, 22), Text::Body, P().textDim);
    float y = hero.bottom + 16;
    const float rowH = (area.bottom - y) / static_cast<float>(std::max<size_t>(1, sys.items.size()));
    const float rh = std::clamp(rowH, 40.0f, 58.0f);
    for (size_t i = 0; i < sys.items.size(); ++i)
    {
        const SystemItem& it = sys.items[i];
        if (y + rh > area.bottom + 4) break;
        const D2D1_RECT_F r = { area.left, y, area.right, y + rh - 6 };
        c.Card(r, 12);
        const float mid = (r.bottom - r.top) / 2;
        c.Icon(0xE72E, r.left + 18, r.top + mid - 8, 16, P().protectedC);
        c.Text(it.label, Rect(r.left + 48, r.top + mid - 20, 340, 20), Text::BodyStrong, P().text);
        c.Text(it.why, Rect(r.left + 48, r.top + mid, 600, 18), Text::Caption, P().textDim);
        c.Text(it.bytes ? FormatBytes(it.bytes) : std::wstring(L"not measured"), Rect(r.right - 470, r.top, 120, r.bottom - r.top),
            Text::BodyStrong, P().text, Align::Right, true);
        Badge(c, r.right - 330, r.top + mid - 11, Safety::Protected);
        const bool action = !it.command.empty();
        const float fw = c.Measure(it.tool, Text::CaptionStrong).width + 28;
        const D2D1_RECT_F chip = Rect(r.right - 20 - fw, r.top + mid - 14, fw, 28);
        const int id = HotSysTool + static_cast<int>(i);
        c.FillRound(chip, 8, action ? P().accent.WithAlpha(Hovered(id) ? 0.26f : 0.14f) : P().surface2);
        c.Text(it.tool, chip, Text::CaptionStrong, action ? P().accent : P().textDim, Align::Center, true);
        if (action) HotArea(chip, id);
        y += rh;
    }
}

void PageInternals(Canvas& c, const D2D1_RECT_F& area)
{
    const float colW = (area.right - area.left - 20) / 2;
    const D2D1_RECT_F cm = Rect(area.left, area.top, colW, 300);
    c.Card(cm, 16);
    c.Text(L"Cluster map", Rect(cm.left + 20, cm.top + 16, 300, 22), Text::BodyStrong, P().text);
    const u64 clusters = g.vol.totalClusters, freeC = g.vol.freeClusters;
    c.Text(FormatCount(clusters) + L" clusters of " + FormatBytes(g.vol.bytesPerCluster) + L" \u00B7 " + FormatCount(freeC) + L" free",
        Rect(cm.left + 20, cm.top + 38, colW - 40, 18), Text::Caption, P().textDim);
    const int cols = 48, rows = 12;
    const float cell = std::min((colW - 40) / cols, 13.0f);
    const int used = clusters ? static_cast<int>(std::lround(static_cast<double>(clusters - freeC) / clusters * cols * rows)) : 0;
    unsigned seed = 7;
    for (int i = 0; i < cols * rows; ++i)
    {
        seed = seed * 1103515245u + 12345u;
        const bool isUsed = i < used ? ((seed >> 16) % 23 != 0) : ((seed >> 16) % 31 == 0);
        const float x = cm.left + 20 + static_cast<float>(i % cols) * cell, y = cm.top + 68 + static_cast<float>(i / cols) * cell;
        c.FillRound(Rect(x, y, cell - 2, cell - 2), 2, isUsed ? P().accent.WithAlpha(0.75f) : P().freeSpace);
    }
    c.Text(L"Each square is about " + FormatBytes(clusters ? clusters * g.vol.bytesPerCluster / (cols * rows) : 0) +
        L". Teal is in use, grey is free (real ratio; the exact layout needs administrator rights).",
        Rect(cm.left + 20, cm.bottom - 52, colW - 40, 40), Text::Caption, P().textDim);

    const D2D1_RECT_F fl = Rect(area.left + colW + 20, area.top, colW, 300);
    c.Card(fl, 16);
    c.Text(L"How a file sits on disk", Rect(fl.left + 20, fl.top + 16, colW - 180, 22), Text::BodyStrong, P().text);
    Button(c, Rect(fl.right - 150, fl.top + 12, 130, 30), L"Choose a file\u2026", Btn::Secondary, HotBrowseFile);
    const FileInternals& f = g.bigFile;
    const size_t slash = f.path.find_last_of(L'\\');
    c.Text(slash == std::wstring::npos ? f.path : f.path.substr(slash + 1), Rect(fl.left + 20, fl.top + 42, colW - 40, 18), Text::Caption, P().textDim);
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
    std::wstring verdict = f.error ? L"Couldn't read this file's layout: " + ErrorText(f.error) :
        f.resident ? L"Tiny file: stored inside its MFT record, no clusters of its own." :
        f.fragments <= 1 ? L"Contiguous: stored in one piece." : Fmt(L"Fragmented: stored in %llu pieces across the disk.", f.fragments);
    c.Text(verdict, Rect(fl.left + 20, strip.bottom + 10, colW - 40, 20), Text::BodyStrong,
        f.error ? P().caution : f.fragments <= 1 ? P().safe : P().review);
    const wchar_t* labels[] = { L"Logical size", L"Size on disk", L"Slack" };
    const u64 vals[] = { f.logicalSize, f.allocatedSize, f.slack };
    for (int i = 0; i < 3; ++i)
    {
        const float x = fl.left + 20 + static_cast<float>(i) * (colW - 40) / 3;
        Overline(c, labels[i], x, strip.bottom + 46, 140);
        c.Text(FormatBytes(vals[i]), Rect(x, strip.bottom + 62, 160, 28), Text::Number, P().text);
    }
    c.Text(Explain(Metric::Extents), Rect(fl.left + 20, fl.bottom - 70, colW - 40, 60), Text::Caption, P().textDim);

    struct Card { const wchar_t* title; std::wstring value; Metric m; };
    const Card cards[] = {
        { L"Cluster size", FormatBytes(g.vol.bytesPerCluster), Metric::ClusterSize },
        { L"Slack space", FormatBytes(g.slack.totalSlack), Metric::Slack },
        { L"Master File Table", g.vol.ntfsData ? FormatCount(g.vol.mftRecordCount) + L" records \u00B7 " + FormatBytes(g.vol.mftValidDataLength) :
          std::wstring(L"Run as admin to read"), Metric::Mft },
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
    if (g.report->hasSnapshot && !g.report->growth.empty())
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
            c.Text((e.delta > 0 ? L"+" : L"\u2212") + FormatBytes(static_cast<u64>(std::llabs(e.delta))), Rect(gc.right - 160, y, 140, 22),
                Text::Body, P().textDim, Align::Right, true);
            y += 32;
        }
    }
    else
    {
        for (int i = 0; i < 5; ++i)
            c.FillRound(Rect(gc.left + 40, gc.top + 70 + static_cast<float>(i) * 30, 120.0f + static_cast<float>((i * 97) % 260), 12), 6, P().surface2);
        EmptyState(c, { gc.left + 300, gc.top + 20, gc.right, gc.bottom }, 0xE81C, L"First scan of " + g.volume.substr(0, 2),
            L"Growth appears after your next scan. Snapshots are saved on a drive that isn't full.");
    }
    const D2D1_RECT_F nc = { area.left, gc.bottom + 16, area.right, area.bottom };
    c.Card(nc, 16);
    c.Text(L"Low-space alerts", Rect(nc.left + 20, nc.top + 16, 500, 22), Text::BodyStrong, P().text);
    const bool on = IsStartWithWindows();
    Badge(c, nc.left + 180, nc.top + 16, on ? Safety::Safe : Safety::Review);
    c.Text(on ? L"Alerts are on: a quiet notification when a drive crosses 50, 20, 10, 5, 3, 2, 1 GB or 512 MB." :
        L"Alerts are off. Turn them on in Settings to be warned before a drive fills up.",
        Rect(nc.left + 20, nc.top + 46, nc.right - nc.left - 40, 20), Text::Body, P().textDim);
}

void PageSettings(Canvas& c, const D2D1_RECT_F& area)
{
    Settings& st = Settings::Get();
    struct Row { const wchar_t* title; const wchar_t* sub; };
    const Row rows[] = {
        { L"Low-space alerts", L"A quiet notification when a drive crosses a threshold. Starts with Windows, runs in the tray (~2 MB)." },
        { L"Alert thresholds", L"Notify when free space drops below these levels." },
        { L"Reserve file", L"Space set aside on the system drive. Release it in Rescue Mode to get Windows breathing again." },
        { L"Work volume", L"Where quarantine, undo logs and snapshots go, so a full drive is never written to." },
        { L"Scan threads", L"More threads scan faster on SSDs. Rescue Mode uses at most 4. Applies to the next scan." },
        { L"Theme", L"Follows Windows unless you choose one." },
    };
    const float rowH = 76;
    const D2D1_RECT_F card = { area.left, area.top, std::min(area.right, area.left + 1040), area.top + rowH * 6 + 8 };
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
        case 0: Toggle(c, card.right - 64, y + 26, IsStartWithWindows(), HotAlerts); break;
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
        case 2:
        {
            const u64 sizes[] = { 256 * MiB, 512 * MiB, 1 * GiB, 2 * GiB };
            int sel = 1;
            for (int k = 0; k < 4; ++k) if (st.rescue.reserveSize == sizes[k]) sel = k;
            Segmented(c, rx, y + 22, { L"256 MB", L"512 MB", L"1 GB", L"2 GB" }, sel, HotReserveSize);
            const ReserveStatus rs = QueryReserve();
            Button(c, Rect(card.right - 190, y + 22, 166, 32), rs.exists ? L"Release (" + FormatBytes(rs.size) + L")" : std::wstring(L"Create now"),
                Btn::Secondary, HotReserveAction);
            break;
        }
        case 3:
        {
            const std::wstring auto_ = PickWorkVolume(g.volume, st.rescue);
            const std::wstring s = !st.rescue.workVolumeOverride.empty() ? st.rescue.workVolumeOverride.substr(0, 2) + L"  \u00B7  chosen" :
                auto_.empty() ? std::wstring(L"No other drive \u2014 kept in memory") : auto_.substr(0, 2) + L"  \u00B7  automatic";
            const D2D1_RECT_F r = Rect(rx, y + 22, 300, 32);
            c.FillRound(r, 8, Hovered(HotWorkVolume) ? P().surface3 : P().surface2);
            c.StrokeRound(r, 8, P().stroke);
            c.Text(s, Rect(r.left + 12, r.top, 260, 32), Text::Body, P().text, Align::Left, true);
            c.Icon(0xE70D, r.right - 28, r.top + 9, 14, P().textDim);
            HotArea(r, HotWorkVolume);
            break;
        }
        case 4:
        {
            const D2D1_RECT_F r = Rect(rx, y + 22, 132, 32);
            c.FillRound(r, 8, P().surface2);
            const int threads = g.threads ? g.threads : static_cast<int>(std::min(8u, std::max(1u, std::thread::hardware_concurrency())));
            c.Text(L"\u2212", Rect(r.left, r.top, 36, 32), Text::BodyStrong, P().text, Align::Center, true);
            c.Text(Fmt(L"%d", threads), Rect(r.left + 36, r.top, 60, 32), Text::BodyStrong, P().text, Align::Center, true);
            c.Text(L"+", Rect(r.right - 36, r.top, 36, 32), Text::BodyStrong, P().text, Align::Center, true);
            HotArea(Rect(r.left, r.top, 36, 32), HotThreadsMinus);
            HotArea(Rect(r.right - 36, r.top, 36, 32), HotThreadsPlus);
            break;
        }
        case 5: Segmented(c, rx, y + 22, { L"System", L"Light", L"Dark" }, g.opt.theme, HotTheme); break;
        }
    }
    c.Text(L"Settings: " + (st.WritesSuppressed() ? std::wstring(L"kept in memory (drive is full)") : st.Location()) +
        L"  \u00B7  SpaceRescue \u00B7 GPL-3.0 \u00B7 built on WinDirStat (windirstat.net)",
        Rect(area.left, card.bottom + 16, 1000, 18), Text::Caption, P().textFaint);
}

// --- Review list -----------------------------------------------------------------

void PageReviewList(Canvas& c, const D2D1_RECT_F& area)
{
    Button(c, Rect(area.left, area.top - 4, 90, 30), L"\u2190 Back", Btn::Secondary, HotRevBack);
    c.Text(g.reviewTitle, Rect(area.left + 104, area.top - 6, 700, 34), Text::Section, P().text, Align::Left, true);
    u64 selBytes = 0;
    size_t selCount = 0;
    bool anyLocked = false;
    for (const ReviewItem& it : g.review)
        if (it.checked) { selBytes += g.tree->At(it.node).size; ++selCount; anyLocked |= it.locked; }
    // List
    const D2D1_RECT_F list = { area.left, area.top + 40, area.right, area.bottom - 120 };
    c.Card(list, 12);
    const float rowH = 40;
    const float maxScroll = std::max(0.0f, static_cast<float>(g.review.size()) * rowH - (list.bottom - list.top - 16));
    g.reviewScroll = std::clamp(g.reviewScroll, 0.0f, maxScroll);
    c.PushClip(gfx::Inset(list, 2));
    float y = list.top + 8 - g.reviewScroll;
    for (size_t i = 0; i < g.review.size(); ++i, y += rowH)
    {
        if (y + rowH < list.top || y > list.bottom) continue;
        const ReviewItem& it = g.review[i];
        const D2D1_RECT_F row = { list.left + 8, y, list.right - 8, y + rowH - 4 };
        if (Hovered(HotRevItem + static_cast<int>(i))) c.FillRound(row, 8, P().surface2);
        // Checkbox
        const D2D1_RECT_F box = Rect(row.left + 10, y + 9, 18, 18);
        if (it.locked) { c.Icon(0xE72E, box.left + 1, box.top + 1, 16, P().protectedC); }
        else if (it.checked) { c.FillRound(box, 4, P().accent); c.Icon(0xE73E, box.left + 2, box.top + 2, 14, P().accentInk); }
        else c.StrokeRound(box, 4, P().textDim, 1.5f);
        const ScanTree& t = *g.tree;
        c.Text(t.Name(it.node), Rect(row.left + 40, y, 380, 20), Text::BodyStrong, P().text, Align::Left, false);
        c.Text(it.group + L"  \u00B7  " + t.Path(t.At(it.node).parent), Rect(row.left + 40, y + 18, row.right - row.left - 360, 18),
            Text::Caption, P().textDim);
        Badge(c, row.right - 260, y + 7, it.safety);
        c.Text(FormatAge(t.At(it.node).writeTime), Rect(row.right - 170, y, 80, 36), Text::Caption, P().textDim, Align::Right, true);
        c.Text(FormatBytes(t.At(it.node).size), Rect(row.right - 90, y, 80, 36), Text::BodyStrong, P().text, Align::Right, true);
        if (!it.locked) HotArea(row, HotRevItem + static_cast<int>(i));
    }
    c.PopClip();
    if (g.review.empty()) EmptyState(c, list, 0xE73E, L"Nothing left here", L"Everything in this group has been handled.");

    // Action bar
    const float by = area.bottom - 108;
    c.Text(FormatCount(selCount) + L" selected \u00B7 " + FormatBytes(selBytes) + (g.volTotal ? L" (" +
        FormatPercent(static_cast<double>(selBytes) / static_cast<double>(g.volTotal)) + L" of " + g.volume.substr(0, 2) + L")" : L""),
        Rect(area.left, by, 600, 24), Text::BodyStrong, P().text, Align::Left, true);
    if (const u32 bin = g.tree->Find(JoinPath(g.volume, L"$Recycle.Bin")); bin != NoNode && g.tree->At(bin).size && g.reviewTitle == L"Windows temp")
        Button(c, Rect(area.right - 470, by - 4, 230, 30), L"Empty Recycle Bin (" + FormatBytes(g.tree->At(bin).size) + L")…",
            g.busy ? Btn::Disabled : Btn::Danger, HotRevEmptyBin);
    Button(c, Rect(area.right - 230, by - 4, 110, 30), L"Select all", Btn::Secondary, HotRevAll);
    Button(c, Rect(area.right - 110, by - 4, 110, 30), L"Select none", Btn::Secondary, HotRevNone);
    const std::wstring work = PickWorkVolume(g.volume, Settings::Get().rescue);
    const bool can = selCount && !g.busy;
    float bx = area.left;
    auto action = [&](const std::wstring& label, Btn kind, int id, float w) {
        Button(c, Rect(bx, by + 34, w, 38), label, can || id == HotRevOpen ? kind : Btn::Disabled, id);
        bx += w + 10;
    };
    action(L"Move to quarantine" + (work.empty() ? std::wstring() : L" (" + work.substr(0, 2) + L")"), work.empty() ? Btn::Disabled : Btn::Primary,
        HotRevQuarantine, 230);
    action(L"Move to Recycle Bin", Btn::Secondary, HotRevRecycle, 180);
    action(L"Compress (NTFS)", Btn::Secondary, HotRevCompress, 160);
    action(L"Open location", Btn::Secondary, HotRevOpen, 140);
    action(L"Delete permanently\u2026", anyLocked ? Btn::Disabled : Btn::Danger, HotRevDelete, 190);
    c.Text(g.busy ? L"Working\u2026" : work.empty() ?
        L"Quarantine needs another drive. The Recycle Bin keeps files on the same drive, so it frees nothing until you empty it." :
        L"Quarantine copies to " + work.substr(0, 2) + L", checks the copy, then removes the original: space is freed now and you can undo. The Recycle Bin frees nothing until emptied.",
        Rect(area.left, by + 80, area.right - area.left, 18), Text::Caption, P().textDim);
}

// --- Command palette ---------------------------------------------------------------

void BuildPalette()
{
    g.items.clear();
    g.paletteSel = 0;
    std::wstring q = ToLower(g.query);
    auto add = [&](const std::wstring& title, const std::wstring& detail, wchar_t icon, std::function<void()> run) {
        if (q.empty() || ContainsI(title, q.c_str()) || ContainsI(detail, q.c_str())) g.items.push_back({ title, detail, icon, std::move(run) });
    };
    for (int i = 0; i < 8; ++i) add(L"Go to " + std::wstring(PageNames[i]), Fmt(L"Ctrl+%d", i + 1), PageIcons[i], [i] { SetPage(i); });
    for (const DriveInfo& d : EnumerateDrives(Settings::Get().rescue))
    {
        const std::wstring r = d.root;
        add(L"Scan " + d.root.substr(0, 2) + L" " + d.label, FormatBytes(d.free) + L" free of " + FormatBytes(d.total), 0xEDA2, [r] { StartScan(r); });
    }
    add(L"Rescan", g.root, 0xE72C, [] { StartScan(g.root); });
    add(L"Choose a folder to scan\u2026", L"Pick any folder", 0xE8B7, [] { PostMessageW(g.hwnd, WM_COMMAND, HotDriveChip, 1); });
    add(L"Review everything that can be freed", L"Clean up", 0xE74D, [] { SetPage(3); });
    add(L"Open Disk Cleanup", L"Windows tool", 0xE74D, [] { ShellExecuteW(nullptr, L"open", L"cleanmgr.exe", nullptr, nullptr, SW_SHOWNORMAL); });
    add(L"Open Storage settings", L"Windows Settings", 0xE713, [] { ShellExecuteW(nullptr, L"open", L"ms-settings:storagesense", nullptr, nullptr, SW_SHOWNORMAL); });
    // Files and folders by name (min 2 characters), largest first.
    if (g.tree && q.size() >= 2)
    {
        std::vector<u32> hits;
        const ScanTree& t = *g.tree;
        for (u32 i = 1; i < t.Count(); ++i)
            if (t.At(i).parent != NoNode && t.NameContainsI(i, q)) hits.push_back(i);
        std::sort(hits.begin(), hits.end(), [&t](u32 a, u32 b) { return t.At(a).size > t.At(b).size; });
        for (size_t k = 0; k < hits.size() && k < 30; ++k)
        {
            const u32 n = hits[k];
            g.items.push_back({ t.Name(n), FormatBytes(t.At(n).size) + L"  \u00B7  " + t.Path(t.At(n).parent), t.IsDir(n) ? wchar_t(0xE8B7) : wchar_t(0xE7C3),
                [n] {
                    const ScanTree& tr = *g.tree;
                    g.exploreRoot = tr.IsDir(n) ? n : tr.At(n).parent;
                    g.highlight = tr.IsDir(n) ? NoNode : n;
                    SetPage(1);
                } });
        }
    }
}

void DrawPalette(Canvas& c)
{
    if (!g.palette) return;
    c.Fill(Rect(0, 0, c.Width(), c.Height()), Color(0x000000, P().dark ? 0.45f : 0.25f));
    const float w = std::min(720.0f, c.Width() - 80), x = (c.Width() - w) / 2, y = 70;
    const float h = std::min(520.0f, c.Height() - 140);
    const D2D1_RECT_F box = Rect(x, y, w, h);
    c.Shadow(box, 14, 32, 12, 0.4f);
    c.FillRound(box, 14, P().surface2);
    c.StrokeRound(box, 14, P().stroke);
    HotArea(box, HotPaletteBox);
    c.Icon(0xE721, x + 18, y + 18, 20, P().textDim);
    c.Text(g.query.empty() ? std::wstring(L"Search files and folders, or type a command\u2026") : g.query + L"\u2502",
        Rect(x + 50, y + 8, w - 70, 40), Text::Section, g.query.empty() ? P().textFaint : P().text, Align::Left, true);
    c.Line(x + 12, y + 56, x + w - 12, y + 56, P().stroke);
    float ry = y + 64;
    const float rowH = 44;
    const int visible = static_cast<int>((h - 72) / rowH);
    const int first = std::max(0, g.paletteSel - visible + 1);
    for (int i = first; i < static_cast<int>(g.items.size()) && i < first + visible; ++i)
    {
        const PaletteItem& it = g.items[static_cast<size_t>(i)];
        const D2D1_RECT_F row = Rect(x + 8, ry, w - 16, rowH - 4);
        if (i == g.paletteSel) c.FillRound(row, 8, P().accent.WithAlpha(0.18f));
        else if (Hovered(HotPaletteItem + i)) c.FillRound(row, 8, P().surface3);
        c.Icon(it.icon, x + 22, ry + 12, 16, i == g.paletteSel ? P().accent : P().textDim);
        c.Text(it.title, Rect(x + 52, ry + 2, w * 0.45f, 36), Text::Body, P().text, Align::Left, true);
        c.Text(it.detail, Rect(x + 52 + w * 0.45f, ry + 2, w * 0.55f - 76, 36), Text::Caption, P().textDim, Align::Right, true);
        HotArea(row, HotPaletteItem + i);
        ry += rowH;
    }
    if (g.items.empty()) c.Text(L"No matches. Try another name.", Rect(x + 52, ry + 6, w - 80, 30), Text::Body, P().textFaint);
    c.Text(L"\u2191\u2193 to move \u00B7 Enter to open \u00B7 Esc to close", Rect(x, y + h + 8, w, 18), Text::Caption, Color(0xFFFFFF, 0.8f), Align::Center);
}

void OpenPalette()
{
    g.palette = true;
    g.query.clear();
    BuildPalette();
    Invalidate();
}

void RunPaletteItem(int i)
{
    if (i < 0 || i >= static_cast<int>(g.items.size())) return;
    auto run = g.items[static_cast<size_t>(i)].run;
    g.palette = false;
    run();
    Invalidate();
}

// --- Paint -----------------------------------------------------------------------

void DrawScanning(Canvas& c, const D2D1_RECT_F& area)
{
    const ScanProgress p = g.scanner ? g.scanner->Progress() : ScanProgress();
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
        c.Text(Fmt(L"safe to free \u00B7 %.1f s", (GetTickCount() - g.scanStart) / 1000.0), Rect(qx + 20, card.top + 70, qw - 40, 18),
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
    c.Text(FormatCount(p.files) + L" files \u00B7 " + FormatBytes(p.bytes), Rect(x, y + 20, w, 56), Text::Hero, P().text);
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
    const float banner = DrawBanner(c, Top + 12);
    const float t = gfx::Ease(g.pageStart, 200);
    const D2D1_RECT_F area = { Rail + Margin, Top + 16 + banner + (1 - t) * 8, c.Width() - Margin, c.Height() - 24 };
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
        case 7: PageSettings(c, area); break;
        default: PageReviewList(c, area); break;
        }
        c.Text(FormatCount(g.tree->fileCount) + L" files \u00B7 scanned in " + Fmt(L"%.1f s", g.tree->scanMillis / 1000.0) +
            (g.tree->usedMft ? L" (MFT)" : L"") + L" \u00B7 " + FormatBytes(PeakWorkingSet()) + L" peak memory",
            Rect(c.Width() - 440, c.Height() - 20, 420, 16), Text::Caption, P().textFaint, Align::Right);
    }
    DrawToast(c);
    DrawPalette(c);
    if (t < 1 || (g.unfoldStart && gfx::Ease(g.unfoldStart, 700, true) < 1)) SetTimer(g.hwnd, TimerAnim, 16, nullptr);
    else KillTimer(g.hwnd, TimerAnim);
    c.End();
}

// --- Actions -----------------------------------------------------------------------

void ApplyTheme()
{
    g.pal = g.opt.theme == 1 ? &gfx::Light() : g.opt.theme == 2 ? &gfx::Dark() : (gfx::SystemPrefersDark() ? &gfx::Dark() : &gfx::Light());
    const BOOL dark = P().dark ? TRUE : FALSE;
    DwmSetWindowAttribute(g.hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof dark);
    const COLORREF caption = RGB(static_cast<int>(P().base.r * 255), static_cast<int>(P().base.g * 255), static_cast<int>(P().base.b * 255));
    DwmSetWindowAttribute(g.hwnd, 35 /* DWMWA_CAPTION_COLOR */, &caption, sizeof caption);
    g.treemapW = 0; // colours changed
    Invalidate();
}

void SetPage(int p)
{
    if (g.scanning && p != g.page) { g.page = p; Invalidate(); return; }
    g.page = std::clamp(p, 0, PageReview);
    g.pageStart = GetTickCount();
    g.hoverSeg = -1;
    g.hoverTile = NoNode;
    if ((g.page == 0 || g.page == 2) && g.tree)
    {
        g.unfoldStart = GetTickCount();
        g.sunRoot = g.tree->Root();
        g.sun.Build(*g.tree, g.sunRoot, g.page == 0 && g.root.size() <= 3 ? g.volFree : 0, g.page == 0 ? 3 : 5);
    }
    Invalidate();
}

void RefreshVolume()
{
    ULARGE_INTEGER avail, total, freeAll;
    if (GetDiskFreeSpaceExW(g.volume.c_str(), &avail, &total, &freeAll)) { g.volFree = avail.QuadPart; g.volTotal = total.QuadPart; }
}

void Analyze()
{
    InsightsOptions io;
    g.report = std::make_unique<InsightsReport>(sr::Analyze(*g.tree, io));
    g.cleanup = std::make_unique<CleanupReport>(BuildCleanup(*g.tree, g.report.get()));
    g.system = std::make_unique<SystemReport>(AnalyzeSystem(*g.tree));
    g.kinds = std::make_unique<viz::KindModel>(viz::BuildKinds(*g.tree));
    g.slack = EstimateSlack(*g.tree);
    g.treemapRoot = NoNode;
}

void StartScan(const std::wstring& root)
{
    if (g.busy) return;
    if (g.scanner) g.scanner->Cancel();
    if (g.quick) g.quick->Cancel();
    g.scanner.reset();
    g.quick.reset();
    g.tree.reset();
    g.report.reset();
    g.cleanup.reset();
    g.system.reset();
    g.kinds.reset();
    g.root = root;
    if (g.root.size() == 2 && g.root[1] == L':') g.root += L'\\';
    g.volume = VolumeOf(g.root);
    RefreshVolume();
    g.exploreRoot = 0;
    g.highlight = NoNode;
    g.scanning = true;
    if (g.page == PageReview) g.page = 3;
    g.scanner = std::make_unique<Scanner>();
    ScanOptions o;
    o.root = g.root;
    o.threads = g.threads;
    o.lowPriority = Low();
    o.memoryBudget = Settings::Get().memoryBudgetMB ? Settings::Get().memoryBudgetMB * MiB : (Low() ? 256 * MiB : 0);
    g.scanner->Start(o, g.hwnd, WM_SCAN_DONE);
    g.scanStart = GetTickCount();
    if (g.root.size() <= 3)
    {
        g.quick = std::make_unique<QuickWins>();
        g.quick->Start(g.volume);
    }
    Settings::Get().lastScanPath = g.root;
    Settings::Get().Save();
    SetTimer(g.hwnd, TimerProgress, 100, nullptr);
    Invalidate();
}

void OnScanDone()
{
    g.tree = g.scanner->TakeResult();
    KillTimer(g.hwnd, TimerProgress);
    if (g.tree->fileCount == 0 && g.tree->errorCount)
        ShowToast(L"Couldn't read " + g.root + L". Check that it exists and you have access.", -1, true);
    Analyze();
    g.vol = QueryVolume(g.volume);
    u32 best = NoNode;
    for (u32 i = 1; i < g.tree->Count(); ++i)
    {
        const Node& n = g.tree->At(i);
        if (n.parent != NoNode && !(n.flags & (NF_DIR | NF_AGGREGATE)) && (best == NoNode || n.size > g.tree->At(best).size)) best = i;
    }
    g.bigFile = best != NoNode ? QueryFile(g.tree->Path(best), g.vol.bytesPerCluster) : FileInternals();
    g.scanning = false;
    const int p = g.page;
    g.page = -1;
    SetPage(p);
}

void ShowToast(const std::wstring& text, int undoId, bool error)
{
    g.toast = text;
    g.toastUndo = undoId;
    g.toastError = error;
    SetTimer(g.hwnd, TimerToast, 12000, nullptr);
    Invalidate();
}

void OpenReview(const std::wstring& title, std::vector<ReviewItem> items)
{
    g.reviewTitle = title;
    for (ReviewItem& it : items)
    {
        it.locked = IsProtectedPath(g.tree->Path(it.node));
        if (it.locked) it.checked = false;
    }
    g.review = std::move(items);
    g.reviewScroll = 0;
    if (g.page != PageReview) g.backPage = g.page;
    SetPage(PageReview);
}

void OpenSectionReview(int s)
{
    const CleanSectionResult& sec = g.cleanup->sections[static_cast<size_t>(s)];
    std::vector<ReviewItem> items;
    for (const CleanGroup& grp : sec.groups)
        for (const CleanItem& it : grp.items)
        {
            // The Recycle Bin gets its own "Empty" action; moving the bin folder itself makes no sense.
            if (_wcsicmp(g.tree->Name(it.node).c_str(), L"$Recycle.Bin") == 0) continue;
            items.push_back({ it.node, grp.name, it.safety, it.safety == Safety::Safe, false });
        }
    OpenReview(sec.title, std::move(items));
}

void RunAction(ActionKind kind)
{
    std::vector<std::wstring> paths;
    std::vector<u32> nodes;
    u64 bytes = 0;
    for (const ReviewItem& it : g.review)
        if (it.checked && !it.locked) { paths.push_back(g.tree->Path(it.node)); nodes.push_back(it.node); bytes += g.tree->At(it.node).size; }
    if (paths.empty() || g.busy) return;
    const std::wstring work = PickWorkVolume(g.volume, Settings::Get().rescue);
    std::wstring what;
    switch (kind)
    {
    case ActionKind::Quarantine: what = L"Move " + FormatCount(paths.size()) + L" items (" + FormatBytes(bytes) + L") to quarantine on " +
        work.substr(0, 2) + L"?\n\nThey are copied, checked, then removed here. You can undo."; break;
    case ActionKind::Recycle: what = L"Move " + FormatCount(paths.size()) + L" items (" + FormatBytes(bytes) +
        L") to the Recycle Bin?\n\nYou can undo. Note: space is freed only when the Recycle Bin is emptied."; break;
    case ActionKind::Compress: what = L"Compress " + FormatCount(paths.size()) + L" items (" + FormatBytes(bytes) +
        L") with NTFS compression?\n\nFiles stay where they are and open normally. You can undo."; break;
    case ActionKind::DeletePermanent: what = L"Permanently delete " + FormatCount(paths.size()) + L" items (" + FormatBytes(bytes) +
        L")?\n\nThis cannot be undone."; break;
    }
    if (MessageBoxW(g.hwnd, what.c_str(), L"SpaceRescue", MB_OKCANCEL | (kind == ActionKind::DeletePermanent ? MB_ICONWARNING : MB_ICONQUESTION)) != IDOK)
        return;
    if (kind == ActionKind::DeletePermanent &&
        MessageBoxW(g.hwnd, L"Are you sure? These files will be gone for good.", L"SpaceRescue", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
        return;
    g.busy = true;
    if (g.worker.joinable()) g.worker.join();
    g.worker = std::thread([kind, paths, nodes, work] {
        auto* res = new std::pair<ActionResult, std::vector<u32>>(ActionManager::Get().Apply(kind, paths, work), nodes);
        PostMessageW(g.hwnd, WM_ACTION_DONE, 0, reinterpret_cast<LPARAM>(res));
    });
    Invalidate();
}

void OnActionDone(std::pair<ActionResult, std::vector<u32>>* res)
{
    g.busy = false;
    const ActionResult& r = res->first;
    // Remove what was handled from the tree (compress keeps files in place).
    if (r.kind != ActionKind::Compress)
    {
        for (size_t i = 0; i < res->second.size(); ++i)
        {
            const std::wstring path = g.tree->Path(res->second[i]);
            if (std::find(r.done.begin(), r.done.end(), path) != r.done.end()) g.tree->Detach(res->second[i]);
        }
        g.review.erase(std::remove_if(g.review.begin(), g.review.end(), [&](const ReviewItem& it) {
            return std::find(r.done.begin(), r.done.end(), g.tree->Path(it.node)) != r.done.end(); }), g.review.end());
    }
    RefreshVolume();
    Analyze();
    std::wstring text = r.summary.empty() ? Fmt(L"Done: %zu items.", r.done.size()) : r.summary;
    if (!r.failed.empty()) text += Fmt(L" %zu couldn't be handled (first: %s).", r.failed.size(), r.failed[0].error.c_str());
    ShowToast(text, ActionManager::Get().CanUndo(r.undoId) ? r.undoId : -1, r.done.empty() && !r.failed.empty());
    delete res;
}

void RunUndo()
{
    if (g.toastUndo < 0 || g.busy) return;
    const int id = g.toastUndo;
    g.busy = true;
    g.toast = L"Undoing\u2026";
    g.toastUndo = -1;
    if (g.worker.joinable()) g.worker.join();
    g.worker = std::thread([id] {
        auto* res = new ActionResult(ActionManager::Get().Undo(id));
        PostMessageW(g.hwnd, WM_UNDO_DONE, 0, reinterpret_cast<LPARAM>(res));
    });
    Invalidate();
}

std::wstring PickPath(bool folder)
{
    std::wstring result;
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return result;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_FORCEFILESYSTEM | (folder ? FOS_PICKFOLDERS : 0));
    dlg->SetTitle(folder ? L"Choose a folder to scan" : L"Choose a file to inspect");
    if (SUCCEEDED(dlg->Show(g.hwnd)))
    {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item)))
        {
            wchar_t* path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) { result = path; CoTaskMemFree(path); }
            item->Release();
        }
    }
    dlg->Release();
    return result;
}

void DriveMenu()
{
    HMENU menu = CreatePopupMenu();
    const std::vector<DriveInfo> drives = EnumerateDrives(Settings::Get().rescue);
    for (size_t i = 0; i < drives.size(); ++i)
    {
        const DriveInfo& d = drives[i];
        AppendMenuW(menu, MF_STRING | (_wcsicmp(d.root.c_str(), g.root.c_str()) == 0 ? MF_CHECKED : 0), 1 + i,
            (L"Scan " + d.root.substr(0, 2) + L"  " + d.label + L"   (" + FormatBytes(d.free) + L" free of " + FormatBytes(d.total) + L")" +
             (d.low ? L"  \u2014 almost full" : L"")).c_str());
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 100, L"Choose a folder\u2026");
    AppendMenuW(menu, MF_STRING, 101, L"Rescan");
    POINT pt;
    GetCursorPos(&pt);
    const UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, g.hwnd, nullptr);
    DestroyMenu(menu);
    if (cmd >= 1 && cmd <= drives.size()) StartScan(drives[cmd - 1].root);
    else if (cmd == 100) { const std::wstring p = PickPath(true); if (!p.empty()) StartScan(p); }
    else if (cmd == 101) StartScan(g.root);
}

void WorkVolumeMenu()
{
    Settings& st = Settings::Get();
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | (st.rescue.workVolumeOverride.empty() ? MF_CHECKED : 0), 1, L"Automatic (drive with most free space)");
    const std::vector<DriveInfo> drives = EnumerateDrives(st.rescue);
    for (size_t i = 0; i < drives.size(); ++i)
        AppendMenuW(menu, MF_STRING | (_wcsicmp(drives[i].root.c_str(), st.rescue.workVolumeOverride.c_str()) == 0 ? MF_CHECKED : 0), 2 + i,
            (drives[i].root.substr(0, 2) + L"  " + FormatBytes(drives[i].free) + L" free").c_str());
    POINT pt;
    GetCursorPos(&pt);
    const UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, g.hwnd, nullptr);
    DestroyMenu(menu);
    if (cmd == 1) st.rescue.workVolumeOverride.clear();
    else if (cmd >= 2 && cmd - 2 < drives.size()) st.rescue.workVolumeOverride = drives[cmd - 2].root;
    if (cmd) st.Save();
    Invalidate();
}

void RowMenu(u32 node)
{
    const ScanTree& t = *g.tree;
    const std::wstring path = t.Path(node);
    HMENU menu = CreatePopupMenu();
    if (t.IsDir(node)) AppendMenuW(menu, MF_STRING, 1, L"Open folder here");
    AppendMenuW(menu, MF_STRING, 2, L"Show in File Explorer");
    AppendMenuW(menu, MF_STRING, 3, L"Copy path");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (IsProtectedPath(path) ? MF_GRAYED : 0), 4, L"Review and remove\u2026");
    AppendMenuW(menu, MF_STRING, 5, L"Show how it sits on disk");
    POINT pt;
    GetCursorPos(&pt);
    const UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, g.hwnd, nullptr);
    DestroyMenu(menu);
    switch (cmd)
    {
    case 1: g.exploreRoot = node; g.highlight = NoNode; Invalidate(); break;
    case 2: ShellExecuteW(nullptr, L"open", L"explorer.exe", (L"/select,\"" + path + L"\"").c_str(), nullptr, SW_SHOWNORMAL); break;
    case 3:
        if (OpenClipboard(g.hwnd))
        {
            EmptyClipboard();
            const size_t bytes = (path.size() + 1) * sizeof(wchar_t);
            if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes))
            {
                memcpy(GlobalLock(h), path.c_str(), bytes);
                GlobalUnlock(h);
                SetClipboardData(CF_UNICODETEXT, h);
            }
            CloseClipboard();
            ShowToast(L"Path copied.");
        }
        break;
    case 4: OpenReview(t.Name(node), { { node, L"Chosen in Explore", Safety::Review, true, false } }); break;
    case 5:
        if (!t.IsDir(node)) { g.bigFile = QueryFile(path, g.vol.bytesPerCluster); SetPage(5); }
        else ShowToast(L"Pick a file (not a folder) to see its layout.");
        break;
    }
}

void RunSystemTool(size_t i)
{
    const SystemItem& it = g.system->items[i];
    const std::wstring& cmd = it.command;
    std::wstring confirm = it.tool + L"\n\nSpaceRescue will run:\n" + cmd;
    if (MessageBoxW(g.hwnd, confirm.c_str(), L"SpaceRescue", MB_OKCANCEL | MB_ICONINFORMATION) != IDOK) return;
    const bool admin = cmd.find(L"(admin)") != std::wstring::npos;
    std::wstring line = cmd.substr(0, cmd.find(L"  ("));
    if (line.compare(0, 11, L"ms-settings") == 0) { ShellExecuteW(nullptr, L"open", line.c_str(), nullptr, nullptr, SW_SHOWNORMAL); return; }
    const size_t space = line.find(L' ');
    const std::wstring exe = line.substr(0, space), args = space == std::wstring::npos ? L"" : line.substr(space + 1);
    if (admin)
        ShellExecuteW(g.hwnd, L"runas", L"cmd.exe", (L"/k " + line).c_str(), nullptr, SW_SHOWNORMAL); // shows the result
    else
        ShellExecuteW(g.hwnd, L"open", exe.c_str(), args.empty() ? nullptr : args.c_str(), nullptr, SW_SHOWNORMAL);
}

void ToggleAlerts()
{
    const bool on = !IsStartWithWindows();
    SetStartWithWindows(on);
    if (on)
    {
        wchar_t exe[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        ShellExecuteW(nullptr, L"open", exe, L"/tray", nullptr, SW_HIDE);
        ShowToast(L"Low-space alerts are on. Look for the SpaceRescue icon in the tray.");
    }
    else
    {
        if (HWND tray = FindWindowW(L"SpaceRescue.Tray", nullptr)) PostMessageW(tray, WM_CLOSE, 0, 0);
        ShowToast(L"Low-space alerts are off.");
    }
}

void ReserveAction()
{
    const ReserveStatus rs = QueryReserve();
    if (rs.exists)
    {
        if (MessageBoxW(g.hwnd, (L"Release the reserve file?\n\nThis frees " + FormatBytes(rs.size) + L" on " + VolumeOf(rs.path).substr(0, 2) +
            L" immediately.").c_str(), L"SpaceRescue", MB_OKCANCEL | MB_ICONQUESTION) != IDOK) return;
        const DWORD e = ReleaseReserve();
        ShowToast(e ? L"Couldn't release the reserve: " + ErrorText(e) : L"Reserve released: " + FormatBytes(rs.size) + L" freed.", -1, e != 0);
    }
    else
    {
        const u64 size = Settings::Get().rescue.reserveSize;
        if (MessageBoxW(g.hwnd, (L"Create a " + FormatBytes(size) + L" reserve file on the system drive?\n\nIt sets space aside that you can release "
            L"in one click when the drive is full.").c_str(), L"SpaceRescue", MB_OKCANCEL | MB_ICONQUESTION) != IDOK) return;
        const DWORD e = CreateReserve(size);
        ShowToast(e ? L"Couldn't create the reserve: " + ErrorText(e) : L"Reserve file created (" + FormatBytes(size) + L").", -1, e != 0);
    }
    RefreshVolume();
}

void Click(int hot)
{
    if (g.palette && hot != HotPaletteBox && (hot < HotPaletteItem || hot >= HotPaletteItem + 1000)) { g.palette = false; Invalidate(); return; }
    if (hot >= HotPaletteItem && hot < HotPaletteItem + 1000) { RunPaletteItem(hot - HotPaletteItem); return; }
    if (hot >= HotRail && hot < HotRail + 8) { SetPage(hot - HotRail); return; }
    if (hot == HotDriveChip) { DriveMenu(); return; }
    if (hot == HotSearch) { OpenPalette(); return; }
    if (hot == HotToastClose) { g.toast.clear(); Invalidate(); return; }
    if (hot == HotToastUndo) { RunUndo(); return; }
    if (hot == HotBannerRelease) { ReserveAction(); return; }
    if (g.scanning) return;
    if (hot >= HotMapMode && hot < HotMapMode + 3) { g.mapMode = hot - HotMapMode; g.unfoldStart = GetTickCount(); Invalidate(); return; }
    if (hot == HotReviewAll) { SetPage(3); return; }
    if (hot == HotExploreBtn) { g.exploreRoot = 0; SetPage(1); return; }
    if (hot >= HotHog && hot < HotHog + 5)
    {
        const u32 n = g.hogNodes[static_cast<size_t>(hot - HotHog)];
        g.exploreRoot = g.tree->IsDir(n) ? n : g.tree->At(n).parent;
        g.highlight = g.tree->IsDir(n) ? NoNode : n;
        SetPage(1);
        return;
    }
    if (hot == HotUp && g.exploreRoot != 0) { g.highlight = g.exploreRoot; g.exploreRoot = g.tree->At(g.exploreRoot).parent; Invalidate(); return; }
    if (hot >= HotRow && hot < HotRow + 1000 && hot - HotRow < static_cast<int>(g.rowNodes.size()))
    {
        const u32 n = g.rowNodes[static_cast<size_t>(hot - HotRow)];
        if (g.tree->IsDir(n)) { g.exploreRoot = n; g.highlight = NoNode; }
        else g.highlight = n;
        Invalidate();
        return;
    }
    if (hot == HotTreemap)
    {
        const float s = g.canvas.Dpi() / 96.0f;
        const u32 n = g.treemap.Hit(static_cast<int>((g.mouseX - g.treemapRect.left) * s), static_cast<int>((g.mouseY - g.treemapRect.top) * s));
        if (n != NoNode && n != 0)
        {
            g.exploreRoot = g.tree->IsDir(n) ? n : g.tree->At(n).parent;
            g.highlight = g.tree->IsDir(n) ? NoNode : n;
            SetPage(1);
        }
        return;
    }
    if (hot == HotSunCard && g.page == 2)
    {
        if (g.hoverSeg >= 0)
        {
            const viz::Segment& sg = g.sun.Segments()[static_cast<size_t>(g.hoverSeg)];
            if (sg.node != NoNode && g.tree->IsDir(sg.node)) g.sunRoot = sg.node;
        }
        else if (g.sunRoot != g.tree->Root()) g.sunRoot = g.tree->At(g.sunRoot).parent;
        g.sun.Build(*g.tree, g.sunRoot, 0, 5);
        g.unfoldStart = GetTickCount();
        g.hoverSeg = -1;
        Invalidate();
        return;
    }
    if (hot >= HotCleanReview && hot < HotCleanReview + 4) { OpenSectionReview(hot - HotCleanReview); return; }
    if (hot >= HotSysTool && hot < HotSysTool + 50) { RunSystemTool(static_cast<size_t>(hot - HotSysTool)); return; }
    if (hot == HotAlerts) { ToggleAlerts(); return; }
    if (hot >= HotReserveSize && hot < HotReserveSize + 4)
    {
        const u64 sizes[] = { 256 * MiB, 512 * MiB, 1 * GiB, 2 * GiB };
        Settings::Get().rescue.reserveSize = sizes[hot - HotReserveSize];
        Settings::Get().Save();
        Invalidate();
        return;
    }
    if (hot == HotReserveAction) { ReserveAction(); return; }
    if (hot == HotWorkVolume) { WorkVolumeMenu(); return; }
    if (hot == HotThreadsMinus || hot == HotThreadsPlus)
    {
        const int cur = g.threads ? g.threads : static_cast<int>(std::min(8u, std::max(1u, std::thread::hardware_concurrency())));
        g.threads = std::clamp(cur + (hot == HotThreadsPlus ? 1 : -1), 1, 16);
        Invalidate();
        return;
    }
    if (hot >= HotTheme && hot < HotTheme + 3) { g.opt.theme = hot - HotTheme; ApplyTheme(); return; }
    if (hot == HotBrowseFile)
    {
        const std::wstring p = PickPath(false);
        if (!p.empty()) { g.bigFile = QueryFile(p, g.vol.bytesPerCluster); Invalidate(); }
        return;
    }
    // Review page
    if (hot == HotRevBack) { SetPage(g.backPage); return; }
    if (hot == HotRevAll || hot == HotRevNone) { for (ReviewItem& it : g.review) it.checked = hot == HotRevAll && !it.locked; Invalidate(); return; }
    if (hot >= HotRevItem && hot - HotRevItem < static_cast<int>(g.review.size()))
    {
        ReviewItem& it = g.review[static_cast<size_t>(hot - HotRevItem)];
        if (!it.locked) it.checked = !it.checked;
        Invalidate();
        return;
    }
    if (hot == HotRevEmptyBin)
    {
        const u32 bin = g.tree->Find(JoinPath(g.volume, L"$Recycle.Bin"));
        const u64 bytes = bin == NoNode ? 0 : g.tree->At(bin).size;
        if (MessageBoxW(g.hwnd, (L"Empty the Recycle Bin on " + g.volume.substr(0, 2) + L"?\n\nThis frees about " + FormatBytes(bytes) +
            L" and cannot be undone.").c_str(), L"SpaceRescue", MB_OKCANCEL | MB_ICONWARNING) != IDOK) return;
        const HRESULT hr = SHEmptyRecycleBinW(g.hwnd, g.volume.c_str(), SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND);
        if (SUCCEEDED(hr) || hr == E_UNEXPECTED) // E_UNEXPECTED = already empty
        {
            if (bin != NoNode) g.tree->Detach(bin);
            RefreshVolume();
            Analyze();
            ShowToast(L"Recycle Bin emptied: about " + FormatBytes(bytes) + L" freed.");
        }
        else ShowToast(L"Couldn't empty the Recycle Bin: " + ErrorText(static_cast<DWORD>(hr)), -1, true);
        return;
    }
    if (hot == HotRevRecycle) RunAction(ActionKind::Recycle);
    else if (hot == HotRevQuarantine) RunAction(ActionKind::Quarantine);
    else if (hot == HotRevCompress) RunAction(ActionKind::Compress);
    else if (hot == HotRevDelete) RunAction(ActionKind::DeletePermanent);
    else if (hot == HotRevOpen)
    {
        for (const ReviewItem& it : g.review)
            if (it.checked)
            {
                ShellExecuteW(nullptr, L"open", L"explorer.exe", (L"/select,\"" + g.tree->Path(it.node) + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
                break;
            }
    }
}

int HotAt(float x, float y)
{
    for (auto it = g.hots.rbegin(); it != g.hots.rend(); ++it)
        if (gfx::Contains(it->r, x, y)) return it->id;
    return -1;
}

void SaveSnapshotBmp()
{
    wchar_t spec[MAX_PATH] = {};
    GetEnvironmentVariableW(L"SR_SNAPSHOT", spec, MAX_PATH);
    std::wstring file = spec;
    file = file.substr(0, file.find(L';'));
    RECT rc;
    GetClientRect(g.hwnd, &rc);
    HDC screen = GetDC(g.hwnd);
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
    ReleaseDC(g.hwnd, screen);
}

void UpdateHover(HWND hwnd, LPARAM lp)
{
    const float s = 96.0f / static_cast<float>(GetDpiForWindow(hwnd));
    g.mouseX = static_cast<float>(static_cast<short>(LOWORD(lp))) * s;
    g.mouseY = static_cast<float>(static_cast<short>(HIWORD(lp))) * s;
    const int hot = HotAt(g.mouseX, g.mouseY);
    g.hoverHot = hot;
    g.hoverRail = hot >= HotRail && hot < HotRail + 8 ? hot - HotRail : -1;
    g.hoverSeg = -1;
    g.hoverTile = NoNode;
    if (!g.scanning && !g.palette && g.tree)
    {
        if (g.page == 0 || (g.page == 2 && g.mapMode == 0))
        {
            const float banner = Low() ? 64.0f : 0.0f;
            const D2D1_RECT_F area = { Rail + Margin, Top + 16 + banner, g.canvas.Width() - Margin, g.canvas.Height() - 24 };
            SunGeom geo;
            if (g.page == 0)
            {
                const float heroW = std::min((area.right - area.left) * 0.52f, area.bottom - area.top);
                const float outer = std::min(heroW - 48, area.bottom - area.top - 64 - 40) / 2;
                geo = { area.left + heroW / 2, area.top + 20 + outer, outer * 0.40f, outer };
            }
            else geo = MapGeometry({ area.left, area.top + 50, area.right, area.bottom });
            g.hoverSeg = g.sun.Hit(g.mouseX, g.mouseY, geo.cx, geo.cy, geo.inner, geo.outer);
        }
        if (hot == HotTreemap && g.page == 2)
        {
            const float sc = g.canvas.Dpi() / 96.0f;
            g.hoverTile = g.treemap.Hit(static_cast<int>((g.mouseX - g.treemapRect.left) * sc), static_cast<int>((g.mouseY - g.treemapRect.top) * sc));
        }
    }
    Invalidate();
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
        Invalidate();
        return 0;
    case WM_DPICHANGED:
    {
        const RECT* r = reinterpret_cast<const RECT*>(lp);
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        g.treemapW = 0;
        return 0;
    }
    case WM_SETTINGCHANGE:
        if (g.opt.theme == 0) ApplyTheme(); // follow Windows light/dark switches live
        return 0;
    case WM_TIMER:
        if (wp == TimerSnapshot) { KillTimer(hwnd, TimerSnapshot); SaveSnapshotBmp(); return 0; }
        if (wp == TimerToast) { KillTimer(hwnd, TimerToast); g.toast.clear(); }
        Invalidate();
        return 0;
    case WM_SCAN_DONE:
        OnScanDone();
        return 0;
    case WM_ACTION_DONE:
        if (g.worker.joinable()) g.worker.join();
        OnActionDone(reinterpret_cast<std::pair<ActionResult, std::vector<u32>>*>(lp));
        return 0;
    case WM_UNDO_DONE:
    {
        if (g.worker.joinable()) g.worker.join();
        g.busy = false;
        auto* r = reinterpret_cast<ActionResult*>(lp);
        const std::wstring text = r->summary.empty() ? std::wstring(L"Undone.") : r->summary;
        delete r;
        ShowToast(text + L" Rescanning to show the restored files\u2026");
        StartScan(g.root);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == HotDriveChip && lp == 1) { const std::wstring p = PickPath(true); if (!p.empty()) StartScan(p); }
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT)
        {
            const int hot = g.hoverHot;
            const bool clickable = hot >= HotRail && hot != HotPaletteBox;
            SetCursor(LoadCursorW(nullptr, g.busy ? IDC_APPSTARTING : clickable ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_MOUSEMOVE:
    {
        UpdateHover(hwnd, lp);
        TRACKMOUSEEVENT tme = { sizeof tme, TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }
    case WM_MOUSELEAVE:
        g.hoverRail = g.hoverSeg = g.hoverHot = -1;
        g.hoverTile = NoNode;
        Invalidate();
        return 0;
    case WM_MOUSEWHEEL:
        if (g.page == PageReview)
        {
            g.reviewScroll -= static_cast<float>(GET_WHEEL_DELTA_WPARAM(wp)) / 120.0f * 80.0f;
            Invalidate();
        }
        else if (g.palette)
        {
            g.paletteSel = std::clamp(g.paletteSel + (GET_WHEEL_DELTA_WPARAM(wp) < 0 ? 1 : -1), 0, std::max(0, static_cast<int>(g.items.size()) - 1));
            Invalidate();
        }
        return 0;
    case WM_LBUTTONDOWN:
        UpdateHover(hwnd, lp);
        Click(HotAt(g.mouseX, g.mouseY));
        return 0;
    case WM_RBUTTONUP:
    {
        UpdateHover(hwnd, lp);
        const int hot = HotAt(g.mouseX, g.mouseY);
        if (!g.scanning && g.page == 1 && hot >= HotRow && hot - HotRow < static_cast<int>(g.rowNodes.size()))
            RowMenu(g.rowNodes[static_cast<size_t>(hot - HotRow)]);
        else if (!g.scanning && g.page == PageReview && hot >= HotRevItem && hot - HotRevItem < static_cast<int>(g.review.size()))
            RowMenu(g.review[static_cast<size_t>(hot - HotRevItem)].node);
        return 0;
    }
    case WM_CHAR:
        if (!g.palette)
        {
            if (wp == L'/' ) OpenPalette();
            return 0;
        }
        if (wp == 8) { if (!g.query.empty()) g.query.pop_back(); }
        else if (wp >= 32) g.query += static_cast<wchar_t>(wp);
        else return 0;
        BuildPalette();
        Invalidate();
        return 0;
    case WM_KEYDOWN:
    {
        const bool ctrl = GetKeyState(VK_CONTROL) < 0;
        if (g.palette)
        {
            if (wp == VK_ESCAPE) g.palette = false;
            else if (wp == VK_DOWN) g.paletteSel = std::min(g.paletteSel + 1, std::max(0, static_cast<int>(g.items.size()) - 1));
            else if (wp == VK_UP) g.paletteSel = std::max(g.paletteSel - 1, 0);
            else if (wp == VK_RETURN) { RunPaletteItem(g.paletteSel); return 0; }
            Invalidate();
            return 0;
        }
        if (ctrl && (wp == 'K' || wp == 'F')) OpenPalette();
        else if (ctrl && wp >= '1' && wp <= '8') SetPage(static_cast<int>(wp - '1'));
        else if (ctrl && wp == VK_TAB) SetPage(((g.page == PageReview ? 3 : g.page) + (GetKeyState(VK_SHIFT) < 0 ? 7 : 1)) % 8);
        else if (wp == VK_F5) StartScan(g.root);
        else if (wp == VK_ESCAPE && g.page == PageReview) SetPage(g.backPage);
        else if (wp == VK_ESCAPE && g.toast.size()) { g.toast.clear(); Invalidate(); }
        else if (wp == VK_BACK && g.page == 1 && g.tree && g.exploreRoot != 0) Click(HotUp);
        return 0;
    }
    case WM_CLOSE:
        if (g.busy && MessageBoxW(hwnd, L"An action is still running. Close anyway?", L"SpaceRescue", MB_YESNO | MB_ICONWARNING) != IDYES) return 0;
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (g.scanner) g.scanner->Cancel();
        if (g.quick) g.quick->Cancel();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
}

bool ParsePreviewArgs(const int argc, wchar_t** argv, PreviewOptions* options)
{
    bool classic = false;
    for (int i = 1; i < argc; ++i)
    {
        const wchar_t* a = argv[i];
        const bool sw = a[0] == L'/' || a[0] == L'-';
        if (sw && _wcsicmp(a + 1, L"classic") == 0) classic = true;
        else if (sw && _wcsicmp(a + 1, L"light") == 0) options->theme = 1;
        else if (sw && _wcsicmp(a + 1, L"dark") == 0) options->theme = 2;
        else if (sw && _wcsicmp(a + 1, L"page") == 0 && i + 1 < argc) options->page = _wtoi(argv[++i]);
        else if (!sw)
        {
            // "C:\" /dark can arrive as one argument: C:" /dark (the backslash escapes the quote).
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
    return !classic; // the new interface is the default; /classic opens the old one
}

int RunPreview(const HINSTANCE instance, const int show, const PreviewOptions& options)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    g.opt = options;
    g.page = std::clamp(options.page, 0, 7);
    std::wstring root = options.path;
    if (root.empty()) root = VolumeOf(KnownFolder(CSIDL_WINDOWS));

    WNDCLASSEXW wc = { sizeof wc };
    wc.lpfnWndProc = Proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    wc.lpszClassName = L"SpaceRescue.Main";
    RegisterClassExW(&wc);
    g.hwnd = CreateWindowExW(0, wc.lpszClassName, L"SpaceRescue", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1500, 980,
        nullptr, nullptr, instance, nullptr);
    ApplyTheme();
    const int backdrop = 2; // DWMSBT_MAINWINDOW (Mica)
    DwmSetWindowAttribute(g.hwnd, 38 /* DWMWA_SYSTEMBACKDROP_TYPE */, &backdrop, sizeof backdrop);
    ShowWindow(g.hwnd, show);
    StartScan(root);
    if (wchar_t spec[MAX_PATH]; GetEnvironmentVariableW(L"SR_SNAPSHOT", spec, MAX_PATH))
    {
        const wchar_t* ms = wcschr(spec, L';');
        SetTimer(g.hwnd, TimerSnapshot, ms ? static_cast<UINT>(_wtoi(ms + 1)) : 3000, nullptr);
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (g.worker.joinable()) g.worker.join();
    g.scanner.reset();
    g.quick.reset();
    g.canvas.Discard();
    return 0;
}
}
