// SpaceRescue UI - SPDX-License-Identifier: GPL-3.0-or-later

#include "theme.h"
#include <uxtheme.h>

namespace ui
{
static Theme g_theme;
Theme& T() { return g_theme; }

std::wstring Str(UINT id)
{
    wchar_t buf[1024];
    const int n = LoadStringW(GetModuleHandleW(nullptr), id, buf, 1023);
    if (n <= 0) return L"[string " + sr::Fmt(L"%u", id) + L"]";
    std::wstring s(buf, static_cast<size_t>(n));
    static const struct { const wchar_t* token; const wchar_t* text; } map[] = {
        { L"{ndash}", L"–" }, { L"{mdash}", L"—" }, { L"{dot}", L"·" }, { L"{ell}", L"…" } };
    for (const auto& m : map)
    {
        size_t pos = 0;
        const size_t len = wcslen(m.token);
        while ((pos = s.find(m.token, pos)) != std::wstring::npos)
        {
            s.replace(pos, len, m.text);
            ++pos;
        }
    }
    return s;
}

bool SystemUsesDarkMode()
{
    DWORD value = 1, size = sizeof(value);
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &key) != ERROR_SUCCESS)
        return false;
    const LONG rc = RegQueryValueExW(key, L"AppsUseLightTheme", nullptr, nullptr, reinterpret_cast<LPBYTE>(&value), &size);
    RegCloseKey(key);
    return rc == ERROR_SUCCESS && value == 0;
}

int DpiForWindow(HWND hwnd)
{
    typedef UINT (WINAPI *GetDpiForWindowFn)(HWND);
    static GetDpiForWindowFn fn = reinterpret_cast<GetDpiForWindowFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    if (fn && hwnd)
    {
        const UINT d = fn(hwnd);
        if (d) return static_cast<int>(d);
    }
    HDC dc = GetDC(nullptr);
    const int d = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(nullptr, dc);
    return d ? d : 96;
}

static int CALLBACK FaceProbe(const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM p)
{
    *reinterpret_cast<bool*>(p) = true;
    return 0;
}

static bool FaceExists(const wchar_t* face)
{
    HDC dc = GetDC(nullptr);
    bool found = false;
    LOGFONTW lf = {};
    lf.lfCharSet = DEFAULT_CHARSET;
    wcsncpy(lf.lfFaceName, face, LF_FACESIZE - 1);
    EnumFontFamiliesExW(dc, &lf, FaceProbe, reinterpret_cast<LPARAM>(&found), 0);
    ReleaseDC(nullptr, dc);
    return found;
}

static HFONT MakeFont(const wchar_t* face, int points, int weight, int dpi)
{
    LOGFONTW lf = {};
    lf.lfHeight = -MulDiv(points, dpi, 72);
    lf.lfWeight = weight;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcsncpy(lf.lfFaceName, face, LF_FACESIZE - 1);
    return CreateFontIndirectW(&lf);
}

void ThemeRefresh(HWND hwnd)
{
    Theme& t = g_theme;
    t.dark = SystemUsesDarkMode();
    t.dpi = DpiForWindow(hwnd);
    if (t.dark)
    {
        t.bg = RGB(32, 32, 32);      t.card = RGB(43, 43, 43);   t.border = RGB(64, 64, 64);
        t.text = RGB(255, 255, 255); t.dim = RGB(176, 176, 176);
        t.accent = RGB(0x4C, 0xC2, 0xFF); t.onAccent = RGB(0, 0, 0);
        t.good = RGB(108, 203, 95);  t.warn = RGB(252, 225, 0);  t.danger = RGB(255, 153, 164);
        t.track = RGB(68, 68, 68);   t.hover = RGB(58, 58, 58);
        t.banner = RGB(68, 39, 38);  t.bannerText = RGB(255, 255, 255);
    }
    else
    {
        t.bg = RGB(243, 243, 243);   t.card = RGB(255, 255, 255); t.border = RGB(224, 224, 224);
        t.text = RGB(26, 26, 26);    t.dim = RGB(96, 96, 96);
        t.accent = RGB(0x00, 0x67, 0xC0); t.onAccent = RGB(255, 255, 255);
        t.good = RGB(15, 123, 15);   t.warn = RGB(157, 93, 0);   t.danger = RGB(196, 43, 28);
        t.track = RGB(230, 230, 230); t.hover = RGB(245, 245, 245);
        t.banner = RGB(253, 231, 233); t.bannerText = RGB(26, 26, 26);
    }
    HFONT* fonts[] = { &t.font, &t.bold, &t.caption, &t.title, &t.big };
    for (HFONT* f : fonts)
        if (*f) { DeleteObject(*f); *f = nullptr; }
    static const wchar_t* face = FaceExists(L"Segoe UI Variable Text") ? L"Segoe UI Variable Text" : L"Segoe UI";
    static const wchar_t* display = FaceExists(L"Segoe UI Variable Display") ? L"Segoe UI Variable Display" : L"Segoe UI";
    t.font = MakeFont(face, 10, FW_NORMAL, t.dpi);
    t.bold = MakeFont(face, 10, FW_SEMIBOLD, t.dpi);
    t.caption = MakeFont(face, 9, FW_NORMAL, t.dpi);
    t.title = MakeFont(display, 16, FW_SEMIBOLD, t.dpi);
    t.big = MakeFont(display, 26, FW_SEMIBOLD, t.dpi);
    if (t.bgBrush) DeleteObject(t.bgBrush);
    if (t.cardBrush) DeleteObject(t.cardBrush);
    t.bgBrush = CreateSolidBrush(t.bg);
    t.cardBrush = CreateSolidBrush(t.card);
}

void StyleTopLevel(HWND hwnd)
{
    typedef HRESULT (WINAPI *DwmSetFn)(HWND, DWORD, LPCVOID, DWORD);
    static HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    if (!dwm) return;
    static DwmSetFn fn = reinterpret_cast<DwmSetFn>(GetProcAddress(dwm, "DwmSetWindowAttribute"));
    if (!fn) return;
    const BOOL dark = g_theme.dark;
    fn(hwnd, 20, &dark, sizeof(dark)); // DWMWA_USE_IMMERSIVE_DARK_MODE (19 on early Win10 builds)
    fn(hwnd, 19, &dark, sizeof(dark));
}

void StyleControl(HWND ctl)
{
    SetWindowTheme(ctl, g_theme.dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    SendMessageW(ctl, WM_SETFONT, reinterpret_cast<WPARAM>(g_theme.font), TRUE);
}

void StyleListView(HWND lv)
{
    StyleControl(lv);
    HWND header = ListView_GetHeader(lv);
    if (header) SetWindowTheme(header, g_theme.dark ? L"DarkMode_ItemsView" : L"Explorer", nullptr);
    ListView_SetBkColor(lv, g_theme.card);
    ListView_SetTextBkColor(lv, g_theme.card);
    ListView_SetTextColor(lv, g_theme.text);
}

void StyleTreeView(HWND tv)
{
    StyleControl(tv);
    TreeView_SetBkColor(tv, g_theme.card);
    TreeView_SetTextColor(tv, g_theme.text);
    SendMessageW(tv, TVM_SETEXTENDEDSTYLE, TVS_EX_DOUBLEBUFFER, TVS_EX_DOUBLEBUFFER);
    TreeView_SetItemHeight(tv, g_theme.S(26));
}
}
