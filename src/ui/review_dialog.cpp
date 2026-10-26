// SpaceRescue UI - SPDX-License-Identifier: GPL-3.0-or-later

#include "review_dialog.h"
#include "widgets.h"
#include <set>

namespace ui
{
namespace
{
enum
{
    ID_LIST = 1, ID_SELALL = 10, ID_SELNONE = 11, ID_OPEN = 12,
    ID_RECYCLE = 20, ID_QUARANTINE = 21, ID_COMPRESS = 22, ID_DELETE = 23
};

struct Item
{
    sr::u32 node;
    std::wstring name, folder, path;
    sr::u64 size;
    sr::u64 time;
    std::wstring sizeText, timeText;
};

struct Job
{
    sr::ActionKind kind;
    std::vector<std::wstring> paths;
    std::wstring workVolume;
    HWND notify;
};

class Dialog
{
public:
    explicit Dialog(const ReviewRequest& r) : m_req(r) {}
    void Run(HWND owner);

private:
    static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
    LRESULT Handle(UINT, WPARAM, LPARAM);
    void Build();
    void Layout();
    void Paint();
    void Populate();
    void UpdateSummary();
    std::vector<sr::u32> CheckedNodes() const;       // top-most only (no node under another selected node)
    void OnAction(sr::ActionKind kind);
    void SetBusy(bool busy);
    void OpenLocation();
    void Sort(int column);
    static int CALLBACK Compare(LPARAM a, LPARAM b, LPARAM self);
    HWND AddButton(int id, const std::wstring& text);

