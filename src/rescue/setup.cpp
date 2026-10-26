// SpaceRescue - SPDX-License-Identifier: GPL-3.0-or-later

#include "setup.h"
#include "../core/bench.h"
#include "../notify/tray.h"
#include "rescue.h"
#include <shlwapi.h>

namespace sr
{
namespace
{
const wchar_t* kVerbKey = L"SpaceRescue";
const wchar_t* kUninstallKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\SpaceRescue";

DWORD SetStr(HKEY key, const wchar_t* name, const std::wstring& value)
{
    return static_cast<DWORD>(RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                                             static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))));
}

DWORD SetDword(HKEY key, const wchar_t* name, DWORD value)
{
    return static_cast<DWORD>(RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value)));
}

DWORD WriteVerb(const std::wstring& parent, const std::wstring& exe, const wchar_t* arg)
{
    const std::wstring base = L"Software\\Classes\\" + parent + L"\\shell\\" + kVerbKey;
    HKEY key = nullptr;
    LONG rc = RegCreateKeyExW(HKEY_CURRENT_USER, base.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr);
    if (rc != ERROR_SUCCESS) return static_cast<DWORD>(rc);
    DWORD e = SetStr(key, L"MUIVerb", L"Analyze with SpaceRescue");
    if (e == ERROR_SUCCESS) e = SetStr(key, L"Icon", exe);
    RegCloseKey(key);
    if (e != ERROR_SUCCESS) return e;
    rc = RegCreateKeyExW(HKEY_CURRENT_USER, (base + L"\\command").c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr);
    if (rc != ERROR_SUCCESS) return static_cast<DWORD>(rc);
    e = SetStr(key, nullptr, L"\"" + exe + L"\" \"" + arg + L"\"");
    RegCloseKey(key);
    return e;
}

std::wstring InstallDir()
{
    std::wstring base = KnownFolder(CSIDL_LOCAL_APPDATA);
    return JoinPath(JoinPath(JoinPath(base, L"Programs"), L"SpaceRescue"), L"");
}

std::wstring Trim(std::wstring s)
{
    while (!s.empty() && s.back() == L'\\') s.pop_back();
    return s;
}

std::wstring InstalledExe() { return JoinPath(Trim(InstallDir()), L"SpaceRescue.exe"); }
std::wstring ShortcutPath() { return JoinPath(KnownFolder(CSIDL_PROGRAMS), L"SpaceRescue.lnk"); }
std::wstring SelfPath()
{
    wchar_t b[MAX_PATH * 2] = {};
    GetModuleFileNameW(nullptr, b, MAX_PATH * 2);
    return b;
}

void MakeDirs(const std::wstring& dir)
{
    if (dir.size() <= 3 || GetFileAttributesW(dir.c_str()) != INVALID_FILE_ATTRIBUTES) return;
    MakeDirs(ParentPath(dir));
    CreateDirectoryW(dir.c_str(), nullptr);
}

void RemoveTree(const std::wstring& dir)
{
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(JoinPath(dir, L"*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
            const std::wstring p = JoinPath(dir, fd.cFileName);
            SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL);
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) RemoveTree(p);
            else if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) RemoveDirectoryW(p.c_str());
            else DeleteFileW(p.c_str());
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir.c_str());
}

DWORD CreateShortcut(const std::wstring& exe, const std::wstring& lnk)
{
    const HRESULT init = CoInitialize(nullptr);
    DWORD result = ERROR_SUCCESS;
    IShellLinkW* link = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void**>(&link));
    if (SUCCEEDED(hr))
    {
        link->SetPath(exe.c_str());
        link->SetDescription(L"SpaceRescue - disk space management");
        link->SetIconLocation(exe.c_str(), 0);
        link->SetWorkingDirectory(ParentPath(exe).c_str());
        IPersistFile* file = nullptr;
        hr = link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&file));
        if (SUCCEEDED(hr))
        {
            hr = file->Save(lnk.c_str(), TRUE);
            file->Release();
        }
        link->Release();
    }
    if (FAILED(hr)) result = static_cast<DWORD>(hr & 0xFFFF) ? static_cast<DWORD>(hr & 0xFFFF) : ERROR_GEN_FAILURE;
    if (SUCCEEDED(init)) CoUninitialize();
    return result;
}

DWORD WriteUninstallEntry(const std::wstring& exe)
{
    HKEY key = nullptr;
    LONG rc = RegCreateKeyExW(HKEY_CURRENT_USER, kUninstallKey, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr);
    if (rc != ERROR_SUCCESS) return static_cast<DWORD>(rc);
    WIN32_FILE_ATTRIBUTE_DATA d = {};
    GetFileAttributesExW(exe.c_str(), GetFileExInfoStandard, &d);
    SetStr(key, L"DisplayName", L"SpaceRescue");
    SetStr(key, L"DisplayIcon", exe);
    SetStr(key, L"UninstallString", L"\"" + exe + L"\" /uninstall");
    SetStr(key, L"Publisher", L"SpaceRescue contributors");
    SetStr(key, L"InstallLocation", ParentPath(exe));
    SetDword(key, L"EstimatedSize", d.nFileSizeLow / 1024);
    SetDword(key, L"NoModify", 1);
    SetDword(key, L"NoRepair", 1);
    RegCloseKey(key);
    return ERROR_SUCCESS;
}

