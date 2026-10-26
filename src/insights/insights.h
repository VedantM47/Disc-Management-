// SpaceRescue - SPDX-License-Identifier: GPL-3.0-or-later
//
// F2 Space Insights engine: smart categories, stale files, top space hogs, growth since
// last scan, duplicates. Pure analysis over a ScanTree - no UI. Owner: core-engineer.

#pragma once

#include "../core/scanner.h"

namespace sr
{
enum class Category
{
    OldDownloads,   // Downloads older than N days
    Installers,     // .exe/.msi/.iso/.msix/.zip installers in user folders
    DevCaches,      // node_modules, .gradle, .m2, pip/npm/nuget caches, build outputs
    TempFiles,      // %TEMP%, Windows\Temp
    BrowserCaches,  // Chrome/Edge/Firefox cache folders
    WindowsUpdate,  // Windows\SoftwareDistribution\Download (+ link to Disk Cleanup)
    Videos,         // .mp4/.mkv/.mov/.avi/...
    RecycleBin,     // $Recycle.Bin contents
    Duplicates,     // filled by FindDuplicates()
    Count
};

struct CategoryResult
{
    Category id = Category::Count;
    std::wstring title;        // "Developer caches"
    std::wstring explanation;  // one plain-language line: what it is and whether it is safe
    u64 bytes = 0;
    u64 count = 0;             // files
    std::vector<u32> nodes;    // top-level matching nodes (folders or files), largest first
    bool reclaimable = false;  // counts toward the "safely reclaimable" estimate
};

enum class StaleSignal { LastAccess, LastModified };

struct StaleGroup
{
    u32 folder = NoNode;       // folder node the stale files live in
    u64 bytes = 0;
    u64 count = 0;
    std::vector<u32> files;    // stale file nodes in that folder (largest first, capped)
};

struct StaleResult
{
    StaleSignal signal = StaleSignal::LastModified;
    std::wstring signalReason;               // e.g. "NTFS last-access updates are disabled on this PC"
    static const int Buckets = 3;            // 6, 12, 24 months
    int months[Buckets] = { 6, 12, 24 };
    u64 bytes[Buckets] = {};
    u64 count[Buckets] = {};
    std::vector<StaleGroup> groups[Buckets]; // largest first, capped at 200 groups
};

struct Hog
{
    u32 node = NoNode;
    std::wstring explanation;  // "Video files, mostly .mp4" / "Visual Studio build output"
};

struct GrowthEntry
{
    std::wstring path;
    long long delta = 0;       // bytes; positive = grew
    u64 now = 0;
};

struct InsightsReport
{
    std::vector<CategoryResult> categories;  // indexed by Category
    StaleResult stale;
    std::vector<Hog> topFolders;             // 10 largest folders (not just ancestors of each other)
    std::vector<Hog> topFiles;               // 10 largest files
    u64 reclaimable = 0;                     // sum of reclaimable categories
    bool hasSnapshot = false;
    u64 snapshotTicks = 0;
    std::vector<GrowthEntry> growth;         // top 10 by delta
};

struct InsightsOptions
{
    int downloadsAgeDays = 90;
    std::wstring snapshotDir;  // where scan snapshots live (work volume); "" = no growth info
};

// Whether NTFS updates last-access times (registry NtfsDisableLastAccessUpdate).
// `reason` gets a plain-language explanation of what was found.
bool LastAccessUpdatesEnabled(std::wstring* reason);

// Runs all categories, stale detection, top hogs and growth. Does not read file contents.
InsightsReport Analyze(const ScanTree& tree, const InsightsOptions& options);

// Reads file contents to find duplicates (size groups -> partial hash -> full hash).
// Only files >= minSize. Checks *cancel periodically. Result goes in report.categories[Duplicates].
CategoryResult FindDuplicates(const ScanTree& tree, u64 minSize, volatile bool* cancel);

// Saves a compact snapshot (folder sizes to depth 3) for growth comparison next time.
bool SaveSnapshot(const ScanTree& tree, const std::wstring& snapshotDir);

// Explanation shown under a category.
std::wstring CategoryTitle(Category c);
}
