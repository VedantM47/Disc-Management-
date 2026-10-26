// SpaceRescue - SPDX-License-Identifier: GPL-3.0-or-later

#include "tray.h"

#include "../rescue/rescue.h"

#include <map>

namespace sr
{
const u64 AlertLevel::Thresholds[AlertLevel::Count] = {
    50 * GiB, 20 * GiB, 10 * GiB, 5 * GiB, 3 * GiB, 2 * GiB, 1 * GiB, 512 * MiB };

int AlertLevel::Update(const u64 freeBytes)
{
    int below = 0;
    while (below < Count && freeBytes < Thresholds[below]) ++below;

    if (m_level < 0)
    {
        m_level = below;
        return below > 0 && Thresholds[below - 1] <= 1 * GiB ? below - 1 : -1;
    }
    if (below > m_level)
    {
        m_level = below;
        return below - 1;
    }
    // Re-arm only once free space is 10% above a threshold, so hovering around it stays quiet.
    while (m_level > 0 && freeBytes > Thresholds[m_level - 1] + Thresholds[m_level - 1] / 10) --m_level;
    return -1;
}

namespace
{
constexpr UINT WM_TRAY = WM_APP + 40;
constexpr UINT_PTR PollTimer = 1;
constexpr UINT PollMillis = 30 * 1000;
constexpr UINT IdOpen = 1, IdCheck = 2, IdExit = 3;
const wchar_t* RunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

struct TrayState
{
    HWND hwnd = nullptr;
    NOTIFYICONDATAW icon = {};
    std::map<std::wstring, AlertLevel> drives;
    std::wstring lastDrive; // drive of the last notification (opened on click)
};
TrayState g_tray;

std::wstring ExePath()
{
    wchar_t buffer[MAX_PATH * 2] = {};
    GetModuleFileNameW(nullptr, buffer, MAX_PATH * 2);
    return buffer;
}

// Bytes in a folder tree, bounded so a notification never waits long. No allocations kept.
u64 FolderBytes(const std::wstring& root, DWORD deadline)
{
    u64 total = 0;
    std::vector<std::wstring> stack{ root };
    while (!stack.empty() && GetTickCount() < deadline)
    {
        const std::wstring dir = std::move(stack.back());
        stack.pop_back();
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileExW(LongPath(JoinPath(dir, L"*")).c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch,
            nullptr, FIND_FIRST_EX_LARGE_FETCH);
        if (h == INVALID_HANDLE_VALUE) continue;
        do
        {
            if (fd.cFileName[0] == L'.' && (!fd.cFileName[1] || (fd.cFileName[1] == L'.' && !fd.cFileName[2]))) continue;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) stack.push_back(JoinPath(dir, fd.cFileName));
            }
            else total += (static_cast<u64>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
        }
        while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    return total;
}

// Quick estimate of easy wins on a drive: temp folders, browser caches, Windows Update downloads.
u64 QuickReclaim(const std::wstring& drive)
{
    const DWORD deadline = GetTickCount() + 3000;
    std::vector<std::wstring> spots;
    wchar_t temp[MAX_PATH] = {};
    if (GetTempPathW(MAX_PATH, temp)) spots.push_back(temp);
    const std::wstring win = KnownFolder(CSIDL_WINDOWS);
    const std::wstring local = KnownFolder(CSIDL_LOCAL_APPDATA);
    spots.push_back(JoinPath(win, L"Temp"));
    spots.push_back(JoinPath(win, L"SoftwareDistribution\\Download"));
    spots.push_back(JoinPath(local, L"Google\\Chrome\\User Data\\Default\\Cache"));
    spots.push_back(JoinPath(local, L"Microsoft\\Edge\\User Data\\Default\\Cache"));
    spots.push_back(JoinPath(local, L"Microsoft\\Windows\\INetCache"));
    spots.push_back(JoinPath(local, L"CrashDumps"));
    u64 total = 0;
    for (const std::wstring& s : spots)
        if (_wcsicmp(VolumeOf(s).c_str(), drive.c_str()) == 0) total += FolderBytes(s, deadline);
    return total;
}

void Notify(const std::wstring& drive, u64 freeBytes, int threshold)
{
    const u64 t = AlertLevel::Thresholds[threshold];
    const std::wstring letter = drive.substr(0, 2);
    const u64 easy = QuickReclaim(drive);
    std::wstring title, text;
    if (t <= 1 * GiB)
    {
        title = Fmt(L"%s is almost full", letter.c_str());
        text = Fmt(L"Only %s left. Windows may stop saving files. Click to open SpaceRescue in Rescue Mode.",
            FormatBytes(freeBytes).c_str());
    }
    else if (t <= 10 * GiB)
    {
        title = Fmt(L"%s is running low", letter.c_str());
        text = Fmt(L"%s has %s left.", letter.c_str(), FormatBytes(freeBytes).c_str());
    }
    else
    {
        title = Fmt(L"%s is filling up", letter.c_str());
        text = Fmt(L"%s has %s left.", letter.c_str(), FormatBytes(freeBytes).c_str());
    }
    if (easy >= 100 * MiB) text += Fmt(L" Temp files and caches could free about %s.", FormatBytes(easy).c_str());

    NOTIFYICONDATAW& n = g_tray.icon;
    n.uFlags = NIF_INFO;
    wcsncpy(n.szInfoTitle, title.c_str(), ARRAYSIZE(n.szInfoTitle) - 1);
    wcsncpy(n.szInfo, text.c_str(), ARRAYSIZE(n.szInfo) - 1);
    n.dwInfoFlags = (t <= 5 * GiB ? NIIF_WARNING : NIIF_INFO) | NIIF_LARGE_ICON;
    if (t > 10 * GiB) n.dwInfoFlags |= NIIF_NOSOUND; // quiet tone while there is still room
    g_tray.lastDrive = drive;
    Shell_NotifyIconW(NIM_MODIFY, &n);
}

void Poll()
{
    RescueConfig config;
    for (const DriveInfo& d : EnumerateDrives(config))
    {
        if (d.type != DRIVE_FIXED || !d.total) continue;
        const int crossed = g_tray.drives[d.root].Update(d.free);
        if (crossed >= 0) Notify(d.root, d.free, crossed);
    }
}

void OpenApp(const std::wstring& drive)
{
    const std::wstring args = drive.empty() ? L"" : L"\"" + drive + L"\"";
    ShellExecuteW(nullptr, L"open", ExePath().c_str(), args.empty() ? nullptr : args.c_str(), nullptr, SW_SHOWNORMAL);
}

LRESULT CALLBACK TrayProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    static UINT taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    if (msg == taskbarCreated) // Explorer restarted: put the icon back
    {
        g_tray.icon.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
        Shell_NotifyIconW(NIM_ADD, &g_tray.icon);
        return 0;
    }
    switch (msg)
    {
    case WM_TIMER:
        if (wp != PollTimer) KillTimer(hwnd, wp); // one-shot re-check after a device change
        Poll();
        return 0;
    case WM_DEVICECHANGE:
        SetTimer(hwnd, PollTimer + 1, 2000, nullptr); // new drive: check shortly
        return TRUE;
    case WM_TRAY:
        switch (LOWORD(lp))
        {
        case NIN_BALLOONUSERCLICK:
            OpenApp(g_tray.lastDrive);
            break;
        case WM_LBUTTONDBLCLK:
            OpenApp(L"");
            break;
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
        {
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, IdOpen, L"Open SpaceRescue");
            AppendMenuW(menu, MF_STRING, IdCheck, L"Check drives now");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, IdExit, L"Stop low-space alerts");
            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(hwnd); // required so the menu closes when clicking elsewhere
            const UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
            DestroyMenu(menu);
            if (cmd == IdOpen) OpenApp(L"");
            else if (cmd == IdCheck) Poll();
            else if (cmd == IdExit) DestroyWindow(hwnd);
            break;
        }
        }
        return 0;
    case WM_DESTROY:
        Shell_NotifyIconW(NIM_DELETE, &g_tray.icon);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
}

