// SpaceRescue - SPDX-License-Identifier: GPL-3.0-or-later

#include "rescue.h"

namespace sr
{
namespace
{
bool GetFree(const std::wstring& root, u64* freeBytes, u64* total)
{
    ULARGE_INTEGER avail = {}, tot = {}, totalFree = {};
    // Avoid the "no disk in drive" dialog on empty card readers.
    const UINT oldMode = SetErrorMode(SEM_FAILCRITICALERRORS);
    const BOOL ok = GetDiskFreeSpaceExW(root.c_str(), &avail, &tot, &totalFree);
    SetErrorMode(oldMode);
    if (!ok) return false;
    *freeBytes = avail.QuadPart;
    *total = tot.QuadPart;
    return true;
}

std::wstring SystemRoot()
{
    wchar_t buffer[MAX_PATH] = {};
    GetWindowsDirectoryW(buffer, MAX_PATH);
    return VolumeOf(buffer);
}

bool EnablePrivilege(const wchar_t* name)
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) return false;
    Handle guard(token);
    TOKEN_PRIVILEGES tp = {};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    if (!LookupPrivilegeValueW(nullptr, name, &tp.Privileges[0].Luid)) return false;
    AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), nullptr, nullptr);
    return GetLastError() == ERROR_SUCCESS; // ERROR_NOT_ALL_ASSIGNED when we do not hold it
}

void EnsureDirectory(const std::wstring& dir)
{
    if (dir.size() <= 3 || GetFileAttributesW(dir.c_str()) != INVALID_FILE_ATTRIBUTES) return;
    EnsureDirectory(ParentPath(dir));
    CreateDirectoryW(dir.c_str(), nullptr);
}
}

bool IsLow(const u64 free, const u64 total, const RescueConfig& config)
{
    return free < config.lowBytes || static_cast<double>(free) < config.lowFraction * static_cast<double>(total);
}

std::vector<DriveInfo> EnumerateDrives(const RescueConfig& config)
{
    std::vector<DriveInfo> drives;
    const DWORD mask = GetLogicalDrives();
    const std::wstring sys = SystemRoot();
    for (int i = 0; i < 26; ++i)
    {
        if (!(mask & (1u << i))) continue;
        DriveInfo d;
        d.root = std::wstring(1, static_cast<wchar_t>(L'A' + i)) + L":\\";
        d.type = GetDriveTypeW(d.root.c_str());
        if (d.type != DRIVE_FIXED && d.type != DRIVE_REMOVABLE) continue;
        wchar_t label[MAX_PATH + 1] = {}, fs[MAX_PATH + 1] = {};
        const UINT oldMode = SetErrorMode(SEM_FAILCRITICALERRORS);
        const BOOL mounted = GetVolumeInformationW(d.root.c_str(), label, MAX_PATH, nullptr, nullptr, nullptr, fs, MAX_PATH);
        SetErrorMode(oldMode);
        if (!mounted) continue;
        d.label = label[0] ? label : L"Local Disk";
        d.fileSystem = fs;
        if (!GetFree(d.root, &d.free, &d.total) || d.total == 0) continue;
        d.isSystem = _wcsicmp(d.root.c_str(), sys.c_str()) == 0;
        d.low = IsLow(d.free, d.total, config);
        drives.push_back(d);
    }
    return drives;
}

std::wstring PickWorkVolume(const std::wstring& exclude, const RescueConfig& config)
{
    if (!config.workVolumeOverride.empty())
    {
        const std::wstring v = VolumeOf(config.workVolumeOverride);
        u64 f, t;
        if (!v.empty() && GetFree(v, &f, &t)) return v;
    }
    const std::wstring ex = VolumeOf(exclude);
    std::wstring best;
    u64 bestFree = 0;
    for (const DriveInfo& d : EnumerateDrives(config))
    {
        if (d.low || _wcsicmp(d.root.c_str(), ex.c_str()) == 0) continue;
        if (d.free > bestFree)
        {
            bestFree = d.free;
            best = d.root;
        }
    }
    return best;
}

// --- Reserve file ------------------------------------------------------------------

std::wstring ReservePath()
{
    std::wstring base = KnownFolder(CSIDL_COMMON_APPDATA);
    if (base.empty()) base = L"C:\\ProgramData";
    return JoinPath(JoinPath(base, L"SpaceRescue"), L"reserve.bin");
}

