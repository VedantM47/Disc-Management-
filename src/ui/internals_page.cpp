// SpaceRescue UI - SPDX-License-Identifier: GPL-3.0-or-later

#include "internals_page.h"
#include "widgets.h"

namespace ui
{
namespace
{
const wchar_t* kClass = L"SpaceRescueInternals";
const size_t kTopN = 200;
enum { ID_BROWSE = 1, ID_LIST = 2 };

struct FragJob
{
    const sr::ScanTree* tree;
    volatile bool* cancel;
    HWND notify;
};

DWORD WINAPI FragMain(LPVOID p)
{
    std::unique_ptr<FragJob> job(static_cast<FragJob*>(p));
    auto* r = new std::vector<sr::FragmentedFile>(sr::MostFragmented(*job->tree, kTopN, job->cancel));
    if (!PostMessageW(job->notify, WM_SR_FRAGDONE, 0, reinterpret_cast<LPARAM>(r))) delete r;
    return 0;
}

std::wstring Hex(sr::u64 v)
{
    return sr::Fmt(L"%08X%08X", static_cast<unsigned>(v >> 32), static_cast<unsigned>(v & 0xFFFFFFFFu));
}
}

// Scrollable panel with the volume facts and the selected file.
class InfoCanvas : public Canvas
{
public:
    explicit InfoCanvas(InternalsPage* page) : m_page(page) {}

protected:
    int OnPaint(Painter& p) override;
    void OnButton(int id) override
    {
        if (id == ID_BROWSE) m_page->Browse();
    }

private:
    int Fact(Painter& p, int x, int y, int w, UINT labelId, const std::wstring& value, const std::wstring& explain, COLORREF valueColor);
    int Strip(Painter& p, int x, int y, int w);
    InternalsPage* m_page;
};

int InfoCanvas::Fact(Painter& p, int x, int y, int w, UINT labelId, const std::wstring& value, const std::wstring& explain, COLORREF valueColor)
{
    const Theme& t = p.t;
    const int labelW = std::min(p.S(210), w / 3);
    p.Text(x, y + p.S(1), labelW - p.S(12), Str(labelId), t.font, t.dim);
    const int vw = w - labelW;
    int h = p.Text(x + labelW, y, vw, value, t.bold, valueColor);
    if (!explain.empty())
        h += p.S(2) + p.Text(x + labelW, y + h + p.S(2), vw, Str(IDS_IN_WHAT) + L" " + explain, t.caption, t.dim);
    return y + h + p.S(14);
}

int InfoCanvas::Strip(Painter& p, int x, int y, int w)
{
    const Theme& t = p.t;
    const sr::FileInternals& f = m_page->file;
    sr::u64 total = 0;
    for (const sr::Extent& e : f.extents) total += e.clusters;
    const int h = p.S(32);
    RECT frame = { x, y, x + w, y + h };
    p.Round(frame, p.S(4), t.track);
    if (total == 0) return y + h;
    const COLORREF a = t.accent, b = Blend(t.accent, t.card, 55);
    sr::u64 acc = 0;
    int idx = 0;
    for (const sr::Extent& e : f.extents)
    {
        const int x0 = x + static_cast<int>(static_cast<double>(acc) / static_cast<double>(total) * w);
        acc += e.clusters;
        int x1 = x + static_cast<int>(static_cast<double>(acc) / static_cast<double>(total) * w);
        if (x1 < x0 + 2) x1 = x0 + 2;
        if (x1 > x + w) x1 = x + w;
        RECT r = { x0, y + 2, x1 - (x1 - x0 > 3 ? 1 : 0), y + h - 2 };
        if (e.hole)
        {
            // gaps for sparse/compressed holes: leave the track visible, outline only
            HBRUSH nb = static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
            HPEN pen = CreatePen(PS_DOT, 1, t.dim);
            HGDIOBJ op = SelectObject(p.dc, pen), ob = SelectObject(p.dc, nb);
            Rectangle(p.dc, r.left, r.top, r.right, r.bottom);
            SelectObject(p.dc, op);
            SelectObject(p.dc, ob);
            DeleteObject(pen);
        }
        else
            p.Fill(r, (idx++ & 1) ? b : a);
    }
    return y + h;
}

int InfoCanvas::OnPaint(Painter& p)
{
    const Theme& t = p.t;
    App& app = App::Get();
    const sr::ScanTree* tree = app.Tree();
    InternalsPage& pg = *m_page;
    const int M = p.S(24);
    const int cw = std::max(p.S(320), std::min(p.width - 2 * M, p.S(1000)));
    const int x = std::max(M, (p.width - cw) / 2);
    int y = M;
    if (!tree || !pg.haveVolume)
    {
        p.Text(x, y, cw, Str(IDS_IN_TITLE), t.title, t.text);
        p.Text(x, y + p.S(32), cw, Str(IDS_IN_EMPTY), t.font, t.dim);
        return y + p.S(100);
    }
    const sr::VolumeInternals& v = pg.vol;

    // --- Volume panel ---
    p.Text(x, y, cw, Strf(IDS_IN_VOLUME, v.root.c_str(), v.fileSystem.empty() ? L"?" : v.fileSystem.c_str()), t.title, t.text);
    y += p.S(36);
    const int pad = 0, iw = cw;
    y = Fact(p, x + pad, y, iw, IDS_IN_CLUSTER,
             Strf(IDS_IN_CLUSTER_V, sr::FormatBytes(v.bytesPerCluster).c_str(), sr::FormatCount(v.bytesPerCluster).c_str(),
                  sr::FormatCount(v.bytesPerSector).c_str()),
             sr::Explain(sr::Metric::ClusterSize), t.text);
    y = Fact(p, x + pad, y, iw, IDS_IN_CLUSTERS,
             Strf(IDS_IN_CLUSTERS_V, sr::FormatCount(v.totalClusters).c_str(), sr::FormatCount(v.freeClusters).c_str(),
                  v.totalClusters ? sr::FormatPercent(static_cast<double>(v.freeClusters) / static_cast<double>(v.totalClusters)).c_str() : L"-",
                  sr::FormatBytes(v.freeClusters * v.bytesPerCluster).c_str()),
             sr::Explain(sr::Metric::FreeClusters), t.text);
    if (v.ntfsData)
        y = Fact(p, x + pad, y, iw, IDS_IN_MFT,
                 Strf(IDS_IN_MFT_V, sr::FormatBytes(v.mftValidDataLength).c_str(), sr::FormatCount(v.mftRecordCount).c_str(),
                      sr::FormatBytes(v.bytesPerFileRecord).c_str()),
                 sr::Explain(sr::Metric::Mft), t.text);
    else
    {
        std::wstring msg;
        if (_wcsicmp(v.fileSystem.c_str(), L"NTFS") != 0) msg = Str(IDS_IN_MFT_NOTNTFS);
        else if (v.ntfsError == ERROR_ACCESS_DENIED || !sr::IsElevated()) msg = Str(IDS_IN_MFT_ADMIN);
        else msg = Strf(IDS_IN_MFT_ERR, sr::ErrorText(v.ntfsError).c_str());
        y = Fact(p, x + pad, y, iw, IDS_IN_MFT, msg, sr::Explain(sr::Metric::Mft), t.dim);
    }
    {
        const sr::SlackSummary& s = pg.slack;
        const sr::u64 used = tree->At(0).size;
        std::wstring val = Strf(IDS_IN_SLACK_V, sr::FormatBytes(s.totalSlack).c_str(), sr::FormatCount(s.filesCounted).c_str(),
                                sr::FormatBytes(s.smallFilesWaste).c_str());
        if (used) val += L" " + Strf(IDS_IN_SLACK_SHARE, sr::FormatPercent(static_cast<double>(s.totalSlack) / static_cast<double>(used)).c_str());
        y = Fact(p, x + pad, y, iw, IDS_IN_SLACK, val, sr::Explain(sr::Metric::Slack), t.text);
    }
    y += p.S(14);
    y += p.S(32);

    // --- File panel ---
    p.Text(x, y, cw - p.S(150), Str(IDS_IN_FILE_TITLE), t.title, t.text);
    p.Button(ID_BROWSE, x + cw - p.ButtonWidth(Str(IDS_IN_BROWSE), BtnStyle::Normal), y, Str(IDS_IN_BROWSE), BtnStyle::Normal);
    y += p.S(44);
    const sr::FileInternals& f = pg.file;
    if (!pg.haveFile)
    {
        p.Text(x, y, cw, Str(IDS_IN_FILE_EMPTY), t.font, t.dim);
        return y + p.S(56) + M;
    }
    RECT pr = { x, y, x + cw, y + p.S(22) };
    p.TextBox(pr, f.path, t.bold, t.text, DT_SINGLELINE | DT_PATH_ELLIPSIS);
    y += p.S(32);
    if (f.error)
    {
        p.Text(x, y, cw, Strf(IDS_IN_FILE_ERR, sr::ErrorText(f.error).c_str()), t.font, t.danger);
        return y + p.S(48) + M;
    }
    y = Fact(p, x, y, cw, IDS_IN_LOGICAL, SizeShare(f.logicalSize, tree->At(0).size, tree->rootPath.size() > 3 ? Str(IDS_IN_THIS_SCAN) : v.root.substr(0, 2)), L"", t.text);
    y = Fact(p, x, y, cw, IDS_IN_ONDISK,
             sr::FormatBytes(f.allocatedSize) + (f.resident ? L"  (" + Str(IDS_IN_RESIDENT_NOTE) + L")" : L""), L"", t.text);
    {
        const std::wstring share = f.allocatedSize ? sr::FormatPercent(static_cast<double>(f.slack) / static_cast<double>(f.allocatedSize)) : L"0%";
        y = Fact(p, x, y, cw, IDS_IN_FILE_SLACK, Strf(IDS_IN_FILE_SLACK_V, sr::FormatBytes(f.slack).c_str(), share.c_str()),
                 sr::Explain(sr::Metric::Slack), t.text);
    }
    // flags as chips
    p.Text(x, y + p.S(2), p.S(190), Str(IDS_IN_FLAGS), t.font, t.dim);
    {
        int cx = x + std::min(p.S(210), cw / 3), cy = y;
        auto chip = [&](const std::wstring& s, COLORREF c) {
            const int w = p.TextWidth(s, t.caption) + p.S(16);
            if (cx + w > x + cw) { cx = x + std::min(p.S(210), cw / 3); cy += p.S(26); }
            cx += p.Chip(cx, cy, s, c) + p.S(8);
        };
        bool any = false;
        if (f.resident) { chip(Str(IDS_IN_FLAG_RESIDENT), t.accent); any = true; }
        if (f.compressed) { chip(Str(IDS_IN_FLAG_COMPRESSED), t.accent); any = true; }
        if (f.sparse) { chip(Str(IDS_IN_FLAG_SPARSE), t.accent); any = true; }
        if (f.encrypted) { chip(Str(IDS_IN_FLAG_ENCRYPTED), t.accent); any = true; }
        if (f.hardLinks > 1) { chip(Strf(IDS_IN_FLAG_LINKS, sr::FormatCount(f.hardLinks).c_str()), t.warn); any = true; }
        if (!any) chip(Str(IDS_IN_FLAG_PLAIN), t.dim);
        y = cy + p.S(32);
    }
    y = Fact(p, x, y, cw, IDS_IN_FRAGMENTS,
             f.resident ? Str(IDS_IN_FRAG_RESIDENT)
                        : Strf(f.fragments == 1 ? IDS_IN_FRAG_ONE : IDS_IN_FRAG_MANY, sr::FormatCount(f.fragments).c_str()),
             sr::Explain(f.resident ? sr::Metric::Resident : sr::Metric::Extents), f.fragments > 8 ? t.warn : t.text);
    if (f.compressed || f.sparse)
        y = Fact(p, x, y, cw, IDS_IN_COMPRESSION, Str(f.compressed ? IDS_IN_FLAG_COMPRESSED : IDS_IN_FLAG_SPARSE),
                 sr::Explain(sr::Metric::Compression), t.text);
    if (!f.resident)
    {
        p.Text(x, y, cw, Str(IDS_IN_STRIP_TITLE), t.bold, t.text);
        y += p.S(26);
        y = Strip(p, x, y, cw) + p.S(8);
        sr::u64 clusters = 0;
        for (const sr::Extent& e : f.extents) clusters += e.clusters;
        y += p.Text(x, y, cw, Strf(IDS_IN_STRIP_LEGEND, sr::FormatCount(clusters).c_str(), sr::FormatBytes(clusters * v.bytesPerCluster).c_str()), t.caption, t.dim) + p.S(8);
        const size_t shown = std::min<size_t>(f.extents.size(), 6);
        for (size_t i = 0; i < shown; ++i)
        {
            const sr::Extent& e = f.extents[i];
            std::wstring line = e.hole ? Strf(IDS_IN_EXTENT_HOLE, sr::FormatCount(i + 1).c_str(), sr::FormatCount(e.clusters).c_str())
                                       : Strf(IDS_IN_EXTENT, sr::FormatCount(i + 1).c_str(), sr::FormatCount(e.lcn).c_str(),
                                              sr::FormatCount(e.clusters).c_str(), sr::FormatBytes(e.clusters * v.bytesPerCluster).c_str());
            y += p.Text(x, y, cw, line, t.caption, t.dim);
        }
        if (f.extents.size() > shown)
            y += p.Text(x, y, cw, Strf(IDS_IN_EXTENT_MORE, sr::FormatCount(f.extents.size() - shown).c_str()), t.caption, t.dim);
    }
    (void)Hex;
    return y + M;
}

// --- InternalsPage -------------------------------------------------------------------

HWND InternalsPage::Create(HWND parent)
{
    static bool registered = false;
    if (!registered)
    {
        registered = true;
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = Proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClass;
        RegisterClassExW(&wc);
    }
    m_hwnd = CreateWindowExW(0, kClass, L"", WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0, 10, 10, parent,
                             nullptr, GetModuleHandleW(nullptr), this);
    return m_hwnd;
}

LRESULT CALLBACK InternalsPage::Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    InternalsPage* self;
    if (msg == WM_NCCREATE)
    {
        self = static_cast<InternalsPage*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
    }
    else
        self = reinterpret_cast<InternalsPage*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
    return self->Handle(msg, wp, lp);
}