    ReviewRequest m_req;
    HWND m_hwnd = nullptr, m_list = nullptr;
    HWND m_buttons[8] = {};
    std::vector<Item> m_items;
    bool m_populating = false, m_busy = false, m_alive = true;
    int m_sortColumn = 1;
    bool m_sortAsc = false;
    std::wstring m_summary, m_note, m_busyText;
    RECT m_summaryRect = {}, m_noteRect = {}, m_headerRect = {};
    std::wstring m_workVolume;
    bool m_isNtfs = false;
    std::vector<sr::u32> m_pendingNodes;
    std::vector<std::wstring> m_pendingPaths;
};

const wchar_t* kClass = L"SpaceRescueReview";

HWND Dialog::AddButton(int id, const std::wstring& text)
{
    HWND b = CreateWindowExW(0, L"BUTTON", text.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                             0, 0, 10, 10, m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                             GetModuleHandleW(nullptr), nullptr);
    StyleControl(b);
    return b;
}

void Dialog::Run(HWND owner)
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
        wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(IDI_APP));
        wc.lpszClassName = kClass;
        RegisterClassExW(&wc);
    }
    const Theme& t = T();
    RECT orc;
    GetWindowRect(owner, &orc);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &mi);
    const RECT wa = mi.rcWork;
    const int w = std::min(t.S(980), static_cast<int>(wa.right - wa.left));
    const int h = std::min(t.S(720), static_cast<int>(wa.bottom - wa.top));
    int x = orc.left + (orc.right - orc.left - w) / 2;
    int y = orc.top + (orc.bottom - orc.top - h) / 2;
    x = std::max(static_cast<int>(wa.left), std::min(x, static_cast<int>(wa.right) - w));
    y = std::max(static_cast<int>(wa.top), std::min(y, static_cast<int>(wa.bottom) - h));
    m_hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, kClass, m_req.title.c_str(),
                             WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MAXIMIZEBOX | WS_CLIPCHILDREN,
                             x, y, w, h, owner, nullptr, GetModuleHandleW(nullptr), this);
    if (!m_hwnd) return;
    EnableWindow(owner, FALSE);
    ShowWindow(m_hwnd, SW_SHOW);
    SetFocus(m_list);
    MSG msg;
    while (m_alive && GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        if (!IsDialogMessage(m_hwnd, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (m_alive) PostQuitMessage(0); // WM_QUIT arrived while the dialog was open
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
}

void Dialog::Build()
{
    const App& app = App::Get();
    m_workVolume = app.WorkVolume();
    m_isNtfs = app.Tree() && _wcsicmp(app.Tree()->fileSystem.c_str(), L"NTFS") == 0;
    StyleTopLevel(m_hwnd);

    m_list = CreateWindowExW(0, WC_LISTVIEWW, L"",
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS,
                             0, 0, 10, 10, m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_LIST)),
                             GetModuleHandleW(nullptr), nullptr);
    ListView_SetExtendedListViewStyle(m_list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    StyleListView(m_list);
    const Theme& t = T();
    struct { UINT ids; int width; int fmt; } cols[] = {
        { IDS_RV_COL_NAME, 280, LVCFMT_LEFT }, { IDS_RV_COL_SIZE, 90, LVCFMT_RIGHT },
        { IDS_RV_COL_DATE, 170, LVCFMT_LEFT }, { IDS_RV_COL_FOLDER, 360, LVCFMT_LEFT } };
    for (int i = 0; i < 4; ++i)
    {
        const std::wstring text = Str(cols[i].ids);
        LVCOLUMNW c = {};
        c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        c.pszText = const_cast<wchar_t*>(text.c_str());
        c.cx = t.S(cols[i].width);
        c.fmt = cols[i].fmt;
        ListView_InsertColumn(m_list, i, &c);
    }

    AddButton(ID_SELALL, Str(IDS_RV_SELECT_ALL));
    AddButton(ID_SELNONE, Str(IDS_RV_SELECT_NONE));
    AddButton(ID_OPEN, Str(IDS_RV_OPEN_LOCATION));
    m_buttons[0] = GetDlgItem(m_hwnd, ID_SELALL);
    m_buttons[1] = GetDlgItem(m_hwnd, ID_SELNONE);
    m_buttons[2] = GetDlgItem(m_hwnd, ID_OPEN);
    m_buttons[3] = AddButton(ID_RECYCLE, Str(IDS_RV_RECYCLE));
    m_buttons[4] = AddButton(ID_QUARANTINE, Str(IDS_RV_QUARANTINE));
    m_buttons[5] = AddButton(ID_COMPRESS, Str(IDS_RV_COMPRESS));
    m_buttons[6] = AddButton(ID_DELETE, Str(IDS_RV_DELETE));
    EnableWindow(m_buttons[4], !m_workVolume.empty());
    EnableWindow(m_buttons[5], m_isNtfs);

    // Plain-language note about where each action sends the files.
    m_note = Str(IDS_RV_NOTE_RECYCLE) + L" ";
    m_note += m_workVolume.empty() ? Str(IDS_RV_NO_WORKVOL) : Strf(IDS_RV_NOTE_QUARANTINE, m_workVolume.c_str());
    if (!m_isNtfs) m_note += L" " + Str(IDS_RV_NOT_NTFS);

    Populate();
    UpdateSummary();
    Layout();
}

void Dialog::Populate()
{
    const sr::ScanTree* tree = App::Get().Tree();
    if (!tree) return;
    m_items.clear();
    for (sr::u32 n : m_req.nodes)
    {
        if (n >= tree->Count()) continue;
        const sr::Node& node = tree->At(n);
        Item it;
        it.node = n;
        it.path = tree->Path(n);
        it.name = tree->Name(n) + (tree->IsDir(n) ? L"\\" : L"");
        it.folder = sr::ParentPath(it.path);
        it.size = node.size;
        it.time = m_req.useAccessTime ? node.accessTime : node.writeTime;
        it.sizeText = sr::FormatBytes(it.size);
        it.timeText = it.time ? sr::FormatAge(it.time) + L"  (" + FormatDate(it.time) + L")" : L"";
        m_items.push_back(it);
    }
    m_populating = true;
    ListView_DeleteAllItems(m_list);
    for (size_t i = 0; i < m_items.size(); ++i)
    {
        LVITEMW li = {};
        li.mask = LVIF_TEXT | LVIF_PARAM;
        li.iItem = static_cast<int>(i);
        li.pszText = LPSTR_TEXTCALLBACKW;
        li.lParam = static_cast<LPARAM>(i);
        ListView_InsertItem(m_list, &li);
        if (m_req.preselect) ListView_SetCheckState(m_list, i, TRUE);
    }
    m_populating = false;
    m_sortColumn = -1;
    Sort(1);
}

int CALLBACK Dialog::Compare(LPARAM a, LPARAM b, LPARAM selfPtr)
{
    const Dialog* d = reinterpret_cast<const Dialog*>(selfPtr);
    const Item& x = d->m_items[static_cast<size_t>(a)];
    const Item& y = d->m_items[static_cast<size_t>(b)];
    int r = 0;
    switch (d->m_sortColumn)
    {
    case 0: r = _wcsicmp(x.name.c_str(), y.name.c_str()); break;
    case 1: r = x.size < y.size ? -1 : x.size > y.size ? 1 : 0; break;
    case 2: r = x.time < y.time ? -1 : x.time > y.time ? 1 : 0; break;
    default: r = _wcsicmp(x.folder.c_str(), y.folder.c_str()); break;
    }
    return d->m_sortAsc ? r : -r;
}

void Dialog::Sort(int column)
{
    if (column == m_sortColumn) m_sortAsc = !m_sortAsc;
    else { m_sortColumn = column; m_sortAsc = column == 0 || column == 3; }
    ListView_SortItems(m_list, Compare, reinterpret_cast<LPARAM>(this));
}

std::vector<sr::u32> Dialog::CheckedNodes() const
{
    std::set<sr::u32> chosen;
    const int count = ListView_GetItemCount(m_list);
    for (int i = 0; i < count; ++i)
    {
        if (!ListView_GetCheckState(m_list, i)) continue;
        LVITEMW li = {};
        li.mask = LVIF_PARAM;
        li.iItem = i;
        ListView_GetItem(m_list, &li);
        chosen.insert(m_items[static_cast<size_t>(li.lParam)].node);
    }
    std::vector<sr::u32> out;
    const sr::ScanTree* tree = App::Get().Tree();
    for (sr::u32 n : chosen)
    {
        bool covered = false;
        if (tree)
            for (sr::u32 p = tree->At(n).parent; p != sr::NoNode; p = tree->At(p).parent)
                if (chosen.count(p)) { covered = true; break; }
        if (!covered) out.push_back(n);
    }
    return out;
}

void Dialog::UpdateSummary()
{
    const sr::ScanTree* tree = App::Get().Tree();
    sr::u64 bytes = 0;
    const std::vector<sr::u32> nodes = CheckedNodes();
    for (sr::u32 n : nodes) bytes += tree ? tree->At(n).size : 0;
    const sr::DriveInfo* d = App::Get().TreeDrive();
    if (nodes.empty())
        m_summary = Strf(IDS_RV_NONE_SELECTED, sr::FormatCount(m_items.size()).c_str());
    else
    {
        m_summary = Strf(IDS_RV_SELECTED, sr::FormatCount(nodes.size()).c_str(), sr::FormatBytes(bytes).c_str());
        if (d && d->total)
            m_summary += L" " + Strf(IDS_RV_OF_DRIVE, sr::FormatPercent(static_cast<double>(bytes) / static_cast<double>(d->total)).c_str(),
                                    d->root.substr(0, 2).c_str());
    }
    const BOOL any = !nodes.empty() && !m_busy;
    EnableWindow(m_buttons[3], any);
    EnableWindow(m_buttons[4], any && !m_workVolume.empty());
    EnableWindow(m_buttons[5], any && m_isNtfs);
    EnableWindow(m_buttons[6], any);
    InvalidateRect(m_hwnd, &m_summaryRect, FALSE);
}

void Dialog::Layout()
{
    const Theme& t = T();
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    const int m = t.S(24), g = t.S(8), bh = t.S(32);
    const int w = rc.right;
    m_headerRect = { m, t.S(16), w - m, t.S(88) };
    const int bottomH = t.S(24) + g + bh + g + bh + g + t.S(48) + t.S(8);
    const int listTop = t.S(92), listBottom = rc.bottom - bottomH;
    MoveWindow(m_list, m, listTop, w - 2 * m, std::max(t.S(80), listBottom - listTop), TRUE);
    int y = listBottom + g;
    m_summaryRect = { m, y, w - m, y + t.S(24) };
    y += t.S(24) + g;
    // Row 1: selection tools (left)
    HDC dc = GetDC(m_hwnd);
    HGDIOBJ old = SelectObject(dc, t.font);
    auto bw = [&](HWND b) {
        wchar_t text[128];
        GetWindowTextW(b, text, 128);
        SIZE sz = {};
        GetTextExtentPoint32W(dc, text, static_cast<int>(wcslen(text)), &sz);
        return sz.cx + t.S(32);
    };
    int x = m;
    for (int i = 0; i < 3; ++i)
    {
        const int width = bw(m_buttons[i]);
        MoveWindow(m_buttons[i], x, y, width, bh, TRUE);
        x += width + g;
    }
    // Row 2: actions (right-aligned)
    y += bh + g;
    int widths[4];
    for (int i = 0; i < 4; ++i) widths[i] = bw(m_buttons[3 + i]);
    int right = w - m;
    for (int i = 3; i >= 0; --i)
    {
        right -= widths[i];
        MoveWindow(m_buttons[3 + i], right, y, widths[i], bh, TRUE);
        right -= g;
    }
    SelectObject(dc, old);
    ReleaseDC(m_hwnd, dc);
    m_noteRect = { m, y + bh + g, w - m, rc.bottom - t.S(8) };
    InvalidateRect(m_hwnd, nullptr, TRUE);
}

void Dialog::Paint()
{
    const Theme& t = T();
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(m_hwnd, &ps);
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    FillRect(dc, &rc, t.bgBrush);
    SetBkMode(dc, TRANSPARENT);
    auto draw = [&](RECT r, const std::wstring& s, HFONT f, COLORREF c, UINT fl) {
        SelectObject(dc, f);
        SetTextColor(dc, c);
        DrawTextW(dc, s.c_str(), static_cast<int>(s.size()), &r, fl | DT_NOPREFIX);
    };
    RECT titleRc = { m_headerRect.left, m_headerRect.top, m_headerRect.right, m_headerRect.top + t.S(30) };
    draw(titleRc, m_req.title, t.title, t.text, DT_SINGLELINE | DT_END_ELLIPSIS);
    RECT blurbRc = { m_headerRect.left, m_headerRect.top + t.S(32), m_headerRect.right, m_headerRect.bottom };
    draw(blurbRc, m_busy ? m_busyText : m_req.blurb, t.font, t.dim, DT_WORDBREAK);
    draw(m_summaryRect, m_summary, t.bold, t.text, DT_SINGLELINE | DT_VCENTER);
    draw(m_noteRect, m_note, t.caption, t.dim, DT_WORDBREAK);
    EndPaint(m_hwnd, &ps);
}

void Dialog::SetBusy(bool busy)
{
    m_busy = busy;
    EnableWindow(m_list, !busy);
    for (int i = 0; i < 3; ++i) EnableWindow(m_buttons[i], !busy);
    UpdateSummary();
    InvalidateRect(m_hwnd, nullptr, TRUE);
}

void Dialog::OpenLocation()
{
    int sel = ListView_GetNextItem(m_list, -1, LVNI_SELECTED);
    if (sel < 0) sel = ListView_GetNextItem(m_list, -1, LVNI_FOCUSED);
    if (sel < 0) { MessageBeep(MB_ICONINFORMATION); return; }
    LVITEMW li = {};
    li.mask = LVIF_PARAM;
    li.iItem = sel;
    ListView_GetItem(m_list, &li);
    RevealInExplorer(m_hwnd, m_items[static_cast<size_t>(li.lParam)].path);
}

DWORD WINAPI JobMain(LPVOID p)
{
    std::unique_ptr<Job> job(static_cast<Job*>(p));
    sr::ActionResult* r = new sr::ActionResult(sr::ActionManager::Get().Apply(job->kind, job->paths, job->workVolume));
    if (!PostMessageW(job->notify, WM_SR_ACTIONDONE, 0, reinterpret_cast<LPARAM>(r))) delete r;
    return 0;
}

void Dialog::OnAction(sr::ActionKind kind)
{
    const sr::ScanTree* tree = App::Get().Tree();
    const std::vector<sr::u32> nodes = CheckedNodes();
    if (nodes.empty() || !tree) return;
    sr::u64 bytes = 0;
    std::vector<std::wstring> paths;
    for (sr::u32 n : nodes)
    {
        bytes += tree->At(n).size;
        paths.push_back(tree->Path(n));
    }
    const std::wstring what = Strf(IDS_CONFIRM_WHAT, sr::FormatCount(nodes.size()).c_str(), sr::FormatBytes(bytes).c_str());
    std::wstring text, title = Str(IDS_CONFIRM_TITLE);
    UINT flags = MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON1;
    switch (kind)
    {
    case sr::ActionKind::Recycle: text = Strf(IDS_CONFIRM_RECYCLE, what.c_str()); break;
    case sr::ActionKind::Quarantine: text = Strf(IDS_CONFIRM_QUARANTINE, what.c_str(), m_workVolume.c_str()); break;
    case sr::ActionKind::Compress: text = Strf(IDS_CONFIRM_COMPRESS, what.c_str()); break;
    case sr::ActionKind::DeletePermanent:
        text = Strf(IDS_CONFIRM_DELETE, what.c_str());
        title = Str(IDS_CONFIRM_DELETE_TITLE);
        flags = MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2;
        break;
    }
    if (MessageBoxW(m_hwnd, text.c_str(), title.c_str(), flags) != IDYES) return;

    m_pendingNodes = nodes;
    m_pendingPaths = paths;
    m_busyText = Str(IDS_RV_WORKING);
    SetBusy(true);
    Job* job = new Job{ kind, paths, m_workVolume, m_hwnd };
    HANDLE h = CreateThread(nullptr, 0, JobMain, job, 0, nullptr);
    if (h) CloseHandle(h);
    else
    {
        delete job;
        SetBusy(false);
    }
}

LRESULT CALLBACK Dialog::Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    Dialog* self;
    if (msg == WM_NCCREATE)
    {
        self = static_cast<Dialog*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
    }
    else
        self = reinterpret_cast<Dialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
    return self->Handle(msg, wp, lp);
}

