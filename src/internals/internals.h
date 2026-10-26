// SpaceRescue - SPDX-License-Identifier: GPL-3.0-or-later
//
// F3 Disk Internals: clusters, MFT, slack space, file extents. Owner: core-engineer.
// Numbers must match `fsutil fsinfo ntfsinfo C:` and `fsutil file queryextents <file>`.

#pragma once

#include "../core/scanner.h"

namespace sr
{
struct VolumeInternals
{
    std::wstring root;
    std::wstring fileSystem;
    std::wstring label;
    DWORD serial = 0;
    DWORD bytesPerSector = 0;
    DWORD bytesPerCluster = 0;
    u64 totalClusters = 0;
    u64 freeClusters = 0;
    // NTFS only (FSCTL_GET_NTFS_VOLUME_DATA; needs administrator rights)
    bool ntfsData = false;
    DWORD ntfsError = 0;            // why ntfsData is false
    DWORD bytesPerFileRecord = 0;
    u64 mftValidDataLength = 0;     // MFT size in bytes
    u64 mftRecordCount = 0;         // mftValidDataLength / bytesPerFileRecord
    u64 mftStartLcn = 0;
    u64 mftZoneStart = 0, mftZoneEnd = 0;
};

struct Extent
{
    u64 vcn = 0;          // first virtual cluster number (offset inside the file)
    u64 lcn = 0;          // first logical cluster number (position on the volume)
    u64 clusters = 0;
    bool hole = false;    // sparse/compressed gap (lcn == -1)
};

struct FileInternals
{
    std::wstring path;
    u64 logicalSize = 0;
    u64 allocatedSize = 0;  // size on disk
    u64 slack = 0;          // allocated - logical (0 when resident/compressed smaller)
    bool resident = false;  // data stored inside the MFT record (no extents)
    bool compressed = false, sparse = false, encrypted = false;
    DWORD hardLinks = 1;
    std::vector<Extent> extents;   // capped at 4096 for display; fragments counts all
    u64 fragments = 0;             // non-hole extents, after merging physically adjacent runs
    DWORD error = 0;
};

VolumeInternals QueryVolume(const std::wstring& root);
FileInternals QueryFile(const std::wstring& path, DWORD bytesPerCluster);

struct FragmentedFile
{
    u32 node = NoNode;
    u64 fragments = 0;
};

struct SlackSummary
{
    u64 totalSlack = 0;        // sum over scanned files of (allocated - logical), estimate
    u64 filesCounted = 0;
    u64 smallFilesWaste = 0;   // slack from files smaller than one cluster
};

// From the scan tree alone (no disk reads).
SlackSummary EstimateSlack(const ScanTree& tree);
// Queries extents for the `topN` largest files and returns them by fragment count, most first.
std::vector<FragmentedFile> MostFragmented(const ScanTree& tree, size_t topN, volatile bool* cancel);

enum class Metric { ClusterSize, Slack, Extents, Mft, FreeClusters, Resident, Compression };
// Short "What does this mean?" text for the demo.
std::wstring Explain(Metric metric);
}