int RunTray()
{
    // One watcher per user session.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\SpaceRescue.Tray");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    const HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc = { sizeof wc };
    wc.lpfnWndProc = TrayProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"SpaceRescue.Tray";
    RegisterClassExW(&wc);
    // A hidden top-level window (not message-only) so it receives WM_DEVICECHANGE and TaskbarCreated.
    g_tray.hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"SpaceRescue alerts", WS_POPUP,
        0, 0, 0, 0, nullptr, nullptr, inst, nullptr);
    if (!g_tray.hwnd) return 1;

    NOTIFYICONDATAW& n = g_tray.icon;
    n.cbSize = sizeof n;
    n.hWnd = g_tray.hwnd;
    n.uID = 1;
    n.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    n.uCallbackMessage = WM_TRAY;
    n.hIcon = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
    if (!n.hIcon) n.hIcon = LoadIconW(nullptr, IDI_INFORMATION);
    wcsncpy(n.szTip, L"SpaceRescue is watching your free space", ARRAYSIZE(n.szTip) - 1);
    Shell_NotifyIconW(NIM_ADD, &n);
    n.uVersion = NOTIFYICON_VERSION;
    Shell_NotifyIconW(NIM_SETVERSION, &n);

    Poll();
    SetTimer(g_tray.hwnd, PollTimer, PollMillis, nullptr);
    SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1)); // trim after startup

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CloseHandle(mutex);
    return 0;
}

DWORD SetStartWithWindows(const bool enable)
{
    HKEY key = nullptr;
    DWORD e = RegCreateKeyExW(HKEY_CURRENT_USER, RunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (e != ERROR_SUCCESS) return e;
    if (enable)
    {
        const std::wstring cmd = L"\"" + ExePath() + L"\" /tray";
        e = RegSetValueExW(key, L"SpaceRescue", 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()),
            static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
    }
    else
    {
        e = RegDeleteValueW(key, L"SpaceRescue");
        if (e == ERROR_FILE_NOT_FOUND) e = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    return e;
}

bool IsStartWithWindows()
{
    wchar_t buffer[MAX_PATH * 2] = {};
    DWORD size = sizeof buffer;
    return RegGetValueW(HKEY_CURRENT_USER, RunKey, L"SpaceRescue", RRF_RT_REG_SZ, nullptr, buffer, &size) == ERROR_SUCCESS;
}
}
