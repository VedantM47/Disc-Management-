// SpaceRescue UI - SPDX-License-Identifier: GPL-3.0-or-later

#include "folders_page.h"
#include "widgets.h"

namespace ui
{
namespace
{
const wchar_t* kClass = L"SpaceRescueFolders";
const size_t kMaxChildren = 2000;     // children inserted per folder; the rest is summarised
const sr::u32 kMoreMarker = 0xFFFFFFFEu;
enum { CMD_OPEN = 1, CMD_INTERNALS, CMD_COPY, CMD_REVIEW };

// TVM_GETITEMRECT takes the item handle in the first bytes of the RECT (the macro type-puns).
bool ItemRect(HWND tv, HTREEITEM item, RECT* out, bool textOnly)
{
    union { RECT rc; HTREEITEM h; } u;
    u.h = item;
    const bool ok = SendMessageW(tv, TVM_GETITEMRECT, textOnly ? TRUE : FALSE, reinterpret_cast<LPARAM>(&u)) != 0;
    *out = u.rc;
    return ok;
}

std::wstring ShareLabel()
{
    const sr::DriveInfo* d = App::Get().TreeDrive();
    const sr::ScanTree* t = App::Get().Tree();
    if (d) return d->root.substr(0, 2);
    return t ? t->rootPath : L"";
}

sr::u64 ShareTotal()
{
    const sr::DriveInfo* d = App::Get().TreeDrive();
    if (d && d->total) return d->total;
    const sr::ScanTree* t = App::Get().Tree();
    return t ? t->At(0).size : 0;
}
}

HWND FoldersPage::Create(HWND parent)
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

LRESULT CALLBACK FoldersPage::Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    FoldersPage* self;
    if (msg == WM_NCCREATE)
    {
        self = static_cast<FoldersPage*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
    }
    else
        self = reinterpret_cast<FoldersPage*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
    return self->Handle(msg, wp, lp);
}

void FoldersPage::Restyle()
{
    if (!m_tree) return;
    StyleTreeView(m_tree);
    Layout();
    InvalidateRect(m_hwnd, nullptr, TRUE);
}

void FoldersPage::Layout()
{
    if (!m_tree) return;
    const Theme& t = T();
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    const int m = t.S(24);
    m_treeRect = { m, t.S(88), rc.right - m, rc.bottom - t.S(64) };
    if (m_treeRect.bottom < m_treeRect.top + t.S(80)) m_treeRect.bottom = m_treeRect.top + t.S(80);
    const bool has = App::Get().Tree() != nullptr;
    ShowWindow(m_tree, has ? SW_SHOW : SW_HIDE);
    // 1 px inset leaves room for the card border painted around the tree.
    MoveWindow(m_tree, m_treeRect.left + 1, m_treeRect.top + 1, m_treeRect.right - m_treeRect.left - 2,
               m_treeRect.bottom - m_treeRect.top - 2, TRUE);
}

std::wstring FoldersPage::ItemText(sr::u32 node) const
{
    const sr::ScanTree* tree = App::Get().Tree();
    if (!tree || node >= tree->Count()) return L"";
    const sr::Node& n = tree->At(node);
    const sr::u64 total = ShareTotal();
    std::wstring name = node == 0 ? tree->rootPath : tree->Name(node);
    std::wstring text = Strf(IDS_FP_ITEM, name.c_str(), sr::FormatBytes(n.size).c_str(),
                             total ? sr::FormatPercent(static_cast<double>(n.size) / static_cast<double>(total)).c_str() : L"-");
    if (n.flags & sr::NF_CLOUD) text += L"  " + Str(IDS_FP_CLOUD);
    if (n.flags & sr::NF_REPARSE) text += L"  " + Str(IDS_FP_LINK);
    if (n.flags & sr::NF_ERROR) text += L"  " + Str(IDS_FP_ERROR);
    return text;
}

sr::u32 FoldersPage::NodeOf(HTREEITEM item) const
{
    if (!item) return sr::NoNode;
    TVITEMW it = {};
    it.mask = TVIF_PARAM;
    it.hItem = item;
    TreeView_GetItem(m_tree, &it);
    return static_cast<sr::u32>(it.lParam);
}

