// SpaceRescue - SPDX-License-Identifier: GPL-3.0-or-later
//
// Compact in-memory scan tree filled by a multithreaded scanner. Design goals (Rescue Mode, F1):
//  - zero writes: everything lives in RAM, nothing is cached to disk;
//  - bounded memory: fixed 64-byte nodes with 32-bit links, names in pooled chunks; chunk tables
//    are fixed-size so growth never reallocates and workers never contend on one allocator;
//  - optional memory budget: once exceeded, files below a size threshold are folded into one
//    "[N small files]" node per folder instead of getting a node each;
//  - speed: N workers pull folders from a shared LIFO queue (modelled on upstream WinDirStat's
//    BlockingQueue + CItem::ScanItems) and enumerate with NtQueryDirectoryFile, like upstream FinderBasic.

#pragma once

#include "common.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace sr
{
enum NodeFlags : unsigned short
{
    NF_DIR       = 1 << 0,
    NF_AGGREGATE = 1 << 1, // "[N small files]" node created by memory-budget folding
    NF_ERROR     = 1 << 2, // folder could not be (fully) enumerated
    NF_REPARSE   = 1 << 3, // junction/symlink - not followed
    NF_CLOUD     = 1 << 4, // cloud placeholder (OneDrive etc.) - takes no local space
};

constexpr u32 NoNode = 0xFFFFFFFFu;

struct Node
{
    u64 size = 0;        // logical bytes (whole subtree for folders)
    u64 alloc = 0;       // bytes allocated on disk (subtree for folders)
    u64 writeTime = 0;   // FILETIME ticks; for folders: newest in subtree
    u64 accessTime = 0;  // FILETIME ticks; for folders: newest in subtree
    u32 parent = NoNode;
    u32 firstChild = NoNode;
    u32 nextSibling = NoNode;
    u32 name = 0;        // handle into the name arena
    u32 files = 0;       // files in subtree (folded files included)
    u32 attributes = 0;
    unsigned short nameLen = 0;
    unsigned short flags = 0;
    u32 nameWide = 0;    // 1 = name stored as UTF-16, 0 = stored as 1 byte per char (pure ASCII)
};
static_assert(sizeof(Node) == 64, "Node must stay 64 bytes");

struct ScanOptions
{
    std::wstring root;              // "C:\" or a folder
    u64 memoryBudget = 0;           // bytes of tree memory before folding starts; 0 = unlimited
    u64 foldThreshold = 1 * MiB;    // files smaller than this get folded once over budget
    bool lowPriority = false;       // background CPU/IO priority (Rescue Mode)
    int threads = 0;                // 0 = min(logical cores, 8); clamped to 1..16
    bool useMft = true;             // whole-drive NTFS scans read the MFT directly when elevated
};

class ScanTree
{
public:
    explicit ScanTree(const std::wstring& rootPath);
    ~ScanTree();
    ScanTree(const ScanTree&) = delete;
    ScanTree& operator=(const ScanTree&) = delete;

    u32 Root() const { return 0; }
    const Node& At(u32 i) const { return m_nodes[i >> NodeShift].load(std::memory_order_relaxed)[i & NodeMask]; }
    Node& At(u32 i) { return m_nodes[i >> NodeShift].load(std::memory_order_relaxed)[i & NodeMask]; }
    u32 Count() const { return m_count.load(std::memory_order_acquire); }

    std::wstring Name(u32 i) const;
    std::wstring Path(u32 i) const;
    bool IsDir(u32 i) const { return (At(i).flags & NF_DIR) != 0; }
    // Children sorted by size, largest first.
    std::vector<u32> Children(u32 i) const;
    // Removes a node's size from all ancestors and unlinks it (after delete/quarantine).
    void Detach(u32 i);
    // Finds a node by full path, NoNode if absent.
    u32 Find(const std::wstring& path) const;
    // Nodes reachable from the root, children before parents. Use this for bottom-up passes:
    // index order is NOT guaranteed to be parent-first (the MFT scanner indexes by record number).
    std::vector<u32> PostOrder() const;

    // Approximate bytes used by the tree itself.
    u64 MemoryUsed() const;

    // Statistics
    std::wstring rootPath;
    u64 fileCount = 0;
    u64 dirCount = 0;
    u64 foldedFiles = 0;
    u64 errorCount = 0;
    u64 scanMillis = 0;
    u64 peakWorkingSet = 0;
    DWORD clusterSize = 4096;
    std::wstring fileSystem;
    int threadsUsed = 1;
    bool usedMft = false;           // filled by the MFT fast path
    bool cancelled = false;
    bool budgetHit = false;

private:
    friend class Scanner;
    static constexpr u32 NodeShift = 14;                 // 16384 nodes (1 MB) per chunk
    static constexpr u32 NodeMask = (1u << NodeShift) - 1;
    static constexpr u32 MaxNodeChunks = 1u << 16;       // up to ~1G nodes
    static constexpr u32 NameChunk = 1u << 20;           // 1 MB per name chunk
    static constexpr u32 MaxNameChunks = 1u << 12;       // 4096 chunks -> 32-bit byte handles

    // A worker's private position in the name arena (no locking on the hot path).
    struct NameCursor
    {
        u32 chunk = 0xFFFFFFFFu;
        u32 used = NameChunk;
    };

    // Reserves `n` consecutive node indexes (thread-safe); returns the first.
    u32 ReserveNodes(u32 n);
    // Stores ASCII names at 1 byte per char (most names), others as UTF-16. Sets node.name/nameWide.
    void SetName(Node& node, NameCursor& cursor, const wchar_t* name, size_t len);
    void InitNode(u32 index, u32 parent, NameCursor& cursor, const wchar_t* name, size_t len);
    const BYTE* NameBytes(u32 handle) const
    {
        return m_names[handle / NameChunk].load(std::memory_order_relaxed) + handle % NameChunk;
    }
    bool NameEquals(const Node& node, const std::wstring& s) const;

    std::unique_ptr<std::atomic<Node*>[]> m_nodes;
    std::unique_ptr<std::atomic<BYTE*>[]> m_names;
    std::atomic<u32> m_count{ 0 };
    std::atomic<u32> m_nameChunks{ 0 };
    std::atomic<u32> m_nodeChunks{ 0 };
    std::mutex m_chunkLock;
    NameCursor m_mainCursor; // for names added outside the workers (root, aggregates)
};

struct TopLevelProgress
{
    std::wstring name;
    u64 bytes = 0;
};

struct ScanProgress
{
    u64 files = 0;
    u64 dirs = 0;
    u64 bytes = 0;
    std::wstring currentPath;
    // Live size of each top-level folder of the scan root (grows during the scan), largest first.
    std::vector<TopLevelProgress> topLevel;
};

// Runs a scan on background threads and posts `doneMessage` to `notify` when finished.
class Scanner
{
public:
    ~Scanner();
    bool Start(const ScanOptions& options, HWND notify, UINT doneMessage);
    void Cancel() { m_cancel = true; m_cv.notify_all(); }
    bool Running() const { return m_running.load(); }
    ScanProgress Progress();
    // Takes ownership of the finished tree (call after doneMessage).
    std::unique_ptr<ScanTree> TakeResult();

private:
    struct Work
    {
        u32 dir;
        u32 top;            // index into m_top (top-level ancestor), or NoNode for the root itself
        std::wstring path;
    };
    struct WorkerStats
    {
        u64 files = 0, dirs = 0, folded = 0, errors = 0;
    };

    void Run();
    // MFT fast path (scanner_mft.cpp). Returns false to fall back to directory enumeration.
    bool ScanMft();
    static void RollUp(ScanTree& tree);
    void ScanFolder(const Work& work, ScanTree::NameCursor& cursor, WorkerStats& stats, std::vector<Work>& subdirs);

    ScanOptions m_options;
    HWND m_notify = nullptr;
    UINT m_doneMessage = 0;
    std::thread m_thread;
    std::atomic<bool> m_running{ false };
    std::atomic<bool> m_cancel{ false };
    std::unique_ptr<ScanTree> m_tree;
    bool m_isNtfs = false;

    // Work queue (LIFO = depth-first, keeps the working set small).
    std::mutex m_qLock;
    std::condition_variable m_cv;
    std::vector<Work> m_queue;
    int m_busy = 0;

    // Progress (atomics on the hot path; the path string under a lock, updated rarely).
    std::atomic<u64> m_files{ 0 }, m_dirs{ 0 }, m_bytes{ 0 };
    std::mutex m_pathLock;
    std::wstring m_currentPath;
    std::vector<std::wstring> m_topNames;
    std::unique_ptr<std::atomic<u64>[]> m_topBytes;
};
}