void InternalsPage::StopBackground()
{
    if (m_thread)
    {
        m_cancel = true;
        WaitForSingleObject(m_thread, INFINITE);
        CloseHandle(m_thread);
        m_thread = nullptr;
        MSG msg;
        while (PeekMessageW(&msg, m_hwnd, WM_SR_FRAGDONE, WM_SR_FRAGDONE, PM_REMOVE))
            delete reinterpret_cast<std::vector<sr::FragmentedFile>*>(msg.lParam);
    }
    fragLoading = false;
}

void InternalsPage::Tick()
{
    if (fragLoading) InvalidateRect(m_hwnd, &m_labelRect, TRUE);
}

void InternalsPage::Restyle()
{
    if (m_list) StyleListView(m_list);
    Layout();
    if (m_info) m_info->Redraw();
    InvalidateRect(m_hwnd, nullptr, TRUE);
}

void InternalsPage::Reload()
{
    StopBackground();
    const sr::ScanTree* tree = App::Get().Tree();
    haveFile = false;
    file = sr::FileInternals();
    haveVolume = false;
    fragDone = false;
    m_frags.clear();
    if (m_list) ListView_DeleteAllItems(m_list);
    if (tree)
    {
        vol = sr::QueryVolume(sr::VolumeOf(tree->rootPath));
        slack = sr::EstimateSlack(*tree);
        haveVolume = true;
        m_cancel = false;
        fragLoading = true;
        FragJob* job = new FragJob{ tree, &m_cancel, m_hwnd };
        m_thread = CreateThread(nullptr, 0, FragMain, job, 0, nullptr);
        if (!m_thread)
        {
            delete job;
            fragLoading = false;
        }
    }
    if (m_info) m_info->ScrollToTop();
    Layout();
    if (m_info) m_info->Redraw();
    InvalidateRect(m_hwnd, nullptr, TRUE);
}