HTREEITEM FoldersPage::InsertNode(HTREEITEM parent, sr::u32 node, bool isMore, sr::u64 moreCount)
{
    const sr::ScanTree* tree = App::Get().Tree();
    TVINSERTSTRUCTW is = {};
    is.hParent = parent;
    is.hInsertAfter = TVI_LAST;
    is.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_CHILDREN;
    std::wstring more;
    if (isMore)
    {
        more = Strf(IDS_FP_MORE, sr::FormatCount(moreCount).c_str());
        is.item.pszText = const_cast<wchar_t*>(more.c_str());
        is.item.lParam = kMoreMarker;
        is.item.cChildren = 0;
    }
    else
    {
        is.item.pszText = LPSTR_TEXTCALLBACKW;
        is.item.lParam = node;
        is.item.cChildren = tree->At(node).firstChild != sr::NoNode ? 1 : 0;
    }
    return TreeView_InsertItem(m_tree, &is);
}

void FoldersPage::Populate(HTREEITEM parent, sr::u32 node)
{
    const sr::ScanTree* tree = App::Get().Tree();
    if (!tree) return;
    const std::vector<sr::u32> kids = tree->Children(node);
    SendMessageW(m_tree, WM_SETREDRAW, FALSE, 0);
    const size_t n = std::min(kids.size(), kMaxChildren);
    for (size_t i = 0; i < n; ++i) InsertNode(parent, kids[i], false, 0);
    if (kids.size() > n) InsertNode(parent, 0, true, kids.size() - n);
    SendMessageW(m_tree, WM_SETREDRAW, TRUE, 0);
}

void FoldersPage::Reload()
{
    if (!m_tree) return;
    const sr::ScanTree* tree = App::Get().Tree();
    std::wstring keep;
    const sr::u32 sel = NodeOf(TreeView_GetSelection(m_tree));
    if (tree && sel != sr::NoNode && sel != kMoreMarker && sel < tree->Count() && sel != 0) keep = tree->Path(sel);
    TreeView_DeleteAllItems(m_tree);
    if (tree)
    {
        HTREEITEM root = InsertNode(TVI_ROOT, 0, false, 0);
        TreeView_Expand(m_tree, root, TVE_EXPAND);
        TreeView_SelectItem(m_tree, root);
        if (!keep.empty())
        {
            const sr::u32 again = tree->Find(keep);
            if (again != sr::NoNode) Select(again);
        }
    }
    Layout();
    InvalidateRect(m_hwnd, nullptr, TRUE);
}

void FoldersPage::Select(sr::u32 node)
{
    const sr::ScanTree* tree = App::Get().Tree();
    if (!tree || !m_tree || node >= tree->Count()) return;
    std::vector<sr::u32> chain;
    for (sr::u32 n = node; n != sr::NoNode; n = tree->At(n).parent) chain.push_back(n);
    std::reverse(chain.begin(), chain.end());
    HTREEITEM cur = TreeView_GetRoot(m_tree);
    for (size_t i = 1; cur && i < chain.size(); ++i)
    {
        TreeView_Expand(m_tree, cur, TVE_EXPAND);
        HTREEITEM child = TreeView_GetChild(m_tree, cur);
        while (child && NodeOf(child) != chain[i]) child = TreeView_GetNextSibling(m_tree, child);
        if (!child) break;
        cur = child;
    }
    if (cur)
    {
        TreeView_SelectItem(m_tree, cur);
        TreeView_EnsureVisible(m_tree, cur);
    }
}

void FoldersPage::Paint()
{
    const Theme& t = T();
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(m_hwnd, &ps);
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    FillRect(dc, &rc, t.bgBrush);
    Painter p(dc, rc.right, nullptr);
    const int m = t.S(24);
    const int w = rc.right - 2 * m;
    p.Text(m, t.S(20), w, Str(IDS_FP_TITLE), t.title, t.text, DT_SINGLELINE);
    const sr::ScanTree* tree = App::Get().Tree();
    if (!tree)
    {
        p.Text(m, t.S(88), w, Str(IDS_FP_EMPTY), t.font, t.dim);
        EndPaint(m_hwnd, &ps);
        return;
    }
    p.Text(m, t.S(52), w, Strf(IDS_FP_HINT, ShareLabel().c_str()), t.font, t.dim, DT_SINGLELINE | DT_END_ELLIPSIS);
    // Card border around the tree.
    HBRUSH nb = static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
    HPEN pen = CreatePen(PS_SOLID, 1, t.border);
    HGDIOBJ op = SelectObject(dc, pen), ob = SelectObject(dc, nb);
    RoundRect(dc, m_treeRect.left, m_treeRect.top, m_treeRect.right, m_treeRect.bottom, t.S(6), t.S(6));
    SelectObject(dc, op);
    SelectObject(dc, ob);
    DeleteObject(pen);
    // Footer: the selected item with its numbers in context.
    const sr::u32 sel = NodeOf(TreeView_GetSelection(m_tree));
    if (sel != sr::NoNode && sel != kMoreMarker && sel < tree->Count())
    {
        const sr::Node& n = tree->At(sel);
        const int fy = m_treeRect.bottom + t.S(10);
        RECT pr = { m, fy, rc.right - m, fy + t.S(20) };
        p.TextBox(pr, tree->Path(sel), t.bold, t.text, DT_SINGLELINE | DT_PATH_ELLIPSIS);
        std::wstring line = SizeShare(n.size, ShareTotal(), ShareLabel());
        if (tree->IsDir(sel)) line += L" · " + Strf(IDS_INS_FILES_COUNT, sr::FormatCount(n.files).c_str());
        if (n.writeTime) line += L" · " + Strf(IDS_FP_MODIFIED, sr::FormatAge(n.writeTime).c_str());
        RECT lr = { m, fy + t.S(22), rc.right - m, fy + t.S(42) };
        p.TextBox(lr, line, t.font, t.dim, DT_SINGLELINE);
    }
    EndPaint(m_hwnd, &ps);
}

