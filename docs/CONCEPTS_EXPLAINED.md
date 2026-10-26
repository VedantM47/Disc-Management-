# Every OS concept and algorithm in SpaceRescue, explained twice

Each entry has three parts:
- **Simple:** how you'd explain it to a 10-year-old.
- **Technical:** the exact OS idea, in the words an examiner expects.
- **In our code:** where it lives, so you can point to it.

---

## Part 1 – File systems and storage

### 1. Master File Table (MFT)
- **Simple:** A school register with one line per student, saying their name, which class they're in, and where they sit. NTFS has one line per file.
- **Technical:** The MFT is NTFS's table of file records (usually 1 KB each). Every file and folder has a record holding attributes: `$STANDARD_INFORMATION` (times, flags), `$FILE_NAME` (name and parent folder) and `$DATA` (size and where the data is). It's the NTFS equivalent of the Unix **inode table**.
- **In our code:** `src/core/scanner_mft.cpp` `ScanMft` reads the MFT directly when you run as admin.

### 2. Directory entries
- **Simple:** Opening a drawer and reading the label on every folder inside.
- **Technical:** Directory enumeration asks the file system for a folder's entries. Each entry carries the name, size, allocation size, timestamps and attributes. One call fills a buffer with many entries.
- **In our code:** `scanner.cpp` `Enumerate` uses `NtQueryDirectoryFile` with a 64 KiB buffer.

### 3. Extents: contiguous vs fragmented allocation
- **Simple:** A long book can sit on one shelf (contiguous), or be split across five shelves around the library (fragmented). The second is slower to read.
- **Technical:** NTFS stores file data as **extents**: runs of consecutive clusters, each mapping virtual cluster numbers (VCN, the position in the file) to logical cluster numbers (LCN, the position on the disk). One extent is **contiguous allocation**; many extents is **non-contiguous allocation**, or fragmentation.
- **In our code:** `internals.cpp` `QueryFile` gets the runs with `FSCTL_GET_RETRIEVAL_POINTERS`. Disk Internals draws them as a strip; an 8 GB file was in 58 pieces.

### 4. Free-space management
- **Simple:** The disk is a huge grid of boxes, and the OS keeps a count of empty boxes.
- **Technical:** The volume is divided into **clusters** (4 KB here). NTFS tracks free clusters in a bitmap, so free space = free clusters × cluster size.
- **In our code:** `internals.cpp` `QueryVolume` (`GetDiskFreeSpaceW`), and the cluster map on the Disk Internals tab.

### 5. Internal fragmentation (slack space)
- **Simple:** You get juice in fixed 4-litre jugs. If you need 1 litre you still take a whole jug, and 3 litres of space are wasted.
- **Technical:** Files get whole clusters. The unused tail of the last cluster is **slack**, which is **internal fragmentation**. Many tiny files waste a lot of space this way.
- **In our code:** `internals.cpp` `EstimateSlack`, which found about 4.1 GB of slack on C:.

### 6. Reserved blocks (our reserve file)
- **Simple:** Keeping emergency money in a hidden envelope. When everything runs out, you open it.
- **Technical:** ext4 reserves about 5% of blocks for root so the system can still work when users fill the disk. Our reserve file holds 512 MB of real clusters (not a sparse file) that can be released in one click.
- **In our code:** `rescue.cpp` `CreateReserve` (`SetEndOfFile` + `SetFileValidData`) and `ReleaseReserve`.

### 7. Page cache (cold vs warm)
- **Simple:** The first time you read a book you fetch it from the library. Next time it's already on your desk.
- **Technical:** The OS keeps recently read disk data in RAM. A **cold** scan reads metadata from disk; a **warm** scan finds it cached.
- **In our code:** The same prototype scan took 289 s cold and 58 s warm (`PROGRESS.md`).

### 8. Hard links
- **Simple:** One kid known by two nicknames. If you count nicknames, you count the kid twice.
- **Technical:** A hard link is a second directory entry pointing to the same file record. Enumeration lists every name; the MFT has one record.
- **In our code:** `scanner_mft.cpp` counts each file once. That's why the normal scan showed about 15 GB more (Windows' WinSxS folder uses many hard links).

