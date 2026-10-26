// SpaceRescue UI - SPDX-License-Identifier: GPL-3.0-or-later

#include "widgets.h"
#include <cmath>

namespace ui
{
COLORREF Blend(COLORREF a, COLORREF b, int pct)
{
    auto mix = [pct](int x, int y) { return (x * (100 - pct) + y * pct) / 100; };
    return RGB(mix(GetRValue(a), GetRValue(b)), mix(GetGValue(a), GetGValue(b)), mix(GetBValue(a), GetBValue(b)));
}

std::wstring ShortenPath(const std::wstring& path, size_t maxChars)
{
    if (path.size() <= maxChars || maxChars < 12) return path;
    const size_t tail = maxChars - 6;
    return path.substr(0, 3) + L"…" + path.substr(path.size() - tail);
}

// --- Painter ---------------------------------------------------------------------

Painter::Painter(HDC d, int w, Canvas* owner) : dc(d), width(w), t(T()), m_cv(owner)
{
    SetBkMode(dc, TRANSPARENT);
}

void Painter::Fill(const RECT& r, COLORREF c)
{
    HBRUSH b = CreateSolidBrush(c);
    FillRect(dc, &r, b);
    DeleteObject(b);
}

void Painter::Round(const RECT& r, int radius, COLORREF fill, COLORREF border)
{
    HBRUSH b = CreateSolidBrush(fill);
    HPEN pen = border == CLR_NONE ? static_cast<HPEN>(GetStockObject(NULL_PEN)) : CreatePen(PS_SOLID, 1, border);
    HGDIOBJ ob = SelectObject(dc, b);
    HGDIOBJ op = SelectObject(dc, pen);
    RoundRect(dc, r.left, r.top, r.right + 1, r.bottom + 1, radius * 2, radius * 2);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(b);
    if (border != CLR_NONE) DeleteObject(pen);
}

int Painter::Text(int x, int y, int w, const std::wstring& s, HFONT f, COLORREF c, UINT flags)
{
    if (s.empty()) return 0;
    HGDIOBJ old = SelectObject(dc, f);
    SetTextColor(dc, c);
    UINT fl = flags | DT_NOPREFIX;
    if (w > 0 && !(flags & DT_SINGLELINE)) fl |= DT_WORDBREAK;
    RECT r = { x, y, x + (w > 0 ? w : 4000), y + 4000 };
    RECT calc = r;
    DrawTextW(dc, s.c_str(), static_cast<int>(s.size()), &calc, fl | DT_CALCRECT);
    const int h = calc.bottom - calc.top;
    r.bottom = y + h;
    if (w <= 0) fl |= DT_NOCLIP;
    DrawTextW(dc, s.c_str(), static_cast<int>(s.size()), &r, fl);
    SelectObject(dc, old);
    return h;
}

void Painter::TextBox(const RECT& box, const std::wstring& s, HFONT f, COLORREF c, UINT flags)
{
    if (s.empty()) return;
    HGDIOBJ old = SelectObject(dc, f);
    SetTextColor(dc, c);
    RECT r = box;
    DrawTextW(dc, s.c_str(), static_cast<int>(s.size()), &r, flags | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dc, old);
}

int Painter::TextWidth(const std::wstring& s, HFONT f)
{
    HGDIOBJ old = SelectObject(dc, f);
    SIZE sz = {};
    GetTextExtentPoint32W(dc, s.c_str(), static_cast<int>(s.size()), &sz);
    SelectObject(dc, old);
    return sz.cx;
}

int Painter::TextHeight(int w, const std::wstring& s, HFONT f)
{
    if (s.empty()) return 0;
    HGDIOBJ old = SelectObject(dc, f);
    RECT r = { 0, 0, w > 0 ? w : 4000, 4000 };
    DrawTextW(dc, s.c_str(), static_cast<int>(s.size()), &r, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, old);
    return r.bottom;
}

int Painter::TextRight(int right, int y, const std::wstring& s, HFONT f, COLORREF c)
{
    const int w = TextWidth(s, f);
    Text(right - w, y, 0, s, f, c, DT_SINGLELINE);
    return right - w;
}

void Painter::Bar(const RECT& r, double frac, COLORREF fill)
{
    const int h = r.bottom - r.top;
    Round(r, h / 2, t.track);
    if (frac <= 0) return;
    if (frac > 1) frac = 1;
    RECT f = r;
    f.right = r.left + std::max(h, static_cast<int>((r.right - r.left) * frac));
    if (f.right > r.right) f.right = r.right;
    Round(f, h / 2, fill);
}

void Painter::Ring(int cx, int cy, int radius, int thickness, double frac, COLORREF fill, COLORREF track, COLORREF hole)
{
    HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
    HBRUSH tb = CreateSolidBrush(track);
    HGDIOBJ oldBrush = SelectObject(dc, tb);
    Ellipse(dc, cx - radius, cy - radius, cx + radius + 1, cy + radius + 1);
    if (frac > 0)
    {
        HBRUSH fb = CreateSolidBrush(fill);
        SelectObject(dc, fb);
        if (frac >= 0.999)
            Ellipse(dc, cx - radius, cy - radius, cx + radius + 1, cy + radius + 1);
        else
        {
            const double a = frac * 6.283185307179586;
            const int ex = cx + static_cast<int>(std::lround(std::sin(a) * radius * 4));
            const int ey = cy - static_cast<int>(std::lround(std::cos(a) * radius * 4));
            const int oldDir = SetArcDirection(dc, AD_CLOCKWISE);
            Pie(dc, cx - radius, cy - radius, cx + radius + 1, cy + radius + 1, cx, cy - radius * 4, ex, ey);
            SetArcDirection(dc, oldDir);
        }
        SelectObject(dc, tb);
        DeleteObject(fb);
    }
    HBRUSH hb = CreateSolidBrush(hole);
    SelectObject(dc, hb);
    const int r2 = radius - thickness;
    Ellipse(dc, cx - r2, cy - r2, cx + r2 + 1, cy + r2 + 1);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(tb);
    DeleteObject(hb);
}

int Painter::Chip(int x, int y, const std::wstring& s, COLORREF c)
{
    const int w = TextWidth(s, t.caption) + S(16);
    RECT r = { x, y, x + w, y + S(20) };
    Round(r, S(10), Blend(t.card, c, 12), Blend(t.card, c, 45));
    Text(x + S(8), y + S(2), 0, s, t.caption, c, DT_SINGLELINE);
    return w;
}

int Painter::ButtonWidth(const std::wstring& s, BtnStyle style)
{
    int w = TextWidth(s, style == BtnStyle::Primary || style == BtnStyle::TabSelected ? t.bold : t.font);
    w += style == BtnStyle::Link ? S(8) : S(32);
    if (style == BtnStyle::Normal || style == BtnStyle::Primary || style == BtnStyle::Danger) w = std::max(w, S(80));
    return w;
}

RECT Painter::Button(int id, int x, int y, const std::wstring& s, BtnStyle style, bool enabled, int fixedWidth)
{
    const int w = fixedWidth > 0 ? fixedWidth : ButtonWidth(s, style);
    const int h = style == BtnStyle::Link ? S(24) : ButtonHeight();
    RECT r = { x, y, x + w, y + h };
    const bool hover = m_cv && m_cv->HoverId() == id && enabled;
    const bool pressed = hover && m_cv->PressedId() == id;
    COLORREF textColor = t.text;
    HFONT font = t.font;
    switch (style)
    {
    case BtnStyle::Primary:
    {
        COLORREF fill = pressed ? Blend(t.accent, t.dark ? RGB(255, 255, 255) : RGB(0, 0, 0), 25)
                                : hover ? Blend(t.accent, t.dark ? RGB(255, 255, 255) : RGB(0, 0, 0), 12) : t.accent;
        if (!enabled) fill = t.track;
        Round(r, S(4), fill);
        textColor = enabled ? t.onAccent : t.dim;
        font = t.bold;
        break;
    }
    case BtnStyle::Danger:
        Round(r, S(4), pressed ? Blend(t.card, t.danger, 25) : hover ? Blend(t.card, t.danger, 12) : t.card,
              enabled ? t.danger : t.border);
        textColor = enabled ? t.danger : t.dim;
        break;
    case BtnStyle::Link:
        textColor = enabled ? (hover ? Blend(t.accent, t.text, 30) : t.accent) : t.dim;
        break;
    case BtnStyle::Tab:
    case BtnStyle::TabSelected:
        if (hover) Round(r, S(4), t.hover);
        if (style == BtnStyle::TabSelected)
        {
            font = t.bold;
            RECT u = { r.left + S(12), r.bottom - S(3), r.right - S(12), r.bottom };
            Round(u, S(1), t.accent);
        }
        else
            textColor = t.dim;
        if (hover) textColor = t.text;
        break;
    default:
        Round(r, S(4), pressed ? t.track : hover ? t.hover : t.card, t.border);
        if (!enabled) textColor = t.dim;
        break;
    }
    RECT tr = r;
    if (style == BtnStyle::Tab || style == BtnStyle::TabSelected) tr.bottom -= S(3);
    HGDIOBJ old = SelectObject(dc, font);
    SetTextColor(dc, textColor);
    DrawTextW(dc, s.c_str(), static_cast<int>(s.size()), &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(dc, old);
    if (m_cv && m_cv->HasFocus() && m_cv->FocusId() == id)
    {
        RECT fr = { r.left - S(2), r.top - S(2), r.right + S(2), r.bottom + S(2) };
        HPEN pen = CreatePen(PS_SOLID, std::max(1, S(2)), t.accent);
        HGDIOBJ op = SelectObject(dc, pen);
        HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
        RoundRect(dc, fr.left, fr.top, fr.right, fr.bottom, S(8), S(8));
        SelectObject(dc, op);
        SelectObject(dc, ob);
        DeleteObject(pen);
    }
    if (m_cv) m_cv->AddHit(r, id, enabled);
    return r;
}

// --- Canvas ----------------------------------------------------------------------

static const wchar_t* kCanvasClass = L"SpaceRescueCanvas";

void RegisterCanvasClass()
{
    static bool done = false;
    if (done) return;
    done = true;
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = Canvas::Proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kCanvasClass;
    RegisterClassExW(&wc);
}

Canvas::~Canvas()
{
    if (m_hwnd && IsWindow(m_hwnd))
    {
        SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, 0);
        DestroyWindow(m_hwnd);
    }
}

HWND Canvas::Create(HWND parent, bool scrollable, bool tabStop)
{
    RegisterCanvasClass();
    m_scrollable = scrollable;
    m_hwnd = CreateWindowExW(0, kCanvasClass, L"", WS_CHILD | WS_CLIPSIBLINGS | (tabStop ? WS_TABSTOP : 0),
                             0, 0, 10, 10, parent, nullptr, GetModuleHandleW(nullptr), this);
    return m_hwnd;
}

void Canvas::Redraw()
{
    if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
}

int Canvas::Measure(int width)
{
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, 1, 1);
    HGDIOBJ old = SelectObject(mem, bmp);
    std::vector<Hit> saved = m_hits;
    Painter p(mem, width, this);
    const int h = OnPaint(p);
    m_hits = saved;
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    return h;
}