void InternalsPage::ShowFile(const std::wstring& path)
{
    const sr::ScanTree* tree = App::Get().Tree();
    if (!tree) return;
    file = sr::QueryFile(path, vol.bytesPerCluster ? vol.bytesPerCluster : tree->clusterSize);
    haveFile = true;
    Layout();
    if (m_info) { m_info->ScrollToTop(); m_info->Redraw(); }
}

void InternalsPage::Browse()
{
    typedef BOOL (WINAPI *OpenFn)(LPOPENFILENAMEW);
    HMODULE dll = LoadLibraryW(L"comdlg32.dll");
    if (!dll) return;
    OpenFn fn = reinterpret_cast<OpenFn>(GetProcAddress(dll, "GetOpenFileNameW"));
    if (fn)
    {
        wchar_t buffer[MAX_PATH * 4] = {};
        OPENFILENAMEW ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = m_hwnd;
        ofn.lpstrFile = buffer;
        ofn.nMaxFile = MAX_PATH * 4;
        const std::wstring filter = Str(IDS_IN_FILTER_ALL) + std::wstring(1, L'\0') + L"*.*" + std::wstring(2, L'\0');
        ofn.lpstrFilter = filter.c_str();
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_EXPLORER;
        if (fn(&ofn)) ShowFile(buffer);
    }
    FreeLibrary(dll);
}