### 9. Reparse points and junctions
- **Simple:** A signpost that says "this folder is really over there". If you follow every signpost you might walk in a circle forever.
- **Technical:** A **reparse point** (junction, symlink, mount point) redirects path lookups elsewhere. Following them can cause cycles or double counting.
- **In our code:** `scanner.cpp` marks them `NF_REPARSE` and doesn't follow them.

### 10. Compression and sparse files
- **Simple:** Vacuum-packing clothes so they take less room in the suitcase.
- **Technical:** NTFS compression stores data in fewer clusters. Sparse files don't allocate clusters for runs of zeros. Either way, the **allocated size** can be smaller than the **logical size**.
- **In our code:** `actions.cpp` Compress action (`FSCTL_SET_COMPRESSION`, with undo); the scanner reads allocated sizes.

### 11. Logical size vs size on disk
- **Simple:** How long your essay is vs how many full pages of paper it used.
- **Technical:** Logical size = bytes in the file. Size on disk = clusters allocated × cluster size. These differ because of slack, compression, sparse files, or tiny files stored inside the MFT record itself ("resident").
- **In our code:** `scanner.h` `Node.size` vs `Node.alloc`.

### 12. Journaled rename (why the Recycle Bin needs no space)
- **Simple:** Moving a book to the "to throw away" shelf in the same room. You don't need a new shelf, just a new label.
- **Technical:** On the same volume, recycling is a **rename** into `$Recycle.Bin`: only metadata changes, and NTFS logs it in its journal (`$LogFile`) so it is crash-safe. No data is copied, so it needs no free space, but it also frees none until the bin is emptied.
- **In our code:** `actions.cpp`. Undo reads the bin's `$I` file to find the original path and renames the `$R` file back.

### 13. Seek time and disk scheduling
- **Simple:** Fetching 400,000 items from all over a supermarket vs taking one trolley straight down a single aisle.
- **Technical:** Random reads cost seek and access time. Disk scheduling (FCFS, SSTF, SCAN) reorders requests to reduce seeking. Reading the MFT is mostly one big sequential read, which avoids the problem.
- **In our code:** `scanner_mft.cpp` sequential MFT read vs `scanner.cpp` per-folder queries.

### 14. Unbuffered I/O
- **Simple:** Carrying boxes straight to the truck instead of stacking them in the hallway first.
- **Technical:** `FILE_FLAG_NO_BUFFERING` skips the OS cache. Reads must be sector-aligned (we read whole clusters). It's good for reading big data once.
- **In our code:** `scanner_mft.cpp` opens the volume this way.

### 15. Overlapped (asynchronous) I/O
- **Simple:** Ordering eight pizzas at once instead of waiting for each before ordering the next.
- **Technical:** With `OVERLAPPED` I/O a thread starts a read and waits on an event. Several workers keep several reads "in flight", so the SSD's queue stays busy.
- **In our code:** `scanner_mft.cpp`, where each of up to 8 workers has its own `OVERLAPPED` and event.

### 16. Volume shadow copies and restore points
- **Simple:** Photos of your room from last week, so you can put things back.
- **Technical:** Volume Shadow Copy keeps snapshots of the volume's state (restore points), stored in System Volume Information and using real disk space.
- **In our code:** `cleanup.cpp` `AnalyzeSystem` lists it with the safe Windows tool to shrink it.

---

## Part 2 – Concurrency and synchronization

### 17. Threads and a thread pool
- **Simple:** One person searching a whole library is slow. Eight friends, each taking a shelf, is fast.
- **Technical:** A thread is an independent flow of execution inside a process. A **thread pool** is a fixed set of worker threads sharing the work.
- **In our code:** `scanner.cpp` `Run` starts min(cores, 8) `std::thread` workers.

### 18. Producer–consumer queue
- **Simple:** A to-do pile. Whoever opens a folder adds its sub-folders to the pile, and whoever is free takes the next one.
- **Technical:** Producers add work items to a shared queue and consumers remove them. Here every worker is both. We use a **LIFO** order (depth-first), which keeps the pile small.
- **In our code:** `scanner.cpp` `m_queue`.