void Message(const std::wstring& text, bool error)
{
    MessageBoxW(nullptr, text.c_str(), L"SpaceRescue", MB_OK | (error ? MB_ICONERROR : MB_ICONINFORMATION));
}

bool IsSwitch(const wchar_t* a) { return a && (a[0] == L'/' || a[0] == L'-') && a[1]; }
bool Is(const wchar_t* a, const wchar_t* name) { return (IsSwitch(a) && _wcsicmp(a + 1, name) == 0) || (a[0] == L'-' && a[1] == L'-' && _wcsicmp(a + 2, name) == 0); }
}

DWORD RegisterContextMenu(const std::wstring& exePath)
{
    DWORD e = WriteVerb(L"Directory", exePath, L"%1");
    if (e == ERROR_SUCCESS) e = WriteVerb(L"Directory\\Background", exePath, L"%V");
    if (e == ERROR_SUCCESS) e = WriteVerb(L"Drive", exePath, L"%1");
    if (e != ERROR_SUCCESS) UnregisterContextMenu();
    return e;
}

DWORD UnregisterContextMenu()
{
    DWORD last = ERROR_SUCCESS;
    const wchar_t* parents[] = { L"Directory", L"Directory\\Background", L"Drive" };
    for (const wchar_t* p : parents)
    {
        const std::wstring key = std::wstring(L"Software\\Classes\\") + p + L"\\shell\\" + kVerbKey;
        const LONG rc = SHDeleteKeyW(HKEY_CURRENT_USER, key.c_str());
        if (rc != ERROR_SUCCESS && rc != ERROR_FILE_NOT_FOUND) last = static_cast<DWORD>(rc);
    }
    return last;
}