void InternalsPage::Layout()
{
    if (!m_info || !m_list) return;
    const Theme& t = T();
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    const int w = rc.right, h = rc.bottom;
    const int labelH = t.S(64);
    const int minList = t.S(140);
    int infoH = m_info->Measure(w);
    infoH = std::min(infoH, h - labelH - minList);
    infoH = std::max(infoH, std::min(h / 2, t.S(200)));
    const int m = t.S(24);
    const int cw = std::max(t.S(320), std::min(w - 2 * m, t.S(1000)));
    const int cx = std::max(m, (w - cw) / 2);
    MoveWindow(m_info->Hwnd(), 0, 0, w, infoH, TRUE);
    m_labelRect = { cx, infoH + t.S(8), cx + cw, infoH + labelH };
    MoveWindow(m_list, cx, infoH + labelH, cw, std::max(t.S(60), h - infoH - labelH - m), TRUE);
}

void InternalsPage::Paint()
{
    const Theme& t = T();
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(m_hwnd, &ps);
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    FillRect(dc, &rc, t.bgBrush);
    Painter p(dc, rc.right, nullptr);
    const int w = m_labelRect.right - m_labelRect.left;
    if (w > 0)
    {
        p.Text(m_labelRect.left, m_labelRect.top, w, Str(IDS_IN_FRAG_TITLE), t.title, t.text, DT_SINGLELINE);
        std::wstring sub;
        if (fragLoading) sub = Strf(IDS_IN_FRAG_LOADING, sr::FormatCount(kTopN).c_str());
        else if (fragDone && fragShown == 0) sub = Strf(IDS_IN_FRAG_NONE, sr::FormatCount(kTopN).c_str());
        else if (fragDone) sub = Strf(IDS_IN_FRAG_SUB, sr::FormatCount(fragShown).c_str(), sr::FormatCount(kTopN).c_str());
        else if (!haveVolume) sub = Str(IDS_IN_FRAG_NOSCAN);
        RECT sr_ = { m_labelRect.left, m_labelRect.top + t.S(28), m_labelRect.right, m_labelRect.top + t.S(52) };
        p.TextBox(sr_, sub, t.font, t.dim, DT_SINGLELINE);
    }
    EndPaint(m_hwnd, &ps);
}

