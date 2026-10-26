// SpaceRescue - SPDX-License-Identifier: GPL-3.0-or-later

#include "internals.h"

namespace sr
{
namespace
{
const size_t kMaxDisplayExtents = 4096;
}

VolumeInternals QueryVolume(const std::wstring& rootIn)
{
    VolumeInternals v;
    v.root = VolumeOf(rootIn);
    if (v.root.empty()) v.root = rootIn;

    DWORD spc = 0, bps = 0, freeC = 0, totalC = 0;
    if (GetDiskFreeSpaceW(v.root.c_str(), &spc, &bps, &freeC, &totalC))
    {
        v.bytesPerSector = bps;
        v.bytesPerCluster = spc * bps;
        v.totalClusters = totalC;
        v.freeClusters = freeC;
    }
    wchar_t label[MAX_PATH + 1] = {}, fs[MAX_PATH + 1] = {};
    DWORD serial = 0;
    if (GetVolumeInformationW(v.root.c_str(), label, MAX_PATH, &serial, nullptr, nullptr, fs, MAX_PATH))
    {
        v.label = label;
        v.fileSystem = fs;
        v.serial = serial;
    }

    if (_wcsicmp(v.fileSystem.c_str(), L"NTFS") != 0)
    {
        v.ntfsError = ERROR_NOT_SUPPORTED;
        return v;
    }
    // "\\.\C:" - opening a volume handle usually needs administrator rights.
    const std::wstring dev = L"\\\\.\\" + v.root.substr(0, 2);
    Handle h(CreateFileW(dev.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr));
    if (!h.valid()) h.reset(CreateFileW(dev.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr));
    if (!h.valid())
    {
        v.ntfsError = GetLastError();
        return v;
    }
    NTFS_VOLUME_DATA_BUFFER data = {};
    DWORD got = 0;
    if (!DeviceIoControl(h.h, FSCTL_GET_NTFS_VOLUME_DATA, nullptr, 0, &data, sizeof(data), &got, nullptr))
    {
        // A zero-access handle can be refused by the driver; retry with read access to learn the real reason.
        h.reset(CreateFileW(dev.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr));
        if (!h.valid())
        {
            v.ntfsError = GetLastError();
            return v;
        }
        if (!DeviceIoControl(h.h, FSCTL_GET_NTFS_VOLUME_DATA, nullptr, 0, &data, sizeof(data), &got, nullptr))
        {
            v.ntfsError = GetLastError();
            return v;
        }
    }
    v.ntfsData = true;
    v.bytesPerFileRecord = data.BytesPerFileRecordSegment;
    v.mftValidDataLength = static_cast<u64>(data.MftValidDataLength.QuadPart);
    v.mftRecordCount = data.BytesPerFileRecordSegment ? v.mftValidDataLength / data.BytesPerFileRecordSegment : 0;
    v.mftStartLcn = static_cast<u64>(data.MftStartLcn.QuadPart);
    v.mftZoneStart = static_cast<u64>(data.MftZoneStart.QuadPart);
    v.mftZoneEnd = static_cast<u64>(data.MftZoneEnd.QuadPart);
    if (data.BytesPerCluster) v.bytesPerCluster = data.BytesPerCluster;
    return v;
}

FileInternals QueryFile(const std::wstring& path, DWORD bytesPerCluster)
{
    FileInternals f;
    f.path = path;
    if (bytesPerCluster == 0)
    {
        DWORD spc = 0, bps = 0, a = 0, b = 0;
        const std::wstring root = VolumeOf(path);
        if (!root.empty() && GetDiskFreeSpaceW(root.c_str(), &spc, &bps, &a, &b)) bytesPerCluster = spc * bps;
        if (bytesPerCluster == 0) bytesPerCluster = 4096;
    }

    Handle h(CreateFileW(LongPath(path).c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    if (!h.valid())
    {
        f.error = GetLastError();
        return f;
    }
    BY_HANDLE_FILE_INFORMATION info = {};
    if (!GetFileInformationByHandle(h.h, &info))
    {
        f.error = GetLastError();
        return f;
    }
    f.logicalSize = (static_cast<u64>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    f.hardLinks = info.nNumberOfLinks;
    f.compressed = (info.dwFileAttributes & FILE_ATTRIBUTE_COMPRESSED) != 0;
    f.sparse = (info.dwFileAttributes & FILE_ATTRIBUTE_SPARSE_FILE) != 0;
    f.encrypted = (info.dwFileAttributes & FILE_ATTRIBUTE_ENCRYPTED) != 0;
    if (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return f; // folders: attributes only

    std::vector<char> buffer(64 * 1024);
    STARTING_VCN_INPUT_BUFFER in = {};
    u64 allocatedClusters = 0;
    std::vector<Extent> raw;
    for (;;)
    {
        DWORD got = 0;
        const BOOL ok = DeviceIoControl(h.h, FSCTL_GET_RETRIEVAL_POINTERS, &in, sizeof(in), buffer.data(),
                                        static_cast<DWORD>(buffer.size()), &got, nullptr);
        const DWORD err = ok ? ERROR_SUCCESS : GetLastError();
        if (!ok && err != ERROR_MORE_DATA)
        {
            if (err == ERROR_HANDLE_EOF)
            {
                // No clusters at all: the data lives inside the MFT record (or the file is empty).
                f.resident = f.logicalSize > 0;
                f.allocatedSize = 0;
            }
            else
                f.error = err;
            break;
        }
        const RETRIEVAL_POINTERS_BUFFER* rp = reinterpret_cast<const RETRIEVAL_POINTERS_BUFFER*>(buffer.data());
        u64 vcn = static_cast<u64>(rp->StartingVcn.QuadPart);
        for (DWORD i = 0; i < rp->ExtentCount; ++i)
        {
            Extent e;
            e.vcn = vcn;
            const u64 next = static_cast<u64>(rp->Extents[i].NextVcn.QuadPart);
            e.clusters = next - vcn;
            e.hole = rp->Extents[i].Lcn.QuadPart == -1;
            e.lcn = e.hole ? 0 : static_cast<u64>(rp->Extents[i].Lcn.QuadPart);
            if (!e.hole) allocatedClusters += e.clusters;
            vcn = next;
            // Merge runs that are physically back-to-back (contiguous allocation).
            if (!raw.empty() && !e.hole && !raw.back().hole && raw.back().lcn + raw.back().clusters == e.lcn)
                raw.back().clusters += e.clusters;
            else if (!raw.empty() && e.hole && raw.back().hole)
                raw.back().clusters += e.clusters;
            else
                raw.push_back(e);
        }
        if (ok) break;
        in.StartingVcn.QuadPart = static_cast<LONGLONG>(vcn); // ERROR_MORE_DATA: continue where we stopped
    }
    for (const Extent& e : raw)
        if (!e.hole) ++f.fragments;
    if (raw.size() > kMaxDisplayExtents) raw.resize(kMaxDisplayExtents);
    f.extents = raw;
    if (!f.resident)
    {
        f.allocatedSize = allocatedClusters * bytesPerCluster;
        f.slack = f.allocatedSize > f.logicalSize ? f.allocatedSize - f.logicalSize : 0;
    }
    return f;
}

SlackSummary EstimateSlack(const ScanTree& tree)
{
    SlackSummary s;
    const u64 cluster = tree.clusterSize ? tree.clusterSize : 4096;
    // Walk with an explicit stack: the tree can be very deep.
    std::vector<u32> stack(1, tree.Root());
    while (!stack.empty())
    {
        const u32 d = stack.back();
        stack.pop_back();
        for (u32 ch = tree.At(d).firstChild; ch != NoNode; ch = tree.At(ch).nextSibling)
        {
            const Node& n = tree.At(ch);
            if (n.flags & NF_DIR)
            {
                if (!(n.flags & NF_REPARSE)) stack.push_back(ch);
                continue;
            }
            if (n.flags & (NF_AGGREGATE | NF_CLOUD)) continue;
            ++s.filesCounted;
            if (n.alloc > n.size)
            {
                const u64 slack = n.alloc - n.size;
                s.totalSlack += slack;
                if (n.size < cluster) s.smallFilesWaste += slack;
            }
        }
    }
    return s;
}

std::vector<FragmentedFile> MostFragmented(const ScanTree& tree, const size_t topN, volatile bool* cancel)
{
    std::vector<u32> files;
    std::vector<u32> stack(1, tree.Root());
    while (!stack.empty())
    {
        const u32 d = stack.back();
        stack.pop_back();
        for (u32 ch = tree.At(d).firstChild; ch != NoNode; ch = tree.At(ch).nextSibling)
        {
            const Node& n = tree.At(ch);
            if (n.flags & NF_DIR)
            {
                if (!(n.flags & NF_REPARSE)) stack.push_back(ch);
            }
            else if (!(n.flags & (NF_AGGREGATE | NF_CLOUD)) && n.size > 0)
                files.push_back(ch);
        }
        if (cancel && *cancel) return {};
    }
    const size_t keep = std::min(topN, files.size());
    std::partial_sort(files.begin(), files.begin() + keep, files.end(),
                      [&](u32 a, u32 b) { return tree.At(a).size > tree.At(b).size; });
    files.resize(keep);

    std::vector<FragmentedFile> out;
    for (u32 node : files)
    {
        if (cancel && *cancel) break;
        const FileInternals fi = QueryFile(tree.Path(node), tree.clusterSize);
        if (fi.error != 0) continue;
        FragmentedFile ff;
        ff.node = node;
        ff.fragments = fi.fragments;
        out.push_back(ff);
    }
    std::stable_sort(out.begin(), out.end(), [](const FragmentedFile& a, const FragmentedFile& b) { return a.fragments > b.fragments; });
    return out;
}

std::wstring Explain(const Metric metric)
{
    switch (metric)
    {
    case Metric::ClusterSize:
        return L"A cluster is the smallest block the file system hands out. Files are stored in whole clusters, so a 1 KB file still "
               L"occupies one full cluster (typically 4 KB). Bigger clusters mean fewer lookups but more wasted space.";
    case Metric::Slack:
        return L"Slack space is the unused tail of a file's last cluster: size on disk minus the file's real size. It is internal "
               L"fragmentation - space that is allocated but holds no data. Millions of tiny files can waste gigabytes this way.";
    case Metric::Extents:
        return L"An extent is a run of consecutive clusters. A file stored in one extent has contiguous allocation and is fast to read; "
               L"a file split across many extents (non-contiguous allocation) is fragmented and needs extra seeks, which hurts hard disks most.";
    case Metric::Mft:
        return L"The Master File Table is NTFS's index of every file, much like the inode table in Unix file systems. Each file has a "
               L"record of about 1 KB holding its name, times and where its data lives. More files means a bigger MFT.";
    case Metric::FreeClusters:
        return L"Free clusters are blocks not owned by any file; the volume bitmap tracks them. Free space is the free cluster count times "
               L"the cluster size. When it reaches zero, even deleting through the Recycle Bin or saving settings can fail.";
    case Metric::Resident:
        return L"Very small files (roughly under 700 bytes) are stored directly inside their MFT record, so they have no clusters or extents "
               L"of their own. They are called resident files and cost no cluster space.";
    case Metric::Compression:
        return L"NTFS compression stores a file in compressed chunks, so its size on disk can be smaller than its real size. It is "
               L"transparent to programs and trades a little CPU for disk space. Sparse files similarly skip unwritten zero ranges.";
    }
    return std::wstring();
}
}
