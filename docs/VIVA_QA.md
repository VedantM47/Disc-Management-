# Viva Q&A – SpaceRescue

Each answer points to where it lives in the code. Numbers are from `PROGRESS.md` and `STATUS_REPORT.md`, or were measured on the test laptop
(i5-1335U, 16 GB RAM, Windows 11, C: 292 GB NTFS, 1.68M files).

## File systems and storage

**Q: What is the MFT and how do you use it?**
NTFS keeps one record per file in the Master File Table, which plays the same role as the Unix inode table. `src/core/scanner_mft.cpp` `Scanner::ScanMft`
gets the MFT location with `FSCTL_GET_NTFS_VOLUME_DATA` and `FSCTL_GET_RETRIEVAL_POINTERS` on `$MFT`. It reads the MFT in 4 MB chunks, applies the
update-sequence fixups (`Fixup`), and parses `$STANDARD_INFORMATION`, `$FILE_NAME` and `$DATA`.

**Q: How do you read directory entries without the MFT?**
`src/core/scanner.cpp` `Enumerate` opens the folder with `FILE_LIST_DIRECTORY` and calls `NtQueryDirectoryFile` (`FileFullDirectoryInformation`) into a
64 KiB per-thread buffer. Each entry gives the name, size, allocation size, timestamps and attributes.

**Q: Contiguous vs non-contiguous allocation – where do you show it?**
`src/internals/internals.cpp` `QueryFile` calls `FSCTL_GET_RETRIEVAL_POINTERS`, which returns extents (VCN→LCN runs). Runs where the next LCN equals
the end of the previous one are merged. One run means contiguous; many runs means fragmented. Disk Internals draws the strip: an 8 GB image in 58 fragments.

**Q: Free-space management?**
`QueryVolume` uses `GetDiskFreeSpaceW`: free bytes = free clusters × sectors per cluster × bytes per sector (4 KB clusters here).

**Q: What is slack space?**
Internal fragmentation: the unused tail of a file's last cluster. Computed per file in `QueryFile`, and estimated for the whole volume in
`EstimateSlack` (allocation size minus size), about 4.1 GB on C:.

**Q: What is the reserve file and how is it like ext4?**
`src/rescue/rescue.cpp` `CreateReserve` allocates real clusters (`SetEndOfFile` + `SetFileValidData`, falling back to writing zeros), never a sparse
file. Releasing it in Rescue Mode gives the space back at once. ext4 does the same by reserving about 5% of blocks for root.

**Q: Why was the first scan 289 s but the next one 58 s?**
The page cache. The first run read directory metadata from disk; the second found it already in memory. Same prototype, same drive.

**Q: Hard links?**
One file with several names. Directory enumeration counts each name, while the MFT reader takes the first non-DOS `$FILE_NAME` and counts the file
once. That is why `/bench` showed about 15 GB more with enumeration (WinSxS uses many hard links).

**Q: Reparse points and junctions?**
`scanner.cpp` sets `NF_REPARSE` and never queues them, so there are no cycles and nothing is counted twice. `scanner_mft.cpp` also clears the children of
reparse folders.

**Q: Logical size vs size on disk?**
`Node.size` (end of file) vs `Node.alloc` (`AllocationSize` from the file system). Compressed and sparse files can allocate less than their logical
size; `actions.cpp` applies `FSCTL_SET_COMPRESSION` (the Compress action, with undo).

**Q: Why does Recycle Bin need no free space?**
On the same volume, recycling is a rename into `$Recycle.Bin`: a metadata operation that NTFS journals in `$LogFile`. No data is copied. Our undo in
`actions.cpp` reads the `$I` metadata file and renames the matching `$R` file back. It also frees nothing until the bin is emptied, and the UI says so.

**Q: Disk scheduling and seek time?**
Enumeration does around 400K small random directory reads. The MFT path reads one large table mostly sequentially. FCFS, SSTF and SCAN all try to
reduce seek distance; sequential access avoids most seeks in the first place.

**Q: Unbuffered and overlapped I/O?**
`scanner_mft.cpp` opens the volume with `FILE_FLAG_NO_BUFFERING | FILE_FLAG_OVERLAPPED`. Reads are whole clusters (sector-aligned), and each of up to 8
workers has its own `OVERLAPPED` + event, so up to 8 reads are in flight.

**Q: Restore points?**
`cleanup.cpp` `AnalyzeSystem` lists System Volume Information (needs admin to measure) with the Windows tool to shrink it.

## Concurrency and synchronization

**Q: How is the scanner parallel?**
`scanner.cpp` `Scanner::Run` starts min(cores, 8) `std::thread` workers (clamped 1–16, at most 4 in Rescue Mode).

**Q: Producer–consumer?**
Workers pop a folder from `m_queue` and push its sub-folders, so every worker is both producer and consumer. The queue is a LIFO (`vector` back), which
gives a depth-first order and keeps the queue small.

**Q: Which lock?**
`std::mutex m_qLock` with `std::condition_variable m_cv`. The built exe imports `AcquireSRWLockExclusive` and `SleepConditionVariableSRW`, so in our
toolchain (llvm-mingw libc++) these are Windows SRW locks in exclusive mode plus condition variables. We use no shared (reader) mode.

**Q: How do workers know the scan is over?**
`m_cv.wait(lock, [] { return cancel || !queue.empty() || busy == 0; })`. When the queue is empty and `m_busy == 0`, every worker exits and calls
`notify_all`.

**Q: Atomics?**
`ScanTree::ReserveNodes` reserves a contiguous block of node indexes with `m_count.fetch_add(n)`. Progress counters (`m_files`, `m_dirs`, `m_bytes`,
per-top-folder bytes) are `std::atomic<u64>` with relaxed `fetch_add`. A new 1 MB node chunk is created under `m_chunkLock` with a double-checked
atomic pointer.

