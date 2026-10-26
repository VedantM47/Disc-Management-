// SpaceRescue - SPDX-License-Identifier: GPL-3.0-or-later

#include "quickwins.h"

namespace sr
{
namespace
{
struct Spot
{
    const wchar_t* label;
    std::wstring path;
    bool safe;
    bool file = false; // a single file (hiberfil.sys) rather than a folder
};

std::wstring TempDir()
{
    wchar_t temp[MAX_PATH] = {}, full[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, temp);
    if (GetLongPathNameW(temp, full, MAX_PATH)) return full;
    return temp;
}
}

QuickWins::~QuickWins()
{
    Cancel();
    for (std::thread& t : m_threads) if (t.joinable()) t.join();
}

void QuickWins::Start(const std::wstring& drive)
{
    const std::wstring win = KnownFolder(CSIDL_WINDOWS), local = KnownFolder(CSIDL_LOCAL_APPDATA);
    const std::wstring profile = KnownFolder(CSIDL_PROFILE), roaming = KnownFolder(CSIDL_APPDATA);
    const Spot spots[] = {
        { L"Temporary files", TempDir(), true },
        { L"Windows temp", JoinPath(win, L"Temp"), true },
        { L"Windows Update downloads", JoinPath(win, L"SoftwareDistribution\\Download"), true },
        { L"Chrome cache", JoinPath(local, L"Google\\Chrome\\User Data"), true },
        { L"Edge cache", JoinPath(local, L"Microsoft\\Edge\\User Data"), true },
        { L"Crash dumps", JoinPath(local, L"CrashDumps"), true },
        { L"npm cache", JoinPath(local, L"npm-cache"), true },
        { L"Gradle cache", JoinPath(profile, L".gradle\\caches"), true },
        { L"pip cache", JoinPath(local, L"pip\\cache"), true },
        { L"VS Code cache", JoinPath(roaming, L"Code\\Cache"), true },
        { L"Recycle Bin", JoinPath(drive, L"$Recycle.Bin"), false },
        { L"Downloads", JoinPath(profile, L"Downloads"), false },
        { L"Hibernation file", JoinPath(drive, L"hiberfil.sys"), false, true },
    };
    m_cancel = false;
    for (const Spot& s : spots)
    {
        if (_wcsicmp(VolumeOf(s.path).c_str(), drive.c_str()) != 0) continue;
        const size_t slot = m_items.size();
        {
            // Earlier worker threads write m_items[slot] under m_lock; a reallocation must not race them.
            std::lock_guard<std::mutex> guard(m_lock);
            m_items.push_back({ s.label, 0, false, s.safe });
        }
        ++m_running;
        m_threads.emplace_back([this, slot, s] {
            SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN); // never slow the main scan
            u64 total = 0;
            if (s.file)
            {
                WIN32_FILE_ATTRIBUTE_DATA d;
                if (GetFileAttributesExW(s.path.c_str(), GetFileExInfoStandard, &d))
                    total = (static_cast<u64>(d.nFileSizeHigh) << 32) | d.nFileSizeLow;
            }
            else
            {
                // Browser "User Data": count only cache folders inside each profile.
                const bool browser = s.path.find(L"User Data") != std::wstring::npos;
                std::vector<std::wstring> stack{ s.path };
                while (!stack.empty() && !m_cancel)
                {
                    const std::wstring dir = std::move(stack.back());
                    stack.pop_back();
                    WIN32_FIND_DATAW fd;
                    HANDLE h = FindFirstFileExW(LongPath(JoinPath(dir, L"*")).c_str(), FindExInfoBasic, &fd,
                        FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
                    if (h == INVALID_HANDLE_VALUE) continue;
                    u64 local = 0;
                    do
                    {
                        const wchar_t* n = fd.cFileName;
                        if (n[0] == L'.' && (!n[1] || (n[1] == L'.' && !n[2]))) continue;
                        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                        {
                            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
                            const std::wstring child = JoinPath(dir, n);
                            // In browser data only descend into profiles and their cache folders.
                            if (browser && dir != s.path && !ContainsI(child, L"cache")) continue;
                            stack.push_back(child);
                        }
                        else if (!browser || ContainsI(dir, L"cache"))
                            local += (static_cast<u64>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
                    }
                    while (FindNextFileW(h, &fd));
                    FindClose(h);
                    total += local;
                    std::lock_guard<std::mutex> guard(m_lock);
                    m_items[slot].bytes = total;
                }
            }
            std::lock_guard<std::mutex> guard(m_lock);
            m_items[slot].bytes = total;
            m_items[slot].done = true;
            --m_running;
        });
    }
}

std::vector<QuickWin> QuickWins::Snapshot()
{
    std::lock_guard<std::mutex> guard(m_lock);
    std::vector<QuickWin> out = m_items;
    std::sort(out.begin(), out.end(), [](const QuickWin& a, const QuickWin& b) { return a.bytes > b.bytes; });
    return out;
}

u64 QuickWins::SafeTotal()
{
    std::lock_guard<std::mutex> guard(m_lock);
    u64 total = 0;
    for (const QuickWin& q : m_items) if (q.safe) total += q.bytes;
    return total;
}

bool QuickWins::Done() { return m_running.load() == 0; }
}