int Canvas::MaxScroll() const
{
    if (!m_scrollable || !m_hwnd) return 0;
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    return std::max(0, m_contentH - static_cast<int>(rc.bottom));
}

void Canvas::ScrollTo(int y)
{
    const int clamped = std::max(0, std::min(y, MaxScroll()));
    if (clamped == m_scroll) return;
    m_scroll = clamped;
    Redraw();
}

RECT Canvas::ThumbRect() const
{
    RECT none = {};
    if (!m_scrollable || !m_hwnd) return none;
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    const int max = MaxScroll();
    if (max <= 0) return none;
    const Theme& t = T();
    const int h = rc.bottom;
    const int thumbH = std::max(t.S(40), static_cast<int>(static_cast<long long>(h) * h / m_contentH));
    const int y = static_cast<int>(static_cast<long long>(m_scroll) * (h - thumbH) / max);
    RECT r = { rc.right - t.S(12), y + t.S(2), rc.right - t.S(4), y + thumbH - t.S(2) };
    return r;
}

int Canvas::HitAt(int x, int y) const
{
    for (const Hit& h : m_hits)
        if (h.enabled && x >= h.r.left && x < h.r.right && y >= h.r.top && y < h.r.bottom) return h.id;
    return -1;
}

void Canvas::EnsureVisible(const RECT& r)
{
    if (!m_scrollable) return;
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    const int pad = T().S(16);
    if (r.top - pad < m_scroll) ScrollTo(r.top - pad);
    else if (r.bottom + pad > m_scroll + rc.bottom) ScrollTo(r.bottom + pad - rc.bottom);
}