LRESULT FoldersPage::CustomDraw(NMTVCUSTOMDRAW* cd)
{
    switch (cd->nmcd.dwDrawStage)
    {
    case CDDS_PREPAINT:
        return CDRF_NOTIFYITEMDRAW;
    case CDDS_ITEMPREPAINT:
        return CDRF_NOTIFYPOSTPAINT;
    case CDDS_ITEMPOSTPAINT:
    {
        const sr::ScanTree* tree = App::Get().Tree();
        const sr::u32 node = static_cast<sr::u32>(cd->nmcd.lItemlParam);
        if (!tree || node == kMoreMarker || node >= tree->Count()) break;
        const Theme& t = T();
        RECT row, textRc, client;
        HTREEITEM item = reinterpret_cast<HTREEITEM>(cd->nmcd.dwItemSpec);
        GetClientRect(m_tree, &client);
        ItemRect(m_tree, item, &row, false);
        ItemRect(m_tree, item, &textRc, true);
        // Size bar: share of the parent folder, right-aligned.
        const int barW = t.S(120), barH = t.S(8);
        RECT bar = { client.right - barW - t.S(24), row.top + (row.bottom - row.top - barH) / 2, client.right - t.S(24), 0 };
        bar.bottom = bar.top + barH;
        if (textRc.right + t.S(16) >= bar.left) break;
        const sr::u32 parent = tree->At(node).parent;
        const sr::u64 base = parent == sr::NoNode ? tree->At(node).size : tree->At(parent).size;
        const double frac = base ? static_cast<double>(tree->At(node).size) / static_cast<double>(base) : 0.0;
        Painter p(cd->nmcd.hdc, client.right, nullptr);
        p.Round(bar, barH / 2, t.track);
        if (frac > 0)
        {
            RECT fill = bar;
            fill.right = bar.left + std::max(barH, static_cast<int>((bar.right - bar.left) * std::min(1.0, frac)));
            p.Round(fill, barH / 2, t.accent);
        }
        break;
    }
    }
    return CDRF_DODEFAULT;
}