### 19. Mutual exclusion (SRW lock)
- **Simple:** A bathroom key. Only the person holding it can go in.
- **Technical:** A **mutex** makes sure only one thread is in a **critical section** at a time. Our `std::mutex` compiles to a Windows **SRW (slim reader/writer) lock**, used in exclusive mode. We checked the exe imports `AcquireSRWLockExclusive`.
- **In our code:** `scanner.h` `m_qLock` guards the queue.

### 20. Condition variable
- **Simple:** Workers nap by the to-do pile and someone rings a bell when new work arrives, or when everything is done.
- **Technical:** A condition variable lets a thread sleep until a condition becomes true, without busy-waiting. We wait on "queue not empty, or nobody busy" for idle and termination detection.
- **In our code:** `scanner.cpp` `m_cv.wait(...)` (Windows `SleepConditionVariableSRW` underneath).

### 21. Atomic operations
- **Simple:** A ticket machine. Two people pressing at the same time still get different numbers.
- **Technical:** An atomic read-modify-write (`fetch_add`) can't be interrupted halfway, so counters stay correct without a lock.
- **In our code:** `scanner.cpp` `ReserveNodes` (`m_count.fetch_add`) and the progress counters.

### 22. Thread-local storage
- **Simple:** Everyone gets their own notebook instead of fighting over one.
- **Technical:** A `thread_local` variable has a separate copy per thread, so no locking is needed.
- **In our code:** `scanner.cpp` `t_scratch`, a 64 KiB enumeration buffer per worker.

### 23. Race conditions (and how we avoid them)
- **Simple:** Two kids writing on the same line of a page at once makes a mess.
- **Technical:** A **race condition** is when the result depends on thread timing. We avoid shared writes: only the worker scanning a folder links its children, and each worker writes names into its own arena.
- **In our code:** `scanner.cpp` `ScanFolder`, `InitNode`, `NameCursor`.

### 24. Deadlock (and how we avoid it)
- **Simple:** Two kids each hold one toy and wait for the other's toy. Nobody moves, forever.
- **Technical:** A deadlock needs four things together: mutual exclusion, hold-and-wait, no preemption and circular wait. We never hold two locks at once (no hold-and-wait), so it can't happen. We also never hold a lock during disk I/O.
- **In our code:** `scanner.cpp`: `Enumerate` runs unlocked, and the lock is taken only to push results.

### 25. Starvation, fairness and load balancing
- **Simple:** Everyone takes from the same pile, so nobody sits idle while someone else has a huge stack.
- **Technical:** One shared queue gives **dynamic load balancing**: any idle worker takes the next folder. No worker waits forever (no starvation).
- **In our code:** the worker loop in `scanner.cpp`.

### 26. Thread-safe UI
- **Simple:** Only the teacher writes on the board. Students pass notes to the teacher.
- **Technical:** Windows windows belong to the thread that created them. Background threads must not touch them; they **post messages** to the UI thread.
- **In our code:** `PostMessageW(WM_SCAN_DONE …)` in `scanner.cpp`, handled in `src/ui2/preview.cpp`.

### 27. Cooperative cancellation
- **Simple:** "Stop when you finish this page," not ripping the book away.
- **Technical:** A shared atomic flag is checked between work items, and threads exit cleanly.
- **In our code:** `scanner.cpp` `m_cancel`.

### 28. Amdahl's law and contention
- **Simple:** Ten cooks don't make soup 10× faster if there's only one stove.
- **Technical:** Speedup is limited by the serial part. With more threads, contention on the disk queue and the lock grows. We measured 1 thread 25.1 s, 4 threads 11.6 s, 8 threads 8.2 s, and 12 threads 11.8 s (slower).
- **In our code:** benchmark in `PROGRESS.md`, plus the slide chart.

### 29. File sharing and access denied
- **Simple:** Some rooms are locked or in use. You note it and move on instead of crashing.
- **Technical:** Opening files with full sharing flags avoids sharing violations. Access-control lists can deny access, and we count those folders instead of failing.
- **In our code:** `FILE_SHARE_READ|WRITE|DELETE` in `scanner.cpp`; 520 denied folders counted (`NF_ERROR`).

---

## Part 3 – Processes, memory, scheduling and protection

