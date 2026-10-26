# Contributions: what we built vs. what came from WinDirStat

WinDirStat (`upstream/`, GPL-3.0, v2.10.0 @ `4c24e70`) needs Visual Studio 2026 (C++23 + ATL). A timeboxed spike showed it
needs about 75 MSVC-specific fixes to build with our toolchain (`tools/upstream-spike/`, PROGRESS.md). So instead of forking
the whole app, we **ported its engines and views into a new, smaller app** and added our own features on top.

## Reused or adapted from WinDirStat
| Our file | Upstream source | How |
|---|---|---|
| `src/viz/viz.cpp` `Treemap::Node` | `Controls/TreeMapLayout.cpp` (`ArrangeSquarified`) | Squarified layout algorithm, rewritten for our tree and integer pixel rects |
| `src/viz/viz.cpp` `Treemap::Node`/`Cushion` | `Controls/TreeMap.cpp` (`AddRidge`, `DrawCushion`, light vector, defaults 0.38 / 0.91 / 0.13) | Cushion shading math ported, with the same parameters |
| `src/core/scanner.cpp` `Enumerate` | `FinderBasic.cpp` (`NtQueryDirectoryFile`, `FileFullDirectoryInformation`) | Same API and buffer approach; buffer tuned to 64 KiB by measurement |
| `src/core/scanner.cpp` worker queue | `BlockingQueue.h`, `CItem::ScanItems` | LIFO folder queue with N workers that stops when all are idle |
| `src/core/scanner_mft.cpp` | `FinderNtfs.cpp` | MFT read via `FSCTL_GET_NTFS_VOLUME_DATA` + `$MFT` retrieval pointers, fixups, `$STANDARD_INFORMATION`/`$FILE_NAME`/`$DATA` parsing |
| `src/rescue/setup.cpp` context menu | `Pages/PageGeneral.cpp`, `setup/msi/WinDirStat.wxs` | Same registry verbs, plus `Directory\Background` |
| `src/rescue/rescue.cpp` portable INI | `WinDirStat.cpp` (`InPortableMode`, `-portable.exe`) | Same detection rules |
| `src/insights/cleanup.cpp` system tools | `windirstat.rc` Cleanup menu (Disk Cleanup, DISM, hibernate) | Same safe Windows tools, offered instead of Delete |
| `ARCHITECTURE_NOTES.md`, `STATUS_REPORT.md` §4 | whole upstream codebase | Study of architecture, views, treemap and engines |

## New in SpaceRescue
- **Rescue Mode** (`src/rescue`): low-drive detection, zero-write settings, reserve ("ballast") file, work-volume selection, quarantine with verified copy and undo log, Recycle Bin undo by `$I/$R` rename, same-drive quarantine guard.
- **Compact scan tree** (`src/core/scanner.*`): 64-byte nodes, lock-free per-folder node reservation, per-worker name arenas, ASCII names at 1 byte per char, memory-budget folding, live per-folder progress.
- **Insights** (`src/insights`): smart categories, stale-file detection with the NTFS last-access check, top hogs with explanations, growth snapshots, duplicates.
- **Segmented cleanup and system awareness** (`src/insights/cleanup.*`): a data table of ~60 app/dev/Windows cache locations, project grouping with "last opened", protected-path classification, "Windows itself uses X GB".
- **Disk internals** (`src/internals`): volume/MFT statistics, extents and fragment merging, slack estimate, plain-language explanations.
- **Design system and new UI** (`DESIGN.md`, `src/gfx`, `src/ui2`): Direct2D/DirectWrite rendering, tokens, motion, sunburst, flame graph.
- **Low-space alerts** (`src/notify`): tray watcher with hysteresis thresholds.
- **Toolchain and packaging** (`build.ps1`, `package.ps1`): llvm-mingw x64 with parallel incremental builds, an IExpress installer, a portable zip.

## Not used from upstream
The `Ui*` MFC-replacement framework, settings pages, remote/MTP/cloud scanning, deleted-file recovery, permissions and file
watcher. They depend on MSVC/ATL or are outside the project's scope.

## Team
Commits are split between **Vedant** (@VedantM47) and **Shreeviraj** (@reesick) by module. See `git log` on the `revamp` branch.
