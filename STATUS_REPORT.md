# SpaceRescue – Status Report

Date: 2026-10-26 · Repo: `C:\Users\imved\OneDrive\Desktop\OS_CP` (branch `main`, plus local `rewrite`) · No code was changed for this report.
Throwaway benchmark/probe programs were built in the session scratchpad and deleted afterwards.
Anything not checked by running a command or reading code is marked **NOT VERIFIED**.

---

## 1. Local machine and toolchain

### Machine
| Item | Value (measured) |
|---|---|
| OS | Windows 11 Home Single Language, 10.0.26300 (build 26300), 64-bit |
| CPU | 13th Gen Intel Core i5-1335U: 10 cores, 12 logical processors |
| RAM | 15.7 GB total, 2.9 GB free at the time of measurement |
| C: | NTFS, 291.8 GB, 53.2 GB free |
| D: | NTFS, 66.0 GB, 65.9 GB free |
| Shell elevated (admin)? | **No** |
| NTFS last-access updates | `NtfsDisableLastAccessUpdate = 0x80000002`, i.e. system-managed, **enabled** (`fsutil behavior query disablelastaccess` → 2) |

### Build tools found
| Tool | Found | Details |
|---|---|---|
| g++ / gcc | Yes, `C:\MinGW\bin` | **MinGW.org** GCC 6.3.0-1, target `mingw32` (**32-bit**), thread model `win32` (no `std::thread`/`std::mutex`), max `-std=c++17`/`gnu++17` (`c++2a` rejected) |
| windres | Yes | GNU Binutils 2.28 |
| git | Yes | 2.50.0.windows.1, credential manager configured |
| python | Yes | 3.13.0 |
| dotnet | Yes, runtime only | `dotnet --list-sdks` is empty, so there is **no SDK** and `dotnet tool install wix` is impossible |
| winget | Yes | v1.29.380 |
| clang/clang++, cl.exe, MSBuild, cmake, ninja, scoop, choco, wix, MinGW-w64 (`x86_64-w64-mingw32-g++`) | **Not found** | |
| WSL | Ubuntu (WSL2) present | Its apt was broken by WSL clock skew earlier, and gcc is not installed there. Not used. |

### Installable without Visual Studio (searched only, nothing installed)
| Toolchain | winget id | Installer type | Admin needed? | Command |
|---|---|---|---|---|
| llvm-mingw (clang 2x, libc++, 64-bit, C++23-capable) | `MartinStorsjo.LLVM-MinGW.UCRT` (23.1.4) | portable zip | **No** (per-user portable) | `winget install MartinStorsjo.LLVM-MinGW.UCRT` |
| MinGW-w64 GCC 16 (WinLibs) | `BrechtSanders.WinLibs.POSIX.UCRT` (16.2.0) | portable zip | **No** | `winget install BrechtSanders.WinLibs.POSIX.UCRT` |
| MSYS2 | `MSYS2.MSYS2` (20260927) | installer | Normally yes, NOT VERIFIED | `winget install MSYS2.MSYS2`, then `pacman -S mingw-w64-ucrt-x86_64-gcc` |
| VS Build Tools 2026 (command line only) | `Microsoft.VisualStudio.BuildTools` (18.11.2) | exe (VS installer) | **Yes** | `winget install Microsoft.VisualStudio.BuildTools --override "--quiet --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.ATL --includeRecommended"` |
| WiX v7 CLI | `WiXToolset.WiXCLI` (7.0.0.0) | MSI | Likely yes, NOT VERIFIED | `winget install WiXToolset.WiXCLI` |
| WiX v3 | `WiXToolset.WiXToolset` (3.14.1) | exe/MSI | Likely yes, NOT VERIFIED | `winget install WiXToolset.WiXToolset` |

### Can anything here build upstream WinDirStat?
Upstream needs the v145 toolset (VS 2026), `stdcpp23` and ATL.
- **MinGW.org g++ 6.3 (the only compiler present): no.** Test compiles of `upstream/windirstat/Filtering.cpp` and `Controls/TreeMap.cpp` (with `-std=gnu++14` and `-std=c++17`) stop at the first error:
  `pch.h:21: fatal error: VersionHelpers.h: No such file or directory`.
  Of the 87 headers `pch.h` includes, **27 are missing** from MinGW.org: `VersionHelpers.h, atlbase.h, bcrypt.h, comdef.h, dwmapi.h, fdi.h, PortableDeviceApi.h, PortableDevice.h, propkey.h, sal.h, vssym32.h, wbemidl.h, virtdisk.h, winternl.h, <bit>, <concepts>, <execution>, <expected>, <filesystem>, <format>, <numbers>, <optional>, <ranges>, <source_location>, <span>, <string_view>, contrib/xxhash/xxhash.h`.
  Upstream also uses `std::span` 270×, `std::ranges`/`views` 324×, `std::format` ~67–78×, `std::jthread` 13×, and ATL 95 lines (`CComPtr` 61, `CRegKey` 18, `CComBSTR` 13, `CComVariant` 6).
- **VS Build Tools 2026 + ATL** builds it unchanged. This is the only zero-port option, and it needs admin.
- **llvm-mingw / WinLibs GCC 16:** the C++23 library parts would compile, but ATL does not exist there. About 95 ATL lines would need a small `ComPtr`/registry shim, plus MSVC-isms. NOT VERIFIED (not installed).

---

## 2. What is built (feature by feature)

How each row was verified:
- **ran**: seen working in this session, either in screenshots under `docs/screens/` or in the engine agent's test exe.
- **code**: read the code only.
- **NOT VERIFIED**: not executed.

### F1 Rescue Mode
| Feature | Files | Status | Verified |
|---|---|---|---|
| Drive detection | `src/rescue/rescue.cpp` `EnumerateDrives`, `IsLow` (l.50–76) | Done | ran (Home cards show C:/D: correctly, `dark-home.png`) |
| Rescue banner | `src/ui/app_window.cpp` banner widget | Done | ran (forced with a high `LowMB` in a test INI, `dark-rescue-banner-insights.png`). Bug: with D: also "low" under that test INI it reads "No other drive", which is correct for the test but untested for the real case. |
| Reserve file create/release | `rescue.cpp` `CreateReserve` l.130–160 (refuses when the result would be low, l.137; `SetFileValidData` + zero-fill fallback), `ReleaseReserve` | Partial | code. **NOT VERIFIED** at runtime (deliberately not created on this PC). Re-create after release is not wired into the UI. |
| Work volume | `rescue.cpp` `PickWorkVolume` l.82; `app_window.cpp` l.755 | Partial | ran (`PickWorkVolume` → D:). No UI to override it (`RescueConfig.workVolumeOverride` exists but has no UI). |
| Zero-write settings | `rescue.cpp` l.230 (`m_suppressed` when the settings drive is low), l.242 the only `WritePrivateProfileStringW` | Done | code + ran (banner screenshot status bar: "Settings kept in memory (drive is full)") |
| Snapshot writes | `insights.cpp:665` (`CreateFileW GENERIC_WRITE`); `app_window.cpp:681–699` `ChooseSnapshotDir` refuses low drives | Done | code |
| Memory-budget folding | `scanner.cpp` `ScanFolder` (aggregate `[N small files]` node) | Done | code. Runtime **NOT VERIFIED** (budget never hit in the benchmarks). |
| Low-priority scan in Rescue | `scanner.cpp` `THREAD_MODE_BACKGROUND_BEGIN` | Done | code |
| Auto-open in Rescue on launch | banner appears on every view when a drive is low | Partial | ran. There is no separate "Rescue screen"; it's a banner only. |
| MFT fast path | – | **Missing** | – |
| Portable mode | `rescue.cpp` Settings (INI next to exe / `-portable.exe`) | Done | code + `package.ps1` writes `SpaceRescue.ini` into the zip |

### Safe actions (F1/F2)
| Feature | Files | Status | Verified |
|---|---|---|---|
| Recycle (+`FOF_WANTNUKEWARNING`) | `actions.cpp:367–380` | Done | ran (engine test) |
| Recycle undo via `$I`/`$R` rename | `actions.cpp:201–276` | Done | ran (engine test, scratch file) |
| Quarantine (copy → verify → delete) + undo | `actions.cpp:343–358` + copy/verify helpers | Partial | ran (engine test). **Bug:** `Apply` doesn't check the work volume differs from the source volume. The engine test quarantined C:→C:, which frees nothing; the UI passes `PickWorkVolume(exclude source)`, so only direct callers are at risk. |
| Compress / decompress undo | `actions.cpp:148–153` `FSCTL_SET_COMPRESSION` | Done | ran (engine test) |
| Permanent delete (confirm) | `actions.cpp`, `review_dialog.cpp` | Done | ran (engine test); confirm dialog code only |
| Undo log | `actions.cpp:358` `undo-log.txt` on the work volume, else memory | Done | ran (log written in engine test) |
| Undo toast | `app_window.cpp` toast widget | Partial | code only. Undo triggers a full rescan rather than reattaching nodes. |

### F2 Insights
| Feature | Files | Status | Verified |
|---|---|---|---|
| Smart categories (downloads, installers, dev caches, temp, browser caches, Windows Update, videos, Recycle Bin) | `src/insights/insights.cpp` `Analyze` | Done | ran (`dark-insights-*.png`, `dark-review-temp.png`) |
| Duplicates | `insights.cpp` `FindDuplicates` (size → partial FNV → full) | Done | ran (engine test: 107 MB in Downloads). No progress callback. |
| Stale files 6/12/24 mo + signal shown | `insights.cpp:541–574` `LastAccessUpdatesEnabled` | Done | ran: on this PC returns **enabled** (`0x80000002`) with the reason text. Earlier engine claim "key absent" was wrong. |
| Top hogs (folders + files with explanation) | `insights.cpp` | Done | ran (`dark-insights.png`, `-3.png`). Weak explanations, e.g. "Mostly files without an extension" for `AppData\Local`; dates like "46 years ago" for 1980-stamped files. |
| Growth since last scan | `insights.cpp` `SaveSnapshot`/load | Partial | ran (snapshot reload). A non-empty growth list was never observed. |
| Reclaimable estimate | `insights.cpp` | Done | ran (24.7 GB for the profile scan) |