LRESULT Dialog::Handle(UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_CREATE:
        Build();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        Paint();
        return 0;
    case WM_SIZE:
        if (m_list) Layout();
        return 0;
    case WM_GETMINMAXINFO:
    {
        MINMAXINFO* mm = reinterpret_cast<MINMAXINFO*>(lp);
        mm->ptMinTrackSize.x = T().S(760);
        mm->ptMinTrackSize.y = T().S(480);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wp))
        {
        case ID_SELALL:
        case ID_SELNONE:
            m_populating = true;
            for (int i = 0; i < ListView_GetItemCount(m_list); ++i) ListView_SetCheckState(m_list, i, LOWORD(wp) == ID_SELALL);
            m_populating = false;
            UpdateSummary();
            break;
        case ID_OPEN: OpenLocation(); break;
        case ID_RECYCLE: OnAction(sr::ActionKind::Recycle); break;
        case ID_QUARANTINE: OnAction(sr::ActionKind::Quarantine); break;
        case ID_COMPRESS: OnAction(sr::ActionKind::Compress); break;
        case ID_DELETE: OnAction(sr::ActionKind::DeletePermanent); break;
        case IDCANCEL:
            if (!m_busy) DestroyWindow(m_hwnd);
            break;
        }
        return 0;
    case WM_NOTIFY:
    {
        NMHDR* h = reinterpret_cast<NMHDR*>(lp);
        if (h->hwndFrom != m_list) break;
        if (h->code == LVN_GETDISPINFOW)
        {
            NMLVDISPINFOW* di = reinterpret_cast<NMLVDISPINFOW*>(lp);
            if ((di->item.mask & LVIF_TEXT) && di->item.lParam >= 0 && static_cast<size_t>(di->item.lParam) < m_items.size())
            {
                const Item& it = m_items[static_cast<size_t>(di->item.lParam)];
                const std::wstring* s = &it.name;
                switch (di->item.iSubItem)
                {
                case 1: s = &it.sizeText; break;
                case 2: s = &it.timeText; break;
                case 3: s = &it.folder; break;
                }
                wcsncpy(di->item.pszText, s->c_str(), static_cast<size_t>(di->item.cchTextMax) - 1);
                di->item.pszText[di->item.cchTextMax - 1] = L'\0';
            }
        }
        else if (h->code == LVN_ITEMCHANGED && !m_populating)
        {
            NMLISTVIEW* nm = reinterpret_cast<NMLISTVIEW*>(lp);
            if ((nm->uChanged & LVIF_STATE) && ((nm->uNewState ^ nm->uOldState) & LVIS_STATEIMAGEMASK)) UpdateSummary();
        }
        else if (h->code == LVN_COLUMNCLICK)
            Sort(reinterpret_cast<NMLISTVIEW*>(lp)->iSubItem);
        else if (h->code == static_cast<UINT>(NM_CUSTOMDRAW))
        {
            NMLVCUSTOMDRAW* cd = reinterpret_cast<NMLVCUSTOMDRAW*>(lp);
            if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
            if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT)
            {
                cd->clrText = T().text;
                cd->clrTextBk = T().card;
                return CDRF_DODEFAULT;
            }
        }
        break;
    }
    case WM_SR_ACTIONDONE:
    {
        std::unique_ptr<sr::ActionResult> result(reinterpret_cast<sr::ActionResult*>(lp));
        if (result->done.empty())
        {
            std::wstring msgText = result->summary.empty() ? Str(IDS_ACTION_FAILED) : result->summary;
            for (size_t i = 0; i < result->failed.size() && i < 5; ++i)
                msgText += L"\n\n" + result->failed[i].path + L"\n" + result->failed[i].error;
            MessageBoxW(m_hwnd, msgText.c_str(), Str(IDS_APP_NAME).c_str(), MB_OK | MB_ICONERROR);
            SetBusy(false);
            return 0;
        }
        // Map finished paths back to nodes (skip those nested inside another finished node).
        std::vector<sr::u32> detached;
        for (size_t i = 0; i < m_pendingPaths.size(); ++i)
            for (const std::wstring& d : result->done)
                if (_wcsicmp(d.c_str(), m_pendingPaths[i].c_str()) == 0) { detached.push_back(m_pendingNodes[i]); break; }
        if (!result->failed.empty())
        {
            std::wstring msgText = Strf(IDS_ACTION_PARTIAL, sr::FormatCount(result->failed.size()).c_str());
            for (size_t i = 0; i < result->failed.size() && i < 5; ++i)
                msgText += L"\n\n" + result->failed[i].path + L"\n" + result->failed[i].error;
            MessageBoxW(m_hwnd, msgText.c_str(), Str(IDS_APP_NAME).c_str(), MB_OK | MB_ICONWARNING);
        }
        m_busy = false;
        App::Get().OnActionApplied(*result, detached);
        DestroyWindow(m_hwnd);
        return 0;
    }
    case WM_CLOSE:
        if (!m_busy) DestroyWindow(m_hwnd);
        return 0;
    case WM_DESTROY:
        m_alive = false;
        PostMessageW(nullptr, WM_NULL, 0, 0);
        return 0;
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}
}

void ShowReviewDialog(HWND owner, const ReviewRequest& request)
{
    Dialog d(request);
    d.Run(owner);
}
}
