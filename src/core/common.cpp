// SpaceRescue - SPDX-License-Identifier: GPL-3.0-or-later

#include "common.h"

#include <cstdarg>

namespace sr
{
std::wstring Fmt(const wchar_t* format, ...)
{
    wchar_t buffer[1024];
    va_list args;
    va_start(args, format);
    const int n = _vsnwprintf(buffer, 1023, format, args);
    va_end(args);
    buffer[1023] = L'\0';
    return std::wstring(buffer, n < 0 ? wcslen(buffer) : static_cast<size_t>(n));
}

std::wstring FormatBytes(const u64 bytes)
{
    static const wchar_t* units[] = { L"bytes", L"KB", L"MB", L"GB", L"TB", L"PB" };
    if (bytes < 1024) return Fmt(L"%llu %s", bytes, bytes == 1 ? L"byte" : units[0]);
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 5)
    {
        value /= 1024.0;
        ++unit;
    }
    return Fmt(value >= 100.0 ? L"%.0f %s" : L"%.1f %s", value, units[unit]);
}

std::wstring FormatCount(const u64 n)
{
    std::wstring digits = Fmt(L"%llu", n);
    std::wstring out;
    int count = 0;
    for (auto it = digits.rbegin(); it != digits.rend(); ++it)
    {
        if (count && count % 3 == 0) out.insert(out.begin(), L',');
        out.insert(out.begin(), *it);
        ++count;
    }
    return out;
}

std::wstring FormatPercent(const double fraction)
{
    const double pct = fraction * 100.0;
    if (pct > 0.0 && pct < 1.0) return Fmt(L"%.1f%%", pct);
    return Fmt(L"%.0f%%", pct);
}

std::wstring ErrorText(const DWORD error)
{
    wchar_t* buffer = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring text = buffer ? buffer : Fmt(L"Error %lu", error);
    if (buffer) LocalFree(buffer);
    while (!text.empty() && (text.back() == L'\n' || text.back() == L'\r' || text.back() == L' ')) text.pop_back();
    return text;
}

u64 NowTicks()
{
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return Ticks(ft);
}

std::wstring FormatAge(const u64 ticks)
{
    const u64 now = NowTicks();
    if (ticks == 0 || ticks > now) return L"unknown";
    const u64 days = (now - ticks) / TicksPerDay;
    if (days == 0) return L"today";
    if (days < 60) return Fmt(L"%llu day%s ago", days, days == 1 ? L"" : L"s");
    if (days < 730) return Fmt(L"%llu months ago", days / 30);
    return Fmt(L"%llu years ago", days / 365);
}

std::wstring ToLower(std::wstring s)
{
    if (!s.empty()) CharLowerBuffW(&s[0], static_cast<DWORD>(s.size()));
    return s;
}

bool EndsWithI(const std::wstring& s, const wchar_t* suffix)
{
    const size_t n = wcslen(suffix);
    return s.size() >= n && _wcsicmp(s.c_str() + s.size() - n, suffix) == 0;
}

bool ContainsI(const std::wstring& haystack, const wchar_t* needle)
{
    return ToLower(haystack).find(ToLower(needle)) != std::wstring::npos;
}

std::wstring VolumeOf(const std::wstring& path)
{
    if (path.size() >= 2 && path[1] == L':') return std::wstring(1, static_cast<wchar_t>(towupper(path[0]))) + L":\\";
    return std::wstring();
}

std::wstring ParentPath(const std::wstring& path)
{
    std::wstring p = path;
    if (p.size() > 3 && p.back() == L'\\') p.pop_back();
    const size_t pos = p.find_last_of(L'\\');
    if (pos == std::wstring::npos) return std::wstring();
    if (pos == 2 && p[1] == L':') return p.substr(0, 3);
    return p.substr(0, pos);
}

std::wstring JoinPath(const std::wstring& a, const std::wstring& b)
{
    if (a.empty()) return b;
    if (a.back() == L'\\') return a + b;
    return a + L"\\" + b;
}

std::wstring LongPath(const std::wstring& path)
{
    if (path.size() >= 3 && path[1] == L':' && path[2] == L'\\') return L"\\\\?\\" + path;
    if (path.compare(0, 2, L"\\\\") == 0 && path.compare(0, 4, L"\\\\?\\") != 0) return L"\\\\?\\UNC\\" + path.substr(2);
    return path;
}

std::wstring ExeDirectory()
{
    wchar_t buffer[MAX_PATH * 2] = {};
    GetModuleFileNameW(nullptr, buffer, MAX_PATH * 2);
    return ParentPath(buffer);
}

std::wstring KnownFolder(const int csidl)
{
    wchar_t buffer[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, csidl, nullptr, 0, buffer))) return buffer;
    return std::wstring();
}

u64 PeakWorkingSet()
{
    PROCESS_MEMORY_COUNTERS pmc = {};
    pmc.cb = sizeof pmc;
    return GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc) ? pmc.PeakWorkingSetSize : 0;
}

u64 CurrentWorkingSet()
{
    PROCESS_MEMORY_COUNTERS pmc = {};
    pmc.cb = sizeof pmc;
    return GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc) ? pmc.WorkingSetSize : 0;
}

bool IsElevated()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION elevation = {};
    DWORD size = 0;
    const bool ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof elevation, &size) != FALSE;
    CloseHandle(token);
    return ok && elevation.TokenIsElevated != 0;
}
}