ReserveStatus QueryReserve()
{
    ReserveStatus st;
    st.path = ReservePath();
    WIN32_FILE_ATTRIBUTE_DATA data = {};
    if (GetFileAttributesExW(st.path.c_str(), GetFileExInfoStandard, &data) && !(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
    {
        st.exists = true;
        st.size = (static_cast<u64>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    }
    return st;
}

DWORD CreateReserve(const u64 size)
{
    const std::wstring path = ReservePath();
    const std::wstring root = VolumeOf(path);
    if (QueryReserve().exists) return ERROR_ALREADY_EXISTS;

    // Refuse if the reserve itself would push the drive into the "low" zone.
    u64 freeBytes = 0, total = 0;
    if (!GetFree(root, &freeBytes, &total)) return GetLastError();
    const RescueConfig& cfg = Settings::Get().rescue;
    if (freeBytes < size || IsLow(freeBytes - size, total, cfg)) return ERROR_DISK_FULL;

    EnsureDirectory(ParentPath(path));
    Handle file(CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                            FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file.valid()) return GetLastError();

    LARGE_INTEGER end;
    end.QuadPart = static_cast<LONGLONG>(size);
    bool done = false;
    // Fast path: allocate clusters and mark them valid without writing (needs SeManageVolumePrivilege).
    if (EnablePrivilege(L"SeManageVolumePrivilege") && SetFilePointerEx(file.h, end, nullptr, FILE_BEGIN) &&
        SetEndOfFile(file.h) && SetFileValidData(file.h, static_cast<LONGLONG>(size)))
        done = true;

    DWORD error = ERROR_SUCCESS;
    if (!done)
    {
        // Fallback: truncate and physically write zeros so real clusters are allocated.
        LARGE_INTEGER zero = {};
        SetFilePointerEx(file.h, zero, nullptr, FILE_BEGIN);
        SetEndOfFile(file.h);
        std::vector<char> chunk(static_cast<size_t>(MiB), 0);
        u64 written = 0;
        while (written < size)
        {
            const DWORD n = static_cast<DWORD>(std::min<u64>(chunk.size(), size - written));
            DWORD got = 0;
            if (!WriteFile(file.h, chunk.data(), n, &got, nullptr) || got != n)
            {
                error = GetLastError();
                if (error == ERROR_SUCCESS) error = ERROR_DISK_FULL;
                break;
            }
            written += got;
        }
        if (error == ERROR_SUCCESS) FlushFileBuffers(file.h);
    }
    file.reset();
    if (error != ERROR_SUCCESS) DeleteFileW(path.c_str());
    return error;
}

DWORD ReleaseReserve()
{
    const std::wstring path = ReservePath();
    if (DeleteFileW(path.c_str())) return ERROR_SUCCESS;
    const DWORD e = GetLastError();
    return e == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : e;
}

// --- Settings ----------------------------------------------------------------------

Settings& Settings::Get()
{
    static Settings instance;
    return instance;
}

void Settings::Load()
{
    wchar_t exe[MAX_PATH * 2] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH * 2);
    const std::wstring exeDir = ParentPath(exe);
    const std::wstring portableIni = JoinPath(exeDir, L"SpaceRescue.ini");
    m_portable = GetFileAttributesW(portableIni.c_str()) != INVALID_FILE_ATTRIBUTES || EndsWithI(exe, L"-portable.exe");
    if (m_portable)
        m_path = portableIni;
    else
    {
        std::wstring appData = KnownFolder(CSIDL_APPDATA);
        m_path = JoinPath(JoinPath(appData, L"SpaceRescue"), L"settings.ini");
    }

    const wchar_t* sec = L"SpaceRescue";
    const wchar_t* p = m_path.c_str();
    rescue.lowBytes = static_cast<u64>(GetPrivateProfileIntW(sec, L"LowMB", static_cast<int>(rescue.lowBytes / MiB), p)) * MiB;
    wchar_t buf[MAX_PATH * 2] = {};
    GetPrivateProfileStringW(sec, L"LowPercent", L"", buf, 32, p);
    if (buf[0]) rescue.lowFraction = std::max(0.0, std::min(1.0, _wtof(buf) / 100.0));
    rescue.reserveSize = static_cast<u64>(GetPrivateProfileIntW(sec, L"ReserveMB", static_cast<int>(rescue.reserveSize / MiB), p)) * MiB;
    rescue.reserveSize = std::max<u64>(256 * MiB, std::min<u64>(2 * GiB, rescue.reserveSize));
    GetPrivateProfileStringW(sec, L"WorkVolume", L"", buf, MAX_PATH, p);
    rescue.workVolumeOverride = buf;
    staleMonthsDefault = static_cast<int>(GetPrivateProfileIntW(sec, L"StaleMonths", staleMonthsDefault, p));
    downloadsAgeDays = static_cast<int>(GetPrivateProfileIntW(sec, L"DownloadsAgeDays", downloadsAgeDays, p));
    memoryBudgetMB = static_cast<u64>(GetPrivateProfileIntW(sec, L"MemoryBudgetMB", static_cast<int>(memoryBudgetMB), p));
    GetPrivateProfileStringW(sec, L"LastScanPath", L"", buf, MAX_PATH * 2, p);
    lastScanPath = buf;

    // Zero-write rule: never put a settings file on a drive that is already low.
    u64 freeBytes = 0, total = 0;
    const std::wstring root = VolumeOf(m_path);
    m_suppressed = !root.empty() && GetFree(root, &freeBytes, &total) && IsLow(freeBytes, total, rescue);
}

bool Settings::Save()
{
    if (m_path.empty()) Load();
    if (m_suppressed) return false;
    EnsureDirectory(ParentPath(m_path));
    const wchar_t* sec = L"SpaceRescue";
    const wchar_t* p = m_path.c_str();
    bool ok = true;
    auto put = [&](const wchar_t* key, const std::wstring& value) {
        if (!WritePrivateProfileStringW(sec, key, value.c_str(), p)) ok = false;
    };
    put(L"LowMB", Fmt(L"%llu", rescue.lowBytes / MiB));
    put(L"LowPercent", Fmt(L"%.2f", rescue.lowFraction * 100.0));
    put(L"ReserveMB", Fmt(L"%llu", rescue.reserveSize / MiB));
    put(L"WorkVolume", rescue.workVolumeOverride);
    put(L"StaleMonths", Fmt(L"%d", staleMonthsDefault));
    put(L"DownloadsAgeDays", Fmt(L"%d", downloadsAgeDays));
    put(L"MemoryBudgetMB", Fmt(L"%llu", memoryBudgetMB));
    put(L"LastScanPath", lastScanPath);
    return ok;
}
}
