# SpaceRescue – what to say (read this only)

## The whole project in 3 lines
1. When C: is full, Windows breaks, and disk tools break too because they also need space and RAM.
2. SpaceRescue scans fast (8 s for 1.7M files), tells you what is SAFE to delete, and writes nothing to the full drive.
3. It also shows OS internals (clusters, slack, fragments, MFT) on your real disk.

## One line per slide
| Slide | Say this |
|---|---|
| Title | "A disk manager that still works when the disk is full." |
| Introduction | "The OS knows where every byte is; the user just sees 'low disk space'. We connect the two." |
| Problem | "At 512 MB free, updates fail and apps crash. Tools are slow and don't say what's safe." |
| Windows tools table | "No built-in tool shows where space went, offers undo, AND warns early." |
| Use cases | "Student, developer, gamer, normal user: each gets a specific answer." |
| User's side | "Notice → understand → act safely → stay safe." |
| Our solution | "Each problem has a fix: Rescue Mode, fast scan, safety badges, locks, alerts." |
| Objectives | "Six goals: rescue, fast, explain, clean safely, warn early, teach internals." |
| Related work | "WinDirStat, WizTree and others show sizes, but none warn early or handle a full disk." |
| OS concepts (file systems) | "We use the MFT, extents, clusters and slack directly. Each card shows the file in our code." |
| Concurrency | "8 threads share a folder queue protected by a lock; no lock is held during disk reads." |
| Memory and protection | "Full disk = less RAM (pagefile can't grow), so we cap memory and lower our priority." |
| Built for 0 bytes free | "Scan lives only in RAM: 255 MB peak, tray uses 1.6 MB, 19 bytes written." |
| Algorithms | "Parallel tree walk, MFT parsing, treemap, duplicate funnel, alert hysteresis." |
| Deep dive | "1 thread 25 s → 8 threads 8.2 s. 12 threads got slower: that's Amdahl's law." |
| Methodology | "Scan → model → analyse → decide → act with undo. Tested with benchmarks and fsutil." |
| Architecture | "User → UI → engine → Windows APIs. Nothing is written to the full drive." |
| Product | "Overview, Clean up, System, Disk internals, Rescue Mode: real screenshots." |
| Results | "58 s → 8.2 s, answer in 3 s, 255 MB RAM, tray 1.6 MB." |
| Conclusion | "Done: fast, safe, zero-write. Pending: 0-byte disk test, admin MFT benchmark." |

## 8 questions they will ask (short answers)
1. **What is the MFT?** NTFS's table with one record per file, like Unix inodes. We read it directly when run as admin, which is faster.
2. **What is slack space?** Files get whole 4 KB clusters; the unused tail is wasted. That's internal fragmentation (about 4 GB on our C:).
3. **What is fragmentation?** A file stored in many separate pieces (extents) instead of one. We draw it: one 8 GB file was in 58 pieces.
4. **How is it multithreaded?** 8 worker threads take folders from a shared queue (producer–consumer). A mutex (Windows SRW lock) guards the queue, and a condition variable tells workers when the work is done.
5. **How do you avoid deadlock?** We never hold two locks at once, and never hold a lock while reading the disk.
6. **Why not use 16 threads?** We measured it: 12 threads were slower than 8. The disk and the lock become the bottleneck (Amdahl's law).
7. **Why does a full disk reduce RAM?** The pagefile is on the same disk and can't grow, so we cap our memory and fold small files together.
8. **Why does the Recycle Bin free nothing?** It's just a rename on the same drive. Space comes back only when the bin is emptied, which is why we offer quarantine to another drive.

## If you forget everything, say this
"SpaceRescue reads the file system the way the OS does: the MFT, clusters and extents. It uses 8 threads to scan 1.7 million files in 8 seconds, tells you what's safe to delete with undo, and never writes to the full disk."
