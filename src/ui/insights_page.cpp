// SpaceRescue UI - SPDX-License-Identifier: GPL-3.0-or-later

#include "insights_page.h"

namespace ui
{
namespace
{
enum
{
    ID_DUP_FIND = 1, ID_DUP_CANCEL = 2, ID_DUP_REVIEW = 3, ID_CLEANMGR = 4,
    ID_HOG_FOLDER_REVIEW = 100, ID_HOG_FOLDER_SHOW = 120, ID_HOG_FILE_REVIEW = 140, ID_HOG_FILE_SHOW = 160,
    ID_CAT_REVIEW = 300, ID_STALE_REVIEW = 400
};
const sr::u64 kDupMinSize = 1 * sr::MiB;

struct RowBtn
{
    int id;
    std::wstring text;
    BtnStyle style;
    bool enabled;
};

// One row of a list card: title / two detail lines on the left, size + share and buttons on the right.
void DrawRow(Painter& p, int x, int y, int w, int h, bool separator, const std::wstring& title,
             const std::wstring& line2, const std::wstring& line3, const std::wstring& size,
             const std::wstring& share, const std::vector<RowBtn>& btns, COLORREF sizeColor,
             const std::wstring& chip = std::wstring(), COLORREF chipColor = 0)
{
    const Theme& t = p.t;
    if (separator)
    {
        RECT sep = { x + p.S(16), y, x + w - p.S(16), y + 1 };
        p.Fill(sep, t.border);
    }
    int right = x + w - p.S(16);
    for (auto it = btns.rbegin(); it != btns.rend(); ++it)
    {
        const int bw = p.ButtonWidth(it->text, it->style);
        p.Button(it->id, right - bw, y + (h - p.ButtonHeight()) / 2, it->text, it->style, it->enabled);
        right -= bw + p.S(8);
    }
    const int sizeW = p.S(150);
    right -= p.S(8);
    const int sizeLeft = right - sizeW;
    if (!size.empty())
    {
        RECT r1 = { sizeLeft, y + h / 2 - p.S(20), right, y + h / 2 };
        p.TextBox(r1, size, t.bold, sizeColor, DT_RIGHT | DT_SINGLELINE | DT_BOTTOM);
        RECT r2 = { sizeLeft, y + h / 2 + p.S(1), right, y + h / 2 + p.S(21) };
        p.TextBox(r2, share, t.caption, t.dim, DT_RIGHT | DT_SINGLELINE);
    }
    const int tx = x + p.S(16);
    const int tw = std::max(p.S(80), sizeLeft - p.S(16) - tx);
    const int titleW = p.TextWidth(title, t.bold);
    int ty = y + p.S(10);
    RECT tr = { tx, ty, tx + tw, ty + p.S(22) };
    p.TextBox(tr, title, t.bold, t.text, DT_SINGLELINE);
    if (!chip.empty() && titleW + p.S(100) < tw) p.Chip(tx + titleW + p.S(10), ty + p.S(1), chip, chipColor);
    ty += p.S(22);
    if (!line2.empty())
    {
        const bool two = line3.empty() && h >= p.S(76);
        RECT r = { tx, ty, tx + tw, ty + (two ? p.S(38) : p.S(20)) };
        p.TextBox(r, line2, t.font, t.dim, two ? DT_WORDBREAK : DT_SINGLELINE);
        ty += two ? p.S(38) : p.S(20);
    }
    if (!line3.empty())
    {
        RECT r = { tx, ty, tx + tw, ty + p.S(18) };
        p.TextBox(r, line3, t.caption, t.dim, DT_SINGLELINE | DT_PATH_ELLIPSIS);
    }
}

DWORD WINAPI DupMain(LPVOID param);

struct DupJob
{
    const sr::ScanTree* tree;
    volatile bool* cancel;
    HWND notify;
};
}

std::wstring InsightsPage::DriveLabel() const
{
    const App& app = App::Get();
    const sr::DriveInfo* d = app.TreeDrive();
    const sr::ScanTree* tree = app.Tree();
    if (!tree) return L"";
    const std::wstring letter = d ? d->root.substr(0, 2) : sr::VolumeOf(tree->rootPath).substr(0, 2);
    return letter;
}

sr::u64 InsightsPage::ShareTotal() const
{
    const App& app = App::Get();
    const sr::DriveInfo* d = app.TreeDrive();
    if (d && d->total) return d->total;
    const sr::ScanTree* tree = app.Tree();
    return tree ? tree->At(0).size : 0;
}

void InsightsPage::Reset()
{
    StopBackground();
    m_dup.reset();
    m_dupState = DupIdle;
    ScrollToTop();
    Redraw();
}

void InsightsPage::StopBackground()
{
    if (m_dupThread)
    {
        m_dupCancel = true;
        WaitForSingleObject(m_dupThread, INFINITE);
        CloseHandle(m_dupThread);
        m_dupThread = nullptr;
        // Drop any result that is already queued for this window.
        MSG msg;
        HWND parent = m_hwnd ? GetParent(m_hwnd) : nullptr;
        while (parent && PeekMessageW(&msg, parent, WM_SR_DUPDONE, WM_SR_DUPDONE, PM_REMOVE))
            delete reinterpret_cast<sr::CategoryResult*>(msg.lParam);
        m_dupState = DupIdle;
    }
}

void InsightsPage::Tick()
{
    if (m_dupState == DupRunning || App::Get().Analyzing()) Redraw();
}

namespace
{
DWORD WINAPI DupMain(LPVOID param)
{
    std::unique_ptr<DupJob> job(static_cast<DupJob*>(param));
    sr::CategoryResult* r = new sr::CategoryResult(sr::FindDuplicates(*job->tree, kDupMinSize, job->cancel));
    if (!PostMessageW(job->notify, WM_SR_DUPDONE, 0, reinterpret_cast<LPARAM>(r))) delete r;
    return 0;
}
}

void InsightsPage::StartDuplicates()
{
    if (m_dupThread || !App::Get().Tree()) return;
    m_dupCancel = false;
    m_dup.reset();
    m_dupState = DupRunning;
    m_dupStart = GetTickCount();
    DupJob* job = new DupJob{ App::Get().Tree(), &m_dupCancel, GetParent(m_hwnd) };
    m_dupThread = CreateThread(nullptr, 0, DupMain, job, 0, nullptr);
    if (!m_dupThread)
    {
        delete job;
        m_dupState = DupIdle;
    }
    Redraw();
}

void InsightsPage::OnDuplicatesDone(sr::CategoryResult* result)
{
    std::unique_ptr<sr::CategoryResult> r(result);
    if (m_dupThread)
    {
        WaitForSingleObject(m_dupThread, INFINITE);
        CloseHandle(m_dupThread);
        m_dupThread = nullptr;
    }
    if (m_dupCancel) m_dupState = DupCancelled;
    else
    {
        m_dup = std::move(r);
        m_dupState = DupDone;
    }
    Redraw();
}

void InsightsPage::Review(const std::wstring& title, const std::wstring& blurb, const std::vector<sr::u32>& nodes, bool access)
{
    ReviewRequest req;
    req.title = title;
    req.blurb = blurb;
    req.nodes = nodes;
    req.useAccessTime = access;
    App::Get().OpenReview(req);
}

void InsightsPage::OnButton(int id)
{
    App& app = App::Get();
    const sr::InsightsReport* rep = app.Report();
    const sr::ScanTree* tree = app.Tree();
    if (!tree) return;
    switch (id)
    {
    case ID_DUP_FIND: StartDuplicates(); return;
    case ID_DUP_CANCEL: m_dupCancel = true; Redraw(); return;
    case ID_CLEANMGR:
        ShellExecuteW(m_hwnd, L"open", L"cleanmgr.exe", nullptr, nullptr, SW_SHOWNORMAL);
        return;
    case ID_DUP_REVIEW:
        if (m_dup) Review(m_dup->title.empty() ? Str(IDS_INS_SEC_DUP) : m_dup->title, m_dup->explanation, m_dup->nodes, false);
        return;
    }
    if (!rep) return;
    auto hogNode = [&](const std::vector<sr::Hog>& v, int base) -> sr::u32 {
        const int i = id - base;
        return i >= 0 && i < static_cast<int>(v.size()) ? v[static_cast<size_t>(i)].node : sr::NoNode;
    };
    struct { int review, show; const std::vector<sr::Hog>* v; bool isFolder; } groups[] = {
        { ID_HOG_FOLDER_REVIEW, ID_HOG_FOLDER_SHOW, &rep->topFolders, true },
        { ID_HOG_FILE_REVIEW, ID_HOG_FILE_SHOW, &rep->topFiles, false } };
    for (const auto& g : groups)
    {
        sr::u32 n = hogNode(*g.v, g.review);
        if (n != sr::NoNode)
        {
            std::vector<sr::u32> nodes(1, n);
            ReviewRequest req;
            req.title = tree->Name(n);
            req.blurb = (*g.v)[static_cast<size_t>(id - g.review)].explanation;
            req.nodes = nodes;
            req.preselect = true;
            app.OpenReview(req);
            return;
        }
        n = hogNode(*g.v, g.show);
        if (n != sr::NoNode) { app.ShowNodeInFolders(n); return; }
    }
    if (id >= ID_CAT_REVIEW && id < ID_CAT_REVIEW + static_cast<int>(sr::Category::Count))
    {
        const size_t c = static_cast<size_t>(id - ID_CAT_REVIEW);
        if (c < rep->categories.size())
            Review(rep->categories[c].title, rep->categories[c].explanation, rep->categories[c].nodes, false);
        return;
    }
    if (id >= ID_STALE_REVIEW && id < ID_STALE_REVIEW + sr::StaleResult::Buckets)
    {
        const int b = id - ID_STALE_REVIEW;
        std::vector<sr::u32> nodes;
        for (const sr::StaleGroup& g : rep->stale.groups[b])
            nodes.insert(nodes.end(), g.files.begin(), g.files.end());
        Review(Strf(IDS_INS_STALE_ROW, rep->stale.months[b]), rep->stale.signalReason, nodes,
               rep->stale.signal == sr::StaleSignal::LastAccess);
    }
}

int InsightsPage::Section(Painter& p, int x, int y, const std::wstring& title, const std::wstring& sub, int w)
{
    int h = p.Text(x, y, w, title, p.t.title, p.t.text);
    if (!sub.empty()) h += p.S(2) + p.Text(x, y + h + p.S(2), w, sub, p.t.font, p.t.dim);
    return y + h + p.S(12);
}

int InsightsPage::EmptyCard(Painter& p, int x, int y, int w, const std::wstring& title, const std::wstring& body)
{
    const int tw = w - p.S(48);
    const int bodyH = p.TextHeight(tw, body, p.t.font);
    const int h = p.S(20) + p.S(24) + p.S(6) + bodyH + p.S(20);
    RECT card = { x, y, x + w, y + h };
    p.Round(card, p.S(8), p.t.card, p.t.border);
    p.Text(x + p.S(24), y + p.S(20), tw, title, p.t.bold, p.t.text);
    p.Text(x + p.S(24), y + p.S(50), tw, body, p.t.font, p.t.dim);
    return y + h;
}

int InsightsPage::Hero(Painter& p, int x, int y, int w)
{
    const Theme& t = p.t;
    App& app = App::Get();
    const sr::ScanTree* tree = app.Tree();
    const sr::InsightsReport* rep = app.Report();
    const sr::DriveInfo* d = app.TreeDrive();
    const int h = p.S(208);
    RECT card = { x, y, x + w, y + h };
    p.Round(card, p.S(8), t.card, t.border);

    int tx = x + p.S(24);
    if (d && d->total)
    {
        const double used = static_cast<double>(d->total - std::min(d->free, d->total)) / static_cast<double>(d->total);
        const int r = p.S(64), cx = x + p.S(24) + r, cy = y + h / 2;
        p.Ring(cx, cy, r, p.S(14), used, d->low ? t.danger : t.accent, t.track, t.card);
        RECT c1 = { cx - r, cy - p.S(26), cx + r, cy + p.S(6) };
        p.TextBox(c1, sr::Fmt(L"%d%%", static_cast<int>(used * 100.0 + 0.5)), t.title, t.text, DT_CENTER | DT_SINGLELINE);
        RECT c2 = { cx - r, cy + p.S(4), cx + r, cy + p.S(24) };
        p.TextBox(c2, Str(IDS_INS_RING_USED), t.caption, t.dim, DT_CENTER | DT_SINGLELINE);
        tx = cx + r + p.S(32);
    }
    const int tw = x + w - p.S(24) - tx;
    int ty = y + p.S(20);
    std::wstring label = d ? (d->label.empty() ? Str(IDS_HOME_LOCAL_DISK) : d->label) + L" (" + d->root.substr(0, 2) + L")" : tree->rootPath;
    if (d && _wcsicmp(tree->rootPath.c_str(), d->root.c_str()) != 0) label = Strf(IDS_INS_FOLDER_OF, tree->rootPath.c_str(), d->root.substr(0, 2).c_str());
    RECT lr = { tx, ty, tx + tw, ty + p.S(20) };
    p.TextBox(lr, label, t.caption, t.dim, DT_SINGLELINE | DT_PATH_ELLIPSIS);
    ty += p.S(20);
    if (rep)
    {
        RECT br = { tx, ty, tx + tw, ty + p.S(50) };
        p.TextBox(br, sr::FormatBytes(rep->reclaimable), t.big, t.text, DT_SINGLELINE);
        ty += p.S(50);
        p.Text(tx, ty, tw, Str(IDS_INS_RECLAIMABLE), t.bold, t.accent);
        ty += p.S(24);
        ty += p.Text(tx, ty, tw, Str(IDS_INS_RECLAIM_NOTE), t.caption, t.dim) + p.S(8);
    }
    else
    {
        RECT br = { tx, ty, tx + tw, ty + p.S(50) };
        p.TextBox(br, Str(IDS_INS_ANALYZING_SHORT), t.title, t.dim, DT_SINGLELINE);
        ty += p.S(58);
    }
    if (d && d->total)
    {
        const std::wstring used = sr::FormatBytes(d->total - std::min(d->free, d->total));
        std::wstring line = Strf(IDS_INS_USAGE, used.c_str(), sr::FormatBytes(d->total).c_str(),
                                 SizeShare(d->free, d->total, d->root.substr(0, 2)).c_str());
        ty += p.Text(tx, ty, tw, line, t.font, d->low ? t.danger : t.text);
    }
    else
        ty += p.Text(tx, ty, tw, Strf(IDS_INS_FOLDER_SIZE, sr::FormatBytes(tree->At(0).size).c_str()), t.font, t.text);
    const std::wstring scanned = Strf(IDS_INS_SCANNED, sr::FormatCount(tree->fileCount).c_str(),
                                      sr::FormatCount(tree->dirCount).c_str(), FormatDuration(tree->scanMillis).c_str());
    p.Text(tx, ty + p.S(2), tw, scanned, t.caption, t.dim);
    return y + h;
}

int InsightsPage::HogRows(Painter& p, int x, int y, int w, const std::vector<sr::Hog>& hogs, int reviewBase, int showBase)
{
    const sr::ScanTree* tree = App::Get().Tree();
    const int rowH = p.S(84);
    const size_t n = std::min<size_t>(hogs.size(), 8);
    if (n == 0) return y;
    RECT card = { x, y, x + w, y + static_cast<int>(n) * rowH };
    p.Round(card, p.S(8), p.t.card, p.t.border);
    const std::wstring label = DriveLabel();
    const sr::u64 total = ShareTotal();
    for (size_t i = 0; i < n; ++i)
    {
        const sr::u32 node = hogs[i].node;
        if (node >= tree->Count()) continue;
        const sr::Node& nd = tree->At(node);
        std::vector<RowBtn> btns;
        btns.push_back({ showBase + static_cast<int>(i), Str(IDS_INS_SHOW), BtnStyle::Normal, true });
        btns.push_back({ reviewBase + static_cast<int>(i), Str(IDS_INS_REVIEW), BtnStyle::Normal, true });
        std::wstring share;
        if (total) share = Strf(IDS_INS_SHARE_OF, sr::FormatPercent(static_cast<double>(nd.size) / static_cast<double>(total)).c_str(), label.c_str());
        DrawRow(p, x, y + static_cast<int>(i) * rowH, w, rowH, i > 0, tree->Name(node) + (tree->IsDir(node) ? L"\\" : L""),
                hogs[i].explanation, tree->Path(node), sr::FormatBytes(nd.size), share, btns, p.t.text);
    }
    return y + static_cast<int>(n) * rowH;
}

int InsightsPage::OnPaint(Painter& p)
{
    const Theme& t = p.t;
    App& app = App::Get();
    const sr::ScanTree* tree = app.Tree();
    const sr::InsightsReport* rep = app.Report();
    const int M = p.S(24);
    const int cw = std::max(p.S(320), std::min(p.width - 2 * M, p.S(1000)));
    const int x = std::max(M, (p.width - cw) / 2);
    int y = M;

    if (!tree)
        return EmptyCard(p, x, y, cw, Str(IDS_INS_NOSCAN_T), Str(IDS_INS_NOSCAN_B)) + M;
    if (tree->At(0).size == 0 && tree->fileCount == 0)
    {
        y = EmptyCard(p, x, y, cw, Str(IDS_INS_EMPTY_T), tree->errorCount ? Str(IDS_INS_EMPTY_ERR) : Str(IDS_INS_EMPTY_B));
        return y + M;
    }

    y = Hero(p, x, y, cw) + p.S(32);
    if (!rep)
    {
        if (app.Analyzing())
            y = EmptyCard(p, x, y, cw, Str(IDS_INS_LOADING_T), Str(IDS_INS_LOADING_B));
        else
            y = EmptyCard(p, x, y, cw, Str(IDS_INS_FAILED_T), Str(IDS_INS_FAILED_B));
        return y + M;
    }
    const std::wstring label = DriveLabel();
    const sr::u64 total = ShareTotal();

    // --- Top space hogs ---
    y = Section(p, x, y, Str(IDS_INS_SEC_HOGS), Str(IDS_INS_SEC_HOGS_SUB), cw);
    if (!rep->topFolders.empty())
    {
        y += p.Text(x, y, cw, Str(IDS_INS_HOGS_FOLDERS), t.bold, t.text) + p.S(8);
        y = HogRows(p, x, y, cw, rep->topFolders, ID_HOG_FOLDER_REVIEW, ID_HOG_FOLDER_SHOW) + p.S(20);
    }
    if (!rep->topFiles.empty())
    {
        y += p.Text(x, y, cw, Str(IDS_INS_HOGS_FILES), t.bold, t.text) + p.S(8);
        y = HogRows(p, x, y, cw, rep->topFiles, ID_HOG_FILE_REVIEW, ID_HOG_FILE_SHOW) + p.S(20);
    }
    y += p.S(12);

    // --- Smart categories ---
    y = Section(p, x, y, Str(IDS_INS_SEC_CATS), Str(IDS_INS_SEC_CATS_SUB), cw);
    std::vector<size_t> found;
    std::wstring emptyNames;
    for (size_t i = 0; i < rep->categories.size(); ++i)
    {
        const sr::CategoryResult& c = rep->categories[i];
        if (c.id == sr::Category::Duplicates) continue;
        if (c.bytes > 0) found.push_back(i);
        else emptyNames += (emptyNames.empty() ? L"" : L", ") + c.title;
    }
    if (!found.empty())
    {
        const int rowH = p.S(84);
        RECT card = { x, y, x + cw, y + static_cast<int>(found.size()) * rowH };
        p.Round(card, p.S(8), t.card, t.border);
        for (size_t k = 0; k < found.size(); ++k)
        {
            const size_t i = found[k];
            const sr::CategoryResult& c = rep->categories[i];
            std::vector<RowBtn> btns;
            if (c.id == sr::Category::WindowsUpdate) btns.push_back({ ID_CLEANMGR, Str(IDS_INS_CLEANMGR), BtnStyle::Normal, true });
            btns.push_back({ ID_CAT_REVIEW + static_cast<int>(i), Str(IDS_INS_REVIEW), BtnStyle::Normal, !c.nodes.empty() });
            std::wstring share = total ? Strf(IDS_INS_SHARE_OF, sr::FormatPercent(static_cast<double>(c.bytes) / static_cast<double>(total)).c_str(), label.c_str()) : L"";
            share += (share.empty() ? L"" : L" · ") + Strf(IDS_INS_FILES_COUNT, sr::FormatCount(c.count).c_str());
            DrawRow(p, x, y + static_cast<int>(k) * rowH, cw, rowH, k > 0, c.title, c.explanation, L"",
                    sr::FormatBytes(c.bytes), share, btns, t.text,
                    c.reclaimable ? Str(IDS_INS_SAFE) : Str(IDS_INS_CHECK), c.reclaimable ? t.good : t.warn);
        }
        y += static_cast<int>(found.size()) * rowH;
    }
    else
        y = EmptyCard(p, x, y, cw, Str(IDS_INS_CATS_EMPTY_T), Str(IDS_INS_CATS_EMPTY_B));
    if (!found.empty() && !emptyNames.empty())
        y += p.S(8) + p.Text(x, y + p.S(8), cw, Strf(IDS_INS_CATS_NONE, emptyNames.c_str()), t.caption, t.dim);
    y += p.S(36);

    // --- Duplicates ---
    y = Section(p, x, y, Str(IDS_INS_SEC_DUP), Str(IDS_INS_SEC_DUP_SUB), cw);
    {
        const int h = p.S(84);
        RECT card = { x, y, x + cw, y + h };
        p.Round(card, p.S(8), t.card, t.border);
        const int bx = x + p.S(16), by = y + (h - p.ButtonHeight()) / 2;
        if (m_dupState == DupIdle || m_dupState == DupCancelled)
        {
            const int bw = p.ButtonWidth(Str(IDS_INS_DUP_FIND), BtnStyle::Primary);
            p.Button(ID_DUP_FIND, bx, by, Str(IDS_INS_DUP_FIND), BtnStyle::Primary);
            const std::wstring msg = m_dupState == DupCancelled ? Str(IDS_INS_DUP_CANCELLED)
                                                                 : Strf(IDS_INS_DUP_HINT, sr::FormatBytes(kDupMinSize).c_str());
            p.Text(bx + bw + p.S(16), y + p.S(16), cw - bw - p.S(64), msg, t.font, t.dim);
        }
        else if (m_dupState == DupRunning)
        {
            const std::wstring cancelText = Str(IDS_INS_DUP_CANCEL);
            const int bw = p.ButtonWidth(cancelText);
            p.Button(ID_DUP_CANCEL, bx, by, cancelText, BtnStyle::Normal, !m_dupCancel);
            const std::wstring elapsed = FormatDuration(GetTickCount() - m_dupStart);
            const std::wstring msg = m_dupCancel ? Str(IDS_INS_DUP_CANCELLING)
                                                 : Strf(IDS_INS_DUP_RUNNING, sr::FormatBytes(kDupMinSize).c_str(), elapsed.c_str());
            p.Text(bx + bw + p.S(16), y + p.S(16), cw - bw - p.S(64), msg, t.font, t.text);
        }
        else if (m_dup && m_dup->bytes > 0)
        {
            const std::wstring review = Str(IDS_INS_REVIEW);
            const int bw = p.ButtonWidth(review, BtnStyle::Primary);
            p.Button(ID_DUP_REVIEW, x + cw - p.S(16) - bw, by, review, BtnStyle::Primary, !m_dup->nodes.empty());
            const std::wstring head = Strf(IDS_INS_DUP_FOUND, sr::FormatBytes(m_dup->bytes).c_str(), sr::FormatCount(m_dup->count).c_str());
            p.Text(bx, y + p.S(14), cw - bw - p.S(64), head, t.bold, t.text);
            p.Text(bx, y + p.S(40), cw - bw - p.S(64), m_dup->explanation.empty() ? Str(IDS_INS_DUP_FOUND_NOTE) : m_dup->explanation, t.caption, t.dim);
        }
        else
            p.Text(bx, y + p.S(28), cw - p.S(32), Strf(IDS_INS_DUP_NONE, sr::FormatBytes(kDupMinSize).c_str()), t.font, t.dim);
        y += h + p.S(36);
    }

    // --- Stale files ---
    y = Section(p, x, y, Str(IDS_INS_SEC_STALE), Str(IDS_INS_SEC_STALE_SUB), cw);
    {
        const int rowH = p.S(64);
        const int n = sr::StaleResult::Buckets;
        RECT card = { x, y, x + cw, y + n * rowH };
        p.Round(card, p.S(8), t.card, t.border);
        for (int b = 0; b < n; ++b)
        {
            std::vector<RowBtn> btns;
            btns.push_back({ ID_STALE_REVIEW + b, Str(IDS_INS_REVIEW), BtnStyle::Normal, rep->stale.bytes[b] > 0 });
            std::wstring share = total ? Strf(IDS_INS_SHARE_OF, sr::FormatPercent(static_cast<double>(rep->stale.bytes[b]) / static_cast<double>(total)).c_str(), label.c_str()) : L"";
            share += (share.empty() ? L"" : L" · ") + Strf(IDS_INS_FILES_COUNT, sr::FormatCount(rep->stale.count[b]).c_str());
            DrawRow(p, x, y + b * rowH, cw, rowH, b > 0, Strf(IDS_INS_STALE_ROW, rep->stale.months[b]),
                    rep->stale.bytes[b] ? L"" : Str(IDS_INS_STALE_NONE), L"", sr::FormatBytes(rep->stale.bytes[b]), share, btns, t.text);
        }
        y += n * rowH;
        const std::wstring signal = Strf(rep->stale.signal == sr::StaleSignal::LastAccess ? IDS_INS_SIGNAL_ACCESS : IDS_INS_SIGNAL_MODIFIED,
                                         rep->stale.signalReason.c_str());
        y += p.S(8) + p.Text(x, y + p.S(8), cw, signal, t.caption, t.dim);
        y += p.S(36);
    }

    // --- Growth ---
    y = Section(p, x, y, Str(IDS_INS_SEC_GROWTH), rep->hasSnapshot ? Strf(IDS_INS_SEC_GROWTH_SUB, sr::FormatAge(rep->snapshotTicks).c_str()) : L"", cw);
    if (!rep->hasSnapshot)
        y = EmptyCard(p, x, y, cw, Str(IDS_INS_GROWTH_EMPTY_T), app.SnapshotDir().empty() ? Str(IDS_INS_GROWTH_NOSNAP) : Str(IDS_INS_GROWTH_EMPTY_B));
    else if (rep->growth.empty())
        y = EmptyCard(p, x, y, cw, Str(IDS_INS_GROWTH_NONE_T), Str(IDS_INS_GROWTH_NONE_B));
    else
    {
        const int rowH = p.S(48);
        const int n = static_cast<int>(rep->growth.size());
        RECT card = { x, y, x + cw, y + n * rowH };
        p.Round(card, p.S(8), t.card, t.border);
        for (int i = 0; i < n; ++i)
        {
            const sr::GrowthEntry& g = rep->growth[static_cast<size_t>(i)];
            const int ry = y + i * rowH;
            if (i > 0) { RECT sep = { x + p.S(16), ry, x + cw - p.S(16), ry + 1 }; p.Fill(sep, t.border); }
            const bool grew = g.delta >= 0;
            const sr::u64 mag = static_cast<sr::u64>(grew ? g.delta : -g.delta);
            const std::wstring delta = (grew ? L"+" : L"−") + sr::FormatBytes(mag);
            const int right = x + cw - p.S(16);
            RECT dr = { right - p.S(240), ry + p.S(6), right, ry + p.S(26) };
            p.TextBox(dr, delta, t.bold, grew ? t.warn : t.good, DT_RIGHT | DT_SINGLELINE);
            RECT nr = { right - p.S(240), ry + p.S(26), right, ry + p.S(44) };
            p.TextBox(nr, Strf(IDS_INS_GROWTH_NOW, sr::FormatBytes(g.now).c_str()), t.caption, t.dim, DT_RIGHT | DT_SINGLELINE);
            RECT pr = { x + p.S(16), ry + p.S(14), right - p.S(256), ry + p.S(36) };
            p.TextBox(pr, g.path, t.font, t.text, DT_SINGLELINE | DT_PATH_ELLIPSIS);
        }
        y += n * rowH;
    }
    return y + M + p.S(8);
}
}
