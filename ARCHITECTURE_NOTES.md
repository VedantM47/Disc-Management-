# Architecture Notes (upstream WinDirStat baseline)

Upstream: https://github.com/windirstat/windirstat @ `4c24e70` (master, version 2.10.0).
These notes map the parts of upstream that SpaceRescue builds on. Paths are relative to the repo root.

## Build

- Solution `windirstat.sln`, project `windirstat/windirstat.vcxproj`.
- **Toolset `v145` (Visual Studio 2026)**, C++23 (`stdcpp23`), Windows SDK 10.0, platforms Win32 / x64 / ARM64.
- **No MFC** (`UseOfMfc=false`). Upstream ships its own thin Win32 framework that keeps MFC-like names
  (`CFrameWnd`, `CWnd`, `CDC`, `CPropertySheet`, ...) in `UiFramework.*`, `UiWindow.h`, `UiControls.*`,
  `UiTypes.h`, `UiPanes.h`. Message maps are replaced by `MessageTarget<T, Base>` + a static `Routes()`
  table (`Route::Window<&OnX>(WM_X)`, `Route::Control<&OnY>(BN_CLICKED, id)`).
- `build.cmd` (repo root) builds all platforms via vswhere + msbuild, signs, then calls `setup/msi/build.cmd`.
- CI: `.github/workflows/*.yml` on `windows-latest`.

## Scanning engine

| File | Role |
|---|---|
| `Finder.h` | Abstract enumerator (`FindNext`, size physical/logical, `GetLastWriteTime`, reparse tag, index). |
| `FinderBasic.*` | Normal directory enumeration (works unelevated). |
| `FinderNtfs.*` | Raw MFT reader, used when elevated on NTFS. This is the "fast path" for F1. |
| `FinderMtp.*`, `FinderRemote.*`, `StorageSource.*`, `RemoteStorage.h` | Phones (MTP) and remote/cloud sources. Not relevant to us. |
| `Item.h/.cpp`, `Item.Extended.cpp` | `CItem`, one node per file/folder. |
| `BlockingQueue.h` | Work queue for multithreaded scanning (`CItem::ScanItems`). |

**`CItem` memory layout:** a variable-size allocation: `CItem` header → (containers only) `CHILDINFO`
→ inline name buffer. Fields: two atomic 64-bit sizes, 64-bit index, 64-bit last-change, 32-bit type/flag
bitmask, 16-bit attributes, 16-bit name length. Containers also hold `std::vector<CItem*>` children plus
atomic counters. This is already compact. F1's memory budget work should mainly add
**small-file folding** and peak-working-set reporting. We will not rewrite the node.

**Only the last *write* time is stored** (`m_lastChange`). Stale detection (F2) needs last-access too, so
it must either add a field (costs 8 bytes per file) or query access times lazily for candidate files only.
Lazy querying is preferred.

## Model and views

- `WinDirStatModel.*` (`CWinDirStatModel`): document. Owns the root, selection and actions
  (`WinDirStatModel.Actions.cpp`: delete / Recycle Bin / cleanups). Views get `OnUpdate(MODEL_CHANGE, item)`.
- `MainFrame.*`, `MainFrame.Commands.cpp`: frame, splitters, menus, command handlers.
- `Views/`: `FileTreeView`, `FileTabbedView` (tabs: tree, duplicates, top files, search, perms, watcher),
  `ExtensionView`, `TreeMapView`, `SunburstView`, `FlameGraphView`, `GraphView`, `VisualizationPane`,
  **`StorageAnalyticsView`** (groups files into age tiers by last-change and estimates cloud cost).
  StorageAnalyticsView is the closest upstream analogue to the Insights screen and a useful pattern for
  F2: one DFS over `CItem` and a GDI-painted dashboard inside a `CWinDirStatPane`.
- `Controls/`: list/tree controls, treemap, sunburst, and per-tab list controls (`FileDupeControl`, `FileTopControl`, ...).
- `ItemDupe.*`, `ItemTop.*`, `ItemSearch.*`, `ItemPerm.*`: data behind the tabs. The duplicate finder already exists.
- `Dialogs/`: About, progress, select-drives, search, recovery, message box.
- `Pages/`: settings pages. **`PageGeneral` already registers/unregisters the Explorer context menu**
  (`IsContextMenuRegistered`, `SetContextMenuRegistration`, HKCU or HKLM) and the portable toggle.
- `DarkMode.*`, `UiAppearance.cpp`: dark mode support, which already exists.

## Settings and portable mode

- `Options.*` + `Property.cpp`: settings backend that uses **either the registry (HKCU\Software\WinDirStat) or an
  INI** (`UseRegistry()` / `UseIni(path)`).
- Portable mode is on when `<exe>.ini` exists next to the exe, or when the exe name ends in `-portable.exe`
  (`CDirStatApp::InPortableMode / IsForcedPortableMode / SetPortableMode` in `WinDirStat.cpp`).
  F1 "portable mode" therefore already exists. We only need to make sure Rescue Mode never writes the
  INI to the volume being rescued.
- Command-line parsing: `CWinDirStatCommandLineInfo` in `WinDirStat.cpp` (~line 213). A path argument starts
  a scan. This is the entry point for F4.

## Localization

- `windirstat/res/langs/lang_<xx>.txt`, `IDS_KEY=value` lines (UTF-8), loaded through `Localization.*`.
  New strings go in `lang_en.txt` with `IDS_SR_*` keys.

## Unexpected upstream extras

- `Recovery*.cpp` + `RecoveryDlg`: deleted-file recovery for NTFS/FAT/exFAT that parses raw volume structures.
  This is useful reference code for F3 (MFT record parsing, run lists).

## Installer (F5)

- WiX v4+ (`dotnet tool install --global wix`): `setup/msi/WinDirStat.wxs`, `Defines.wxi`, `build.cmd`,
  `make-multilingual.ps1`. Per-machine MSI.
- Context menu in the MSI: `HKCR\Directory\shell\WinDirStat` and `HKCR\Drive\shell\WinDirStat`.
  **There is no `Directory\Background` entry.** F4 must add one in both the MSI and `PageGeneral`.
- Optional components: desktop shortcut, add to PATH. The reserve-file opt-in fits this same pattern as a
  checkbox property + custom action (run the exe with a `/CreateReserve` style switch).
- `setup/store/` (MSIX for the Store) and `setup/chocolatey/` are out of scope. Leave them untouched or drop them at rebrand.

## Branding touchpoints (Phase 1)

`windirstat/version.h` (`TEXT_PRODUCTNAME`, `STRING_COMPANY`, `STRING_EXENAME`, website, repo),
`version.rc`, `windirstat.rc`, `res/WinDirStat.ico`, `logos/`, `Dialogs/AboutDlg.*`, `IDS_ABOUT_*` strings,
`setup/msi/Defines.wxi` (product name, UpgradeCode: **must be a new GUID**), `WinDirStat.wxs` registry
key names, and `HKCU\Software\WinDirStat` (settings root in `WinDirStat.cpp`/`Options.cpp`).

## Tests

`tests/`: PowerShell UI-automation harness (`Test-WinDirStat.ps1`, `Run-Tests.cmd`), installer test
(`Test-Installer.ps1`) and native recovery tests (`RecoveryTests.vcxproj`). Reuse for the Phase 6 test pass.