### F3 Disk Internals
| Feature | Files | Status | Verified |
|---|---|---|---|
| Volume: FS, cluster size, clusters | `src/internals/internals.cpp` `QueryVolume` | Done | ran (`dark-internals.png`: 4 KB clusters, 76,496,127 total) |
| MFT stats | `QueryVolume` `FSCTL_GET_NTFS_VOLUME_DATA` | Partial | ran unelevated: shows "Run as administrator". Elevated values **NOT VERIFIED** vs `fsutil fsinfo ntfsinfo`. |
| File extents + strip | `QueryFile` `FSCTL_GET_RETRIEVAL_POINTERS`; `internals_page.cpp` | Done | ran (6 GB ISO: 24 merged fragments = `fsutil` runs merged; `dark-internals-file.png`) |
| Slack (file + volume estimate) | `QueryFile`, `EstimateSlack` | Done | ran (1.7 GB across 1.2M files) |
| Most fragmented files | `MostFragmented` | Done | ran (713 fragments on a gradle cache file) |
| Cluster map visual | – | **Missing** | – |

### F4 Explorer integration / F5 Packaging
| Feature | Files | Status | Verified |
|---|---|---|---|
| Context menu: folder / background (`%V`) / drive | `src/rescue/setup.cpp:140–160` (HKCU `Directory`, `Directory\Background`, `Drive`) | Done (code) | **NOT VERIFIED**: never registered on this PC |
| Path argument starts scan | `main.cpp`, `setup.cpp` `HandleCommandLine` | Done | ran (`SpaceRescue.exe <path>` used for every screenshot) |
| Install / uninstall | `setup.cpp` `Install`/`Uninstall`, Apps & features keys l.123–128, self-delete via `cmd /c ping … & del` l.214–222 | Done (code) | **NOT VERIFIED** |
| Packaging | `package.ps1` (zip + IExpress) | Done | ran (both files produced, 653 KB / 704 KB) |
| Windows 11 top-level menu (IExplorerCommand) | – | **Missing** (stretch) | – |

### UI screens
| Screen | Files | Status | Verified |
|---|---|---|---|
| Home (drive cards, choose folder, scan again) | `app_window.cpp` | Done | `dark-home.png` |
| Scanning (live counts, path, cancel) | `app_window.cpp` | Done | `dark-scanning.png`, `dark-cancel-test.png` |
| Insights (ring card, hogs, categories, stale, growth, duplicates) | `src/ui/insights_page.cpp` | Done | `dark-insights*.png` |
| Review dialog (checkbox list, actions, confirm) | `src/ui/review_dialog.cpp` | Done | `dark-review-temp.png` |
| Folders (lazy TreeView + size bars, context menu) | `src/ui/folders_page.cpp` | Done | `dark-folders.png` |
| Disk Internals (volume, single file, fragmented list) | `src/ui/internals_page.cpp` | Done | `dark-internals*.png` |
| Status bar (counts, time, peak memory, tree memory, settings location) | `app_window.cpp` | Done | screenshots |
| Treemap / sunburst / flame graph | – | **Missing** | – |
| Settings screen (thresholds, reserve size, work volume, theme) | – | **Missing** | – |
| Dark mode | `src/ui/theme.cpp` (follows `AppsUseLightTheme`) | Partial | dark ran. List-view headers and popup menus stay light/unstyled. |
| Light mode | same | Partial | **NOT VERIFIED**: no in-app override (`theme.cpp:35–40` reads only the registry) and the system theme was not changed |
| DPI (PerMonitorV2, `WM_DPICHANGED`) | `res/app.manifest`, `theme.cpp` | Partial | ran at 125% only; 150/200% **NOT VERIFIED** |
| Keyboard (Ctrl+1/2/3, Ctrl+Tab, Ctrl+O, F5, Tab) | `app_window.cpp`, `widgets.cpp` | Done (code) | **NOT VERIFIED** interactively |

---

## 3. Scan performance

### How `src/core/scanner.cpp` enumerates
- **One background thread** (`CreateThread` in `Scanner::Start`), with Wow64 FS redirection disabled and optional `THREAD_MODE_BACKGROUND_BEGIN` (low I/O and CPU priority).
- **Depth-first walk with an explicit stack** of `(node, std::wstring path)` pairs (`Scanner::Run`).
- **Per folder:** `FindFirstFileExW(\\?\path\*, FindExInfoStandard, …, FIND_FIRST_EX_LARGE_FETCH)` + `FindNextFileW` (`ScanFolder`). `FindExInfoStandard` also asks the FS for the 8.3 short name. A progress SRW lock is taken twice per folder.
- **Per file:**
  - `wcslen(name)`.
  - **`JoinPath(path, name)`, a heap-allocated full-path string for every file**, built only to pass to `AllocatedSize()`. That function needs it only for compressed or sparse files.
  - `ScanTree::NewNode` (64-byte node in 1 MB chunks) + `AddName` (copy into a 1 MB-chunk name arena).
  - Optionally `MemoryUsed()` when a memory budget is set.
- **Per sub-folder:** another `JoinPath` string pushed on the stack.
- **Afterwards:** one reverse pass rolls up size, allocated size, file count and max timestamps (children always have larger indexes than parents).
- **Memory:** about 64 B per node + name; 2.08M nodes → 254 MB of tree, 263 MB peak working set.

### How upstream does it (`upstream/windirstat/`)
- **FinderBasic** (`FinderBasic.cpp`): `NtOpenFile(FILE_LIST_DIRECTORY, FILE_OPEN_FOR_BACKUP_INTENT)` + `NtQueryDirectoryFile` with `FileIdFullDirectoryInformation` (falls back to `FileFullDirectoryInformation`). Uses a **4 MiB `thread_local` buffer** (64 KiB for remote volumes). No Win32 `FindFirstFile` layer, and no short names.
- **FinderNtfs** (`FinderNtfs.cpp`, `LoadRoot` ~l.151): opens `\\.\X:` (`FILE_FLAG_NO_BUFFERING|OVERLAPPED`), `FSCTL_GET_NTFS_VOLUME_DATA`, opens `X:\$MFT::$DATA`, `FSCTL_GET_RETRIEVAL_POINTERS` for the MFT runs. It then reads the MFT sequentially in 4 MiB aligned chunks, in parallel (`std::execution::par`), applies update-sequence fixups, and parses `$STANDARD_INFORMATION`, `$FILE_NAME`, `$DATA` and reparse attributes. **Needs admin.** If the volume open fails, it falls back to FinderBasic.
- **Selection** (`Item.cpp` ~1309–1352): MFT is used only for whole-drive items when `UseFastScanEngine` (default on) and the MFT loaded. Folder scans always use FinderBasic.
- **Threads:** `BlockingQueue<CItem*>` (LIFO deque + `std::mutex`/`condition_variable` + `std::jthread`). Each volume gets `ScanningThreads` workers (**default 4**, range 1–16; remote ≤4, MTP 1). Each worker runs `CItem::ScanItems`: it pops a folder, enumerates it, and pushes child folders. The queue self-terminates when all workers are idle and it is empty.

### Benchmarks (this machine, unelevated, prototype `bench2` in scratchpad, now deleted)
All runs on the same files. Because the first run cold-reads the disk, **cold vs warm cache matters most**.

**C:\Windows (349,198 files, 193,155 folders, 42.1 GB), warm cache**
| Variant | Time | Peak RAM |
|---|---|---|
| Current scanner (`cur`), run 1 / run 2 | 25.4 s / 21.3 s | 111 MB |
| Enumeration only, `FindExInfoStandard`, 1 thread | 18.7 s | 17 MB |
| (a) `FindExInfoBasic` + `LARGE_FETCH`, 1 thread | 15.8 s | 17 MB |
| (b) same, 4 threads | 7.7 s | 24 MB |
| (b) same, 8 threads | 6.1 s | 29 MB |
| (b) same, 16 threads | 5.5 s | 38 MB |

**C:\ (1.67M files, 402K folders, 215.6 GB logical)**
| Variant | Time | Peak RAM |
|---|---|---|
| Current scanner, **cold cache** (first run of the session, PROGRESS.md baseline) | **289 s** | 264 MB |
| Current scanner, warm cache | 57.9 s | 263 MB |
| (a) `FindExInfoBasic`, 1 thread, warm | 34.5 s | 17 MB |
| (b) `FindExInfoBasic`, 8 threads, warm | **16.1 s** | 29 MB |
| (c) MFT reading | **NOT VERIFIED**: the shell is not elevated, so `\\.\C:` cannot be opened for raw reads | – |