bool IsContextMenuRegistered()
{
    HKEY key = nullptr;
    const std::wstring k = std::wstring(L"Software\\Classes\\Directory\\shell\\") + kVerbKey + L"\\command";
    if (RegOpenKeyExW(HKEY_CURRENT_USER, k.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS) return false;
    RegCloseKey(key);
    return true;
}

DWORD Install(const bool createReserve)
{
    const std::wstring self = SelfPath();
    const std::wstring dest = InstalledExe();
    MakeDirs(ParentPath(dest));
    if (_wcsicmp(self.c_str(), dest.c_str()) != 0 && !CopyFileW(self.c_str(), dest.c_str(), FALSE)) return GetLastError();
    DWORD e = RegisterContextMenu(dest);
    if (e != ERROR_SUCCESS) return e;
    e = CreateShortcut(dest, ShortcutPath());
    if (e != ERROR_SUCCESS) return e;
    e = WriteUninstallEntry(dest);
    if (e != ERROR_SUCCESS) return e;
    if (createReserve)
    {
        Settings& s = Settings::Get();
        s.Load();
        e = CreateReserve(s.rescue.reserveSize);
        if (e == ERROR_ALREADY_EXISTS) e = ERROR_SUCCESS;
    }
    return e;
}

DWORD Uninstall()
{
    DWORD last = UnregisterContextMenu();
    DeleteFileW(ShortcutPath().c_str());
    const LONG rc = SHDeleteKeyW(HKEY_CURRENT_USER, kUninstallKey);
    if (rc != ERROR_SUCCESS && rc != ERROR_FILE_NOT_FOUND) last = static_cast<DWORD>(rc);
    ReleaseReserve();
    const std::wstring reserveDir = ParentPath(ReservePath());
    RemoveDirectoryW(reserveDir.c_str());           // only if empty
    RemoveTree(JoinPath(KnownFolder(CSIDL_APPDATA), L"SpaceRescue")); // settings

    const std::wstring dir = Trim(InstallDir());
    const std::wstring exe = InstalledExe();
    const std::wstring self = SelfPath();
    if (_wcsicmp(self.c_str(), exe.c_str()) == 0)
    {
        // We are the running installed exe: let a hidden cmd delete us after we exit.
        std::wstring cmd = L"cmd.exe /c ping -n 3 127.0.0.1>nul & del /f /q \"" + exe + L"\" & rmdir \"" + dir + L"\"";
        STARTUPINFOW si = {};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi = {};
        std::vector<wchar_t> buf(cmd.begin(), cmd.end());
        buf.push_back(L'\0');
        if (CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
        else last = GetLastError();
    }
    else if (GetFileAttributesW(dir.c_str()) != INVALID_FILE_ATTRIBUTES)
        RemoveTree(dir);
    return last;
}

bool HandleCommandLine(const int argc, wchar_t** argv, int* exitCode, std::wstring* scanPath)
{
    *exitCode = 0;
    bool install = false, uninstall = false, reg = false, unreg = false, quiet = false, reserve = false;
    bool createReserve = false, removeReserve = false, alerts = false;
    int autostart = -1; // -1 = unchanged, 0 = off, 1 = on
    u64 reserveMB = 0;
    for (int i = 1; i < argc; ++i)
    {
        const wchar_t* a = argv[i];
        if (Is(a, L"tray"))
        {
            *exitCode = RunTray();
            return true;
        }
        if (Is(a, L"bench"))
        {
            std::wstring path = i + 1 < argc && !IsSwitch(argv[i + 1]) ? argv[i + 1] : L"";
            while (!path.empty() && path.back() == L'"') path.pop_back();
            *exitCode = RunBench(path);
            return true;
        }
        if (Is(a, L"autostart"))
        {
            autostart = !(i + 1 < argc && _wcsicmp(argv[i + 1], L"off") == 0);
            if (i + 1 < argc && !IsSwitch(argv[i + 1])) ++i;
            continue;
        }
        if (Is(a, L"alerts"))
        {
            alerts = true;
            continue;
        }
        if (!IsSwitch(a))
        {
            if (i == 1 && scanPath)
            {
                std::wstring p = a;
                // Explorer's "%1" for a drive is "C:\" - the trailing \" can leave a stray quote.
                while (!p.empty() && p.back() == L'"') p.pop_back();
                if (p.size() == 2 && p[1] == L':') p += L'\\';
                *scanPath = p;
            }
            continue;
        }
        if (Is(a, L"install")) install = true;
        else if (Is(a, L"uninstall")) uninstall = true;
        else if (Is(a, L"register")) reg = true;
        else if (Is(a, L"unregister")) unreg = true;
        else if (Is(a, L"quiet") || Is(a, L"q") || Is(a, L"silent")) quiet = true;
        else if (Is(a, L"reserve")) reserve = true;
        else if (Is(a, L"removereserve")) removeReserve = true;
        else if (Is(a, L"createreserve"))
        {
            createReserve = true;
            if (i + 1 < argc && !IsSwitch(argv[i + 1])) reserveMB = _wtoi(argv[++i]);
        }
    }
    if (autostart >= 0 && !install && !uninstall)
    {
        *exitCode = static_cast<int>(SetStartWithWindows(autostart == 1));
        if (scanPath) scanPath->clear();
        return true;
    }
    if (!(install || uninstall || reg || unreg || createReserve || removeReserve))
        return false;
    if (scanPath) scanPath->clear();

    DWORD e = ERROR_SUCCESS;
    std::wstring ok;
    if (install)
    {
        e = Install(reserve);
        if (e == ERROR_SUCCESS && alerts)
        {
            // Point the Run entry at the installed copy and start watching right away.
            const std::wstring installed = JoinPath(KnownFolder(CSIDL_LOCAL_APPDATA), L"Programs\\SpaceRescue\\SpaceRescue.exe");
            const std::wstring cmd = L"\"" + installed + L"\" /tray";
            HKEY key = nullptr;
            if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, nullptr, 0,
                KEY_SET_VALUE, nullptr, &key, nullptr) == ERROR_SUCCESS)
            {
                RegSetValueExW(key, L"SpaceRescue", 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()),
                    static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
                RegCloseKey(key);
            }
            ShellExecuteW(nullptr, L"open", installed.c_str(), L"/tray", nullptr, SW_HIDE);
        }
        ok = L"SpaceRescue was installed for your account.\r\n\r\nRight-click a folder or drive in Explorer and choose \"Analyze with SpaceRescue\" (on Windows 11: Show more options).";
    }
    else if (uninstall)
    {
        SetStartWithWindows(false);
        // Stop a running alert watcher so its exe can be deleted.
        if (HWND tray = FindWindowW(L"SpaceRescue.Tray", nullptr)) SendMessageTimeoutW(tray, WM_CLOSE, 0, 0, SMTO_ABORTIFHUNG, 2000, nullptr);
        e = Uninstall();
        ok = L"SpaceRescue was removed (program, shortcut, context menu, settings and reserve file).";
    }
    else if (reg)
    {
        e = RegisterContextMenu(SelfPath());
        ok = L"Explorer context menu registered.";
    }
    else if (unreg)
    {
        e = UnregisterContextMenu();
        ok = L"Explorer context menu removed.";
    }
    else if (removeReserve)
    {
        e = ReleaseReserve();
        ok = L"Reserve file removed.";
    }
    else if (createReserve)
    {
        Settings& s = Settings::Get();
        s.Load();
        const u64 size = reserveMB ? reserveMB * MiB : s.rescue.reserveSize;
        e = CreateReserve(size);
        ok = Fmt(L"Reserve file of %s created.", FormatBytes(size).c_str());
    }
    *exitCode = static_cast<int>(e);
    if (!quiet) Message(e == ERROR_SUCCESS ? ok : L"The operation failed: " + ErrorText(e), e != ERROR_SUCCESS);
    return true;
}
}
