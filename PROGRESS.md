# Progress

## Phase 0 – Baseline

- [x] Upstream WinDirStat fetched (`upstream` remote @ `4c24e70`, v2.10.0), moved to `upstream/` as reference
- [x] Codebase mapped in `ARCHITECTURE_NOTES.md`
- [x] Toolchain decision: no Visual Studio available, so we build a standalone native Win32 app with local MinGW GCC 6.3 (see CLAUDE.md §0)
- [x] `build.ps1`: incremental build with a static link (the exe has no DLL dependencies, so it is portable)
- [x] Core scanner (`src/core/scanner.*`): 64-byte nodes, chunked name arena, memory-budget folding, low-priority mode, Wow64 redirection off

### Baseline (2026-10-26, C: NTFS, 235 GB used, unelevated, single-threaded scanner)

| Metric | Value |
|---|---|
| Files / folders | 1,673,251 / 402,080 (2.08M nodes) |
| Scan time | 289 s |
| Peak working set | 264 MB (tree itself: 254 MB) |
| Inaccessible folders | 520 (access denied without elevation) |

Regression rule: later builds must stay within +10% of these numbers on the same drive.
Possible speed-up later: multi-threaded enumeration (upstream uses a work queue).

## First full build (2026-10-26)

- [x] Engine: rescue, actions (+undo, incl. Recycle undo via `$I`/`$R` rename), setup, insights, internals. Tested on Downloads and a 6 GB ISO (fragment count matches merged `fsutil` runs)
- [x] UI: Home, Rescue banner, Scanning, Insights, Review + undo toast, Folders, Disk Internals, status bar, dark mode, keyboard
- [x] Clean build, 0 warnings, `build\SpaceRescue.exe` 2.3 MB, no DLL dependencies; smoke test OK (17 MB RAM)
- [x] `package.ps1` → `SpaceRescue-portable.zip` (653 KB), `SpaceRescue-Setup-x86.exe` (704 KB)
- [x] Fix: Recycle uses FOF_WANTNUKEWARNING, so items too big for the bin are never silently destroyed

## Not yet verified

- [ ] Elevated run: MFT stats vs `fsutil fsinfo ntfsinfo`
- [ ] Reserve file create/release, install/uninstall, context menu (not run on the dev PC on purpose)
- [ ] 0-byte VHDX test (needs admin for diskpart)
- [ ] Light mode, 150% DPI; dark popup menus
- [ ] README.md, CONTRIBUTIONS.md, user guide, demo script

## Decisions / findings

- Upstream needs VS 2026 (toolset v145, C++23, ATL), which can't be installed here, so it is reference-only.
- `WIN32_FIND_DATA` already gives last-access time, so the scanner stores it per node and stale detection needs no extra I/O.
- Recycle Bin frees nothing on a full drive until it is emptied. The UI says so and offers quarantine to the work volume.
