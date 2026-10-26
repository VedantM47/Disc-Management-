// SpaceRescue - SPDX-License-Identifier: GPL-3.0-or-later
//
// Silent low-space notifications (SpaceRescue.exe /tray): a tray icon, a 30 s free-space poll and
// a Windows notification when a drive crosses 50/20/10/5/3/2/1 GB or 512 MB on the way down.
// Holds no scan tree and writes nothing to disk.

#pragma once

#include "../core/common.h"

namespace sr
{
// Per-drive alert state with hysteresis. Pure logic so it can be tested without a UI.
class AlertLevel
{
public:
    static constexpr int Count = 8;
    static const u64 Thresholds[Count]; // descending: 50 GB ... 512 MB

    // Feeds the current free space. Returns the index of the threshold to announce (the most
    // severe newly crossed one), or -1. The first sample only announces at 1 GB or below.
    int Update(u64 freeBytes);
    int Level() const { return m_level; }

private:
    int m_level = -1;   // number of thresholds currently below; -1 = no sample yet
};

// Runs the tray watcher until the user chooses Exit. Returns the process exit code.
int RunTray();

// HKCU\...\Run entry that starts "<exe> /tray" at sign-in.
DWORD SetStartWithWindows(bool enable);
bool IsStartWithWindows();
}
