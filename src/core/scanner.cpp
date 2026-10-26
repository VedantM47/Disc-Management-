// SpaceRescue - SPDX-License-Identifier: GPL-3.0-or-later

#include "scanner.h"

namespace sr
{
// --- ScanTree ----------------------------------------------------------------

ScanTree::ScanTree(const std::wstring& path) : rootPath(path)
{
    std::wstring name = path;
    if (name.size() > 3 && name.back() == L'\\') name.pop_back();
    const u32 root = NewNode(NoNode, name.c_str(), name.size());
    At(root).flags = NF_DIR;
}

u32 ScanTree::AddName(const wchar_t* name, const size_t len)
{
    if (m_names.empty() || m_nameUsed + len + 1 > NameChunk)
    {
        m_names.emplace_back(new wchar_t[NameChunk]);
        m_nameUsed = 0;
    }
    const u32 handle = static_cast<u32>((m_names.size() - 1) * NameChunk + m_nameUsed);
    wchar_t* dst = m_names.back().get() + m_nameUsed;
    wmemcpy(dst, name, len);
    dst[len] = L'\0';
    m_nameUsed += static_cast<u32>(len + 1);
    return handle;
}

u32 ScanTree::NewNode(const u32 parent, const wchar_t* name, const size_t len)
{
    if ((m_count >> NodeShift) >= m_nodes.size()) m_nodes.emplace_back(new Node[NodeMask + 1]);
    const u32 index = m_count++;
    Node& node = At(index);
    node = Node();
    node.parent = parent;
    node.nameLen = static_cast<unsigned short>(std::min<size_t>(len, 0xFFFF));
    node.name = AddName(name, node.nameLen);
    if (parent != NoNode)
    {
        node.nextSibling = At(parent).firstChild;
        At(parent).firstChild = index;
    }
    return index;
}

std::wstring ScanTree::Name(const u32 i) const
{
    return std::wstring(NamePtr(At(i).name), At(i).nameLen);
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
    std::wstring rest = path;
    std::wstring rootName = Name(0);
    if (rest.size() < rootName.size() || _wcsnicmp(rest.c_str(), rootName.c_str(), rootName.size()) != 0) return NoNode;
    rest = rest.substr(rootName.size());
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
            if (At(c).nameLen == part.size() && _wcsnicmp(NamePtr(At(c).name), part.c_str(), part.size()) == 0)
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
    return static_cast<u64>(m_nodes.size()) * (NodeMask + 1) * sizeof(Node) +
        static_cast<u64>(m_names.size()) * NameChunk * sizeof(wchar_t);
}

// --- Scanner -------------------------------------------------------------------

Scanner::~Scanner()
{
    Cancel();
    if (m_thread.valid()) WaitForSingleObject(m_thread.h, INFINITE);
}

bool Scanner::Start(const ScanOptions& options, HWND notify, UINT doneMessage)
{
    if (Running()) return false;
    m_options = options;
    m_notify = notify;
    m_doneMessage = doneMessage;
    m_cancel = false;
    m_progress = ScanProgress();
    m_tree.reset(new ScanTree(options.root));
    m_thread.reset(CreateThread(nullptr, 256 * 1024, ThreadMain, this, 0, nullptr));
    return m_thread.valid();
}

ScanProgress Scanner::Progress()
{
    SrwGuard guard(&m_lock);
    return m_progress;
}

std::unique_ptr<ScanTree> Scanner::TakeResult()
{
    if (m_thread.valid()) WaitForSingleObject(m_thread.h, INFINITE);
    return std::move(m_tree);
}

DWORD WINAPI Scanner::ThreadMain(LPVOID self)
{
    static_cast<Scanner*>(self)->Run();
    return 0;
}

typedef BOOL (WINAPI* Wow64DisableFn)(PVOID*);

void Scanner::Run()
{
    // A 32-bit build would otherwise see SysWOW64 instead of System32.
    PVOID wowState = nullptr;
    if (auto fn = reinterpret_cast<Wow64DisableFn>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "Wow64DisableWow64FsRedirection")))
    {
        fn(&wowState);
    }
    if (m_options.lowPriority) SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);

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

    // Depth-first walk with an explicit stack (no recursion -> no stack overflow on deep trees).
    std::vector<std::pair<u32, std::wstring>> stack;
    stack.emplace_back(tree.Root(), m_options.root);
    while (!stack.empty() && !m_cancel)
    {
        std::pair<u32, std::wstring> top = std::move(stack.back());
        stack.pop_back();
        const u32 firstNew = tree.Count();
        ScanFolder(top.first, top.second);
        for (u32 c = tree.At(top.first).firstChild; c != NoNode; c = tree.At(c).nextSibling)
        {
            if (c >= firstNew && (tree.At(c).flags & (NF_DIR | NF_REPARSE)) == NF_DIR)
                stack.emplace_back(c, JoinPath(top.second, tree.Name(c)));
        }
    }

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
    if (m_options.lowPriority) SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_END);
    if (m_notify) PostMessageW(m_notify, m_doneMessage, 0, 0);
}