**Q: Thread-local storage?**
`thread_local Scratch t_scratch` holds each worker's 64 KiB `NtQueryDirectoryFile` buffer and entry vectors, reused for every folder.

**Q: How did you prevent races?**
Only the worker that scans a folder links that folder's children (`ScanFolder` → `InitNode`). Names go into per-worker `NameCursor` arenas. The UI
never reads the tree while workers run: it gets `WM_SCAN_DONE` and calls `TakeResult`, which joins the thread. Before an action edits the tree
(`Detach`), the background readers are stopped.

**Q: Deadlock?**
No code path holds two locks at once, and no lock is held during disk I/O: `Enumerate` runs unlocked, and the lock is taken only to push sub-folders.

**Q: Starvation and load balancing?**
There is one shared queue, so any idle worker takes the next folder (dynamic balancing). We don't do work stealing.

**Q: Thread-safe UI?**
Workers never touch windows. They `PostMessageW(WM_SCAN_DONE / WM_ACTION_DONE / WM_UNDO_DONE)` and the UI thread does the rest
(`src/ui2/preview.cpp`).

**Q: Cancellation?**
The atomic `m_cancel` is checked before each folder is popped. `Cancel()` also wakes the condition variable.

**Q: Amdahl's law in your data?**
Full C:, warm cache: 1 thread 25.1 s, 2 threads 14.4 s, 4 threads 11.6 s, 8 threads 8.2 s, 12 threads 11.8 s. The serial parts are the device queue
and the queue lock, and beyond 8 threads contention and context switches cost more than they save.

**Q: Files in use?**
Handles are opened with `FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE`. Access-denied folders are counted (520 on C:) and marked `NF_ERROR`.

## Processes, memory, scheduling and protection

**Q: Priority scheduling?**
In Rescue Mode the workers call `SetThreadPriority(THREAD_MODE_BACKGROUND_BEGIN)`, which lowers CPU and I/O priority (`scanner.cpp`). The quick-win
threads always do this (`quickwins.cpp`).

**Q: Why does a full disk reduce usable memory?**
The pagefile lives on the system drive and can't grow, so commit space runs out. That's why Rescue Mode uses a memory budget (256 MB by default).

**Q: Working set?**
`PeakWorkingSet()` (`common.cpp`, `GetProcessMemoryInfo`) is shown in the UI. The tray process trims itself with
`SetProcessWorkingSetSize(-1, -1)` and measured 1.6 MB private.

**Q: Thrashing?**
When `MemoryUsed()` passes the budget, files under the fold threshold (1 MB) become one "[N small files]" node per folder (`ScanFolder`), so the tree
stops growing.

**Q: Allocation strategy?**
64-byte nodes in 1 MB chunks and names in 1 MB arenas (ASCII stored at 1 byte per char), with no `malloc` per file. This avoids millions of heap
allocations and heap fragmentation. Scanner peak was 255 MB for 1.68M files.

**Q: Kernel mode?**
`NtQueryDirectoryFile`, `DeviceIoControl` (the FSCTLs), `ReadFile` on the volume and `GetDiskFreeSpaceExW` are system calls into the I/O manager and
NTFS.

**Q: UAC and privileges?**
Reading `\\.\C:` raw needs administrator rights. `IsElevated()` (`common.cpp`) decides; without it, `ScanMft` isn't tried and enumeration is used.
DISM and hibernation tools run through `ShellExecute("runas")`.

**Q: Protected system files?**
`cleanup.cpp` `IsProtectedPath` covers Windows, Program Files, `ProgramData\Microsoft`, pagefile, hiberfil, swapfile and System Volume Information,
except cleanable areas such as `Windows\Temp`. Review lists lock those items and Delete is disabled.

**Q: IPC and timers?**
The tray watcher (`notify/tray.cpp`) is a message loop with `SetTimer` every 30 s, `WM_DEVICECHANGE`, and `Shell_NotifyIconW` with `NIF_INFO`. The
main app uses `WM_APP` messages between threads.

## Algorithms

| Algorithm | Where | Notes |
|---|---|---|
| Parallel DFS traversal | `scanner.cpp` | O(n) folders + files; shared LIFO queue |
| MFT parsing | `scanner_mft.cpp` | O(records); extension records applied in a serial pass |
| Size roll-up | `scanner.cpp` `RollUp`, `ScanTree::PostOrder` | O(n) post-order; independent of node order |
| Squarified treemap + cushions | `src/viz/viz.cpp` `Treemap::Node`, `Cushion` | Bruls et al. 2000; van Wijk 1999; whole C: in 76 ms |
| Sunburst | `viz.cpp` `Sunburst::Build` | angle ∝ size; slices < 0.35° grouped |
| Duplicate funnel | `insights.cpp` `FindDuplicates` | size groups → first + last 64 KB hash → full hash (FNV-1a) |
| Stale-file classifier | `insights.cpp` `LastAccessUpdatesEnabled`, `AddStale` | last access if NTFS updates it (0x80000002 here), else last write; dates before 1990 ignored |
| Extent merging | `internals.cpp` `QueryFile` | 86 fsutil runs → 24 fragments on a 6 GB ISO |
| Top-K | `internals.cpp` `MostFragmented` (`std::partial_sort`) | heap select, O(n log k) |
| Alert hysteresis | `notify/tray.cpp` `AlertLevel::Update` | fire on downward crossing, re-arm at +10%; 11/11 tests pass |

## Not measured yet (say so if asked)
- Cold-cache time of the new scanner.
- Elevated benchmark of the parallel MFT reader. The serial version took 16.4 s warm.
- The 0-byte VHDX test and the space it frees.
- Install, upgrade and uninstall, and the Explorer right-click entry, on a clean PC.