### 30. Priority scheduling (CPU and I/O)
- **Simple:** The scanner lets important work go first so your PC doesn't freeze.
- **Technical:** Threads have priorities. **Background mode** lowers both CPU and I/O priority.
- **In our code:** `SetThreadPriority(THREAD_MODE_BACKGROUND_BEGIN)` in Rescue Mode (`scanner.cpp`) and always for quick wins (`quickwins.cpp`).

### 31. Context switching
- **Simple:** Switching between homework subjects every minute wastes time packing and unpacking bags.
- **Technical:** Switching threads saves and restores CPU state and costs cache warmth. More threads than cores adds switches. That's why we use min(cores, 8).

### 32. Virtual memory and the pagefile
- **Simple:** When your desk is full you put papers in a drawer (the disk). If the drawer is also full, you're stuck.
- **Technical:** Virtual memory uses the **pagefile** on disk to back RAM. On a full system drive the pagefile can't grow, so the system effectively has less memory. That's why we cap our memory use.
- **In our code:** the memory budget in `scanner.cpp` (256 MB in Rescue Mode).

### 33. Working set
- **Simple:** How much of your desk you're actually using right now.
- **Technical:** The **working set** is the set of a process's pages currently in RAM.
- **In our code:** `PeakWorkingSet()` in `common.cpp` (255 MB peak scanner). The tray trims itself with `SetProcessWorkingSetSize`, to 1.6 MB.

### 34. Thrashing (and the memory budget)
- **Simple:** So little space that you spend all day moving papers between desk and drawer and never do homework.
- **Technical:** **Thrashing** is when the system pages so much that no useful work gets done. Our budget folds small files into one "[N small files]" node so the tree stops growing.
- **In our code:** `ScanFolder` in `scanner.cpp`.

### 35. Arena allocation
- **Simple:** Buying paper by the ream instead of one sheet at a time.
- **Technical:** Allocating big 1 MB chunks and cutting them into 64-byte nodes avoids millions of `malloc` calls and **heap fragmentation**. Names are stored 1 byte per character when they're ASCII.
- **In our code:** `scanner.h` `ScanTree` (node chunks and name arena).

### 36. System calls, kernel mode and user mode
- **Simple:** Apps can't touch the disk directly. They ask the OS "please read this for me".
- **Technical:** A **system call** switches from user mode to kernel mode, where the I/O manager and the NTFS driver do the work.
- **In our code:** `NtQueryDirectoryFile`, `DeviceIoControl` (FSCTLs), `ReadFile`, `GetDiskFreeSpaceExW`.

### 37. Hibernation file and swap file
- **Simple:** `hiberfil.sys` is a photo of your RAM taken when the PC sleeps deeply. `swapfile.sys` is a small drawer for Store apps.
- **Technical:** `hiberfil.sys` stores memory contents for hibernate and fast startup; `swapfile.sys` supports Store-app swapping. Turning off hibernation removes `hiberfil.sys`.
- **In our code:** `cleanup.cpp` `AnalyzeSystem` shows them with the safe Windows setting.

### 38. Privilege levels and UAC
- **Simple:** Some doors need the head teacher's key.
- **Technical:** Raw volume access (`\\.\C:`) needs administrator rights. UAC elevation gives them; without it, we fall back to normal scanning.
- **In our code:** `IsElevated()` in `common.cpp`; tools needing admin run with `ShellExecute("runas")`.

### 39. Access control lists (ACLs)
- **Simple:** A guest list on each door.
- **Technical:** Every file and folder has an ACL saying who may do what. Denied access returns an error, which we count, and the scan continues.
- **In our code:** `NF_ERROR` in `scanner.cpp`.

### 40. Protected system files
- **Simple:** "Don't touch" stickers on things Windows needs to run.
- **Technical:** Paths managed by Windows (Windows, Program Files, pagefile, hiberfil, System Volume Information…) must only be changed through Windows' own tools.
- **In our code:** `cleanup.cpp` `IsProtectedPath`, which disables Delete for those paths.

### 41. Message loop, IPC and timers
- **Simple:** A mailbox the app checks all the time. Threads, Windows and timers drop letters into it.
- **Technical:** A Windows GUI thread runs a **message loop** (`GetMessage`/`DispatchMessage`). Threads communicate by posting messages. `SetTimer` delivers periodic `WM_TIMER` messages.
- **In our code:** the tray polls free space every 30 s (`tray.cpp`); the scan and actions post `WM_APP` messages (`preview.cpp`).

