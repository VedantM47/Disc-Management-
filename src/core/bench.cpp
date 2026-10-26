// SpaceRescue - SPDX-License-Identifier: GPL-3.0-or-later
//
// SpaceRescue.exe /bench [path]  -  prints scan timings to the calling console. Writes nothing.
// Elevated + whole NTFS drive: compares directory enumeration with the MFT fast path.

#include "bench.h"

#include "scanner.h"

#include <cstdio>

namespace sr
{
namespace
{
struct Result
{
    u64 files = 0, dirs = 0, size = 0, alloc = 0, millis = 0, tree = 0;
    bool mft = false;
};

Result ScanOnce(const std::wstring& root, bool useMft)
{
    ScanOptions o;
    o.root = root;
    o.useMft = useMft;
    Scanner s;
    s.Start(o, nullptr, 0);
    auto t = s.TakeResult();
    return { t->fileCount, t->dirCount, t->At(0).size, t->At(0).alloc, t->scanMillis, t->MemoryUsed(), t->usedMft };
}

// Writes to the inherited stdout (file/pipe) or the parent's console. A GUI-subsystem exe has no
// console of its own, so stdio would print nowhere.
void Emit(const std::wstring& s)
{
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!out || out == INVALID_HANDLE_VALUE || GetFileType(out) == FILE_TYPE_UNKNOWN)
    {
        static const bool attached = AttachConsole(ATTACH_PARENT_PROCESS) != FALSE;
        if (!attached) return;
        out = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    }
    DWORD written = 0, mode = 0;
    if (GetConsoleMode(out, &mode)) WriteConsoleW(out, s.c_str(), static_cast<DWORD>(s.size()), &written, nullptr);
    else
    {
        const int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
        std::string utf8(static_cast<size_t>(n), '\0');
        WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), utf8.data(), n, nullptr, nullptr);
        WriteFile(out, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    }
}

void Print(const wchar_t* label, const Result& r)
{
    Emit(Fmt(L"%-22ls %10llu files %9llu folders  %10ls  on disk %10ls  %7.1f s  tree %ls\r\n", label, r.files, r.dirs,
        FormatBytes(r.size).c_str(), FormatBytes(r.alloc).c_str(), r.millis / 1000.0, FormatBytes(r.tree).c_str()));
}
}

int RunBench(std::wstring path)
{
    if (path.empty()) path = VolumeOf(KnownFolder(CSIDL_WINDOWS));
    if (path.size() == 2 && path[1] == L':') path += L'\\';
    Emit(Fmt(L"\r\nSpaceRescue scan benchmark: %ls (elevated: %ls)\r\n", path.c_str(), IsElevated() ? L"yes" : L"no"));
    const Result dir = ScanOnce(path, false);
    Print(L"Directory enumeration", dir);
    if (IsElevated() && path.size() == 3)
    {
        const Result mft = ScanOnce(path, true);
        Print(mft.mft ? L"MFT fast path" : L"MFT (fell back)", mft);
        if (mft.mft)
            Emit(Fmt(L"Difference: %+lld files, %+lld folders, %ls size (hard links count once in the MFT; folders the\r\n"
                     L"normal scan can't open are included by the MFT)\r\n",
                static_cast<long long>(mft.files) - static_cast<long long>(dir.files),
                static_cast<long long>(mft.dirs) - static_cast<long long>(dir.dirs),
                FormatBytes(mft.size > dir.size ? mft.size - dir.size : dir.size - mft.size).c_str()));
    }
    else Emit(L"Run from an elevated prompt on a whole drive (e.g. /bench C:) to compare the MFT fast path.\r\n");
    Emit(Fmt(L"Peak working set: %ls\r\n", FormatBytes(PeakWorkingSet()).c_str()));
    return 0;
}
}
