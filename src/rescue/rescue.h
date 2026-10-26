// SpaceRescue - SPDX-License-Identifier: GPL-3.0-or-later
//
// F1 Rescue Mode: drive health, work volume, reserve ("ballast") file, settings that
// never get written to a full drive. Owner: core-engineer.

#pragma once

#include "../core/common.h"

namespace sr
{
struct DriveInfo
{
    std::wstring root;        // "C:\"
    std::wstring label;       // volume label ("Local Disk" if empty)
    std::wstring fileSystem;  // "NTFS"
    UINT type = 0;            // DRIVE_FIXED / DRIVE_REMOVABLE
    u64 total = 0;
    u64 free = 0;             // free bytes available to the caller
    bool isSystem = false;    // holds %SystemRoot%
    bool low = false;         // below the Rescue Mode threshold
};

struct RescueConfig
{
    u64 lowBytes = 1 * GiB;          // drive is "low" below this many free bytes...
    double lowFraction = 0.02;       // ...or below this fraction of its size
    u64 reserveSize = 512 * MiB;     // 256 MB - 2 GB
    std::wstring workVolumeOverride; // "" = automatic
};

// Fixed and removable drives with a mounted file system.
std::vector<DriveInfo> EnumerateDrives(const RescueConfig& config);
bool IsLow(u64 free, u64 total, const RescueConfig& config);

// The fixed/removable drive with the most free space, excluding `exclude` and any low drive.
// Returns "" when no other usable drive exists (caller keeps state in memory and says so).
std::wstring PickWorkVolume(const std::wstring& exclude, const RescueConfig& config);

// --- Reserve file (like ext4's reserved blocks for root) ---------------------------
struct ReserveStatus
{
    bool exists = false;
    u64 size = 0;
    std::wstring path;
};
std::wstring ReservePath();       // %ProgramData%\SpaceRescue\reserve.bin
ReserveStatus QueryReserve();
// Allocates real clusters (SetEndOfFile + SetFileValidData when privileged, otherwise
// writes zeros) - never a sparse file. Returns ERROR_SUCCESS or a Win32 error.
DWORD CreateReserve(u64 size);
// Deletes the reserve file, handing its space back to the OS.
DWORD ReleaseReserve();

// --- Settings ----------------------------------------------------------------------
// INI-backed. Location: next to the exe in portable mode, else %APPDATA%\SpaceRescue.
// If that location's drive is low, settings are kept in memory only (zero-write rule).
class Settings
{
public:
    static Settings& Get();
    void Load();
    bool Save();                      // false when writes are suppressed
    bool WritesSuppressed() const { return m_suppressed; }
    std::wstring Location() const { return m_path; }
    bool Portable() const { return m_portable; }

    RescueConfig rescue;
    int staleMonthsDefault = 12;
    int downloadsAgeDays = 90;
    u64 memoryBudgetMB = 0;           // 0 = unlimited
    std::wstring lastScanPath;

private:
    std::wstring m_path;
    bool m_portable = false;
    bool m_suppressed = false;
};
}