---

## Part 4 – Algorithms

### A1. Parallel tree traversal
- **Simple:** Friends exploring a maze. Each time someone finds new corridors they write them on the shared to-do pile.
- **Technical:** Depth-first traversal of the directory tree using a shared LIFO work queue and N workers. Time is O(n) over files and folders.
- **In our code:** `scanner.cpp`.

### A2. MFT parsing
- **Simple:** Reading the school register line by line instead of visiting every classroom.
- **Technical:** Read the MFT in 4 MB chunks. For each 1 KB record, verify the "FILE" signature, apply **update-sequence fixups** (repair the last 2 bytes of each 512-byte sector), then walk its attributes. O(records).
- **In our code:** `scanner_mft.cpp` `ScanMft`, `Fixup`.

### A3. Size roll-up (post-order)
- **Simple:** To know how heavy a box of boxes is, weigh the small boxes first, then add them up.
- **Technical:** A **post-order** traversal visits children before parents, adding each node's size, file count and newest time into its parent. One pass, O(n).
- **In our code:** `scanner.cpp` `RollUp`, using `ScanTree::PostOrder`.

### A4. Squarified treemap + cushion shading
- **Simple:** Cutting a chocolate bar into pieces sized by how much space each folder uses, and keeping the pieces as square as possible so they're easy to see. Then shading each piece like a little pillow so you can tell them apart.
- **Technical:** The **squarified** layout (Bruls, Huizing, van Wijk 2000) fills rows along the shorter side, adding items while the worst aspect ratio improves. **Cushion shading** (van Wijk and van de Wetering 1999) adds a parabolic ridge per nesting level and shades each pixel with Lambert lighting plus ambient light. The whole of C: renders in 76 ms.
- **In our code:** `src/viz/viz.cpp` `Treemap::Node`, `Cushion`.

### A5. Sunburst (angular partition)
- **Simple:** A pie chart, then another ring around it for the folders inside each slice, and so on.
- **Technical:** Each ring is one depth level; a node's angle is proportional to its size. Slices under 0.35° are grouped into "smaller items".
- **In our code:** `viz.cpp` `Sunburst::Build`.

### A6. Duplicate detection funnel
- **Simple:** To find identical twins, first only compare people of the same height, then the same face, and only then check fingerprints.
- **Technical:** Group files by size; within a group, compare a hash of the first and last 64 KB; only on a match compute a full-content hash (FNV-1a). Most files are ruled out without reading them.
- **In our code:** `insights.cpp` `FindDuplicates`.

### A7. Stale-file classification with fallback
- **Simple:** "Haven't touched this toy in a year" means maybe you don't need it. But if the diary of when you played isn't kept, use the date you last fixed it instead.
- **Technical:** Use the last-access time only if NTFS updates it (registry `NtfsDisableLastAccessUpdate`; `0x80000002` on this PC means it does). Otherwise use last-write time. Dates before 1990 are ignored as placeholders. Results go into 6, 12 and 24-month buckets.
- **In our code:** `insights.cpp` `LastAccessUpdatesEnabled`, `AddStale`.

### A8. Extent merging
- **Simple:** If two pieces of a train track touch end to end, it's really one piece.
- **Technical:** Merge consecutive runs where the next LCN equals the previous LCN plus its length. That turned fsutil's 86 runs into 24 real fragments on a 6 GB ISO.
- **In our code:** `internals.cpp` `QueryFile`.

### A9. Top-K largest
- **Simple:** Keep only the 200 tallest kids without sorting the whole school.
- **Technical:** `std::partial_sort` uses heap selection, O(n log k) instead of O(n log n).
- **In our code:** `internals.cpp` `MostFragmented`.

### A10. Threshold alerts with hysteresis
- **Simple:** A smoke alarm that beeps once when smoke appears, not every second while it's smoky. It resets only after the air is clearly clean again.
- **Technical:** Fire when free space crosses a threshold downward (50, 20, 10, 5, 3, 2, 1 GB, 512 MB). Re-arm only after free space rises 10% above that threshold, which stops alerts flapping near the line. 11 of 11 unit tests pass.
- **In our code:** `src/notify/tray.cpp` `AlertLevel::Update`.
