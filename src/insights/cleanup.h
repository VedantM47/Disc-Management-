// SpaceRescue - SPDX-License-Identifier: GPL-3.0-or-later
//
// Segmented cleanup (brief §4.3) and protected/system awareness (§4.2).
// Detection is a data table (cleanup.cpp kRules) so new apps are one line each.

#pragma once

#include "insights.h"

#include <array>

namespace sr
{
enum class Safety { Safe, Review, Caution, Protected };

enum class CleanSection { WindowsTemp, AppCaches, Developer, OldUnused, Count };

struct CleanItem
{
    u32 node = NoNode;
    std::wstring label;      // "Cache", "node_modules", "ext4.vhdx"
    Safety safety = Safety::Safe;
    u64 bytes = 0;
};

// One app or one project, e.g. "Google Chrome" or "C:\src\web-app".
struct CleanGroup
{
    std::wstring name;
    std::wstring why;        // plain-language: what it is and what happens if removed
    std::wstring tool;       // optional safe Windows tool instead of deleting ("Optimize VHD", "Disk Cleanup")
    Safety safety = Safety::Safe;   // worst item safety
    u64 bytes = 0;
    u64 lastOpened = 0;      // projects: newest change outside caches (FILETIME ticks)
    std::vector<CleanItem> items;
};

struct CleanSectionResult
{
    CleanSection id = CleanSection::Count;
    std::wstring title;
    u64 bytes = 0;
    std::vector<CleanGroup> groups;   // largest first
};

struct CleanupReport
{
    std::array<CleanSectionResult, static_cast<size_t>(CleanSection::Count)> sections;
    u64 safeBytes = 0, reviewBytes = 0, cautionBytes = 0;
};

// Builds the four sections. `insights` (optional) supplies old downloads, installers, duplicates, videos, stale files.
CleanupReport BuildCleanup(const ScanTree& tree, const InsightsReport* insights);

// --- Protected / system --------------------------------------------------------

struct SystemItem
{
    std::wstring label;      // "Hibernation file"
    std::wstring why;
    std::wstring tool;       // safe Windows way to shrink it, or "Managed by Windows"
    std::wstring command;    // what the tool runs (shown before running; may need admin)
    u32 node = NoNode;
    u64 bytes = 0;
    u64 reducible = 0;       // estimate of what the tool can free
};

struct SystemReport
{
    bool available = false;  // false when the scan root is not the system drive root
    u64 total = 0, reducible = 0;
    std::vector<SystemItem> items;
};

SystemReport AnalyzeSystem(const ScanTree& tree);

// True for paths Windows manages that must never be deleted by hand (review lists hide Delete for them).
bool IsProtectedPath(const std::wstring& path);
}