bool Canvas::MoveFocus(bool backwards)
{
    std::vector<const Hit*> list;
    for (const Hit& h : m_hits)
        if (h.enabled) list.push_back(&h);
    if (list.empty()) return false;
    int idx = -1;
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i]->id == m_focusId) idx = static_cast<int>(i);
    int next;
    if (idx < 0) next = backwards ? static_cast<int>(list.size()) - 1 : 0;
    else next = backwards ? idx - 1 : idx + 1;
    if (next < 0 || next >= static_cast<int>(list.size())) return false;
    m_focusId = list[next]->id;
    EnsureVisible(list[next]->r);
    Redraw();
    return true;
}

void Canvas::Paint()
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(m_hwnd, &ps);
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    const int w = rc.right, h = rc.bottom;
    if (w > 0 && h > 0)
    {
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, w, h);
        HGDIOBJ old = SelectObject(mem, bmp);
        HBRUSH bg = CreateSolidBrush(Background());
        FillRect(mem, &rc, bg);
        DeleteObject(bg);
        SetViewportOrgEx(mem, 0, -m_scroll, nullptr);
        m_hits.clear();
        int contentH;
        {
            Painter p(mem, w, this);
            contentH = OnPaint(p);
        }
        SetViewportOrgEx(mem, 0, 0, nullptr);
        if (m_hasFocus && m_focusId < 0)
            for (const Hit& hit : m_hits)
                if (hit.enabled) { m_focusId = hit.id; break; }
        const RECT th = ThumbRect();
        if (th.right > th.left)
        {
            const Theme& t = T();
            HBRUSH b = CreateSolidBrush(m_dragThumb ? t.dim : Blend(t.bg, t.dim, 60));
            HGDIOBJ ob = SelectObject(mem, b);
            HGDIOBJ op = SelectObject(mem, GetStockObject(NULL_PEN));
            RoundRect(mem, th.left, th.top, th.right, th.bottom, th.right - th.left, th.right - th.left);
            SelectObject(mem, ob);
            SelectObject(mem, op);
            DeleteObject(b);
        }
        BitBlt(hdc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        if (contentH != m_contentH)
        {
            m_contentH = contentH;
            m_scroll = std::max(0, std::min(m_scroll, MaxScroll()));
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
    }
    EndPaint(m_hwnd, &ps);
}

