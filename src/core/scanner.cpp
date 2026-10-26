// SpaceRescue - SPDX-License-Identifier: GPL-3.0-or-later
//
// Enumeration follows upstream WinDirStat's FinderBasic (NtQueryDirectoryFile with a large
// per-thread buffer) and its BlockingQueue/ScanItems worker model, rewritten for our compact tree.

#include "scanner.h"

namespace sr
{
namespace
{
using NtStatus = LONG;
constexpr NtStatus StatusNoMoreFiles = static_cast<NtStatus>(0x80000006L);
constexpr int FileFullDirectoryInformation = 2;

struct IoStatus
{
    union { NtStatus status; void* pointer; };
    ULONG_PTR information;
};

struct FullDirInfo
{
    ULONG nextEntryOffset;
    ULONG fileIndex;
    LARGE_INTEGER creationTime;
    LARGE_INTEGER lastAccessTime;
    LARGE_INTEGER lastWriteTime;
    LARGE_INTEGER changeTime;
    LARGE_INTEGER endOfFile;
    LARGE_INTEGER allocationSize;
    ULONG fileAttributes;
    ULONG fileNameLength; // bytes
    ULONG eaSize;         // holds the reparse tag for reparse points
    WCHAR fileName[1];
};

using NtQueryDirectoryFileFn = NtStatus (NTAPI*)(HANDLE, HANDLE, void*, void*, IoStatus*, void*, ULONG, int,
    BOOLEAN, void*, BOOLEAN);

NtQueryDirectoryFileFn LoadNtQuery()
{
    static const auto fn = reinterpret_cast<NtQueryDirectoryFileFn>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryDirectoryFile")));
    return fn;
}

constexpr DWORD CloudAttributes = FILE_ATTRIBUTE_OFFLINE | FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS;

// One directory entry, copied out of the enumeration buffer before nodes are reserved.
struct Entry
{
    u32 nameOffset;
    u32 nameLen;
    DWORD attributes;
    u64 size;
    u64 alloc;
    u64 writeTime;
    u64 accessTime;
};

// Per-thread scratch reused for every folder: no allocations on the hot path once warm.
struct Scratch
{
    std::vector<BYTE> buffer = std::vector<BYTE>(64 * KiB); // measured: faster than 1 MiB and less RAM
    std::vector<Entry> entries;
    std::vector<wchar_t> names;
};
thread_local Scratch t_scratch;

bool IsDots(const wchar_t* n, size_t len)
{
    return (len == 1 && n[0] == L'.') || (len == 2 && n[0] == L'.' && n[1] == L'.');
}

// Fills t_scratch.entries for `path`. Returns ERROR_SUCCESS or a Win32 error.
DWORD Enumerate(const std::wstring& path)
{
    Scratch& s = t_scratch;
    s.entries.clear();
    s.names.clear();
    auto add = [&](const wchar_t* name, size_t len, DWORD attr, u64 size, u64 alloc, u64 write, u64 access)
    {
        if (IsDots(name, len)) return;
        s.entries.push_back({ static_cast<u32>(s.names.size()), static_cast<u32>(len), attr, size, alloc, write, access });
        s.names.insert(s.names.end(), name, name + len);
    };

    if (const auto ntQuery = LoadNtQuery())
    {
        Handle dir(CreateFileW(LongPath(path).c_str(), FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS, nullptr));
        if (!dir.valid()) return GetLastError();
        bool restart = true;
        for (;;)
        {
            IoStatus io = {};
            const NtStatus st = ntQuery(dir.h, nullptr, nullptr, nullptr, &io, s.buffer.data(),
                static_cast<ULONG>(s.buffer.size()), FileFullDirectoryInformation, FALSE, nullptr, restart);
            restart = false;
            if (st == StatusNoMoreFiles) break;
            if (st < 0) return s.entries.empty() ? ERROR_ACCESS_DENIED : ERROR_SUCCESS;
            for (const BYTE* p = s.buffer.data();;)
            {
                const auto* e = reinterpret_cast<const FullDirInfo*>(p);
                add(e->fileName, e->fileNameLength / sizeof(WCHAR), e->fileAttributes,
                    static_cast<u64>(e->endOfFile.QuadPart), static_cast<u64>(e->allocationSize.QuadPart),
                    static_cast<u64>(e->lastWriteTime.QuadPart), static_cast<u64>(e->lastAccessTime.QuadPart));
                if (!e->nextEntryOffset) break;
                p += e->nextEntryOffset;
            }
        }
        return ERROR_SUCCESS;
    }

    // Fallback: Win32 enumeration without 8.3 names.
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileExW(LongPath(JoinPath(path, L"*")).c_str(), FindExInfoBasic, &fd,
        FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (find == INVALID_HANDLE_VALUE)
    {
        const DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : error;
    }
    do
    {
        const u64 size = (static_cast<u64>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
        add(fd.cFileName, wcslen(fd.cFileName), fd.dwFileAttributes, size, size,
            Ticks(fd.ftLastWriteTime), Ticks(fd.ftLastAccessTime));
    }
    while (FindNextFileW(find, &fd));
    FindClose(find);
    return ERROR_SUCCESS;
}
}

// --- ScanTree ----------------------------------------------------------------

ScanTree::ScanTree(const std::wstring& path) :
    rootPath(path),
    m_nodes(new std::atomic<Node*>[MaxNodeChunks]()),
    m_names(new std::atomic<BYTE*>[MaxNameChunks]())
{
    std::wstring name = path;
    if (name.size() > 3 && name.back() == L'\\') name.pop_back();
    const u32 root = ReserveNodes(1);
    InitNode(root, NoNode, m_mainCursor, name.c_str(), name.size());
    At(root).flags = NF_DIR;
}

ScanTree::~ScanTree()
{
    for (u32 i = 0; i < MaxNodeChunks; ++i) delete[] m_nodes[i].load();
    for (u32 i = 0; i < MaxNameChunks; ++i) delete[] m_names[i].load();
}

u32 ScanTree::ReserveNodes(const u32 n)
{
    const u32 first = m_count.fetch_add(n, std::memory_order_acq_rel);
    for (u32 c = first >> NodeShift; c <= (first + n - 1) >> NodeShift; ++c)
    {
        if (m_nodes[c].load(std::memory_order_acquire)) continue;
        std::lock_guard<std::mutex> guard(m_chunkLock);
        if (!m_nodes[c].load(std::memory_order_relaxed))
        {
            m_nodes[c].store(new Node[NodeMask + 1], std::memory_order_release);
            m_nodeChunks.fetch_add(1, std::memory_order_relaxed);
        }
    }
    return first;
}

void ScanTree::SetName(Node& node, NameCursor& cursor, const wchar_t* name, const size_t len)
{
    const bool wide = std::any_of(name, name + len, [](wchar_t c) { return c >= 0x80; });
    const u32 bytes = static_cast<u32>(len * (wide ? 2 : 1));
    u32 offset = cursor.used + (wide ? (cursor.used & 1) : 0); // UTF-16 names stay 2-byte aligned
    if (offset + bytes > NameChunk)
    {
        const u32 c = m_nameChunks.fetch_add(1, std::memory_order_relaxed);
        m_names[c].store(new BYTE[NameChunk], std::memory_order_release);
        cursor = { c, 0 };
        offset = 0;
    }
    BYTE* dst = m_names[cursor.chunk].load(std::memory_order_relaxed) + offset;
    if (wide) memcpy(dst, name, bytes);
    else for (size_t i = 0; i < len; ++i) dst[i] = static_cast<BYTE>(name[i]);
    cursor.used = offset + bytes;
    node.name = cursor.chunk * NameChunk + offset;
    node.nameLen = static_cast<unsigned short>(len);
    node.nameWide = wide ? 1 : 0;
}

bool ScanTree::NameEquals(const Node& node, const std::wstring& s) const
{
    if (node.nameLen != s.size()) return false;
    const BYTE* p = NameBytes(node.name);
    for (size_t i = 0; i < s.size(); ++i)
    {
        const wchar_t c = node.nameWide ? reinterpret_cast<const wchar_t*>(p)[i] : static_cast<wchar_t>(p[i]);
        if (towlower(c) != towlower(s[i])) return false;
    }
    return true;
}

void ScanTree::InitNode(const u32 index, const u32 parent, NameCursor& cursor, const wchar_t* name, const size_t len)
{
    Node& node = At(index);
    node = Node();
    node.parent = parent;
    SetName(node, cursor, name, std::min<size_t>(len, 0xFFFF));
    if (parent != NoNode)
    {
        node.nextSibling = At(parent).firstChild;
        At(parent).firstChild = index;
    }
}

std::wstring ScanTree::Name(const u32 i) const
{
    const Node& n = At(i);
    const BYTE* p = NameBytes(n.name);
    if (n.nameWide) return std::wstring(reinterpret_cast<const wchar_t*>(p), n.nameLen);
    return std::wstring(p, p + n.nameLen);
}

std::wstring ScanTree::Path(const u32 i) const
{
    std::vector<u32> chain;
    for (u32 n = i; n != NoNode; n = At(n).parent) chain.push_back(n);
    std::wstring path;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) path = JoinPath(path, Name(*it));
    if (path.size() == 2 && path[1] == L':') path += L'\\';
    return path;
}

std::vector<u32> ScanTree::Children(const u32 i) const
{
    std::vector<u32> out;
    for (u32 c = At(i).firstChild; c != NoNode; c = At(c).nextSibling) out.push_back(c);
    std::sort(out.begin(), out.end(), [this](u32 a, u32 b) { return At(a).size > At(b).size; });
    return out;
}

void ScanTree::Detach(const u32 i)
{
    const Node& node = At(i);
    for (u32 p = node.parent; p != NoNode; p = At(p).parent)
    {
        At(p).size -= std::min(At(p).size, node.size);
        At(p).alloc -= std::min(At(p).alloc, node.alloc);
        At(p).files -= std::min(At(p).files, node.files);
    }
    if (node.parent == NoNode) return;
    u32* link = &At(node.parent).firstChild;
    while (*link != NoNode && *link != i) link = &At(*link).nextSibling;
    if (*link == i) *link = node.nextSibling;
}

u32 ScanTree::Find(const std::wstring& path) const
{
    const std::wstring rootName = Name(0);
    if (path.size() < rootName.size() || _wcsnicmp(path.c_str(), rootName.c_str(), rootName.size()) != 0) return NoNode;
    const std::wstring rest = path.substr(rootName.size());
    u32 cur = 0;
    size_t pos = 0;
    while (pos < rest.size())
    {
        while (pos < rest.size() && rest[pos] == L'\\') ++pos;
        if (pos >= rest.size()) break;
        const size_t end = rest.find(L'\\', pos);
        const std::wstring part = rest.substr(pos, end == std::wstring::npos ? std::wstring::npos : end - pos);
        u32 next = NoNode;
        for (u32 c = At(cur).firstChild; c != NoNode; c = At(c).nextSibling)
        {
            if (NameEquals(At(c), part))
            {
                next = c;
                break;
            }
        }
        if (next == NoNode) return NoNode;
        cur = next;
        if (end == std::wstring::npos) break;
        pos = end;
    }
    return cur;
}

u64 ScanTree::MemoryUsed() const
{
    return static_cast<u64>(m_nodeChunks.load(std::memory_order_relaxed)) * (NodeMask + 1) * sizeof(Node) +
        static_cast<u64>(m_nameChunks.load(std::memory_order_relaxed)) * NameChunk;
}

// --- Scanner -------------------------------------------------------------------

Scanner::~Scanner()
{
    Cancel();
    if (m_thread.joinable()) m_thread.join();
}

bool Scanner::Start(const ScanOptions& options, HWND notify, UINT doneMessage)
{
    if (Running()) return false;
    if (m_thread.joinable()) m_thread.join();
    m_options = options;
    m_notify = notify;
    m_doneMessage = doneMessage;
    m_cancel = false;
    m_files = m_dirs = m_bytes = 0;
    m_queue.clear();
    m_busy = 0;
    m_topNames.clear();
    m_topBytes.reset();
    m_tree.reset(new ScanTree(options.root));
    m_running = true;
    m_thread = std::thread([this] { Run(); });
    return true;
}

ScanProgress Scanner::Progress()
{
    ScanProgress p;
    p.files = m_files.load(std::memory_order_relaxed);
    p.dirs = m_dirs.load(std::memory_order_relaxed);
    p.bytes = m_bytes.load(std::memory_order_relaxed);
    std::lock_guard<std::mutex> guard(m_pathLock);
    p.currentPath = m_currentPath;
    if (m_topBytes)
    {
        p.topLevel.reserve(m_topNames.size());
        for (size_t i = 0; i < m_topNames.size(); ++i)
            p.topLevel.push_back({ m_topNames[i], m_topBytes[i].load(std::memory_order_relaxed) });
        std::sort(p.topLevel.begin(), p.topLevel.end(), [](const auto& a, const auto& b) { return a.bytes > b.bytes; });
    }
    return p;
}

std::unique_ptr<ScanTree> Scanner::TakeResult()
{
    if (m_thread.joinable()) m_thread.join();
    return std::move(m_tree);
}

void Scanner::Run()
{
    const DWORD start = GetTickCount();
    ScanTree& tree = *m_tree;

    const std::wstring volume = VolumeOf(m_options.root);
    if (!volume.empty())
    {
        DWORD spc = 0, bps = 0, freeClusters = 0, totalClusters = 0;
        if (GetDiskFreeSpaceW(volume.c_str(), &spc, &bps, &freeClusters, &totalClusters)) tree.clusterSize = spc * bps;
        wchar_t fs[64] = {};
        if (GetVolumeInformationW(volume.c_str(), nullptr, 0, nullptr, nullptr, nullptr, fs, 64)) tree.fileSystem = fs;
    }
    m_isNtfs = tree.fileSystem == L"NTFS";

    int threads = m_options.threads > 0 ? m_options.threads :
        static_cast<int>(std::min<unsigned>(std::max(1u, std::thread::hardware_concurrency()), 8u));
    threads = std::clamp(threads, 1, 16);
    if (m_options.lowPriority) threads = std::min(threads, 4); // stay gentle on a struggling system
    tree.threadsUsed = threads;

    // The root folder is enumerated here first, so every top-level child gets a live progress slot.
    WorkerStats rootStats;
    std::vector<Work> rootDirs;
    ScanFolder({ tree.Root(), NoNode, m_options.root }, tree.m_mainCursor, rootStats, rootDirs);
    {
        std::lock_guard<std::mutex> guard(m_qLock);
        m_queue = std::move(rootDirs);
    }

    std::vector<std::thread> workers;
    std::vector<WorkerStats> stats(static_cast<size_t>(threads));
    for (int i = 0; i < threads; ++i)
        workers.emplace_back([this, i, &stats] {
            if (m_options.lowPriority) SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
            ScanTree::NameCursor cursor;
            std::vector<Work> subdirs;
            for (;;)
            {
                Work work;
                {
                    std::unique_lock<std::mutex> lock(m_qLock);
                    m_cv.wait(lock, [this] { return m_cancel || !m_queue.empty() || m_busy == 0; });
                    if (m_cancel || m_queue.empty()) break;
                    work = std::move(m_queue.back());
                    m_queue.pop_back();
                    ++m_busy;
                }
                subdirs.clear();
                ScanFolder(work, cursor, stats[static_cast<size_t>(i)], subdirs);
                {
                    std::lock_guard<std::mutex> lock(m_qLock);
                    for (Work& w : subdirs) m_queue.push_back(std::move(w));
                    --m_busy;
                }
                m_cv.notify_all();
            }
            m_cv.notify_all();
        });
    for (std::thread& t : workers) t.join();

    for (const WorkerStats& s : stats)
    {
        rootStats.files += s.files;
        rootStats.dirs += s.dirs;
        rootStats.folded += s.folded;
        rootStats.errors += s.errors;
    }
    tree.fileCount = rootStats.files;
    tree.dirCount = rootStats.dirs;
    tree.foldedFiles = rootStats.folded;
    tree.errorCount = rootStats.errors;
    tree.budgetHit = rootStats.folded > 0;

    // Roll sizes up: children always have larger indexes than their parent.
    for (u32 i = tree.Count(); i-- > 1;)
    {
        const Node& n = tree.At(i);
        Node& p = tree.At(n.parent);
        p.size += n.size;
        p.alloc += n.alloc;
        p.files += n.files;
        p.writeTime = std::max(p.writeTime, n.writeTime);
        p.accessTime = std::max(p.accessTime, n.accessTime);
    }

    tree.cancelled = m_cancel;
    tree.scanMillis = GetTickCount() - start;
    tree.peakWorkingSet = PeakWorkingSet();
    m_running = false;
    if (m_notify) PostMessageW(m_notify, m_doneMessage, 0, 0);
}

void Scanner::ScanFolder(const Work& work, ScanTree::NameCursor& cursor, WorkerStats& stats, std::vector<Work>& subdirs)
{
    ScanTree& tree = *m_tree;
    if (std::unique_lock<std::mutex> lock(m_pathLock, std::try_to_lock); lock.owns_lock()) m_currentPath = work.path;

    if (const DWORD error = Enumerate(work.path); error != ERROR_SUCCESS)
    {
        tree.At(work.dir).flags |= NF_ERROR;
        ++stats.errors;
        return;
    }
    const Scratch& s = t_scratch;
    if (s.entries.empty()) return;

    // Decide folding first, so the folder's nodes can be reserved in one contiguous block.
    const bool overBudget = m_options.memoryBudget && tree.MemoryUsed() >= m_options.memoryBudget;
    size_t keep = 0, folded = 0;
    for (const Entry& e : s.entries)
    {
        const bool dir = (e.attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (!dir && overBudget && e.size < m_options.foldThreshold) ++folded;
        else ++keep;
    }
    const u32 first = tree.ReserveNodes(static_cast<u32>(keep + (folded ? 1 : 0)));
    const bool isRoot = work.dir == tree.Root();
    if (isRoot)
    {
        std::lock_guard<std::mutex> guard(m_pathLock);
        m_topNames.clear();
        m_topBytes.reset(new std::atomic<u64>[s.entries.size() + 1]());
    }

    u32 next = first;
    u64 files = 0, dirs = 0, bytes = 0;
    Node* aggregate = nullptr;
    u32 aggregateIndex = NoNode;
    for (const Entry& e : s.entries)
    {
        const wchar_t* name = s.names.data() + e.nameOffset;
        const bool dir = (e.attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (!dir)
        {
            ++files;
            bytes += e.size;
        }

        if (!dir && overBudget && e.size < m_options.foldThreshold)
        {
            if (!aggregate)
            {
                aggregateIndex = first + static_cast<u32>(keep);
                tree.InitNode(aggregateIndex, work.dir, cursor, L"[small files]", 13);
                aggregate = &tree.At(aggregateIndex);
                aggregate->flags = NF_AGGREGATE;
            }
            aggregate->size += e.size;
            aggregate->alloc += e.alloc;
            aggregate->files += 1;
            aggregate->writeTime = std::max(aggregate->writeTime, e.writeTime);
            aggregate->accessTime = std::max(aggregate->accessTime, e.accessTime);
            continue;
        }

        const u32 index = next++;
        tree.InitNode(index, work.dir, cursor, name, e.nameLen);
        Node& node = tree.At(index);
        node.attributes = e.attributes;
        node.writeTime = e.writeTime;
        node.accessTime = e.accessTime;

        u32 top = work.top;
        if (isRoot)
        {
            std::lock_guard<std::mutex> guard(m_pathLock);
            top = static_cast<u32>(m_topNames.size());
            m_topNames.emplace_back(name, e.nameLen);
            if (!dir) m_topBytes[top].store(e.size, std::memory_order_relaxed);
        }

        if (dir)
        {
            ++dirs;
            node.flags = NF_DIR;
            if (e.attributes & FILE_ATTRIBUTE_REPARSE_POINT) node.flags |= NF_REPARSE;
            else subdirs.push_back({ index, top, JoinPath(work.path, std::wstring(name, e.nameLen)) });
            continue;
        }

        node.size = e.size;
        node.files = 1;
        node.alloc = e.alloc;
        if (e.attributes & CloudAttributes)
        {
            node.flags |= NF_CLOUD;
            node.alloc = 0;
        }
        else if (!LoadNtQuery() && m_isNtfs)
        {
            // Fallback enumeration has no allocation size: estimate (tiny files live in the MFT record).
            const u64 cluster = tree.clusterSize ? tree.clusterSize : 4096;
            node.alloc = e.size < 700 ? 0 : (e.size + cluster - 1) / cluster * cluster;
        }
    }

    if (aggregate)
    {
        const std::wstring label = Fmt(L"[%s small files]", FormatCount(aggregate->files).c_str());
        tree.SetName(*aggregate, cursor, label.c_str(), label.size());
        stats.folded += folded;
    }

    stats.files += files;
    stats.dirs += dirs;
    m_files.fetch_add(files, std::memory_order_relaxed);
    m_dirs.fetch_add(dirs, std::memory_order_relaxed);
    m_bytes.fetch_add(bytes, std::memory_order_relaxed);
    if (!isRoot && work.top != NoNode && m_topBytes) m_topBytes[work.top].fetch_add(bytes, std::memory_order_relaxed);
}
}
