// SpaceRescue UI - SPDX-License-Identifier: GPL-3.0-or-later
//
// Main window: view switching (home / scanning / results), Rescue Mode banner, tab strip,
// status bar, undo toast, scan + analysis orchestration.

#include "app_window.h"
#include "widgets.h"
#include "insights_page.h"
#include "folders_page.h"
#include "internals_page.h"
#include "review_dialog.h"
#include <shlobj.h>

#ifndef BIF_NEWDIALOGSTYLE
#define BIF_NEWDIALOGSTYLE 0x0040
#endif

namespace ui
{
namespace
{
const wchar_t* kMainClass = L"SpaceRescueMain";
const UINT WM_SR_START = WM_APP + 40;
enum { IDT_TICK = 1, IDT_TOAST = 2 };
const UINT kToastMillis = 10000;
const sr::u64 kRescueBudget = 256 * sr::MiB;

// Button ids used by the small chrome canvases.
enum { TAB_BASE = 100, BTN_HOME = 200, BTN_RESCAN = 201, BTN_BACK = 202 };

// ----- Rescue Mode banner ----------------------------------------------------------------

class BannerBar : public Canvas
{
public:
    sr::ReserveStatus reserve;

protected:
    COLORREF Background() const override { return T().banner; }
    int OnPaint(Painter& p) override;
    void OnButton(int id) override;
};

// ----- Tab strip -------------------------------------------------------------------------

class TabBar : public Canvas
{
public:
    TabBar() { m_arrowsNavigate = true; }
    bool hasResults = false;
    int selected = -1; // -1 = no tab highlighted (home / scanning)
    View view = View::Home;

protected:
    COLORREF Background() const override { return T().card; }
    int OnPaint(Painter& p) override;
    void OnButton(int id) override;
};

// ----- Status bar ------------------------------------------------------------------------

class StatusBar : public Canvas
{
public:
    std::vector<std::wstring> items;

protected:
    int OnPaint(Painter& p) override;
};

// ----- Undo toast ------------------------------------------------------------------------

struct UndoJob
{
    int id;
    HWND notify;
};

DWORD WINAPI UndoMain(LPVOID p)
{
    std::unique_ptr<UndoJob> j(static_cast<UndoJob*>(p));
    auto* r = new sr::ActionResult(sr::ActionManager::Get().Undo(j->id));
    if (!PostMessageW(j->notify, WM_SR_UNDODONE, 0, reinterpret_cast<LPARAM>(r))) delete r;
    return 0;
}

class ToastBar : public Canvas
{
public:
    std::wstring text;
    int undoId = -1;
    bool busy = false;

protected:
    COLORREF Background() const override { return T().card; }
    int OnPaint(Painter& p) override;
    void OnButton(int id) override;
};

// ----- Home (drive picker / empty state) ---------------------------------------------------

class HomePage : public Canvas
{
public:
    std::wstring error;

protected:
    int OnPaint(Painter& p) override;
    void OnButton(int id) override;
};

// ----- Scanning progress -------------------------------------------------------------------

class ScanPage : public Canvas
{
public:
    std::wstring root;
    sr::ScanProgress progress;
    sr::u64 driveUsed = 0;     // used bytes of the drive when scanning a whole drive (determinate bar)
    bool rescue = false;
    bool lowPriority = false;
    sr::u64 budget = 0;
    DWORD startTick = 0;
    bool cancelling = false;

protected:
    int OnPaint(Painter& p) override;
    void OnButton(int id) override;
};

struct AnalysisJob
{
    const sr::ScanTree* tree;
    sr::InsightsOptions options;
    std::wstring snapshotDir;
    bool saveSnapshot;
    HWND notify;
    int generation;
};

// Everything the window owns.
struct State
{
    HWND hwnd = nullptr;
    View view = View::Home;
    int tab = 0;
    BannerBar banner;
    TabBar tabbar;
    StatusBar status;
    ToastBar toast;
    HomePage home;
    ScanPage scanPage;
    InsightsPage insights;
    FoldersPage folders;
    InternalsPage internals;
    std::unique_ptr<sr::Scanner> scanner;
    HANDLE analysisThread = nullptr;
    int generation = 0;
    int tickCount = 0;
    bool modalOpen = false;
    bool themePending = false;
    bool toastVisible = false;
    int driveIndex = -1;
    std::wstring pendingScan;
    std::wstring lastRoot;
    bool undoBusy = false;
};
State* S_ = nullptr;
State& G() { return *S_; }

std::wstring DriveName(const sr::DriveInfo& d) { return d.root.substr(0, 2); }

std::wstring FreeText(sr::u64 bytes)
{
    return bytes == 0 ? Str(IDS_ZERO_BYTES) : sr::FormatBytes(bytes);
}

HWND ActivePage()
{
    State& g = G();
    if (g.view == View::Home) return g.home.Hwnd();
    if (g.view == View::Scanning) return g.scanPage.Hwnd();
    if (g.tab == 0) return g.insights.Hwnd();
    if (g.tab == 1) return g.folders.Hwnd();
    return g.internals.Hwnd();
}

// ===== BannerBar =========================================================================

int BannerBar::OnPaint(Painter& p)
{
    const Theme& t = p.t;
    App& app = App::Get();
    std::vector<const sr::DriveInfo*> low;
    for (const sr::DriveInfo& d : app.Drives())
        if (d.low) low.push_back(&d);
    if (low.empty()) return 0;
    std::stable_sort(low.begin(), low.end(), [](const sr::DriveInfo* a, const sr::DriveInfo* b) { return a->isSystem && !b->isSystem; });
    const sr::DriveInfo& d = *low[0];
    const std::wstring name = DriveName(d);
    const int x = p.S(24), w = p.width - 2 * x;
    int y = p.S(12);
    std::wstring head = Strf(IDS_BANNER_HEAD, name.c_str(), FreeText(d.free).c_str(), name.c_str());
    if (low.size() > 1) head += L" " + Strf(IDS_BANNER_MORE, static_cast<int>(low.size() - 1));
    y += p.Text(x, y, w, head, t.bold, t.bannerText) + p.S(4);
    const std::wstring wv = app.WorkVolume();
    y += p.Text(x, y, w, wv.empty() ? Str(IDS_BANNER_NOWORK) : Strf(IDS_BANNER_WORK, wv.c_str()), t.font, t.bannerText) + p.S(10);
    if (reserve.exists)
    {
        const std::wstring label = Strf(IDS_BANNER_RELEASE, sr::FormatBytes(reserve.size).c_str());
        p.Button(1, x, y, label, BtnStyle::Primary);
        y += p.ButtonHeight() + p.S(12);
    }
    else
        y += p.Text(x, y, w, Str(IDS_BANNER_NORESERVE), t.caption, t.bannerText) + p.S(12);
    RECT stripe = { 0, 0, p.S(4), y };
    p.Fill(stripe, t.danger);
    return y;
}

void BannerBar::OnButton(int id)
{
    if (id != 1) return;
    State& g = G();
    const sr::ReserveStatus rs = sr::QueryReserve();
    if (!rs.exists) { App::Get().RefreshDrives(); return; }
    const std::wstring text = Strf(IDS_RESERVE_CONFIRM, sr::FormatBytes(rs.size).c_str());
    if (MessageBoxW(g.hwnd, text.c_str(), Str(IDS_APP_NAME).c_str(), MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return;
    const DWORD err = sr::ReleaseReserve();
    if (err != ERROR_SUCCESS)
    {
        MessageBoxW(g.hwnd, Strf(IDS_RESERVE_FAILED, sr::ErrorText(err).c_str()).c_str(), Str(IDS_APP_NAME).c_str(), MB_OK | MB_ICONERROR);
        return;
    }
    App::Get().RefreshDrives();
    App::Get().ShowToast(Strf(IDS_RESERVE_RELEASED, sr::FormatBytes(rs.size).c_str()), -1);
}

// ===== TabBar ============================================================================

int TabBar::OnPaint(Painter& p)
{
    const Theme& t = p.t;
    const int h = p.S(52);
    RECT line = { 0, h - 1, p.width, h };
    p.Fill(line, t.border);
    int x = p.S(16);
    const std::wstring brand = Str(IDS_APP_NAME);
    p.Text(x, (h - p.S(22)) / 2, 0, brand, t.bold, t.accent, DT_SINGLELINE);
    x += p.TextWidth(brand, t.bold) + p.S(24);
    const int by = (h - p.ButtonHeight()) / 2;
    if (hasResults)
    {
        const UINT ids[3] = { IDS_TAB_INSIGHTS, IDS_TAB_FOLDERS, IDS_TAB_INTERNALS };
        for (int i = 0; i < 3; ++i)
        {
            const std::wstring s = Str(ids[i]);
            const BtnStyle st = (i == selected && view == View::Results) ? BtnStyle::TabSelected : BtnStyle::Tab;
            RECT r = p.Button(TAB_BASE + i, x, by, s, st);
            x = r.right + p.S(4);
        }
    }
    int right = p.width - p.S(16);
    auto rightButton = [&](int id, UINT sid, BtnStyle st) {
        const std::wstring s = Str(sid);
        const int w = p.ButtonWidth(s, st);
        p.Button(id, right - w, by, s, st);
        right -= w + p.S(8);
    };
    if (view == View::Scanning) return h;
    if (view == View::Results) rightButton(BTN_HOME, IDS_BTN_SCAN_ANOTHER, BtnStyle::Normal);
    if (view == View::Results) rightButton(BTN_RESCAN, IDS_BTN_RESCAN, BtnStyle::Normal);
    if (view == View::Home && hasResults) rightButton(BTN_BACK, IDS_BTN_BACK_RESULTS, BtnStyle::Normal);
    return h;
}

void TabBar::OnButton(int id)
{
    App& app = App::Get();
    if (id >= TAB_BASE && id < TAB_BASE + 3) app.SelectTab(id - TAB_BASE);
    else if (id == BTN_HOME)
    {
        State& g = G();
        g.view = View::Home;
        g.home.error.clear();
        SendMessageW(g.hwnd, WM_SIZE, 0, 0);
        app.RefreshDrives();
    }
    else if (id == BTN_RESCAN)
    {
        if (app.Tree()) app.StartScan(app.Tree()->rootPath);
    }
    else if (id == BTN_BACK)
        app.SelectTab(G().tab);
}

// ===== StatusBar =========================================================================

int StatusBar::OnPaint(Painter& p)
{
    const Theme& t = p.t;
    RECT line = { 0, 0, p.width, 1 };
    p.Fill(line, t.border);
    const int rowH = p.S(20), pad = p.S(16);
    int x = pad, y = p.S(6);
    for (size_t i = 0; i < items.size(); ++i)
    {
        const int w = p.TextWidth(items[i], t.caption);
        if (x > pad && x + w > p.width - pad)
        {
            x = pad;
            y += rowH;
        }
        RECT r = { x, y, std::min(x + w + 2, p.width - pad), y + rowH };
        p.TextBox(r, items[i], t.caption, t.dim, DT_SINGLELINE | DT_VCENTER);
        x += w + pad;
        if (i + 1 < items.size() && x < p.width - pad)
        {
            RECT sep = { x - pad / 2, y + p.S(4), x - pad / 2 + 1, y + rowH - p.S(4) };
            p.Fill(sep, t.border);
        }
    }
    return y + rowH + p.S(6);
}

// ===== ToastBar ==========================================================================

int ToastBar::OnPaint(Painter& p)
{
    const Theme& t = p.t;
    RECT rc = { 0, 0, p.width, p.S(64) };
    HBRUSH nb = static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
    HPEN pen = CreatePen(PS_SOLID, 1, t.border);
    HGDIOBJ op = SelectObject(p.dc, pen), ob = SelectObject(p.dc, nb);
    Rectangle(p.dc, 0, 0, p.width, rc.bottom);
    SelectObject(p.dc, op);
    SelectObject(p.dc, ob);
    DeleteObject(pen);
    RECT accent = { 0, 0, p.S(4), rc.bottom };
    p.Fill(accent, t.accent);
    int right = p.width - p.S(12);
    const std::wstring dismiss = Str(IDS_TOAST_DISMISS);
    const int dw = p.ButtonWidth(dismiss, BtnStyle::Link);
    p.Button(2, right - dw, (rc.bottom - p.S(24)) / 2, dismiss, BtnStyle::Link);
    right -= dw + p.S(4);
    if (undoId >= 0)
    {
        const std::wstring undo = Str(IDS_TOAST_UNDO);
        const int uw = p.ButtonWidth(undo, BtnStyle::Normal);
        p.Button(1, right - uw, (rc.bottom - p.ButtonHeight()) / 2, undo, BtnStyle::Normal, !busy);
        right -= uw + p.S(8);
    }
    RECT tr = { p.S(16), p.S(8), right - p.S(8), rc.bottom - p.S(8) };
    p.TextBox(tr, text, t.font, t.text, DT_WORDBREAK | DT_VCENTER);
    return rc.bottom;
}

void ToastBar::OnButton(int id)
{
    State& g = G();
    if (id == 2)
    {
        KillTimer(g.hwnd, IDT_TOAST);
        g.toastVisible = false;
        ShowWindow(m_hwnd, SW_HIDE);
        return;
    }
    if (id != 1 || undoId < 0 || g.undoBusy) return;
    g.undoBusy = true;
    busy = true;
    text = Str(IDS_TOAST_UNDOING);
    KillTimer(g.hwnd, IDT_TOAST);
    Redraw();
    UndoJob* job = new UndoJob{ undoId, g.hwnd };
    HANDLE h = CreateThread(nullptr, 0, UndoMain, job, 0, nullptr);
    if (h) CloseHandle(h);
    else
    {
        delete job;
        g.undoBusy = false;
        busy = false;
    }
}

// ===== HomePage ==========================================================================

int HomePage::OnPaint(Painter& p)
{
    const Theme& t = p.t;
    App& app = App::Get();
    const int M = p.S(32);
    const int cw = std::max(p.S(320), std::min(p.width - 2 * M, p.S(1000)));
    const int x = std::max(M, (p.width - cw) / 2);
    int y = M;
    y += p.Text(x, y, cw, Str(IDS_HOME_TITLE), t.big, t.text) + p.S(6);
    y += p.Text(x, y, cw, Str(IDS_HOME_SUB), t.font, t.dim) + p.S(20);

    if (!error.empty())
    {
        const int th = p.TextHeight(cw - p.S(40), error, t.font);
        RECT r = { x, y, x + cw, y + th + p.S(28) };
        p.Round(r, p.S(8), Blend(t.card, t.danger, 12), t.danger);
        p.Text(x + p.S(20), y + p.S(14), cw - p.S(40), error, t.font, t.text);
        y = r.bottom + p.S(20);
    }

    // Actions
    const std::wstring choose = Str(IDS_HOME_CHOOSE);
    const int cwid = p.ButtonWidth(choose, BtnStyle::Primary);
    p.Button(1000, x, y, choose, BtnStyle::Primary);
    const std::wstring last = sr::Settings::Get().lastScanPath;
    if (!last.empty() && GetFileAttributesW(last.c_str()) != INVALID_FILE_ATTRIBUTES)
    {
        const std::wstring label = Strf(IDS_HOME_SCAN_AGAIN, ShortenPath(last, 48).c_str());
        p.Button(1001, x + cwid + p.S(8), y, label, BtnStyle::Normal);
    }
    y += p.ButtonHeight() + p.S(32);

    y += p.Text(x, y, cw, Str(IDS_HOME_DRIVES), t.title, t.text) + p.S(12);
    const std::vector<sr::DriveInfo>& drives = app.Drives();
    if (drives.empty())
    {
        const std::wstring title = Str(IDS_HOME_NODRIVES_T), body = Str(IDS_HOME_NODRIVES_B);
        const int bh = p.TextHeight(cw - p.S(48), body, t.font);
        RECT r = { x, y, x + cw, y + p.S(60) + bh };
        p.Round(r, p.S(8), t.card, t.border);
        p.Text(x + p.S(24), y + p.S(20), cw - p.S(48), title, t.bold, t.text);
        p.Text(x + p.S(24), y + p.S(48), cw - p.S(48), body, t.font, t.dim);
        y = r.bottom + p.S(24);
    }
    else
    {
        const int gap = p.S(16);
        const int cols = cw >= p.S(820) ? 2 : 1;
        const int cardW = (cw - gap * (cols - 1)) / cols;
        const int cardH = p.S(168);
        for (size_t i = 0; i < drives.size(); ++i)
        {
            const sr::DriveInfo& d = drives[i];
            const int col = static_cast<int>(i) % cols, row = static_cast<int>(i) / cols;
            const int cx = x + col * (cardW + gap), cy = y + row * (cardH + gap);
            RECT card = { cx, cy, cx + cardW, cy + cardH };
            p.Round(card, p.S(8), t.card, d.low ? t.danger : t.border);
            const int ix = cx + p.S(20), iw = cardW - p.S(40);
            const std::wstring name = (d.label.empty() ? Str(IDS_HOME_LOCAL_DISK) : d.label) + L" (" + DriveName(d) + L")";
            const int nw = p.TextWidth(name, t.bold);
            RECT nr = { ix, cy + p.S(16), ix + std::min(nw + 4, iw / 2 + p.S(40)), cy + p.S(38) };
            p.TextBox(nr, name, t.bold, t.text, DT_SINGLELINE);
            int chipX = nr.right + p.S(8);
            if (d.isSystem) chipX += p.Chip(chipX, cy + p.S(17), Str(IDS_HOME_SYSTEM), t.dim) + p.S(6);
            if (d.low) p.Chip(chipX, cy + p.S(17), Str(IDS_HOME_LOW), t.danger);
            if (!d.fileSystem.empty()) p.TextRight(cx + cardW - p.S(20), cy + p.S(18), d.fileSystem, t.caption, t.dim);
            const sr::u64 used = d.total > d.free ? d.total - d.free : 0;
            const double frac = d.total ? static_cast<double>(used) / static_cast<double>(d.total) : 0.0;
            RECT bar = { ix, cy + p.S(52), ix + iw, cy + p.S(62) };
            p.Bar(bar, frac, d.low ? t.danger : t.accent);
            p.Text(ix, cy + p.S(72), iw, Strf(IDS_HOME_USED, sr::FormatBytes(used).c_str(), sr::FormatBytes(d.total).c_str()), t.font, t.text, DT_SINGLELINE | DT_END_ELLIPSIS);
            p.Text(ix, cy + p.S(94), iw, SizeShare(d.free, d.total, DriveName(d)) + L" " + Str(IDS_HOME_FREE_WORD), t.font, d.low ? t.danger : t.dim, DT_SINGLELINE | DT_END_ELLIPSIS);
            const std::wstring scan = Str(IDS_HOME_SCAN);
            const int sw = p.ButtonWidth(scan, BtnStyle::Primary);
            p.Button(2000 + static_cast<int>(i), cx + cardW - p.S(20) - sw, cy + cardH - p.S(20) - p.ButtonHeight(), scan, BtnStyle::Primary);
            if (d.low)
            {
                RECT nr2 = { ix, cy + p.S(124), cx + cardW - p.S(20) - sw - p.S(16), cy + cardH - p.S(12) };
                p.TextBox(nr2, Str(IDS_HOME_RESCUE_NOTE), t.caption, t.dim, DT_WORDBREAK);
            }
        }
        const int rows = (static_cast<int>(drives.size()) + cols - 1) / cols;
        y += rows * (cardH + gap);
    }
    y += p.S(8);
    y += p.Text(x, y, cw, Str(IDS_HOME_PRIVACY), t.caption, t.dim) + M;
    return y;
}

void HomePage::OnButton(int id)
{
    App& app = App::Get();
    if (id == 1000) app.ChooseFolderAndScan();
    else if (id == 1001) app.StartScan(sr::Settings::Get().lastScanPath);
    else if (id >= 2000 && static_cast<size_t>(id - 2000) < app.Drives().size())
        app.StartScan(app.Drives()[static_cast<size_t>(id - 2000)].root);
}

// ===== ScanPage ==========================================================================

int ScanPage::OnPaint(Painter& p)
{
    const Theme& t = p.t;
    const int M = p.S(32);
    const int cw = std::max(p.S(320), std::min(p.width - 2 * M, p.S(760)));
    const int x = std::max(M, (p.width - cw) / 2);
    int y = std::max(M, p.S(72));
    y += p.Text(x, y, cw, Strf(IDS_SCAN_TITLE, root.c_str()), t.title, t.text) + p.S(6);
    y += p.Text(x, y, cw, cancelling ? Str(IDS_SCAN_CANCELLING) : Str(IDS_SCAN_SUB), t.font, t.dim) + p.S(20);

    RECT bar = { x, y, x + cw, y + p.S(8) };
    const bool determinate = driveUsed > 0 && progress.bytes > 0;
    if (determinate)
        p.Bar(bar, std::min(0.99, static_cast<double>(progress.bytes) / static_cast<double>(driveUsed)), t.accent);
    else
    {
        p.Round(bar, p.S(4), t.track);
        const int seg = cw / 4;
        const int pos = static_cast<int>((GetTickCount() / 8) % static_cast<DWORD>(cw + seg)) - seg;
        RECT s = { x + std::max(0, pos), y, x + std::min(cw, pos + seg), y + p.S(8) };
        if (s.right > s.left) p.Round(s, p.S(4), t.accent);
    }
    y += p.S(28);

    // Counters, each with context.
    const int colW = cw / 3;
    const std::wstring files = sr::FormatCount(progress.files), dirs = sr::FormatCount(progress.dirs), bytes = sr::FormatBytes(progress.bytes);
    const std::wstring labels[3] = { Str(IDS_SCAN_FILES), Str(IDS_SCAN_FOLDERS), Str(IDS_SCAN_SIZE) };
    const std::wstring values[3] = { files, dirs, bytes };
    for (int i = 0; i < 3; ++i)
    {
        RECT vr = { x + i * colW, y, x + (i + 1) * colW - p.S(8), y + p.S(44) };
        p.TextBox(vr, values[i], t.big, t.text, DT_SINGLELINE);
        p.Text(x + i * colW, y + p.S(46), colW - p.S(8), labels[i], t.caption, t.dim, DT_SINGLELINE);
    }
    y += p.S(80);
    if (driveUsed > 0 && progress.bytes > 0)
        y += p.Text(x, y, cw, Strf(IDS_SCAN_CONTEXT, sr::FormatPercent(std::min(1.0, static_cast<double>(progress.bytes) / static_cast<double>(driveUsed))).c_str(),
                                   sr::FormatBytes(driveUsed).c_str()), t.font, t.text) + p.S(8);
    y += p.Text(x, y, cw, Strf(IDS_SCAN_ELAPSED, FormatDuration(GetTickCount() - startTick).c_str()), t.font, t.dim) + p.S(12);
    RECT pr = { x, y, x + cw, y + p.S(20) };
    p.TextBox(pr, progress.currentPath.empty() ? Str(IDS_SCAN_STARTING) : progress.currentPath, t.caption, t.dim, DT_SINGLELINE | DT_PATH_ELLIPSIS);
    y += p.S(36);
    if (rescue)
    {
        std::wstring note = lowPriority ? Str(IDS_SCAN_RESCUE_LOW) : Str(IDS_SCAN_RESCUE);
        if (budget) note += L" " + Strf(IDS_SCAN_BUDGET, sr::FormatBytes(budget).c_str());
        const int nh = p.TextHeight(cw - p.S(32), note, t.font);
        RECT r = { x, y, x + cw, y + nh + p.S(24) };
        p.Round(r, p.S(8), Blend(t.card, t.accent, 8), Blend(t.card, t.accent, 40));
        p.Text(x + p.S(16), y + p.S(12), cw - p.S(32), note, t.font, t.text);
        y = r.bottom + p.S(24);
    }
    const std::wstring cancel = Str(IDS_BTN_CANCEL);
    p.Button(1, x, y, cancel, BtnStyle::Normal, !cancelling);
    return y + p.ButtonHeight() + M;
}

void ScanPage::OnButton(int id)
{
    State& g = G();
    if (id == 1 && g.scanner && !cancelling)
    {
        cancelling = true;
        g.scanner->Cancel();
        Redraw();
    }
}

// ===== Analysis thread ===================================================================

DWORD WINAPI AnalysisMain(LPVOID param)
{
    std::unique_ptr<AnalysisJob> job(static_cast<AnalysisJob*>(param));
    auto* report = new sr::InsightsReport(sr::Analyze(*job->tree, job->options));
    if (job->saveSnapshot && !job->snapshotDir.empty()) sr::SaveSnapshot(*job->tree, job->snapshotDir);
    if (!PostMessageW(job->notify, WM_SR_ANALYSIS, static_cast<WPARAM>(job->generation), reinterpret_cast<LPARAM>(report))) delete report;
    return 0;
}

}

// ===== App ===============================================================================

App& App::Get()
{
    static App app;
    return app;
}

const sr::DriveInfo* App::TreeDrive() const
{
    if (!m_tree) return nullptr;
    const std::wstring vol = sr::VolumeOf(m_tree->rootPath);
    for (const sr::DriveInfo& d : m_drives)
        if (_wcsicmp(d.root.c_str(), vol.c_str()) == 0) return &d;
    return nullptr;
}

namespace
{
void UpdateStatus()
{
    State& g = G();
    App& app = App::Get();
    std::vector<std::wstring>& s = g.status.items;
    s.clear();
    const sr::ScanTree* tree = app.Tree();
    if (g.view == View::Results && tree)
    {
        s.push_back(Strf(IDS_ST_COUNTS, sr::FormatCount(tree->fileCount).c_str(), sr::FormatCount(tree->dirCount).c_str()));
        s.push_back(Strf(IDS_ST_TIME, FormatDuration(tree->scanMillis).c_str()));
        s.push_back(Strf(IDS_ST_PEAK, sr::FormatBytes(sr::PeakWorkingSet()).c_str()));
        s.push_back(Strf(IDS_ST_TREE, sr::FormatBytes(tree->MemoryUsed()).c_str()));
        if (tree->foldedFiles)
            s.push_back(Strf(IDS_ST_FOLDED, sr::FormatCount(tree->foldedFiles).c_str()));
        if (tree->errorCount)
            s.push_back(Strf(IDS_ST_ERRORS, sr::FormatCount(tree->errorCount).c_str()));
    }
    else if (g.view == View::Scanning)
        s.push_back(Str(IDS_ST_SCANNING));
    else
        s.push_back(Str(IDS_ST_READY));
    const sr::Settings& st = sr::Settings::Get();
    s.push_back(st.WritesSuppressed() ? Str(IDS_ST_SETTINGS_MEM) : Strf(IDS_ST_SETTINGS, st.Location().c_str()));
    g.status.Redraw();
}

void Layout()
{
    State& g = G();
    if (!g.hwnd) return;
    RECT rc;
    GetClientRect(g.hwnd, &rc);
    const int w = rc.right, h = rc.bottom;
    if (w <= 0 || h <= 0) return;
    int y = 0;
    const bool bannerOn = App::Get().AnyDriveLow();
    int bh = bannerOn ? g.banner.Measure(w) : 0;
    ShowWindow(g.banner.Hwnd(), bannerOn ? SW_SHOW : SW_HIDE);
    MoveWindow(g.banner.Hwnd(), 0, y, w, bh, TRUE);
    y += bh;
    const int th = T().S(52);
    MoveWindow(g.tabbar.Hwnd(), 0, y, w, th, TRUE);
    y += th;
    const int sh = g.status.Measure(w);
    MoveWindow(g.status.Hwnd(), 0, h - sh, w, sh, TRUE);
    const int ch = std::max(0, h - sh - y);

    HWND pages[5] = { g.home.Hwnd(), g.scanPage.Hwnd(), g.insights.Hwnd(), g.folders.Hwnd(), g.internals.Hwnd() };
    HWND active = ActivePage();
    for (HWND pg : pages)
    {
        if (pg == active) MoveWindow(pg, 0, y, w, ch, TRUE);
        ShowWindow(pg, pg == active ? SW_SHOW : SW_HIDE);
    }
    // Toast floats above the status bar, bottom-left aligned with the content margin.
    const int tw = std::min(w - 2 * T().S(16), T().S(680));
    const int toastH = T().S(64);
    SetWindowPos(g.toast.Hwnd(), HWND_TOP, T().S(16), h - sh - toastH - T().S(16), tw, toastH,
                 (g.toastVisible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
    g.tabbar.hasResults = App::Get().Tree() != nullptr;
    g.tabbar.view = g.view;
    g.tabbar.selected = g.tab;
    g.tabbar.Redraw();
}

void ApplyTheme(bool notifyPages)
{
    State& g = G();
    ThemeRefresh(g.hwnd);
    StyleTopLevel(g.hwnd);
    if (notifyPages)
    {
        g.folders.Restyle();
        g.internals.Restyle();
        for (HWND h : { g.banner.Hwnd(), g.tabbar.Hwnd(), g.status.Hwnd(), g.toast.Hwnd(), g.home.Hwnd(), g.scanPage.Hwnd(), g.insights.Hwnd() })
            InvalidateRect(h, nullptr, FALSE);
    }
    InvalidateRect(g.hwnd, nullptr, TRUE);
    Layout();
}

void ShowTab(int tab)
{
    State& g = G();
    g.view = View::Results;
    g.tab = tab;
    Layout();
    UpdateStatus();
    SetFocus(ActivePage());
}

// Picks where scan snapshots may be written: never on a low drive.
std::wstring ChooseSnapshotDir(const std::wstring& scanRoot)
{
    sr::Settings& st = sr::Settings::Get();
    std::wstring dir;
    if (!st.WritesSuppressed() && !st.Location().empty())
        dir = sr::ParentPath(st.Location());
    else
    {
        const std::wstring wv = sr::PickWorkVolume(sr::VolumeOf(scanRoot), st.rescue);
        if (!wv.empty()) dir = wv + L"SpaceRescue-Data\\";
    }
    if (dir.empty()) return L"";
    // Last safety check: the chosen volume must not be low.
    const std::wstring vol = sr::VolumeOf(dir);
    for (const sr::DriveInfo& d : App::Get().Drives())
        if (d.low && _wcsicmp(d.root.c_str(), vol.c_str()) == 0) return L"";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

}

void App::EndAnalysis()
{
    State& g = G();
    if (g.analysisThread)
    {
        WaitForSingleObject(g.analysisThread, INFINITE);
        CloseHandle(g.analysisThread);
        g.analysisThread = nullptr;
    }
    MSG msg;
    while (PeekMessageW(&msg, g.hwnd, WM_SR_ANALYSIS, WM_SR_ANALYSIS, PM_REMOVE))
        delete reinterpret_cast<sr::InsightsReport*>(msg.lParam);
    m_analysisRunning = false;
    ++g.generation;
}

void App::BeginAnalysis(bool saveSnapshot)
{
    State& g = G();
    if (!m_tree) return;
    AnalysisJob* job = new AnalysisJob();
    job->tree = m_tree.get();
    job->options.downloadsAgeDays = sr::Settings::Get().downloadsAgeDays;
    m_snapshotDir = ChooseSnapshotDir(m_tree->rootPath);
    job->snapshotDir = m_snapshotDir;
    job->options.snapshotDir = m_snapshotDir;
    job->saveSnapshot = saveSnapshot;
    job->notify = g.hwnd;
    job->generation = g.generation;
    g.analysisThread = CreateThread(nullptr, 0, AnalysisMain, job, 0, nullptr);
    if (!g.analysisThread)
    {
        delete job;
        return;
    }
    m_analysisRunning = true;
}

void App::RefreshDrives()
{
    State& g = G();
    const sr::RescueConfig& cfg = sr::Settings::Get().rescue;
    m_drives = sr::EnumerateDrives(cfg);
    m_anyLow = false;
    std::wstring exclude;
    for (const sr::DriveInfo& d : m_drives)
        if (d.low)
        {
            m_anyLow = true;
            if (exclude.empty() || d.isSystem) exclude = d.root;
        }
    if (m_tree) exclude = sr::VolumeOf(m_tree->rootPath);
    m_workVolume = sr::PickWorkVolume(exclude, cfg);
    g.banner.reserve = sr::QueryReserve();
    if (g.hwnd)
    {
        g.home.Redraw();
        g.banner.Redraw();
        g.insights.Redraw();
        Layout();
    }
}

void App::ChooseFolderAndScan()
{
    BROWSEINFOW bi = {};
    bi.hwndOwner = m_hwnd;
    const std::wstring title = Str(IDS_CHOOSE_FOLDER_PROMPT);
    bi.lpszTitle = title.c_str();
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return;
    wchar_t path[MAX_PATH] = {};
    if (SHGetPathFromIDListW(pidl, path)) StartScan(path);
    CoTaskMemFree(pidl);
}

void App::StartScan(const std::wstring& pathIn)
{
    State& g = G();
    if (g.scanner) return;
    std::wstring path = pathIn;
    while (path.size() > 3 && (path.back() == L'\\' || path.back() == L'/')) path.pop_back();
    const DWORD attr = GetFileAttributesW(path.c_str());
    if (path.empty() || attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY))
    {
        g.home.error = Strf(IDS_ERR_PATH, path.c_str());
        g.view = View::Home;
        Layout();
        UpdateStatus();
        return;
    }
    // Release the previous result (workers read the tree, so stop them first).
    g.insights.StopBackground();
    g.internals.StopBackground();
    EndAnalysis();
    m_report.reset();
    m_tree.reset();
    g.folders.Reload();
    g.internals.Reload();
    g.insights.Reset();
    RefreshDrives();

    sr::Settings& st = sr::Settings::Get();
    sr::ScanOptions opt;
    opt.root = path;
    const std::wstring vol = sr::VolumeOf(path);
    bool targetLow = false;
    sr::u64 driveUsed = 0;
    for (const sr::DriveInfo& d : m_drives)
        if (_wcsicmp(d.root.c_str(), vol.c_str()) == 0)
        {
            targetLow = d.low;
            if (path.size() <= 3) driveUsed = d.total > d.free ? d.total - d.free : 0;
        }
    opt.lowPriority = targetLow;
    opt.memoryBudget = st.memoryBudgetMB ? st.memoryBudgetMB * sr::MiB : (m_anyLow ? kRescueBudget : 0);

    g.scanner.reset(new sr::Scanner());
    if (!g.scanner->Start(opt, m_hwnd, WM_SR_SCANDONE))
    {
        g.scanner.reset();
        g.home.error = Strf(IDS_ERR_SCAN_START, path.c_str());
        g.view = View::Home;
        Layout();
        return;
    }
    g.scanPage.root = path;
    g.scanPage.progress = sr::ScanProgress();
    g.scanPage.driveUsed = driveUsed;
    g.scanPage.rescue = m_anyLow;
    g.scanPage.lowPriority = targetLow;
    g.scanPage.budget = opt.memoryBudget;
    g.scanPage.startTick = GetTickCount();
    g.scanPage.cancelling = false;
    g.view = View::Scanning;
    g.home.error.clear();
    st.lastScanPath = path;
    if (!st.WritesSuppressed()) st.Save();
    Layout();
    UpdateStatus();
    SetFocus(g.scanPage.Hwnd());
}

void App::SelectTab(int tab)
{
    if (!m_tree) return;
    ShowTab(std::max(0, std::min(2, tab)));
}

void App::ShowFileInInternals(const std::wstring& path)
{
    SelectTab(2);
    G().internals.ShowFile(path);
}

void App::ShowNodeInFolders(sr::u32 node)
{
    SelectTab(1);
    G().folders.Select(node);
}

void App::OpenReview(const ReviewRequest& request)
{
    State& g = G();
    if (!m_tree || g.modalOpen) return;
    g.modalOpen = true;
    ShowReviewDialog(m_hwnd, request);
    g.modalOpen = false;
    if (g.themePending)
    {
        g.themePending = false;
        ApplyTheme(true);
    }
    SetFocus(ActivePage());
}

void App::ShowToast(const std::wstring& text, int undoId)
{
    State& g = G();
    g.toast.text = text;
    g.toast.undoId = (undoId >= 0 && sr::ActionManager::Get().CanUndo(undoId)) ? undoId : -1;
    g.toast.busy = false;
    g.toastVisible = true;
    SetTimer(g.hwnd, IDT_TOAST, kToastMillis, nullptr);
    Layout();
    g.toast.Redraw();
}

void App::RefreshAfterChange()
{
    State& g = G();
    g.insights.StopBackground();
    EndAnalysis();
    m_report.reset();
    g.folders.Reload();
    g.internals.Reload();
    RefreshDrives();
    BeginAnalysis(false);
    g.insights.Redraw();
    UpdateStatus();
}

void App::OnActionApplied(const sr::ActionResult& result, const std::vector<sr::u32>& detached)
{
    // Detach in reverse insertion order; nested nodes were already filtered by the dialog.
    if (m_tree)
        for (sr::u32 n : detached) m_tree->Detach(n);
    std::wstring text = result.summary;
    if (result.kind == sr::ActionKind::Recycle) text += L" " + Str(IDS_TOAST_RECYCLE_NOTE);
    ShowToast(text, result.undoId);
    RefreshAfterChange();
}

// ===== Window procedure ==================================================================

void App::OnScanDone()
{
    State& g = G();
    if (!g.scanner) return;
    std::unique_ptr<sr::ScanTree> tree = g.scanner->TakeResult();
    g.scanner.reset();
    if (!tree || tree->cancelled)
    {
        g.view = View::Home;
        const bool cancelled = tree && tree->cancelled;
        if (!cancelled) g.home.error = Str(IDS_ERR_SCAN_FAILED);
        RefreshDrives();
        Layout();
        UpdateStatus();
        if (cancelled) ShowToast(Str(IDS_SCAN_WAS_CANCELLED), -1);
        SetFocus(g.home.Hwnd());
        return;
    }
    m_tree = std::move(tree);
    RefreshDrives();
    g.insights.Reset();
    g.folders.Reload();
    g.internals.Reload();
    BeginAnalysis(true);
    ShowTab(0);
}

void App::OnAnalysisDone(int generation, sr::InsightsReport* report)
{
    State& g = G();
    if (g.analysisThread)
    {
        WaitForSingleObject(g.analysisThread, INFINITE);
        CloseHandle(g.analysisThread);
        g.analysisThread = nullptr;
    }
    if (generation == g.generation && m_tree)
        m_report.reset(report);
    else
        delete report;
    m_analysisRunning = false;
    g.insights.Redraw();
    UpdateStatus();
}

void App::OnUndoDone(sr::ActionResult* result)
{
    State& g = G();
    std::unique_ptr<sr::ActionResult> r(result);
    g.undoBusy = false;
    ShowToast(r->summary.empty() ? Str(IDS_TOAST_UNDONE) : r->summary, -1);
    // Files came back: the scan no longer matches the disk, so scan the same place again.
    if (!r->done.empty() && m_tree) StartScan(m_tree->rootPath);
}

bool App::PreTranslate(MSG& msg)
{
    if (msg.message != WM_KEYDOWN || (msg.hwnd != m_hwnd && !IsChild(m_hwnd, msg.hwnd))) return false;
    State& g = G();
    const bool ctrl = GetKeyState(VK_CONTROL) < 0;
    if (ctrl && msg.wParam >= '1' && msg.wParam <= '3' && m_tree && !g.scanner)
    {
        SelectTab(static_cast<int>(msg.wParam - '1'));
        return true;
    }
    if (ctrl && msg.wParam == VK_TAB && m_tree && !g.scanner)
    {
        SelectTab((g.tab + (GetKeyState(VK_SHIFT) < 0 ? 2 : 1)) % 3);
        return true;
    }
    if (ctrl && msg.wParam == 'O' && !g.scanner)
    {
        ChooseFolderAndScan();
        return true;
    }
    if (msg.wParam == VK_F5 && m_tree && !g.scanner)
    {
        StartScan(m_tree->rootPath);
        return true;
    }
    return false;
}

LRESULT CALLBACK App::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    App* self;
    if (msg == WM_NCCREATE)
    {
        self = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
        G().hwnd = hwnd;
    }
    else
        self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
    return self->HandleMessage(msg, wp, lp);
}

LRESULT App::HandleMessage(UINT msg, WPARAM wp, LPARAM lp)
{
    State& g = G();
    switch (msg)
    {
    case WM_CREATE:
        ThemeRefresh(m_hwnd);
        StyleTopLevel(m_hwnd);
        g.banner.Create(m_hwnd, false, false);
        g.tabbar.Create(m_hwnd, false, true);
        g.status.Create(m_hwnd, false, false);
        g.toast.Create(m_hwnd, false, true);
        g.home.Create(m_hwnd, true, true);
        g.scanPage.Create(m_hwnd, true, true);
        g.insights.Create(m_hwnd, true, true);
        g.folders.Create(m_hwnd);
        g.internals.Create(m_hwnd);
        ShowWindow(g.tabbar.Hwnd(), SW_SHOW);
        ShowWindow(g.status.Hwnd(), SW_SHOW);
        RefreshDrives();
        SetTimer(m_hwnd, IDT_TICK, 100, nullptr);
        UpdateStatus();
        return 0;
    case WM_ERASEBKGND:
    {
        RECT rc;
        GetClientRect(m_hwnd, &rc);
        FillRect(reinterpret_cast<HDC>(wp), &rc, T().bgBrush);
        return 1;
    }
    case WM_SIZE:
        if (wp != SIZE_MINIMIZED) Layout();
        return 0;
    case WM_GETMINMAXINFO:
    {
        MINMAXINFO* mm = reinterpret_cast<MINMAXINFO*>(lp);
        mm->ptMinTrackSize.x = T().S(760);
        mm->ptMinTrackSize.y = T().S(520);
        return 0;
    }
    case WM_DPICHANGED:
    {
        const RECT* r = reinterpret_cast<const RECT*>(lp);
        SetWindowPos(m_hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        if (g.modalOpen) g.themePending = true;
        else ApplyTheme(true);
        return 0;
    }
    case WM_SETTINGCHANGE:
        if (lp && wcscmp(reinterpret_cast<const wchar_t*>(lp), L"ImmersiveColorSet") == 0)
        {
            if (g.modalOpen) g.themePending = true;
            else ApplyTheme(true);
        }
        return 0;
    case WM_SETFOCUS:
        SetFocus(ActivePage());
        return 0;
    case WM_ACTIVATEAPP:
        if (wp && !g.scanner) RefreshDrives();
        return 0;
    case WM_TIMER:
        if (wp == IDT_TICK)
        {
            ++g.tickCount;
            if (g.scanner && g.view == View::Scanning)
            {
                g.scanPage.progress = g.scanner->Progress();
                g.scanPage.Redraw();
            }
            else if (g.tickCount % 5 == 0)
            {
                g.insights.Tick();
                g.internals.Tick();
            }
        }
        else if (wp == IDT_TOAST)
        {
            KillTimer(m_hwnd, IDT_TOAST);
            if (!g.undoBusy)
            {
                g.toastVisible = false;
                ShowWindow(g.toast.Hwnd(), SW_HIDE);
            }
        }
        return 0;
    case WM_SR_START:
        if (!g.pendingScan.empty())
        {
            const std::wstring p = g.pendingScan;
            g.pendingScan.clear();
            StartScan(p);
        }
        return 0;
    case WM_SR_SCANDONE:
        OnScanDone();
        return 0;
    case WM_SR_ANALYSIS:
        OnAnalysisDone(static_cast<int>(wp), reinterpret_cast<sr::InsightsReport*>(lp));
        return 0;
    case WM_SR_DUPDONE:
        g.insights.OnDuplicatesDone(reinterpret_cast<sr::CategoryResult*>(lp));
        return 0;
    case WM_SR_UNDODONE:
        OnUndoDone(reinterpret_cast<sr::ActionResult*>(lp));
        return 0;
    case WM_SR_KEY:
    case WM_COMMAND:
        return 0;
    case WM_CLOSE:
        if (g.scanner)
        {
            g.scanner->Cancel();
            g.scanner.reset();
        }
        g.insights.StopBackground();
        g.internals.StopBackground();
        EndAnalysis();
        DestroyWindow(m_hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(m_hwnd, IDT_TICK);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}

int App::Run(HINSTANCE instance, int show, const std::wstring& scanPath)
{
    State state;
    S_ = &state;
    state.pendingScan = scanPath;
    ThemeRefresh(nullptr);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP));
    wc.hIconSm = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP));
    wc.lpszClassName = kMainClass;
    RegisterClassExW(&wc);

    const Theme& t = T();
    m_hwnd = CreateWindowExW(0, kMainClass, Str(IDS_APP_NAME).c_str(), WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                             CW_USEDEFAULT, CW_USEDEFAULT, t.S(1180), t.S(780), nullptr, nullptr, instance, this);
    if (!m_hwnd)
    {
        S_ = nullptr;
        return 1;
    }
    ShowWindow(m_hwnd, show);
    UpdateWindow(m_hwnd);
    Layout();
    SetFocus(ActivePage());
    if (!scanPath.empty()) PostMessageW(m_hwnd, WM_SR_START, 0, 0);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        if (PreTranslate(msg)) continue;
        if (IsDialogMessage(m_hwnd, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    S_ = nullptr;
    return static_cast<int>(msg.wParam);
}
}
