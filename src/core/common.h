// SpaceRescue - disk space management with a Rescue Mode for full drives
// Copyright (C) SpaceRescue contributors. Inspired by and crediting WinDirStat (GPL).
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Toolchain note: builds with MinGW.org GCC 6.3 (32-bit, C++14, win32 thread model),
// so no std::thread/std::mutex - Win32 threads and SRW locks are used instead.

#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00 // Windows 10+ APIs (GetDpiForWindow etc.)
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif
#define NOMINMAX

#include <windows.h>
#include <winioctl.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <psapi.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// --- Declarations missing from the old MinGW.org headers --------------------

#ifndef FIND_FIRST_EX_LARGE_FETCH
#define FIND_FIRST_EX_LARGE_FETCH 2
#endif
#ifndef THREAD_MODE_BACKGROUND_BEGIN
#define THREAD_MODE_BACKGROUND_BEGIN 0x00010000
#define THREAD_MODE_BACKGROUND_END   0x00020000
#endif
#ifndef PROCESS_MODE_BACKGROUND_BEGIN
#define PROCESS_MODE_BACKGROUND_BEGIN 0x00100000
#endif
#ifndef FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS
#define FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS 0x00400000
#endif
#ifndef FILE_ATTRIBUTE_RECALL_ON_OPEN
#define FILE_ATTRIBUTE_RECALL_ON_OPEN 0x00040000
#endif
#ifndef COMPRESSION_FORMAT_DEFAULT
#define COMPRESSION_FORMAT_NONE    0
#define COMPRESSION_FORMAT_DEFAULT 1
#endif
#ifndef SRWLOCK_INIT
#define SRWLOCK_INIT RTL_SRWLOCK_INIT
#endif
#ifndef RTL_SRWLOCK_INIT
#define RTL_SRWLOCK_INIT {0}
#endif
#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

namespace sr
{
using u64 = unsigned long long;
using u32 = std::uint32_t;

constexpr u64 KiB = 1024ull;
constexpr u64 MiB = KiB * 1024ull;
constexpr u64 GiB = MiB * 1024ull;

// Formats a byte count as "12.4 GB" (binary units, the way Explorer shows them).
std::wstring FormatBytes(u64 bytes);
// "1,234,567"
std::wstring FormatCount(u64 n);
// "18%" / "0.4%"
std::wstring FormatPercent(double fraction);
// printf-style into a std::wstring (MinGW.org lacks std::to_wstring).
std::wstring Fmt(const wchar_t* format, ...);
// Human text for a Win32 error code.
std::wstring ErrorText(DWORD error);
// FILETIME as 64-bit ticks (100 ns).
inline u64 Ticks(const FILETIME& ft) { return (static_cast<u64>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime; }
u64 NowTicks();
constexpr u64 TicksPerDay = 864000000000ull;
// Timestamps before 1990-01-01 are placeholders (e.g. 1980 DOS epoch from zip extraction), not real ages.
constexpr u64 PlausibleTicks = 122756256000000000ull;
inline bool IsPlausibleTime(u64 ticks) { return ticks >= PlausibleTicks && ticks <= NowTicks() + TicksPerDay; }
// "3 days ago", "14 months ago"
std::wstring FormatAge(u64 ticks);

std::wstring ToLower(std::wstring s);
bool EndsWithI(const std::wstring& s, const wchar_t* suffix);
bool ContainsI(const std::wstring& haystack, const wchar_t* needle);
// "C:\\" from any path on that volume (drive letters only).
std::wstring VolumeOf(const std::wstring& path);
std::wstring ParentPath(const std::wstring& path);
std::wstring JoinPath(const std::wstring& a, const std::wstring& b);
// Prefix with \\?\ so paths over MAX_PATH work.
std::wstring LongPath(const std::wstring& path);
std::wstring ExeDirectory();
std::wstring KnownFolder(int csidl);

// Peak/current working set of this process.
u64 PeakWorkingSet();
u64 CurrentWorkingSet();
bool IsElevated();

// Simple RAII handle.
struct Handle
{
    HANDLE h = INVALID_HANDLE_VALUE;
    Handle() = default;
    explicit Handle(HANDLE v) : h(v) {}
    ~Handle() { reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& o) noexcept : h(o.h) { o.h = INVALID_HANDLE_VALUE; }
    bool valid() const { return h != INVALID_HANDLE_VALUE && h != nullptr; }
    void reset(HANDLE v = INVALID_HANDLE_VALUE)
    {
        if (valid()) CloseHandle(h);
        h = v;
    }
};

// SRW-lock guard (std::mutex is unavailable with this toolchain).
struct SrwGuard
{
    SRWLOCK* lock;
    explicit SrwGuard(SRWLOCK* l) : lock(l) { AcquireSRWLockExclusive(lock); }
    ~SrwGuard() { ReleaseSRWLockExclusive(lock); }
};
}