(The enumeration-only prototypes don't build a tree, so their RAM excludes the 254 MB node store.)

### Top 3 causes of slowness (ranked)
1. **Single-threaded enumeration.** On the same work, 8 threads are 2.1× faster on C:\ (34.5 s → 16.1 s) and 2.6× faster on C:\Windows (15.8 s → 6.1 s). NVMe and the filesystem cache serve parallel directory queries well. Upstream defaults to 4 threads.
2. **Cold-cache directory reads (I/O bound).** The first scan took 289 s and a warm re-scan 58 s (5×), so on a cold disk most time is waiting on random metadata reads. Only the MFT path (sequential reads of `$MFT`) fixes this structurally. That needs admin; NOT VERIFIED here.
3. **Per-file overhead in our scanner:**
   - A heap `JoinPath` string per file (`scanner.cpp`, `ScanFolder` → `AllocatedSize(JoinPath(path, name), fd)`), even though only compressed or sparse files need the path.
   - `FindExInfoStandard` instead of `FindExInfoBasic` (+18% on C:\Windows: 18.7 s vs 15.8 s).
   - Node and name copies.
   Together, our single-threaded scanner takes 57.9 s against 34.5 s for bare single-threaded enumeration on C:\ (+68%).

---

## 4. Upstream WinDirStat study

### 4.1 Architecture and intent
WinDirStat is a Win32 disk-usage analyzer: it scans drives or folders and shows a sortable tree, a file-type table and a graphical view (treemap, flame graph or sunburst), with cleanup, duplicate, search, permission and recovery tools on top. Built with C++23 (`windirstat.vcxproj:74`).
- **No MFC.** A hand-written replacement framework (~5.6k lines) keeps the MFC names:
  - `UiWindow.h:11` defines `CWnd`, and `UiTypes.h` the `CDC`/`CRect`/GDI wrappers.
  - `UiControls.h` holds the common controls, and `UiPanes.h` the splitter, toolbar, statusbar, tab and property sheet.
  - `UiApplication.h` defines `CWinApp`, `CFrameWnd` and `CDialog`.
- **Message routing:** `MessageTarget<Derived, Base>` (`UiTypes.h:286`) builds a chained `RouteTable` from a static `Routes()`. Handlers are declared with `Route::Window/Command/Update/Control<&Handler>` (`UiWindow.h:344-411`). This replaces MFC message maps.
- **App and frame:**
  - `CDirStatApp` (`WinDirStat.h:23`) handles portable mode, icons and the reparse policy.
  - `CMainFrame` (`MainFrame.h:151`; `OnCreateClient` `MainFrame.cpp:593`) builds a splitter with `CVisualizationPane` and a sub-splitter holding `CFileTabbedView` + `CExtensionView`. Layouts are permutable (`MainFrame.Commands.cpp:1194`).
- **Model:** `CWinDirStatModel` (`WinDirStatModel.h:71`) owns the `CItem` tree, the extension data and the scan. Panes get `OnUpdate(sender, MODEL_CHANGE, item)`. Actions are in `WinDirStatModel.Actions.cpp`.
- **Item:** `CItem` (`Item.h:114`). A variable-size allocation with an inline name. Types: file, folder, drive, My Computer, free space, unknown, hardlinks, remote/MTP.
- **Settings:** `COptions` (`Options.h:148`) is a set of static `Setting<T>`, stored as INI in portable mode, otherwise in the registry via ATL `CRegKey` (`Property.cpp:43-96`).
- **Localization:** `Localization.cpp` reads 25 `res/langs/lang_*.txt` files (looked up as `IDS_*` keys).
- **Settings pages:** `Pages/Page*.cpp`.

### 4.2 Views, tabs and panes
| Pane | Shows | How reached | Files |
|---|---|---|---|
| File tree (default tab) | Hierarchical virtual list: size, %, items, dates, attributes | Tab 1 | `Views/FileTreeView`, `Controls/FileTreeControl`, `TreeListControl`, `WdsListControl`, `Item.cpp` |
| Treemap | Cushion or flat rectangles, zoom, extension labels, folder frames | Visualization pane | `Views/TreeMapView`, `Controls/TreeMap`, `TreeMapLayout`, `HilbertMooreTreeMap` |
| Flame graph | Icicle chart, one row per depth, breadcrumbs | View menu | `Views/FlameGraphView`, `Controls/FlameGraph` |
| Sunburst | Radial rings (GDI+) | View menu | `Views/SunburstView`, `Controls/Sunburst` |
| Visualization pane | Container switching the 3 graphs (`VisualizationPane.cpp:13`) | View > Show visualization | `Views/VisualizationPane`, `GraphView` |
| Extensions | Per-extension color, description, bytes, %, files | Right pane | `Views/ExtensionView`, `Controls/ExtensionListControl` |
| Duplicates | Groups of identical files | Tab | `FileDupeControl`, `ItemDupe` |
| Top files | N largest files | Tab | `FileTopControl`, `ItemTop` |
| Search | Name glob/regex results + size filters | Edit > Search → `SearchDlg` → tab | `Dialogs/SearchDlg`, `FileSearchControl`, `ItemSearch` |
| Permissions | DACL / inheritance per item | Tools | `FilePermsControl`, `ItemPerm`, `PagePermissions` |
| File watcher | Live change log (`ReadDirectoryChangesW` on `jthread`s, `FileWatcherControl.cpp:62-113`) | Tools | `FileWatcherControl` |
| Storage analytics | Age tiers + cloud-cost estimate | Tools | `Views/StorageAnalyticsView` |
| Recovery | Undelete on NTFS, FAT and exFAT | Tools | `Dialogs/RecoveryDlg`, `Recovery*.cpp` |
| Select drives | Drives, folder, cloud | File > Select | `Dialogs/SelectDrivesDlg` |

### 4.3 Treemap: computation and drawing
- **Layouts** (`TreeMapLayout.h:12`):
  - Rows (default, KDirStat-style).
  - Squarified (worst-aspect-ratio rows, `TreeMapLayout.cpp:101-143`).
  - Hilbert and Moore space-filling curves (`HilbertMooreTreeMap.cpp`).

  Children are sorted by size descending. Layout runs on an explicit stack (`TreeMap.cpp` ~728–800), with depth capped by `TreeMapMaxDepth` (default 6).
- **Cushion shading** (SequoiaView method, `DrawCushion` `TreeMap.cpp:995`):
  - Each nesting level adds a parabolic ridge (`AddRidge`, l.1059) to a surface `s[0..3]`. Ridge height = `height` × `scaleFactor`^depth.
  - Light vector (lx, ly, 10), normalized (l.432–439).
  - Per pixel: normal = (−(2·s0·(x+.5)+s2), −(2·s1·(y+.5)+s3), 1). `cosa = (n·L)/|n|`, and brightness = `Ia + max(Is·cosa, 0)` with Ia = ambient and Is = 1−Ia.
  - The RGB value is the base color × brightness, with overflow redistributed (`NormalizeColor`).
- **Defaults** (`TreeMap.h:326-340`): brightness 0.88, height 0.38, scale 0.91, ambient 0.13, light (−1,−1). Presets: Classic, Calm, Flat, Pastel, HighContrast, Custom.
- **Pixels:** written directly into a 32-bit top-down DIBSection when available, else a `COLORREF` vector + `SetDIBitsToDevice` (`TreeMap.cpp:189, 699-714`). Rows are parallelized with `std::execution::par` for large cushions only. Labels and frames are drawn with GDI after. A 16 px hit-test grid handles mouse picking.
- **Colors:**
  - Extensions are ranked by total bytes (`WinDirStatModel.cpp:434-495`) and mapped onto 18 `DefaultCushionColors` normalized to brightness 0.6 (`TreeMap.h:344`, `MakeBrightColor` `TreeMap.cpp:236`). Ranks past 18 share the last color.
  - Special items: free space is gray, unknown is yellow, hardlinks are tan (`Item.Extended.cpp:1111`).
- **Options:** `Pages/PageTreeMap.cpp`, with live preview and an `XYSlider` for the light source.

### 4.4 Menus, toolbar and cleanup actions (`windirstat.rc:94-360`)
- **File:** Select, Select cloud, Load/Save results, Save dupes/perms, Refresh all/selected, Run elevated, Exit.
- **Edit:** Copy, Search, Exclude, Compute hash.
- **Cleanup:**
  - Open and Explorer-select, cmd, PowerShell, Properties.
  - Delete, Delete to bin, Empty bin, Remove empty, Empty folder, Move to.
  - Compress submenu (None, LZNT1, XPRESS 4K/8K/16K, LZX), Sparsify, Optimize VHD, Create hardlink, Remove Mark-of-the-Web.
  - Disable hibernate, Disk Cleanup, Storage Sense, Programs and Features.
  - Remove roaming/local profiles, Remove shadow copies.
  - DISM submenu (`/AnalyzeComponentStore`, `/StartComponentCleanup [/ResetBase]`).
  - **User-defined cleanups** (`Options.h:88`, `PageCleanups`).
- **View / Treemap:** styles, flame graph, sunburst, presets, logical vs physical size, folder frames, zoom, select parent.
- **Tools:** Watcher, Permissions, Storage analytics, Recovery, Set dates, Shadow copy, Defrag, Chkdsk.
- **Options:** Show free space and unknown, file types, toolbar, font size, status bar, Settings.
- **Toolbar** (`MainFrame.Commands.cpp` ~705–745): select, resume/suspend/stop, refresh, search, filter, open, copy, console, properties, delete to bin, delete, zoom, layout, settings, help.

### 4.5 What we can port into the MinGW build
| Component | Verdict | What's needed |
|---|---|---|
| Treemap cushion renderer (`TreeMap.cpp` `DrawCushion`/`AddRidge`, `CColorSpace`) | **Portable nearly as-is** | Plain float math + DIB writes. Drop `std::execution::par`, `views::iota/enumerate`, `to_underlying` and designated initializers. |
| Treemap layout, Rows/Squarified | Needs adapting (small) | `std::span` → pointer + count, ranges → `<algorithm>` |
| Hilbert/Moore layout | Needs adapting | 14 `span` + 10 ranges uses, integer math |
| Top files | Portable nearly as-is | `ranges::push_heap` → `std::push_heap` |
| Extension stats + color by rank | Needs adapting (small) | Replace `std::atomic` + cache-line padding |
| Duplicate finder (3-level hash, xxHash3) | Needs adapting | `xxhash.h` is plain C and compiles. Replace `mutex/jthread`. `bcrypt.h` is **missing** in MinGW.org. |
| FinderNtfs MFT parser | Needs adapting | FSCTL structs exist in our headers (verified by an earlier probe). Replace `execution::par` with `CreateThread`, plus `span`/`bit_cast`. **Needs admin at runtime.** |
| Flame graph | Needs adapting | Pure GDI. Replace `requires std::invocable` (`FlameGraph.h:49,99`) and ranges. |
| Recovery parsers (~2.3k lines) | Needs adapting | Byte parsing is portable. ~43 `span<const BYTE>` uses and `std::format` (`RecoveryShared.cpp`). |
| Dark mode (`DarkMode.cpp`) | Needs adapting (small) | uxtheme ordinals 133/135 through `GetProcAddress` are plain Win32 |
| Sunburst | Partly blocked | 101 GDI+ uses. MinGW.org GDI+ headers are incomplete (NOT VERIFIED in detail). |
| `Ui*` framework, dialogs, panes | **Blocked / rewrite** | `span`, `views::enumerate`, `requires std::derived_from`, `optional`. Our own `src/ui` replaces it. |
| Anything using ATL (`CComPtr`, `CRegKey`, `CComBSTR`: shell, WMI, MTP, shadow copies, registry settings) | **Blocked** | ATL is absent. A ~50-line `ComPtr` template would unblock most of it. |

**Net:** the algorithmic cores are small and independent: treemap layout + cushion, MFT parser, top files, extension colors, duplicate grouping and flame graph. Each needs a mechanical C++14 backport (`span` → pointer + length, ranges → `<algorithm>`, `format` → `swprintf`, `jthread/mutex` → Win32 threads). The framework, dialogs and ATL-based features do not port.

---

## 5. UI audit

### Screenshots (`docs/screens/`, captured off-screen with `PrintWindow`, dark theme, 125% DPI)
| File | Screen |
|---|---|
| `dark-home.png` | Home: drive cards, Choose folder, Scan again |
| `dark-scanning.png` | Scanning C:\Users\imved (live counts, current path, Cancel) |
| `dark-cancel-test.png` | State after cancelling a scan |
| `dark-insights.png`, `dark-insights-top.png`, `-2` … `-6.png` | Insights: summary card, top folders/files, categories, stale, growth |
| `dark-rescue-banner-insights.png` | Rescue banner (forced with a high `LowMB` in a test INI) |
| `dark-review-temp.png` | Review dialog for "Temporary files" |
| `dark-folders.png` | Folders tree with size bars |
| `dark-internals.png`, `dark-internals-file.png` | Disk Internals: volume panel, single-file extents, most-fragmented list |

**Light theme: NOT VERIFIED.** The app has no theme override, and changing the Windows theme was out of scope. From the code, light mode swaps the token set in `theme.cpp`, uses the accent #0067C0, and has the same layout.

### Layout of each screen (from the screenshots)
- **Home:** title "Where is your space going?", two buttons, then one card per drive (name, FS, chips, a thin usage bar, used/free text, Scan). The lower 40% of the window is empty.
- **Insights:** one long vertical scroll page.
  - A summary card (donut ring + "X safely reclaimable"), then card lists: Largest folders (10), Largest files (10), Smart categories, Stale files, Growth, Duplicates.
  - Every row has the same two buttons (Show / Review…).
- **Folders:** a plain TreeView with "name — size (%)" text and a right-aligned bar; a detail strip at the bottom.
- **Disk Internals:** a two-column label/value list with long explanatory paragraphs, a "single file" section, and a ListView of fragmented files.

### UI/UX problems found
1. **No visualization at all.** No treemap, sunburst or flame graph; the only graphic is one donut ring. That's the core of WinDirStat and the biggest gap.
2. **Wall of identical cards.** Insights is one undifferentiated scroll (≈6 screens). Hogs, categories and stale files all use the same row style with the same two buttons, so there is no visual hierarchy and no single primary action.
3. **The summary ring mixes scopes.** When a folder is scanned, the ring shows **drive** usage (82%) next to the **folder's** reclaimable number (`dark-insights.png`, `dark-rescue-banner-insights.png`). It's confusing.
4. **Weak explanations and bad dates:**
   - "Mostly files without an extension" for `AppData\Local` (80 GB).
   - "last changed **46 years ago**" for files with 1980 DOS-epoch timestamps (`dark-insights-3.png`).
   - Both come from the `insights.cpp` explanation/age helpers.
5. **Dark-mode gaps:** ListView headers render dark-on-dark and are nearly unreadable (`dark-internals.png`, `dark-review-temp.png`). Popup menus and checkboxes stay in the light system style.
6. **Review dialog:**
   - Category items appear as one folder row ("Temp\ 147 MB") with no way to drill into files.
   - Buttons are uniform grey: the destructive "Delete permanently…" looks the same as "Select all".
   - The window title bar is light while its content is dark.
7. **Folders tab:** the text-only TreeView has no columns (files, modified) and no file-type colors, and it isn't linked to any visualization.
8. **Disk Internals is text-heavy:** long paragraphs, no cluster-map graphic, and the extent strip only appears after picking a file.
9. **Blocking or unprogressed work:**
   - `Analyze` cannot be cancelled.
   - Duplicates show elapsed time only (no progress).
   - Undo triggers a full rescan.
   - The full C: scan takes 58–289 s with no partial results (see §3).
10. **Empty space and density:** Home leaves ~40% blank; Insights wastes width (content column ~1250 px with large margins) while the review list is cramped.
11. **No Settings screen** for thresholds, reserve size, work volume, threads or theme.

---

## 6. Notifications feasibility (low-space warnings at 50/20/10/5/3/2/1 GB and 512 MB)

**Mechanism that works with our compiler (verified by a compile probe):**
- **Tray icon + balloon** via `Shell_NotifyIconW` with `NIF_INFO` (`NOTIFYICONDATAW` is 956 bytes, and `NOTIFYICON_VERSION_4` is defined in MinGW.org headers). On Windows 10/11, balloons from `NIF_INFO` are shown as native toast-style notifications in the Action Center, so no WinRT is needed.
- **Real WinRT toasts** (`Windows.UI.Notifications`) are **not possible** with this toolchain: `roapi.h`, `wrl.h` and the WinRT headers are absent.
- **Startup:** `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\SpaceRescue = "<exe>" /tray`. Per-user, no admin. `setup.cpp` already writes HKCU keys, so the same pattern applies.

**Design sketch:**
- A `/tray` mode: no main window, a hidden message-only window, the tray icon, and a `SetTimer` poll.
- **Thresholds:** 50, 20, 10, 5, 3, 2, 1 GB and 512 MB. Keep the last threshold crossed per drive and notify only when crossing **downwards**, with hysteresis (e.g. re-arm only after free space rises 10% above the threshold). The 1 GB and 512 MB levels match the Rescue Mode trigger.
- Clicking the balloon opens the main window on that drive.
- **Cheaper than polling:** besides a timer, use `WM_DEVICECHANGE` for new drives and optionally `FindFirstChangeNotification`. That one isn't free-space aware, though, so timer polling stays the primary signal.

**Cost (measured):**
- `GetDiskFreeSpaceExW` on 2 drives costs **≈30 µs per poll** (10,000 iterations). Polling every 30–60 s is effectively free; even every 5 s costs well under 0.01% CPU.
- An idle Win32 process's working set: a minimal console probe is **5.3 MB**. A tray-only instance of our static exe should stay around 5–8 MB if it doesn't create the main window or load fonts and canvases (NOT VERIFIED). By contrast, a SpaceRescue window holding a full C:\ scan sits at ~270 MB, so the tray watcher must be a **separate lightweight mode/process** that never keeps a scan tree.
- No disk writes are needed; the watcher's state (last notified threshold) stays in memory.

---

## 7. Packaging and gaps

### Current outputs (`package.ps1`, verified by running it)
- **`build\dist\SpaceRescue-portable.zip` (653 KB):** `SpaceRescue.exe` + an empty `SpaceRescue.ini` (which switches on portable mode) + `LICENSE.md`. It runs from a USB stick with no installation and has no DLL dependencies (static link).
- **`build\dist\SpaceRescue-Setup-x86.exe` (704 KB):** an IExpress self-extractor that runs `SpaceRescue.exe /install`. Per `setup.cpp` it:
  - copies the exe to `%LOCALAPPDATA%\Programs\SpaceRescue\`;
  - creates a Start-menu shortcut;
  - registers the HKCU context menu (folder, background, drive);
  - writes the HKCU Apps & features entry (DisplayName, DisplayIcon, UninstallString `/uninstall`, Publisher, InstallLocation, EstimatedSize, NoModify/NoRepair).

  `/uninstall` removes all of that plus the reserve file and settings, then deletes its own exe through a delayed hidden `cmd /c ping … & del` (`setup.cpp:214–222`).

### What they don't do
- **x86 only** (32-bit GCC). No x64 or ARM64 build.
- **Per-user only.** No per-machine install and no MSI (WiX isn't available without admin, see §1).
- **No setup UI or options:** no install location choice, no checkboxes for context menu, reserve file or start with Windows. `/install /reserve` exists only as a switch.
- **Unsigned:** SmartScreen will warn on the IExpress exe.
- **No upgrade logic** beyond overwriting the copied exe. Upgrade-over-previous is **NOT VERIFIED**.
- The IExpress extractor briefly unpacks to `%TEMP%` on the system drive, which could fail on a 0-byte C:. The portable zip run from another drive is the Rescue-safe path.

### Still unverified (from PROGRESS.md and this audit)
- Elevated MFT numbers vs `fsutil fsinfo ntfsinfo C:`.
- Reserve file: create, release, re-create, removal on uninstall.
- Install, upgrade and uninstall; context menu on folder, background and drive.
- The 0-byte VHDX test (needs admin for diskpart).
- Light mode, 150% and 200% DPI, keyboard-only navigation.
- Memory-budget folding at runtime; a non-empty growth list.

### Bugs and risky code (ranked)
1. **Quarantine target not validated** (`actions.cpp:343–358`): if the work volume equals the source volume, files are "quarantined" onto the same drive, so nothing is freed and the full drive gets extra writes. Fix: refuse when `VolumeOf(path) == workVolume`.
2. **Recycle on huge items:** `FOF_WANTNUKEWARNING` now asks first, but the prompt comes from a worker thread with `FOF_SILENT|FOF_NOERRORUI` (`actions.cpp:374–377`). Whether the warning appears correctly and modally is **NOT VERIFIED**.
3. **Tree mutation vs background readers:** `ScanTree::Detach` (after an action) runs on the UI thread while Insights duplicates and Internals fragmentation workers may still be reading the tree. `StopBackground()` is called on rescans (`app_window.cpp:796, 895, 1133`), but it isn't clear it's called before `Detach` after an action. **Possible use-after-unlink or race.**
4. **Undo = full rescan:** correct but slow (58–289 s on C:).
5. **32-bit limits:** a 2–4 GB address space caps scans at a few million nodes. The 264 MB peak is fine on C:, but a 10M-file drive would hit ~1.3 GB of tree plus fragmentation. `FindDuplicates` reads files in full on a 32-bit process (fine with streaming reads; buffer sizes **NOT VERIFIED**).
6. **Implausible dates:** 1980 or future timestamps feed both "stale" and "age" text ("46 years ago"). Stale detection would flag them as very old. Clamp and label as "date unknown".
7. **Self-delete on uninstall** relies on `ping` timing (3 s). A slow exit leaves the exe behind (harmless, but untidy).
8. **IExpress setup extracts to the system drive's `%TEMP%`.** It can't install on a full C:.
9. The engine agent's tests left **`C:\SpaceRescue-Quarantine\`** on this PC. Waiting for the user's OK to delete it.
10. **No crashes observed** in any run this session (≈15 launches, scans up to 1.2M files).

---

## 8. Summary

Ranked, most important first:
1. **Multithreaded scanner** (work queue, 8 workers, `FindExInfoBasic`, no per-file path strings). Measured 58 s → ~16 s on C: warm (§3).
2. **Show results before the scan ends:** a quick-win pre-scan of known hotspots plus live top-level sizes, so a user with 512 MB free gets an answer in seconds.
3. **Toolchain → llvm-mingw x64, C++23** (installed per-user, no admin). It unlocks porting upstream code (span, ranges, format, jthread) and removes the 32-bit memory ceiling.
4. **Port upstream's treemap** (layout + cushion shader) and add a sunburst, both linked to the folder list. This is the biggest UX gap (§5 #1).
5. **MFT fast path** (port `FinderNtfs`) with a "Fast scan (needs admin)" relaunch. It's the only fix for cold-cache scans (289 s).
6. **Redesign the Insights/Overview hierarchy:** one hero visual, one primary action, segmented cleanup cards instead of a wall of identical rows. Fix dark-mode headers and menus.
7. **Fix the safety bugs:** quarantine same-volume check, `Detach` vs background-worker race, implausible-date handling.
8. **Tray low-space notifications** (`/tray`, `Shell_NotifyIconW` + `NIF_INFO`, HKCU Run). Verified feasible at ≈30 µs per poll (§6).
9. **Protected/system file awareness** (pagefile, hiberfil, WinSxS, etc.) with safe Windows tools instead of Delete, and a **Settings screen**.
10. **Verification pass + x64 installer with options:** VHDX 0-byte test, reserve file, install/upgrade/uninstall, context menu, elevated MFT vs `fsutil`, light mode, 150/200% DPI. Then the docs (README, CONTRIBUTIONS, USER_GUIDE, DEMO_SCRIPT).
# SpaceRescue – Status Report

Date: 2026-10-26 · Repo: `C:\Users\imved\OneDrive\Desktop\OS_CP` (branch `main`, plus local `rewrite`) · No code was changed for this report.
Throwaway benchmark/probe programs were built in the session scratchpad and deleted afterwards.
Anything not checked by running a command or reading code is marked **NOT VERIFIED**.

---

## 1. Local machine and toolchain

### Machine
| Item | Value (measured) |
|---|---|
| OS | Windows 11 Home Single Language, 10.0.26300 (build 26300), 64-bit |
| CPU | 13th Gen Intel Core i5-1335U: 10 cores, 12 logical processors |
| RAM | 15.7 GB total, 2.9 GB free at the time of measurement |
| C: | NTFS, 291.8 GB, 53.2 GB free |
| D: | NTFS, 66.0 GB, 65.9 GB free |
| Shell elevated (admin)? | **No** |
| NTFS last-access updates | `NtfsDisableLastAccessUpdate = 0x80000002`, i.e. system-managed, **enabled** (`fsutil behavior query disablelastaccess` → 2) |

### Build tools found
| Tool | Found | Details |
|---|---|---|
| g++ / gcc | Yes, `C:\MinGW\bin` | **MinGW.org** GCC 6.3.0-1, target `mingw32` (**32-bit**), thread model `win32` (no `std::thread`/`std::mutex`), max `-std=c++17`/`gnu++17` (`c++2a` rejected) |
| windres | Yes | GNU Binutils 2.28 |
| git | Yes | 2.50.0.windows.1, credential manager configured |
| python | Yes | 3.13.0 |
| dotnet | Yes, runtime only | `dotnet --list-sdks` is empty, so there is **no SDK** and `dotnet tool install wix` is impossible |
| winget | Yes | v1.29.380 |
| clang/clang++, cl.exe, MSBuild, cmake, ninja, scoop, choco, wix, MinGW-w64 (`x86_64-w64-mingw32-g++`) | **Not found** | |
| WSL | Ubuntu (WSL2) present | Its apt was broken by WSL clock skew earlier, and gcc is not installed there. Not used. |

### Installable without Visual Studio (searched only, nothing installed)
| Toolchain | winget id | Installer type | Admin needed? | Command |
|---|---|---|---|---|
| llvm-mingw (clang 2x, libc++, 64-bit, C++23-capable) | `MartinStorsjo.LLVM-MinGW.UCRT` (23.1.4) | portable zip | **No** (per-user portable) | `winget install MartinStorsjo.LLVM-MinGW.UCRT` |
| MinGW-w64 GCC 16 (WinLibs) | `BrechtSanders.WinLibs.POSIX.UCRT` (16.2.0) | portable zip | **No** | `winget install BrechtSanders.WinLibs.POSIX.UCRT` |
| MSYS2 | `MSYS2.MSYS2` (20260927) | installer | Normally yes, NOT VERIFIED | `winget install MSYS2.MSYS2`, then `pacman -S mingw-w64-ucrt-x86_64-gcc` |
| VS Build Tools 2026 (command line only) | `Microsoft.VisualStudio.BuildTools` (18.11.2) | exe (VS installer) | **Yes** | `winget install Microsoft.VisualStudio.BuildTools --override "--quiet --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.ATL --includeRecommended"` |
| WiX v7 CLI | `WiXToolset.WiXCLI` (7.0.0.0) | MSI | Likely yes, NOT VERIFIED | `winget install WiXToolset.WiXCLI` |
| WiX v3 | `WiXToolset.WiXToolset` (3.14.1) | exe/MSI | Likely yes, NOT VERIFIED | `winget install WiXToolset.WiXToolset` |

### Can anything here build upstream WinDirStat?
Upstream needs the v145 toolset (VS 2026), `stdcpp23` and ATL.
- **MinGW.org g++ 6.3 (the only compiler present): no.** Test compiles of `upstream/windirstat/Filtering.cpp` and `Controls/TreeMap.cpp` (with `-std=gnu++14` and `-std=c++17`) stop at the first error:
  `pch.h:21: fatal error: VersionHelpers.h: No such file or directory`.
  Of the 87 headers `pch.h` includes, **27 are missing** from MinGW.org: `VersionHelpers.h, atlbase.h, bcrypt.h, comdef.h, dwmapi.h, fdi.h, PortableDeviceApi.h, PortableDevice.h, propkey.h, sal.h, vssym32.h, wbemidl.h, virtdisk.h, winternl.h, <bit>, <concepts>, <execution>, <expected>, <filesystem>, <format>, <numbers>, <optional>, <ranges>, <source_location>, <span>, <string_view>, contrib/xxhash/xxhash.h`.
  Upstream also uses `std::span` 270×, `std::ranges`/`views` 324×, `std::format` ~67–78×, `std::jthread` 13×, and ATL 95 lines (`CComPtr` 61, `CRegKey` 18, `CComBSTR` 13, `CComVariant` 6).
- **VS Build Tools 2026 + ATL** builds it unchanged. This is the only zero-port option, and it needs admin.
- **llvm-mingw / WinLibs GCC 16:** the C++23 library parts would compile, but ATL does not exist there. About 95 ATL lines would need a small `ComPtr`/registry shim, plus MSVC-isms. NOT VERIFIED (not installed).

---

## 2. What is built (feature by feature)

How each row was verified:
- **ran**: seen working in this session, either in screenshots under `docs/screens/` or in the engine agent's test exe.
- **code**: read the code only.
- **NOT VERIFIED**: not executed.

### F1 Rescue Mode
| Feature | Files | Status | Verified |
|---|---|---|---|
| Drive detection | `src/rescue/rescue.cpp` `EnumerateDrives`, `IsLow` (l.50–76) | Done | ran (Home cards show C:/D: correctly, `dark-home.png`) |
| Rescue banner | `src/ui/app_window.cpp` banner widget | Done | ran (forced with a high `LowMB` in a test INI, `dark-rescue-banner-insights.png`). Bug: with D: also "low" under that test INI it reads "No other drive", which is correct for the test but untested for the real case. |
| Reserve file create/release | `rescue.cpp` `CreateReserve` l.130–160 (refuses when the result would be low, l.137; `SetFileValidData` + zero-fill fallback), `ReleaseReserve` | Partial | code. **NOT VERIFIED** at runtime (deliberately not created on this PC). Re-create after release is not wired into the UI. |
| Work volume | `rescue.cpp` `PickWorkVolume` l.82; `app_window.cpp` l.755 | Partial | ran (`PickWorkVolume` → D:). No UI to override it (`RescueConfig.workVolumeOverride` exists but has no UI). |
| Zero-write settings | `rescue.cpp` l.230 (`m_suppressed` when the settings drive is low), l.242 the only `WritePrivateProfileStringW` | Done | code + ran (banner screenshot status bar: "Settings kept in memory (drive is full)") |
| Snapshot writes | `insights.cpp:665` (`CreateFileW GENERIC_WRITE`); `app_window.cpp:681–699` `ChooseSnapshotDir` refuses low drives | Done | code |
| Memory-budget folding | `scanner.cpp` `ScanFolder` (aggregate `[N small files]` node) | Done | code. Runtime **NOT VERIFIED** (budget never hit in the benchmarks). |
| Low-priority scan in Rescue | `scanner.cpp` `THREAD_MODE_BACKGROUND_BEGIN` | Done | code |
| Auto-open in Rescue on launch | banner appears on every view when a drive is low | Partial | ran. There is no separate "Rescue screen"; it's a banner only. |
| MFT fast path | – | **Missing** | – |
| Portable mode | `rescue.cpp` Settings (INI next to exe / `-portable.exe`) | Done | code + `package.ps1` writes `SpaceRescue.ini` into the zip |

### Safe actions (F1/F2)
| Feature | Files | Status | Verified |
|---|---|---|---|
| Recycle (+`FOF_WANTNUKEWARNING`) | `actions.cpp:367–380` | Done | ran (engine test) |
| Recycle undo via `$I`/`$R` rename | `actions.cpp:201–276` | Done | ran (engine test, scratch file) |
| Quarantine (copy → verify → delete) + undo | `actions.cpp:343–358` + copy/verify helpers | Partial | ran (engine test). **Bug:** `Apply` doesn't check the work volume differs from the source volume. The engine test quarantined C:→C:, which frees nothing; the UI passes `PickWorkVolume(exclude source)`, so only direct callers are at risk. |
| Compress / decompress undo | `actions.cpp:148–153` `FSCTL_SET_COMPRESSION` | Done | ran (engine test) |
| Permanent delete (confirm) | `actions.cpp`, `review_dialog.cpp` | Done | ran (engine test); confirm dialog code only |
| Undo log | `actions.cpp:358` `undo-log.txt` on the work volume, else memory | Done | ran (log written in engine test) |
| Undo toast | `app_window.cpp` toast widget | Partial | code only. Undo triggers a full rescan rather than reattaching nodes. |

### F2 Insights
| Feature | Files | Status | Verified |
|---|---|---|---|
| Smart categories (downloads, installers, dev caches, temp, browser caches, Windows Update, videos, Recycle Bin) | `src/insights/insights.cpp` `Analyze` | Done | ran (`dark-insights-*.png`, `dark-review-temp.png`) |
| Duplicates | `insights.cpp` `FindDuplicates` (size → partial FNV → full) | Done | ran (engine test: 107 MB in Downloads). No progress callback. |
| Stale files 6/12/24 mo + signal shown | `insights.cpp:541–574` `LastAccessUpdatesEnabled` | Done | ran: on this PC returns **enabled** (`0x80000002`) with the reason text. Earlier engine claim "key absent" was wrong. |
| Top hogs (folders + files with explanation) | `insights.cpp` | Done | ran (`dark-insights.png`, `-3.png`). Weak explanations, e.g. "Mostly files without an extension" for `AppData\Local`; dates like "46 years ago" for 1980-stamped files. |
| Growth since last scan | `insights.cpp` `SaveSnapshot`/load | Partial | ran (snapshot reload). A non-empty growth list was never observed. |
| Reclaimable estimate | `insights.cpp` | Done | ran (24.7 GB for the profile scan) |

### F3 Disk Internals
| Feature | Files | Status | Verified |
|---|---|---|---|
| Volume: FS, cluster size, clusters | `src/internals/internals.cpp` `QueryVolume` | Done | ran (`dark-internals.png`: 4 KB clusters, 76,496,127 total) |
| MFT stats | `QueryVolume` `FSCTL_GET_NTFS_VOLUME_DATA` | Partial | ran unelevated: shows "Run as administrator". Elevated values **NOT VERIFIED** vs `fsutil fsinfo ntfsinfo`. |
| File extents + strip | `QueryFile` `FSCTL_GET_RETRIEVAL_POINTERS`; `internals_page.cpp` | Done | ran (6 GB ISO: 24 merged fragments = `fsutil` runs merged; `dark-internals-file.png`) |
| Slack (file + volume estimate) | `QueryFile`, `EstimateSlack` | Done | ran (1.7 GB across 1.2M files) |
| Most fragmented files | `MostFragmented` | Done | ran (713 fragments on a gradle cache file) |
| Cluster map visual | – | **Missing** | – |

### F4 Explorer integration / F5 Packaging
| Feature | Files | Status | Verified |
|---|---|---|---|
| Context menu: folder / background (`%V`) / drive | `src/rescue/setup.cpp:140–160` (HKCU `Directory`, `Directory\Background`, `Drive`) | Done (code) | **NOT VERIFIED**: never registered on this PC |
| Path argument starts scan | `main.cpp`, `setup.cpp` `HandleCommandLine` | Done | ran (`SpaceRescue.exe <path>` used for every screenshot) |
| Install / uninstall | `setup.cpp` `Install`/`Uninstall`, Apps & features keys l.123–128, self-delete via `cmd /c ping … & del` l.214–222 | Done (code) | **NOT VERIFIED** |
| Packaging | `package.ps1` (zip + IExpress) | Done | ran (both files produced, 653 KB / 704 KB) |
| Windows 11 top-level menu (IExplorerCommand) | – | **Missing** (stretch) | – |

### UI screens
| Screen | Files | Status | Verified |
|---|---|---|---|
| Home (drive cards, choose folder, scan again) | `app_window.cpp` | Done | `dark-home.png` |
| Scanning (live counts, path, cancel) | `app_window.cpp` | Done | `dark-scanning.png`, `dark-cancel-test.png` |
| Insights (ring card, hogs, categories, stale, growth, duplicates) | `src/ui/insights_page.cpp` | Done | `dark-insights*.png` |
| Review dialog (checkbox list, actions, confirm) | `src/ui/review_dialog.cpp` | Done | `dark-review-temp.png` |
| Folders (lazy TreeView + size bars, context menu) | `src/ui/folders_page.cpp` | Done | `dark-folders.png` |
| Disk Internals (volume, single file, fragmented list) | `src/ui/internals_page.cpp` | Done | `dark-internals*.png` |
| Status bar (counts, time, peak memory, tree memory, settings location) | `app_window.cpp` | Done | screenshots |
| Treemap / sunburst / flame graph | – | **Missing** | – |
| Settings screen (thresholds, reserve size, work volume, theme) | – | **Missing** | – |
| Dark mode | `src/ui/theme.cpp` (follows `AppsUseLightTheme`) | Partial | dark ran. List-view headers and popup menus stay light/unstyled. |
| Light mode | same | Partial | **NOT VERIFIED**: no in-app override (`theme.cpp:35–40` reads only the registry) and the system theme was not changed |
| DPI (PerMonitorV2, `WM_DPICHANGED`) | `res/app.manifest`, `theme.cpp` | Partial | ran at 125% only; 150/200% **NOT VERIFIED** |
| Keyboard (Ctrl+1/2/3, Ctrl+Tab, Ctrl+O, F5, Tab) | `app_window.cpp`, `widgets.cpp` | Done (code) | **NOT VERIFIED** interactively |

---

## 3. Scan performance

### How `src/core/scanner.cpp` enumerates
- **One background thread** (`CreateThread` in `Scanner::Start`), with Wow64 FS redirection disabled and optional `THREAD_MODE_BACKGROUND_BEGIN` (low I/O and CPU priority).
- **Depth-first walk with an explicit stack** of `(node, std::wstring path)` pairs (`Scanner::Run`).
- **Per folder:** `FindFirstFileExW(\\?\path\*, FindExInfoStandard, …, FIND_FIRST_EX_LARGE_FETCH)` + `FindNextFileW` (`ScanFolder`). `FindExInfoStandard` also asks the FS for the 8.3 short name. A progress SRW lock is taken twice per folder.
- **Per file:**
  - `wcslen(name)`.
  - **`JoinPath(path, name)`, a heap-allocated full-path string for every file**, built only to pass to `AllocatedSize()`. That function needs it only for compressed or sparse files.
  - `ScanTree::NewNode` (64-byte node in 1 MB chunks) + `AddName` (copy into a 1 MB-chunk name arena).
  - Optionally `MemoryUsed()` when a memory budget is set.
- **Per sub-folder:** another `JoinPath` string pushed on the stack.
- **Afterwards:** one reverse pass rolls up size, allocated size, file count and max timestamps (children always have larger indexes than parents).
- **Memory:** about 64 B per node + name; 2.08M nodes → 254 MB of tree, 263 MB peak working set.

### How upstream does it (`upstream/windirstat/`)
- **FinderBasic** (`FinderBasic.cpp`): `NtOpenFile(FILE_LIST_DIRECTORY, FILE_OPEN_FOR_BACKUP_INTENT)` + `NtQueryDirectoryFile` with `FileIdFullDirectoryInformation` (falls back to `FileFullDirectoryInformation`). Uses a **4 MiB `thread_local` buffer** (64 KiB for remote volumes). No Win32 `FindFirstFile` layer, and no short names.
- **FinderNtfs** (`FinderNtfs.cpp`, `LoadRoot` ~l.151): opens `\\.\X:` (`FILE_FLAG_NO_BUFFERING|OVERLAPPED`), `FSCTL_GET_NTFS_VOLUME_DATA`, opens `X:\$MFT::$DATA`, `FSCTL_GET_RETRIEVAL_POINTERS` for the MFT runs. It then reads the MFT sequentially in 4 MiB aligned chunks, in parallel (`std::execution::par`), applies update-sequence fixups, and parses `$STANDARD_INFORMATION`, `$FILE_NAME`, `$DATA` and reparse attributes. **Needs admin.** If the volume open fails, it falls back to FinderBasic.
- **Selection** (`Item.cpp` ~1309–1352): MFT is used only for whole-drive items when `UseFastScanEngine` (default on) and the MFT loaded. Folder scans always use FinderBasic.
- **Threads:** `BlockingQueue<CItem*>` (LIFO deque + `std::mutex`/`condition_variable` + `std::jthread`). Each volume gets `ScanningThreads` workers (**default 4**, range 1–16; remote ≤4, MTP 1). Each worker runs `CItem::ScanItems`: it pops a folder, enumerates it, and pushes child folders. The queue self-terminates when all workers are idle and it is empty.

### Benchmarks (this machine, unelevated, prototype `bench2` in scratchpad, now deleted)
All runs on the same files. Because the first run cold-reads the disk, **cold vs warm cache matters most**.

**C:\Windows (349,198 files, 193,155 folders, 42.1 GB), warm cache**
| Variant | Time | Peak RAM |
|---|---|---|
| Current scanner (`cur`), run 1 / run 2 | 25.4 s / 21.3 s | 111 MB |
| Enumeration only, `FindExInfoStandard`, 1 thread | 18.7 s | 17 MB |
| (a) `FindExInfoBasic` + `LARGE_FETCH`, 1 thread | 15.8 s | 17 MB |
| (b) same, 4 threads | 7.7 s | 24 MB |
| (b) same, 8 threads | 6.1 s | 29 MB |
| (b) same, 16 threads | 5.5 s | 38 MB |

**C:\ (1.67M files, 402K folders, 215.6 GB logical)**
| Variant | Time | Peak RAM |
|---|---|---|
| Current scanner, **cold cache** (first run of the session, PROGRESS.md baseline) | **289 s** | 264 MB |
| Current scanner, warm cache | 57.9 s | 263 MB |
| (a) `FindExInfoBasic`, 1 thread, warm | 34.5 s | 17 MB |
| (b) `FindExInfoBasic`, 8 threads, warm | **16.1 s** | 29 MB |
| (c) MFT reading | **NOT VERIFIED**: the shell is not elevated, so `\\.\C:` cannot be opened for raw reads | – |

(The enumeration-only prototypes don't build a tree, so their RAM excludes the 254 MB node store.)

### Top 3 causes of slowness (ranked)
1. **Single-threaded enumeration.** On the same work, 8 threads are 2.1× faster on C:\ (34.5 s → 16.1 s) and 2.6× faster on C:\Windows (15.8 s → 6.1 s). NVMe and the filesystem cache serve parallel directory queries well. Upstream defaults to 4 threads.
2. **Cold-cache directory reads (I/O bound).** The first scan took 289 s and a warm re-scan 58 s (5×), so on a cold disk most time is waiting on random metadata reads. Only the MFT path (sequential reads of `$MFT`) fixes this structurally. That needs admin; NOT VERIFIED here.
3. **Per-file overhead in our scanner:**
   - A heap `JoinPath` string per file (`scanner.cpp`, `ScanFolder` → `AllocatedSize(JoinPath(path, name), fd)`), even though only compressed or sparse files need the path.
   - `FindExInfoStandard` instead of `FindExInfoBasic` (+18% on C:\Windows: 18.7 s vs 15.8 s).
   - Node and name copies.
   Together, our single-threaded scanner takes 57.9 s against 34.5 s for bare single-threaded enumeration on C:\ (+68%).

---

## 4. Upstream WinDirStat study

### 4.1 Architecture and intent
WinDirStat is a Win32 disk-usage analyzer: it scans drives or folders and shows a sortable tree, a file-type table and a graphical view (treemap, flame graph or sunburst), with cleanup, duplicate, search, permission and recovery tools on top. Built with C++23 (`windirstat.vcxproj:74`).
- **No MFC.** A hand-written replacement framework (~5.6k lines) keeps the MFC names:
  - `UiWindow.h:11` defines `CWnd`, and `UiTypes.h` the `CDC`/`CRect`/GDI wrappers.
  - `UiControls.h` holds the common controls, and `UiPanes.h` the splitter, toolbar, statusbar, tab and property sheet.
  - `UiApplication.h` defines `CWinApp`, `CFrameWnd` and `CDialog`.
- **Message routing:** `MessageTarget<Derived, Base>` (`UiTypes.h:286`) builds a chained `RouteTable` from a static `Routes()`. Handlers are declared with `Route::Window/Command/Update/Control<&Handler>` (`UiWindow.h:344-411`). This replaces MFC message maps.
- **App and frame:**
  - `CDirStatApp` (`WinDirStat.h:23`) handles portable mode, icons and the reparse policy.
  - `CMainFrame` (`MainFrame.h:151`; `OnCreateClient` `MainFrame.cpp:593`) builds a splitter with `CVisualizationPane` and a sub-splitter holding `CFileTabbedView` + `CExtensionView`. Layouts are permutable (`MainFrame.Commands.cpp:1194`).
- **Model:** `CWinDirStatModel` (`WinDirStatModel.h:71`) owns the `CItem` tree, the extension data and the scan. Panes get `OnUpdate(sender, MODEL_CHANGE, item)`. Actions are in `WinDirStatModel.Actions.cpp`.
- **Item:** `CItem` (`Item.h:114`). A variable-size allocation with an inline name. Types: file, folder, drive, My Computer, free space, unknown, hardlinks, remote/MTP.
- **Settings:** `COptions` (`Options.h:148`) is a set of static `Setting<T>`, stored as INI in portable mode, otherwise in the registry via ATL `CRegKey` (`Property.cpp:43-96`).
- **Localization:** `Localization.cpp` reads 25 `res/langs/lang_*.txt` files (looked up as `IDS_*` keys).
- **Settings pages:** `Pages/Page*.cpp`.

### 4.2 Views, tabs and panes
| Pane | Shows | How reached | Files |
|---|---|---|---|
| File tree (default tab) | Hierarchical virtual list: size, %, items, dates, attributes | Tab 1 | `Views/FileTreeView`, `Controls/FileTreeControl`, `TreeListControl`, `WdsListControl`, `Item.cpp` |
| Treemap | Cushion or flat rectangles, zoom, extension labels, folder frames | Visualization pane | `Views/TreeMapView`, `Controls/TreeMap`, `TreeMapLayout`, `HilbertMooreTreeMap` |
| Flame graph | Icicle chart, one row per depth, breadcrumbs | View menu | `Views/FlameGraphView`, `Controls/FlameGraph` |
| Sunburst | Radial rings (GDI+) | View menu | `Views/SunburstView`, `Controls/Sunburst` |
| Visualization pane | Container switching the 3 graphs (`VisualizationPane.cpp:13`) | View > Show visualization | `Views/VisualizationPane`, `GraphView` |
| Extensions | Per-extension color, description, bytes, %, files | Right pane | `Views/ExtensionView`, `Controls/ExtensionListControl` |
| Duplicates | Groups of identical files | Tab | `FileDupeControl`, `ItemDupe` |
| Top files | N largest files | Tab | `FileTopControl`, `ItemTop` |
| Search | Name glob/regex results + size filters | Edit > Search → `SearchDlg` → tab | `Dialogs/SearchDlg`, `FileSearchControl`, `ItemSearch` |
| Permissions | DACL / inheritance per item | Tools | `FilePermsControl`, `ItemPerm`, `PagePermissions` |
| File watcher | Live change log (`ReadDirectoryChangesW` on `jthread`s, `FileWatcherControl.cpp:62-113`) | Tools | `FileWatcherControl` |
| Storage analytics | Age tiers + cloud-cost estimate | Tools | `Views/StorageAnalyticsView` |
| Recovery | Undelete on NTFS, FAT and exFAT | Tools | `Dialogs/RecoveryDlg`, `Recovery*.cpp` |
| Select drives | Drives, folder, cloud | File > Select | `Dialogs/SelectDrivesDlg` |

### 4.3 Treemap: computation and drawing
- **Layouts** (`TreeMapLayout.h:12`):
  - Rows (default, KDirStat-style).
  - Squarified (worst-aspect-ratio rows, `TreeMapLayout.cpp:101-143`).
  - Hilbert and Moore space-filling curves (`HilbertMooreTreeMap.cpp`).

  Children are sorted by size descending. Layout runs on an explicit stack (`TreeMap.cpp` ~728–800), with depth capped by `TreeMapMaxDepth` (default 6).
- **Cushion shading** (SequoiaView method, `DrawCushion` `TreeMap.cpp:995`):
  - Each nesting level adds a parabolic ridge (`AddRidge`, l.1059) to a surface `s[0..3]`. Ridge height = `height` × `scaleFactor`^depth.
  - Light vector (lx, ly, 10), normalized (l.432–439).
  - Per pixel: normal = (−(2·s0·(x+.5)+s2), −(2·s1·(y+.5)+s3), 1). `cosa = (n·L)/|n|`, and brightness = `Ia + max(Is·cosa, 0)` with Ia = ambient and Is = 1−Ia.
  - The RGB value is the base color × brightness, with overflow redistributed (`NormalizeColor`).
- **Defaults** (`TreeMap.h:326-340`): brightness 0.88, height 0.38, scale 0.91, ambient 0.13, light (−1,−1). Presets: Classic, Calm, Flat, Pastel, HighContrast, Custom.
- **Pixels:** written directly into a 32-bit top-down DIBSection when available, else a `COLORREF` vector + `SetDIBitsToDevice` (`TreeMap.cpp:189, 699-714`). Rows are parallelized with `std::execution::par` for large cushions only. Labels and frames are drawn with GDI after. A 16 px hit-test grid handles mouse picking.
- **Colors:**
  - Extensions are ranked by total bytes (`WinDirStatModel.cpp:434-495`) and mapped onto 18 `DefaultCushionColors` normalized to brightness 0.6 (`TreeMap.h:344`, `MakeBrightColor` `TreeMap.cpp:236`). Ranks past 18 share the last color.
  - Special items: free space is gray, unknown is yellow, hardlinks are tan (`Item.Extended.cpp:1111`).
- **Options:** `Pages/PageTreeMap.cpp`, with live preview and an `XYSlider` for the light source.

### 4.4 Menus, toolbar and cleanup actions (`windirstat.rc:94-360`)
- **File:** Select, Select cloud, Load/Save results, Save dupes/perms, Refresh all/selected, Run elevated, Exit.
- **Edit:** Copy, Search, Exclude, Compute hash.
- **Cleanup:**
  - Open and Explorer-select, cmd, PowerShell, Properties.
  - Delete, Delete to bin, Empty bin, Remove empty, Empty folder, Move to.
  - Compress submenu (None, LZNT1, XPRESS 4K/8K/16K, LZX), Sparsify, Optimize VHD, Create hardlink, Remove Mark-of-the-Web.
  - Disable hibernate, Disk Cleanup, Storage Sense, Programs and Features.
  - Remove roaming/local profiles, Remove shadow copies.
  - DISM submenu (`/AnalyzeComponentStore`, `/StartComponentCleanup [/ResetBase]`).
  - **User-defined cleanups** (`Options.h:88`, `PageCleanups`).
- **View / Treemap:** styles, flame graph, sunburst, presets, logical vs physical size, folder frames, zoom, select parent.
- **Tools:** Watcher, Permissions, Storage analytics, Recovery, Set dates, Shadow copy, Defrag, Chkdsk.
- **Options:** Show free space and unknown, file types, toolbar, font size, status bar, Settings.
- **Toolbar** (`MainFrame.Commands.cpp` ~705–745): select, resume/suspend/stop, refresh, search, filter, open, copy, console, properties, delete to bin, delete, zoom, layout, settings, help.

### 4.5 What we can port into the MinGW build
| Component | Verdict | What's needed |
|---|---|---|
| Treemap cushion renderer (`TreeMap.cpp` `DrawCushion`/`AddRidge`, `CColorSpace`) | **Portable nearly as-is** | Plain float math + DIB writes. Drop `std::execution::par`, `views::iota/enumerate`, `to_underlying` and designated initializers. |
| Treemap layout, Rows/Squarified | Needs adapting (small) | `std::span` → pointer + count, ranges → `<algorithm>` |
| Hilbert/Moore layout | Needs adapting | 14 `span` + 10 ranges uses, integer math |
| Top files | Portable nearly as-is | `ranges::push_heap` → `std::push_heap` |
| Extension stats + color by rank | Needs adapting (small) | Replace `std::atomic` + cache-line padding |
| Duplicate finder (3-level hash, xxHash3) | Needs adapting | `xxhash.h` is plain C and compiles. Replace `mutex/jthread`. `bcrypt.h` is **missing** in MinGW.org. |
| FinderNtfs MFT parser | Needs adapting | FSCTL structs exist in our headers (verified by an earlier probe). Replace `execution::par` with `CreateThread`, plus `span`/`bit_cast`. **Needs admin at runtime.** |
| Flame graph | Needs adapting | Pure GDI. Replace `requires std::invocable` (`FlameGraph.h:49,99`) and ranges. |
| Recovery parsers (~2.3k lines) | Needs adapting | Byte parsing is portable. ~43 `span<const BYTE>` uses and `std::format` (`RecoveryShared.cpp`). |
| Dark mode (`DarkMode.cpp`) | Needs adapting (small) | uxtheme ordinals 133/135 through `GetProcAddress` are plain Win32 |
| Sunburst | Partly blocked | 101 GDI+ uses. MinGW.org GDI+ headers are incomplete (NOT VERIFIED in detail). |
| `Ui*` framework, dialogs, panes | **Blocked / rewrite** | `span`, `views::enumerate`, `requires std::derived_from`, `optional`. Our own `src/ui` replaces it. |
| Anything using ATL (`CComPtr`, `CRegKey`, `CComBSTR`: shell, WMI, MTP, shadow copies, registry settings) | **Blocked** | ATL is absent. A ~50-line `ComPtr` template would unblock most of it. |

**Net:** the algorithmic cores are small and independent: treemap layout + cushion, MFT parser, top files, extension colors, duplicate grouping and flame graph. Each needs a mechanical C++14 backport (`span` → pointer + length, ranges → `<algorithm>`, `format` → `swprintf`, `jthread/mutex` → Win32 threads). The framework, dialogs and ATL-based features do not port.

---

## 5. UI audit

### Screenshots (`docs/screens/`, captured off-screen with `PrintWindow`, dark theme, 125% DPI)
| File | Screen |
|---|---|
| `dark-home.png` | Home: drive cards, Choose folder, Scan again |
| `dark-scanning.png` | Scanning C:\Users\imved (live counts, current path, Cancel) |
| `dark-cancel-test.png` | State after cancelling a scan |
| `dark-insights.png`, `dark-insights-top.png`, `-2` … `-6.png` | Insights: summary card, top folders/files, categories, stale, growth |
| `dark-rescue-banner-insights.png` | Rescue banner (forced with a high `LowMB` in a test INI) |
| `dark-review-temp.png` | Review dialog for "Temporary files" |
| `dark-folders.png` | Folders tree with size bars |
| `dark-internals.png`, `dark-internals-file.png` | Disk Internals: volume panel, single-file extents, most-fragmented list |

**Light theme: NOT VERIFIED.** The app has no theme override, and changing the Windows theme was out of scope. From the code, light mode swaps the token set in `theme.cpp`, uses the accent #0067C0, and has the same layout.

### Layout of each screen (from the screenshots)
- **Home:** title "Where is your space going?", two buttons, then one card per drive (name, FS, chips, a thin usage bar, used/free text, Scan). The lower 40% of the window is empty.
- **Insights:** one long vertical scroll page.
  - A summary card (donut ring + "X safely reclaimable"), then card lists: Largest folders (10), Largest files (10), Smart categories, Stale files, Growth, Duplicates.
  - Every row has the same two buttons (Show / Review…).
- **Folders:** a plain TreeView with "name — size (%)" text and a right-aligned bar; a detail strip at the bottom.
- **Disk Internals:** a two-column label/value list with long explanatory paragraphs, a "single file" section, and a ListView of fragmented files.

### UI/UX problems found
1. **No visualization at all.** No treemap, sunburst or flame graph; the only graphic is one donut ring. That's the core of WinDirStat and the biggest gap.
2. **Wall of identical cards.** Insights is one undifferentiated scroll (≈6 screens). Hogs, categories and stale files all use the same row style with the same two buttons, so there is no visual hierarchy and no single primary action.
3. **The summary ring mixes scopes.** When a folder is scanned, the ring shows **drive** usage (82%) next to the **folder's** reclaimable number (`dark-insights.png`, `dark-rescue-banner-insights.png`). It's confusing.
4. **Weak explanations and bad dates:**
   - "Mostly files without an extension" for `AppData\Local` (80 GB).
   - "last changed **46 years ago**" for files with 1980 DOS-epoch timestamps (`dark-insights-3.png`).
   - Both come from the `insights.cpp` explanation/age helpers.
5. **Dark-mode gaps:** ListView headers render dark-on-dark and are nearly unreadable (`dark-internals.png`, `dark-review-temp.png`). Popup menus and checkboxes stay in the light system style.
6. **Review dialog:**
   - Category items appear as one folder row ("Temp\ 147 MB") with no way to drill into files.
   - Buttons are uniform grey: the destructive "Delete permanently…" looks the same as "Select all".
   - The window title bar is light while its content is dark.
7. **Folders tab:** the text-only TreeView has no columns (files, modified) and no file-type colors, and it isn't linked to any visualization.
8. **Disk Internals is text-heavy:** long paragraphs, no cluster-map graphic, and the extent strip only appears after picking a file.
9. **Blocking or unprogressed work:**
   - `Analyze` cannot be cancelled.
   - Duplicates show elapsed time only (no progress).
   - Undo triggers a full rescan.
   - The full C: scan takes 58–289 s with no partial results (see §3).
10. **Empty space and density:** Home leaves ~40% blank; Insights wastes width (content column ~1250 px with large margins) while the review list is cramped.
11. **No Settings screen** for thresholds, reserve size, work volume, threads or theme.

---

## 6. Notifications feasibility (low-space warnings at 50/20/10/5/3/2/1 GB and 512 MB)

**Mechanism that works with our compiler (verified by a compile probe):**
- **Tray icon + balloon** via `Shell_NotifyIconW` with `NIF_INFO` (`NOTIFYICONDATAW` is 956 bytes, and `NOTIFYICON_VERSION_4` is defined in MinGW.org headers). On Windows 10/11, balloons from `NIF_INFO` are shown as native toast-style notifications in the Action Center, so no WinRT is needed.
- **Real WinRT toasts** (`Windows.UI.Notifications`) are **not possible** with this toolchain: `roapi.h`, `wrl.h` and the WinRT headers are absent.
- **Startup:** `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\SpaceRescue = "<exe>" /tray`. Per-user, no admin. `setup.cpp` already writes HKCU keys, so the same pattern applies.

**Design sketch:**
- A `/tray` mode: no main window, a hidden message-only window, the tray icon, and a `SetTimer` poll.
- **Thresholds:** 50, 20, 10, 5, 3, 2, 1 GB and 512 MB. Keep the last threshold crossed per drive and notify only when crossing **downwards**, with hysteresis (e.g. re-arm only after free space rises 10% above the threshold). The 1 GB and 512 MB levels match the Rescue Mode trigger.
- Clicking the balloon opens the main window on that drive.
- **Cheaper than polling:** besides a timer, use `WM_DEVICECHANGE` for new drives and optionally `FindFirstChangeNotification`. That one isn't free-space aware, though, so timer polling stays the primary signal.

**Cost (measured):**
- `GetDiskFreeSpaceExW` on 2 drives costs **≈30 µs per poll** (10,000 iterations). Polling every 30–60 s is effectively free; even every 5 s costs well under 0.01% CPU.
- An idle Win32 process's working set: a minimal console probe is **5.3 MB**. A tray-only instance of our static exe should stay around 5–8 MB if it doesn't create the main window or load fonts and canvases (NOT VERIFIED). By contrast, a SpaceRescue window holding a full C:\ scan sits at ~270 MB, so the tray watcher must be a **separate lightweight mode/process** that never keeps a scan tree.
- No disk writes are needed; the watcher's state (last notified threshold) stays in memory.

---

## 7. Packaging and gaps

### Current outputs (`package.ps1`, verified by running it)
- **`build\dist\SpaceRescue-portable.zip` (653 KB):** `SpaceRescue.exe` + an empty `SpaceRescue.ini` (which switches on portable mode) + `LICENSE.md`. It runs from a USB stick with no installation and has no DLL dependencies (static link).
- **`build\dist\SpaceRescue-Setup-x86.exe` (704 KB):** an IExpress self-extractor that runs `SpaceRescue.exe /install`. Per `setup.cpp` it:
  - copies the exe to `%LOCALAPPDATA%\Programs\SpaceRescue\`;
  - creates a Start-menu shortcut;
  - registers the HKCU context menu (folder, background, drive);
  - writes the HKCU Apps & features entry (DisplayName, DisplayIcon, UninstallString `/uninstall`, Publisher, InstallLocation, EstimatedSize, NoModify/NoRepair).

  `/uninstall` removes all of that plus the reserve file and settings, then deletes its own exe through a delayed hidden `cmd /c ping … & del` (`setup.cpp:214–222`).

### What they don't do
- **x86 only** (32-bit GCC). No x64 or ARM64 build.
- **Per-user only.** No per-machine install and no MSI (WiX isn't available without admin, see §1).
- **No setup UI or options:** no install location choice, no checkboxes for context menu, reserve file or start with Windows. `/install /reserve` exists only as a switch.
- **Unsigned:** SmartScreen will warn on the IExpress exe.
- **No upgrade logic** beyond overwriting the copied exe. Upgrade-over-previous is **NOT VERIFIED**.
- The IExpress extractor briefly unpacks to `%TEMP%` on the system drive, which could fail on a 0-byte C:. The portable zip run from another drive is the Rescue-safe path.

### Still unverified (from PROGRESS.md and this audit)
- Elevated MFT numbers vs `fsutil fsinfo ntfsinfo C:`.
- Reserve file: create, release, re-create, removal on uninstall.
- Install, upgrade and uninstall; context menu on folder, background and drive.
- The 0-byte VHDX test (needs admin for diskpart).
- Light mode, 150% and 200% DPI, keyboard-only navigation.
- Memory-budget folding at runtime; a non-empty growth list.

### Bugs and risky code (ranked)
1. **Quarantine target not validated** (`actions.cpp:343–358`): if the work volume equals the source volume, files are "quarantined" onto the same drive, so nothing is freed and the full drive gets extra writes. Fix: refuse when `VolumeOf(path) == workVolume`.
2. **Recycle on huge items:** `FOF_WANTNUKEWARNING` now asks first, but the prompt comes from a worker thread with `FOF_SILENT|FOF_NOERRORUI` (`actions.cpp:374–377`). Whether the warning appears correctly and modally is **NOT VERIFIED**.
3. **Tree mutation vs background readers:** `ScanTree::Detach` (after an action) runs on the UI thread while Insights duplicates and Internals fragmentation workers may still be reading the tree. `StopBackground()` is called on rescans (`app_window.cpp:796, 895, 1133`), but it isn't clear it's called before `Detach` after an action. **Possible use-after-unlink or race.**
4. **Undo = full rescan:** correct but slow (58–289 s on C:).
5. **32-bit limits:** a 2–4 GB address space caps scans at a few million nodes. The 264 MB peak is fine on C:, but a 10M-file drive would hit ~1.3 GB of tree plus fragmentation. `FindDuplicates` reads files in full on a 32-bit process (fine with streaming reads; buffer sizes **NOT VERIFIED**).
6. **Implausible dates:** 1980 or future timestamps feed both "stale" and "age" text ("46 years ago"). Stale detection would flag them as very old. Clamp and label as "date unknown".
7. **Self-delete on uninstall** relies on `ping` timing (3 s). A slow exit leaves the exe behind (harmless, but untidy).
8. **IExpress setup extracts to the system drive's `%TEMP%`.** It can't install on a full C:.
9. The engine agent's tests left **`C:\SpaceRescue-Quarantine\`** on this PC. Waiting for the user's OK to delete it.
10. **No crashes observed** in any run this session (≈15 launches, scans up to 1.2M files).

---

## 8. Summary

Ranked, most important first:
1. **Multithreaded scanner** (work queue, 8 workers, `FindExInfoBasic`, no per-file path strings). Measured 58 s → ~16 s on C: warm (§3).
2. **Show results before the scan ends:** a quick-win pre-scan of known hotspots plus live top-level sizes, so a user with 512 MB free gets an answer in seconds.
3. **Toolchain → llvm-mingw x64, C++23** (installed per-user, no admin). It unlocks porting upstream code (span, ranges, format, jthread) and removes the 32-bit memory ceiling.
4. **Port upstream's treemap** (layout + cushion shader) and add a sunburst, both linked to the folder list. This is the biggest UX gap (§5 #1).
5. **MFT fast path** (port `FinderNtfs`) with a "Fast scan (needs admin)" relaunch. It's the only fix for cold-cache scans (289 s).
6. **Redesign the Insights/Overview hierarchy:** one hero visual, one primary action, segmented cleanup cards instead of a wall of identical rows. Fix dark-mode headers and menus.
7. **Fix the safety bugs:** quarantine same-volume check, `Detach` vs background-worker race, implausible-date handling.
8. **Tray low-space notifications** (`/tray`, `Shell_NotifyIconW` + `NIF_INFO`, HKCU Run). Verified feasible at ≈30 µs per poll (§6).
9. **Protected/system file awareness** (pagefile, hiberfil, WinSxS, etc.) with safe Windows tools instead of Delete, and a **Settings screen**.
10. **Verification pass + x64 installer with options:** VHDX 0-byte test, reserve file, install/upgrade/uninstall, context menu, elevated MFT vs `fsutil`, light mode, 150/200% DPI. Then the docs (README, CONTRIBUTIONS, USER_GUIDE, DEMO_SCRIPT).