u64 Scanner::AllocatedSize(const std::wstring& path, const WIN32_FIND_DATAW& fd) const
{
    const u64 size = (static_cast<u64>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
    if (fd.dwFileAttributes & (FILE_ATTRIBUTE_OFFLINE | FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS)) return 0;
    if (fd.dwFileAttributes & (FILE_ATTRIBUTE_COMPRESSED | FILE_ATTRIBUTE_SPARSE_FILE))
    {
        DWORD high = 0;
        const DWORD low = GetCompressedFileSizeW(LongPath(path).c_str(), &high);
        if (low != INVALID_FILE_SIZE || GetLastError() == NO_ERROR) return (static_cast<u64>(high) << 32) | low;
    }
    // Tiny NTFS files live inside their MFT record and use no clusters.
    if (m_isNtfs && size < 700) return 0;
    const u64 cluster = m_tree->clusterSize ? m_tree->clusterSize : 4096;
    return (size + cluster - 1) / cluster * cluster;
}

void Scanner::ScanFolder(const u32 dir, std::wstring& path)
{
    ScanTree& tree = *m_tree;
    {
        SrwGuard guard(&m_lock);
        m_progress.currentPath = path;
    }

    WIN32_FIND_DATAW fd;
    const std::wstring pattern = LongPath(JoinPath(path, L"*"));
    HANDLE find = FindFirstFileExW(pattern.c_str(), FindExInfoStandard, &fd, FindExSearchNameMatch, nullptr,
        FIND_FIRST_EX_LARGE_FETCH);
    if (find == INVALID_HANDLE_VALUE)
    {
        const DWORD error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND)
        {
            tree.At(dir).flags |= NF_ERROR;
            ++tree.errorCount;
        }
        return;
    }

    u32 aggregate = NoNode;
    u64 files = 0, dirs = 0, bytes = 0;
    do
    {
        if (m_cancel) break;
        const wchar_t* name = fd.cFileName;
        if (name[0] == L'.' && (name[1] == 0 || (name[1] == L'.' && name[2] == 0))) continue;
        const size_t len = wcslen(name);
        const u64 write = Ticks(fd.ftLastWriteTime);
        const u64 access = Ticks(fd.ftLastAccessTime);

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            const u32 n = tree.NewNode(dir, name, len);
            Node& node = tree.At(n);
            node.flags = NF_DIR;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) node.flags |= NF_REPARSE;
            node.attributes = fd.dwFileAttributes;
            node.writeTime = write;
            node.accessTime = access;
            ++dirs;
            continue;
        }

        const u64 size = (static_cast<u64>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
        const u64 alloc = AllocatedSize(JoinPath(path, name), fd);
        ++files;
        bytes += size;

        // Memory budget reached: fold small files into one aggregate node per folder.
        if (m_options.memoryBudget && size < m_options.foldThreshold &&
            tree.MemoryUsed() >= m_options.memoryBudget)
        {
            tree.budgetHit = true;
            if (aggregate == NoNode)
            {
                aggregate = tree.NewNode(dir, L"[small files]", 13);
                tree.At(aggregate).flags = NF_AGGREGATE;
            }
            Node& agg = tree.At(aggregate);
            agg.size += size;
            agg.alloc += alloc;
            agg.files += 1;
            agg.writeTime = std::max(agg.writeTime, write);
            agg.accessTime = std::max(agg.accessTime, access);
            ++tree.foldedFiles;
            continue;
        }

        const u32 n = tree.NewNode(dir, name, len);
        Node& node = tree.At(n);
        node.size = size;
        node.alloc = alloc;
        node.files = 1;
        node.attributes = fd.dwFileAttributes;
        node.writeTime = write;
        node.accessTime = access;
        if (fd.dwFileAttributes & (FILE_ATTRIBUTE_OFFLINE | FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS))
            node.flags |= NF_CLOUD;
    }
    while (FindNextFileW(find, &fd));
    FindClose(find);

    tree.fileCount += files;
    tree.dirCount += dirs;
    if (aggregate != NoNode)
    {
        const u64 n = tree.At(aggregate).files;
        const std::wstring label = Fmt(L"[%s small files]", FormatCount(n).c_str());
        tree.At(aggregate).name = tree.AddName(label.c_str(), label.size());
        tree.At(aggregate).nameLen = static_cast<unsigned short>(label.size());
    }

    SrwGuard guard(&m_lock);
    m_progress.files += files;
    m_progress.dirs += dirs;
    m_progress.bytes += bytes;
}
}
