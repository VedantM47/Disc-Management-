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

## Revamp – Phase A: audit (2026-10-26)
- [x] `STATUS_REPORT.md` complete (sections 1–8), screenshots in `docs/screens/` (dark only, captured off-screen)

## Revamp – Phase B: toolchain
- [x] llvm-mingw 20261020 (clang 23.1.4, x86_64, libc++) installed per-user via winget, no admin
- [x] `build.ps1` → clang++ `-std=c++23`, x64, static, parallel jobs (one per core), `.d`-based incremental, `-Fast`/`-Release`/`-Clean`/`-Gcc`
- [x] Clean build: 0 errors, 0 warnings. Full -O2 build 19.9 s, `-Fast` 6.6 s, one-file incremental **1.7 s**
- [x] **Upstream spike, timebox used.** Compiled a scratch copy of `upstream/windirstat` with llvm-mingw plus a 120-line ATL shim (`tools/upstream-spike/`):
  - Only 2 of 87 pch headers were missing under clang (ATL, xxhash).
  - Qualifying 476 MSVC-style `Route<&Handler>` member pointers took the first-error sites from 293 to 24.
  - Generating `LangStrings.h` then exposed the real count: **75 unique error sites, about 1,100 errors**, all MSVC/STL-specific: `std::execution::par` (absent in libc++), wide-path `ifstream`, `_ITERATOR_DEBUG_LEVEL`, explicit specialization after instantiation, generated git macros, const `WPARAM` writes, etc.
  - Linking (GDI+, WMI, virtdisk) and runtime were never reached.
  - **Decision: no true fork.** Making upstream build would mean editing dozens of upstream files, which breaks the minimal-churn rule. Instead we keep our app and port upstream's engines and views into it (treemap layout + cushion shader first), credited in `CONTRIBUTIONS.md`.

## Revamp – Phase C: fast scanner
- [x] N workers (default min(cores, 8)) on a LIFO folder queue; `NtQueryDirectoryFile(FileFullDirectoryInformation)` with a 64 KiB per-thread buffer (faster than 1 MiB, measured); real `AllocationSize` from the FS (no per-file path strings or extra calls)
- [x] Lock-free node reservation per folder (contiguous blocks), per-worker name cursors; ASCII names stored 1 byte/char
- [x] Live per-top-level-folder sizes in `ScanProgress::topLevel` (for streaming UI)

| Benchmark (warm cache, unelevated, x64) | Before | After |
|---|---|---|
| C:\ full (1.68M files, 402K folders) | 57.9 s (cold 289 s), 263 MB | **10.6 s, 258–261 MB** |
| C:\Windows (349K files) | 21–25 s, 111 MB | **5.3 s**, 170 MB |
| App: scan C:\Users\imved (1.2M files) | 38.7 s, 164 MB | **6.4 s, 139 MB** |

- [ ] Cold-cache and MFT (elevated) timings: not run yet (needs a reboot or admin)
- [ ] Quick-win pre-scan + streaming UI

## Revamp – Phase G: low-space notifications (done early: engine only, no UI dependency)
- [x] `SpaceRescue.exe /tray`: hidden window + tray icon, 30 s `GetDiskFreeSpaceEx` poll, `WM_DEVICECHANGE`, `TaskbarCreated` re-add, single instance
- [x] Thresholds 50/20/10/5/3/2/1 GB + 512 MB, downward-only, 10% hysteresis (`AlertLevel`). **11/11 headless tests pass** (scratch test exe)
- [x] Tone: quiet (no sound) above 10 GB, warning at 5 GB and below, "almost full + Rescue Mode" at 1 GB and below; adds a quick temp/cache estimate
- [x] Click opens the app on that drive; `/autostart on|off`; `/install /alerts`; uninstall removes the Run key and closes the watcher
- [x] Measured: **1.6 MB private memory**, 0.1 MB working set after trim (target under 8 MB)
- [ ] Real VHDX fill test (needs admin)

## Revamp – Phase D: design system (**waiting for approval**)
- [x] `DESIGN.md`: tokens (deep neutral + "rescue teal" accent + Safe/Review/Caution/Protected), 10-colour file-type palette, type scale with tabular figures, 4 px grid, radius, elevation, motion, icons, Rescue Mode rules
- [x] `src/gfx`: Direct2D + DirectWrite layer (DIP-based, per-monitor DPI, text styles, rounded cards with soft shadows, annular sectors, arc meters, pixel blits, eased motion that respects "Animation effects")
- [x] `src/ui2/preview.cpp`: `SpaceRescue.exe /preview [path] [/light|/dark]`, left nav rail with 8 tabs on real scan data, live streaming scan screen, title bar tinted to the base colour + Mica, Ctrl+1…8
- [x] Screenshots: `docs/screens/preview-{dark,light}-*.png` (every tab + map modes + live scan)

## Revamp – Phase E: visualisations (components done, built into the preview)
- [x] Treemap ported from upstream (squarified layout + cushion shading, upstream defaults): **full C: (1.68M files) in 76 ms**
- [x] Sunburst (Direct2D, grouped small slices, free-space slice, unfold animation, hover tooltip, drill in/out)
- [x] Flame graph; one file-kind colour model (`BuildKinds`, dominant kind per folder) shared by all three plus the legends
- [ ] Linked selection with Explore (Phase F, after approval)
- Note: preview peak RAM is 304–314 MB on C: (scan 260 MB + kind model + analysis). Trim the kind model's temporary per-folder sums in Phase F.

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
