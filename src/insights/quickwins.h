// SpaceRescue - SPDX-License-Identifier: GPL-3.0-or-later
//
// Quick-win pre-scan: sizes a short list of known hotspots in parallel with the full scan, so
// "you can free about X GB" appears within seconds on a full drive. Holds no tree; reads only names/sizes.

#pragma once

#include "../core/common.h"

#include <atomic>
#include <mutex>
#include <thread>

namespace sr
{
struct QuickWin
{
    std::wstring label;
    u64 bytes = 0;
    bool done = false;
    bool safe = true;     // Safe vs Review (Recycle Bin, Downloads)
};

class QuickWins
{
public:
    ~QuickWins();
    // Starts sizing hotspots on `drive` ("C:\").
    void Start(const std::wstring& drive);
    void Cancel() { m_cancel = true; }
    std::vector<QuickWin> Snapshot();
    u64 SafeTotal();
    bool Done();

private:
    std::vector<std::thread> m_threads;
    std::mutex m_lock;
    std::vector<QuickWin> m_items;
    std::atomic<bool> m_cancel{ false };
    std::atomic<int> m_running{ 0 };
};
}