LRESULT CALLBACK Canvas::Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    Canvas* self;
    if (msg == WM_NCCREATE)
    {
        self = static_cast<Canvas*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
    }
    else
        self = reinterpret_cast<Canvas*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
    return self->Handle(msg, wp, lp);
}

LRESULT Canvas::Handle(UINT msg, WPARAM wp, LPARAM lp)
{
    const Theme& t = T();
    switch (msg)
    {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        Paint();
        return 0;
    case WM_SIZE:
        m_scroll = std::max(0, std::min(m_scroll, MaxScroll()));
        Redraw();
        return 0;
    case WM_GETDLGCODE:
    {
        LRESULT code = DLGC_WANTARROWS;
        const MSG* m = reinterpret_cast<const MSG*>(lp);
        if (m && m->message == WM_KEYDOWN)
        {
            if (m->wParam == VK_RETURN) code |= DLGC_WANTALLKEYS;
            if (m->wParam == VK_TAB)
            {
                // Keep Tab inside the canvas while there is another button in that direction.
                const bool back = GetKeyState(VK_SHIFT) < 0;
                int idx = -1, count = 0, last = -1;
                for (const Hit& h : m_hits)
                    if (h.enabled)
                    {
                        if (h.id == m_focusId) idx = count;
                        last = count++;
                    }
                if (count > 0 && ((!back && idx < last) || (back && idx != 0))) code |= DLGC_WANTTAB;
            }
        }
        return code;
    }
    case WM_SETFOCUS:
        m_hasFocus = true;
        Redraw();
        return 0;
    case WM_KILLFOCUS:
        m_hasFocus = false;
        Redraw();
        return 0;
    case WM_MOUSEMOVE:
    {
        const int x = static_cast<short>(LOWORD(lp)), y = static_cast<short>(HIWORD(lp));
        if (!m_tracking)
        {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hwnd, 0 };
            TrackMouseEvent(&tme);
            m_tracking = true;
        }
        if (m_dragThumb)
        {
            RECT rc;
            GetClientRect(m_hwnd, &rc);
            const RECT th = ThumbRect();
            const int thumbH = th.bottom - th.top + t.S(4);
            const int range = rc.bottom - thumbH;
            if (range > 0) ScrollTo(static_cast<int>(static_cast<long long>(y - m_dragOffset) * MaxScroll() / range));
            return 0;
        }
        const int hover = HitAt(x, y + m_scroll);
        if (hover != m_hoverId)
        {
            m_hoverId = hover;
            Redraw();
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        m_tracking = false;
        if (m_hoverId != -1)
        {
            m_hoverId = -1;
            Redraw();
        }
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    {
        const int x = static_cast<short>(LOWORD(lp)), y = static_cast<short>(HIWORD(lp));
        SetFocus(m_hwnd);
        m_focusVisible = false;
        RECT th = ThumbRect();
        if (th.right > th.left)
        {
            RECT grab = { th.left - t.S(6), 0, th.right + t.S(4), 100000 };
            if (x >= grab.left && x < grab.right)
            {
                if (y >= th.top && y < th.bottom)
                {
                    m_dragThumb = true;
                    m_dragOffset = y - th.top + t.S(2);
                    SetCapture(m_hwnd);
                }
                else
                {
                    RECT rc;
                    GetClientRect(m_hwnd, &rc);
                    ScrollTo(m_scroll + (y < th.top ? -rc.bottom : rc.bottom) * 9 / 10);
                }
                return 0;
            }
        }
        const int id = HitAt(x, y + m_scroll);
        if (id >= 0)
        {
            m_pressedId = id;
            m_focusId = id;
            SetCapture(m_hwnd);
        }
        Redraw();
        return 0;
    }
    case WM_LBUTTONUP:
    {
        const int x = static_cast<short>(LOWORD(lp)), y = static_cast<short>(HIWORD(lp));
        const bool wasDrag = m_dragThumb;
        const int pressed = m_pressedId;
        m_dragThumb = false;
        m_pressedId = -1;
        if (GetCapture() == m_hwnd) ReleaseCapture();
        Redraw();
        if (!wasDrag && pressed >= 0 && HitAt(x, y + m_scroll) == pressed) OnButton(pressed);
        return 0;
    }
    case WM_MOUSEWHEEL:
        if (m_scrollable)
        {
            const int delta = GET_WHEEL_DELTA_WPARAM(wp);
            ScrollTo(m_scroll - delta * t.S(60) / WHEEL_DELTA);
        }
        return 0;
    case WM_KEYDOWN:
    {
        RECT rc;
        GetClientRect(m_hwnd, &rc);
        const bool ctrl = GetKeyState(VK_CONTROL) < 0;
        switch (wp)
        {
        case VK_TAB:
            m_focusVisible = true;
            MoveFocus(GetKeyState(VK_SHIFT) < 0);
            return 0;
        case VK_RETURN:
        case VK_SPACE:
            for (const Hit& h : m_hits)
                if (h.enabled && h.id == m_focusId)
                {
                    const int id = h.id;
                    OnButton(id);
                    break;
                }
            return 0;
        case VK_LEFT:
        case VK_RIGHT:
            if (m_arrowsNavigate)
            {
                m_focusVisible = true;
                MoveFocus(wp == VK_LEFT);
                return 0;
            }
            break;
        case VK_UP: if (m_scrollable) { ScrollTo(m_scroll - t.S(48)); return 0; } break;
        case VK_DOWN: if (m_scrollable) { ScrollTo(m_scroll + t.S(48)); return 0; } break;
        case VK_PRIOR: if (m_scrollable) { ScrollTo(m_scroll - rc.bottom * 9 / 10); return 0; } break;
        case VK_NEXT: if (m_scrollable) { ScrollTo(m_scroll + rc.bottom * 9 / 10); return 0; } break;
        case VK_HOME: if (m_scrollable && !ctrl) { ScrollTo(0); return 0; } break;
        case VK_END: if (m_scrollable && !ctrl) { ScrollTo(m_contentH); return 0; } break;
        }
        SendMessageW(GetParent(m_hwnd), WM_SR_KEY, wp, lp);
        return 0;
    }
    case WM_CAPTURECHANGED:
        m_dragThumb = false;
        m_pressedId = -1;
        return 0;
    case WM_DESTROY:
        SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, 0);
        m_hwnd = nullptr;
        return 0;
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}
}

