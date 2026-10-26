# Viva Demo Script (5 minutes)

**Setup before the viva:**
- `build.ps1 -Release`.
- Have `SpaceRescue.exe /preview C:\` open on Overview.
- Have a second, **elevated** terminal ready in the repo folder.
- Optional: a 2 GB VHDX filled to 0 bytes (see §5).

| Time | Show | Say |
|---|---|---|
| 0:00 | Title + Overview | "When C: hits 0 bytes, Windows and most disk tools fail. SpaceRescue is built for that moment, on top of WinDirStat." |
| 0:30 | Overview sunburst, hover a slice | "Each ring is a folder level and colour is the file kind. Free space is the grey slice. This 1.7-million-file scan took ~11 s." |
| 1:00 | Terminal: `SpaceRescue.exe /bench C:` (elevated) | "OS angle 1, file-system metadata: the normal path does ~400K directory queries on 8 threads. The MFT path reads NTFS's file table sequentially, like WinDirStat's FinderNtfs." |
| 1:40 | Map > Treemap | "This cushion treemap is ported from WinDirStat: a squarified layout plus per-pixel Lambert shading of parabolic cushions. The whole drive renders in ~80 ms." |
| 2:10 | Disk internals | "OS angle 2, allocation: 4 KB clusters, slack space as internal fragmentation (1.7 GB wasted here), and this file's extents. It's stored in 58 pieces, which is non-contiguous allocation made visible." |
| 3:00 | Clean up > Windows temp > Review > quarantine > Undo (classic UI `SpaceRescue.exe` until the new screens are wired) | "Every destructive action is reversible. Quarantine copies to another drive, verifies the copy, then deletes. On a full drive the Recycle Bin frees nothing, so we say that." |
| 3:40 | System | "Windows files get a lock and the safe Windows tool, never Delete." |
| 4:10 | Rescue Mode (VHDX at 0 bytes, or a test INI with a high `LowMB`) | "Rescue Mode: no writes to the full drive, a memory cap, and the reserve file (the same idea as ext4 reserved blocks) released in one click." |
| 4:40 | Tray notification (`/tray`) | "A 1.6 MB tray watcher warns at 50…1 GB with hysteresis, so you never get here again." |
| 5:00 | README credits | "GPL-3.0, credits to WinDirStat. CONTRIBUTIONS.md lists exactly what we ported versus built." |

## Making a 0-byte test drive (admin)
```powershell
# In an elevated PowerShell:
$v = "$env:USERPROFILE\rescue-test.vhdx"
"create vdisk file=`"$v`" maximum=2048 type=expandable`nattach vdisk`ncreate partition primary`nformat fs=ntfs quick label=RESCUE`nassign letter=R" | diskpart
fsutil volume diskfree R:
fsutil file createnew R:\filler.bin ((Get-Volume R).SizeRemaining)   # fill to ~0 bytes
SpaceRescue.exe R:\                                                     # banner + Rescue Mode
# afterwards: "select vdisk file=`"$v`"`ndetach vdisk" | diskpart ; Remove-Item $v
```
