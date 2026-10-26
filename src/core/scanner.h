// SpaceRescue - SPDX-License-Identifier: GPL-3.0-or-later
//
// Compact in-memory scan tree. Design goals (Rescue Mode, F1):
//  - zero writes: everything lives in RAM, nothing is cached to disk;
//  - bounded memory: fixed 64-byte nodes with 32-bit links, names in a pooled
//    arena, both stored in fixed-size chunks so growth never doubles memory;
//  - optional memory budget: once exceeded, files below a size threshold are
//    folded into one "[small files]" node per folder instead of getting a node each.

#pragma once

#include "common.h"

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
    u64 alloc = 0;       // estimated bytes allocated on disk (subtree for folders)
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
    u32 reserved = 0;
};
static_assert(sizeof(Node) == 64, "Node must stay 64 bytes");

struct ScanOptions
{
    std::wstring root;              // "C:\" or a folder
    u64 memoryBudget = 0;           // bytes of tree memory before folding starts; 0 = unlimited
    u64 foldThreshold = 1 * MiB;    // files smaller than this get folded once over budget
    bool lowPriority = false;       // background CPU/IO priority (Rescue Mode)
};

class ScanTree
{
public:
    explicit ScanTree(const std::wstring& rootPath);

    u32 Root() const { return 0; }
    const Node& At(u32 i) const { return m_nodes[i >> NodeShift][i & NodeMask]; }
    Node& At(u32 i) { return m_nodes[i >> NodeShift][i & NodeMask]; }
    u32 Count() const { return m_count; }

    std::wstring Name(u32 i) const;
    std::wstring Path(u32 i) const;
    bool IsDir(u32 i) const { return (At(i).flags & NF_DIR) != 0; }
    // Children sorted by size, largest first.
    std::vector<u32> Children(u32 i) const;
    // Removes a node's size from all ancestors and unlinks it (after delete/quarantine).
    void Detach(u32 i);
    // Finds a node by full path, NoNode if absent.
    u32 Find(const std::wstring& path) const;

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
    bool cancelled = false;
    bool budgetHit = false;

private:
    friend class Scanner;
    static constexpr u32 NodeShift = 14;              // 16384 nodes (1 MB) per chunk
    static constexpr u32 NodeMask = (1u << NodeShift) - 1;
    static constexpr u32 NameChunk = 1u << 19;        // 512K wchar (1 MB) per name chunk

    u32 NewNode(u32 parent, const wchar_t* name, size_t len);
    u32 AddName(const wchar_t* name, size_t len);
    const wchar_t* NamePtr(u32 handle) const
    {
        return m_names[handle / NameChunk].get() + handle % NameChunk;
    }

    std::vector<std::unique_ptr<Node[]>> m_nodes;
    u32 m_count = 0;
    std::vector<std::unique_ptr<wchar_t[]>> m_names;
    u32 m_nameUsed = 0; // used wchar_t in the last chunk
};

struct ScanProgress
{
    u64 files = 0;
    u64 dirs = 0;
    u64 bytes = 0;
    std::wstring currentPath;
};

// Runs a scan on a background thread and posts `doneMessage` to `notify` when finished.
class Scanner
{
public:
    ~Scanner();
    bool Start(const ScanOptions& options, HWND notify, UINT doneMessage);
    void Cancel() { m_cancel = true; }
    bool Running() const { return m_thread.valid() && WaitForSingleObject(m_thread.h, 0) == WAIT_TIMEOUT; }
    ScanProgress Progress();
    // Takes ownership of the finished tree (call after doneMessage).
    std::unique_ptr<ScanTree> TakeResult();

private:
    static DWORD WINAPI ThreadMain(LPVOID self);
    void Run();
    void ScanFolder(u32 dir, std::wstring& path);
    u64 AllocatedSize(const std::wstring& path, const WIN32_FIND_DATAW& fd) const;

    ScanOptions m_options;
    HWND m_notify = nullptr;
    UINT m_doneMessage = 0;
    Handle m_thread;
    volatile bool m_cancel = false;
    std::unique_ptr<ScanTree> m_tree;
    SRWLOCK m_lock = SRWLOCK_INIT;
    ScanProgress m_progress;
    bool m_isNtfs = false;
};
}