void InternalsPage::FillList(const std::vector<sr::FragmentedFile>& frags)
{
    const sr::ScanTree* tree = App::Get().Tree();
    ListView_DeleteAllItems(m_list);
    fragShown = 0;
    if (!tree) return;
    for (const sr::FragmentedFile& f : frags)
    {
        if (f.fragments < 2 || f.node >= tree->Count()) continue;
        const std::wstring path = tree->Path(f.node);
        LVITEMW li = {};
        li.mask = LVIF_TEXT | LVIF_PARAM;
        li.iItem = static_cast<int>(fragShown);
        li.pszText = const_cast<wchar_t*>(path.c_str());
        li.lParam = static_cast<LPARAM>(f.node);
        const int idx = ListView_InsertItem(m_list, &li);
        const std::wstring frag = sr::FormatCount(f.fragments);
        const std::wstring size = sr::FormatBytes(tree->At(f.node).size);
        ListView_SetItemText(m_list, idx, 1, const_cast<wchar_t*>(frag.c_str()));
        ListView_SetItemText(m_list, idx, 2, const_cast<wchar_t*>(size.c_str()));
        ++fragShown;
    }
}

LRESULT InternalsPage::Handle(UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        m_info = new InfoCanvas(this);
        m_info->Create(m_hwnd, true, true);
        ShowWindow(m_info->Hwnd(), SW_SHOW);
        m_list = CreateWindowExW(0, WC_LISTVIEWW, L"",
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                                 0, 0, 10, 10, m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_LIST)),
                                 GetModuleHandleW(nullptr), nullptr);
        ListView_SetExtendedListViewStyle(m_list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        StyleListView(m_list);
        struct { UINT id; int w; int fmt; } cols[] = { { IDS_IN_COL_FILE, 620, LVCFMT_LEFT }, { IDS_IN_COL_FRAGS, 100, LVCFMT_RIGHT }, { IDS_IN_COL_SIZE, 100, LVCFMT_RIGHT } };
        for (int i = 0; i < 3; ++i)
        {
            const std::wstring text = Str(cols[i].id);
            LVCOLUMNW c = {};
            c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
            c.pszText = const_cast<wchar_t*>(text.c_str());
            c.cx = T().S(cols[i].w);
            c.fmt = cols[i].fmt;
            ListView_InsertColumn(m_list, i, &c);
        }
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        Paint();
        return 0;
    case WM_SIZE:
        Layout();
        InvalidateRect(m_hwnd, nullptr, TRUE);
        return 0;
    case WM_SETFOCUS:
        if (m_info) SetFocus(m_info->Hwnd());
        return 0;
    case WM_SR_KEY:
        return SendMessageW(GetParent(m_hwnd), WM_SR_KEY, wp, lp);
    case WM_SR_FRAGDONE:
    {
        std::unique_ptr<std::vector<sr::FragmentedFile>> r(reinterpret_cast<std::vector<sr::FragmentedFile>*>(lp));
        if (m_thread)
        {
            WaitForSingleObject(m_thread, INFINITE);
            CloseHandle(m_thread);
            m_thread = nullptr;
        }
        fragLoading = false;
        if (!m_cancel)
        {
            m_frags = *r;
            fragDone = true;
            FillList(m_frags);
        }
        Layout();
        InvalidateRect(m_hwnd, nullptr, TRUE);
        return 0;
    }
    case WM_NOTIFY:
    {
        NMHDR* h = reinterpret_cast<NMHDR*>(lp);
        if (h->hwndFrom == m_list && (h->code == static_cast<UINT>(NM_DBLCLK) || (h->code == static_cast<UINT>(LVN_KEYDOWN) && reinterpret_cast<NMLVKEYDOWN*>(lp)->wVKey == VK_RETURN)))
        {
            const int sel = ListView_GetNextItem(m_list, -1, LVNI_SELECTED);
            const sr::ScanTree* tree = App::Get().Tree();
            if (sel >= 0 && tree)
            {
                LVITEMW li = {};
                li.mask = LVIF_PARAM;
                li.iItem = sel;
                ListView_GetItem(m_list, &li);
                if (static_cast<sr::u32>(li.lParam) < tree->Count()) ShowFile(tree->Path(static_cast<sr::u32>(li.lParam)));
            }
        }
        break;
    }
    case WM_SR_THEME:
        Restyle();
        return 0;
    case WM_DESTROY:
        StopBackground();
        delete m_info;
        m_info = nullptr;
        return 0;
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}
}