namespace ui
{
std::wstring FormatDate(sr::u64 ticks)
{
    FILETIME ft;
    ft.dwLowDateTime = static_cast<DWORD>(ticks & 0xFFFFFFFFu);
    ft.dwHighDateTime = static_cast<DWORD>(ticks >> 32);
    SYSTEMTIME st;
    if (!FileTimeToSystemTime(&ft, &st)) return L"";
    return sr::Fmt(L"%04d-%02d-%02d", static_cast<int>(st.wYear), static_cast<int>(st.wMonth), static_cast<int>(st.wDay));
}

std::wstring SizeShare(sr::u64 bytes, sr::u64 total, const std::wstring& label)
{
    const std::wstring size = sr::FormatBytes(bytes);
    if (total == 0) return size;
    const std::wstring pct = sr::FormatPercent(static_cast<double>(bytes) / static_cast<double>(total));
    return label.empty() ? Strf(IDS_SHARE, size.c_str(), pct.c_str()) : Strf(IDS_SHARE_OF, size.c_str(), pct.c_str(), label.c_str());
}

void RevealInExplorer(HWND owner, const std::wstring& path)
{
    const std::wstring args = L"/select,\"" + path + L"\"";
    ShellExecuteW(owner, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
}

void OpenFolderInExplorer(HWND owner, const std::wstring& path)
{
    const DWORD attr = GetFileAttributesW(path.c_str());
    const std::wstring folder = (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) ? path : sr::ParentPath(path);
    ShellExecuteW(owner, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}
}

namespace ui
{
std::wstring FormatDuration(sr::u64 millis)
{
    const sr::u64 secs = millis / 1000;
    if (secs < 60)
    {
        const std::wstring v = sr::Fmt(L"%d.%d", static_cast<int>(secs), static_cast<int>((millis % 1000) / 100));
        return Strf(IDS_DUR_SECONDS, v.c_str());
    }
    if (secs < 3600) return Strf(IDS_DUR_MINUTES, static_cast<int>(secs / 60), static_cast<int>(secs % 60));
    return Strf(IDS_DUR_HOURS, static_cast<int>(secs / 3600), static_cast<int>((secs % 3600) / 60));
}
}