void FoldersPage::ContextMenu(int sx, int sy, bool fromKeyboard)
{
    const sr::ScanTree* tree = App::Get().Tree();
    if (!tree) return;
    HTREEITEM item = nullptr;
    if (fromKeyboard)
    {
        item = TreeView_GetSelection(m_tree);
        RECT r;
        if (item && ItemRect(m_tree, item, &r, true))
        {
            POINT pt = { r.left, r.bottom };
            ClientToScreen(m_tree, &pt);
            sx = pt.x;
            sy = pt.y;
        }
    }
    else
    {
        TVHITTESTINFO h = {};
        h.pt = { sx, sy };
        ScreenToClient(m_tree, &h.pt);
        item = TreeView_HitTest(m_tree, &h);
        if (item) TreeView_SelectItem(m_tree, item);
    }
    const sr::u32 node = NodeOf(item);
    if (node == sr::NoNode || node == kMoreMarker || node >= tree->Count()) return;
    const bool isFile = !tree->IsDir(node);
    const sr::Node& nd = tree->At(node);
    const bool removable = node != 0 && !(nd.flags & sr::NF_AGGREGATE);
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, CMD_OPEN, Str(IDS_FP_MENU_OPEN).c_str());
    AppendMenuW(menu, MF_STRING | (isFile ? 0 : MF_GRAYED), CMD_INTERNALS, Str(IDS_FP_MENU_INTERNALS).c_str());
    AppendMenuW(menu, MF_STRING, CMD_COPY, Str(IDS_FP_MENU_COPY).c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (removable ? 0 : MF_GRAYED), CMD_REVIEW, Str(IDS_FP_MENU_REVIEW).c_str());
    const int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, sx, sy, 0, m_hwnd, nullptr);
    DestroyMenu(menu);
    const std::wstring path = tree->Path(node);
    switch (cmd)
    {
    case CMD_OPEN:
        if (isFile) RevealInExplorer(m_hwnd, path);
        else OpenFolderInExplorer(m_hwnd, path);
        break;
    case CMD_INTERNALS:
        App::Get().ShowFileInInternals(path);
        break;
    case CMD_COPY:
        if (OpenClipboard(m_hwnd))
        {
            EmptyClipboard();
            const size_t bytes = (path.size() + 1) * sizeof(wchar_t);
            HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, bytes);
            if (g)
            {
                memcpy(GlobalLock(g), path.c_str(), bytes);
                GlobalUnlock(g);
                SetClipboardData(CF_UNICODETEXT, g);
            }
            CloseClipboard();
        }
        break;
    case CMD_REVIEW:
        if (App::Get().Analyzing())
        {
            MessageBoxW(m_hwnd, Str(IDS_FP_WAIT_ANALYSIS).c_str(), Str(IDS_APP_NAME).c_str(), MB_OK | MB_ICONINFORMATION);
            break;
        }
        {
            ReviewRequest req;
            req.title = tree->Name(node);
            req.blurb = Strf(IDS_FP_REVIEW_BLURB, SizeShare(nd.size, ShareTotal(), ShareLabel()).c_str());
            req.nodes.push_back(node);
            req.preselect = true;
            App::Get().OpenReview(req);
        }
        break;
    }
}

LRESULT FoldersPage::Handle(UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_CREATE:
        m_tree = CreateWindowExW(0, WC_TREEVIEWW, L"",
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT |
                                     TVS_SHOWSELALWAYS | TVS_FULLROWSELECT,
                                 0, 0, 10, 10, m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(1)),
                                 GetModuleHandleW(nullptr), nullptr);
        StyleTreeView(m_tree);
        return 0;
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
        SetFocus(m_tree);
        return 0;
    case WM_CONTEXTMENU:
        if (reinterpret_cast<HWND>(wp) == m_tree)
            ContextMenu(static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp)), lp == -1);
        return 0;
    case WM_NOTIFY:
    {
        NMHDR* h = reinterpret_cast<NMHDR*>(lp);
        if (h->hwndFrom != m_tree) break;
        switch (h->code)
        {
        case TVN_GETDISPINFOW:
        {
            NMTVDISPINFOW* di = reinterpret_cast<NMTVDISPINFOW*>(lp);
            if (di->item.mask & TVIF_TEXT)
            {
                static std::wstring ring[4];
                static int slot = 0;
                slot = (slot + 1) & 3;
                ring[slot] = ItemText(static_cast<sr::u32>(di->item.lParam));
                di->item.pszText = const_cast<wchar_t*>(ring[slot].c_str());
            }
            return 0;
        }
        case TVN_ITEMEXPANDINGW:
        {
            NMTREEVIEWW* nm = reinterpret_cast<NMTREEVIEWW*>(lp);
            if (nm->action == TVE_EXPAND && !TreeView_GetChild(m_tree, nm->itemNew.hItem))
                Populate(nm->itemNew.hItem, static_cast<sr::u32>(nm->itemNew.lParam));
            return FALSE;
        }
        case TVN_SELCHANGEDW:
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return 0;
        case NM_DBLCLK:
        {
            const sr::ScanTree* tree = App::Get().Tree();
            const sr::u32 node = NodeOf(TreeView_GetSelection(m_tree));
            if (tree && node != sr::NoNode && node != kMoreMarker && node < tree->Count() && !tree->IsDir(node))
            {
                App::Get().ShowFileInInternals(tree->Path(node));
                return 1;
            }
            break;
        }
        case TVN_KEYDOWN:
        {
            TV_KEYDOWN* kd = reinterpret_cast<TV_KEYDOWN*>(lp);
            const sr::ScanTree* tree = App::Get().Tree();
            const sr::u32 node = NodeOf(TreeView_GetSelection(m_tree));
            if (kd->wVKey == VK_RETURN && tree && node != sr::NoNode && node != kMoreMarker && node < tree->Count() && !tree->IsDir(node))
                App::Get().ShowFileInInternals(tree->Path(node));
            return 0;
        }
        case NM_CUSTOMDRAW:
            return CustomDraw(reinterpret_cast<NMTVCUSTOMDRAW*>(lp));
        }
        break;
    }
    case WM_SR_THEME:
        Restyle();
        return 0;
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}
}
