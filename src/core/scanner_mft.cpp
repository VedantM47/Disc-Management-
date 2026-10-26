// SpaceRescue - SPDX-License-Identifier: GPL-3.0-or-later
//
// MFT fast path, following upstream WinDirStat's FinderNtfs: read $MFT sequentially in large chunks,
// apply update-sequence fixups, and take names, parents, sizes and times from $STANDARD_INFORMATION,
// $FILE_NAME and $DATA. Needs administrator rights (raw volume read). Node index = MFT record number
// (records 0 and 5 swapped so the root directory is node 0), so the tree is NOT parent-first ordered;
// bottom-up passes use ScanTree::PostOrder().

#include "scanner.h"

namespace sr
{
namespace
{
constexpr u32 RecordRoot = 5;           // "." root directory
constexpr u32 FirstUserRecord = 16;     // 0..15 are NTFS metafiles ($MFT, $LogFile, $Extend, ...)
constexpr u16 RecordInUse = 0x0001, RecordIsDirectory = 0x0002;
constexpr u32 AttrStandardInfo = 0x10, AttrFileName = 0x30, AttrData = 0x80, AttrReparse = 0xC0, AttrEnd = 0xFFFFFFFF;
constexpr u16 AttrFlagCompressed = 0x0001, AttrFlagSparse = 0x8000;
constexpr u8 NamespaceDos = 2;

inline u32 NodeOf(u64 record) { return record == RecordRoot ? 0 : record == 0 ? RecordRoot : static_cast<u32>(record); }

template <class T> T Read(const BYTE* p, size_t offset)
{
    T v;
    memcpy(&v, p + offset, sizeof v);
    return v;
}

// Applies the update sequence array; false if the record is torn or not a FILE record.
bool Fixup(BYTE* rec, u32 recordSize)
{
    if (Read<u32>(rec, 0) != 0x454C4946) return false; // "FILE"
    const u16 usaOffset = Read<u16>(rec, 4), usaCount = Read<u16>(rec, 6);
    if (usaCount < 2 || usaOffset + usaCount * 2u > recordSize) return false;
    const u16 usn = Read<u16>(rec, usaOffset);
    for (u16 i = 1; i < usaCount; ++i)
    {
        const u32 end = i * 512u - 2;
        if (end + 2 > recordSize) break;
        if (Read<u16>(rec, end) != usn) return false;
        memcpy(rec + end, rec + usaOffset + i * 2, 2);
    }
    return true;
}
}

bool Scanner::ScanMft()
{
    ScanTree& tree = *m_tree;
    const std::wstring volume = L"\\\\.\\" + m_options.root.substr(0, 2);
    Handle vol(CreateFileW(volume.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
    if (!vol.valid()) return false;

    NTFS_VOLUME_DATA_BUFFER nv = {};
    DWORD got = 0;
    if (!DeviceIoControl(vol.h, FSCTL_GET_NTFS_VOLUME_DATA, nullptr, 0, &nv, sizeof nv, &got, nullptr)) return false;
    const u32 recordSize = nv.BytesPerFileRecordSegment;
    const u64 cluster = nv.BytesPerCluster;
    if (recordSize < 512 || recordSize > 4096 || !cluster) return false;
    const u64 records = static_cast<u64>(nv.MftValidDataLength.QuadPart) / recordSize;
    if (records < FirstUserRecord || records > 0xF0000000ull) return false;

    // Where $MFT itself lives on the volume (it can be fragmented).
    Handle mft(CreateFileW((m_options.root + L"$MFT::$DATA").c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    if (!mft.valid()) return false;
    struct Run { u64 vcn, lcn, clusters; };
    std::vector<Run> runs;
    {
        STARTING_VCN_INPUT_BUFFER in = {};
        std::vector<BYTE> out(64 * 1024);
        for (;;)
        {
            const BOOL ok = DeviceIoControl(mft.h, FSCTL_GET_RETRIEVAL_POINTERS, &in, sizeof in, out.data(),
                static_cast<DWORD>(out.size()), &got, nullptr);
            const DWORD err = ok ? ERROR_SUCCESS : GetLastError();
            if (!ok && err != ERROR_MORE_DATA) break;
            const auto* rp = reinterpret_cast<const RETRIEVAL_POINTERS_BUFFER*>(out.data());
            u64 vcn = static_cast<u64>(rp->StartingVcn.QuadPart);
            for (DWORD i = 0; i < rp->ExtentCount; ++i)
            {
                const u64 next = static_cast<u64>(rp->Extents[i].NextVcn.QuadPart);
                runs.push_back({ vcn, static_cast<u64>(rp->Extents[i].Lcn.QuadPart), next - vcn });
                vcn = next;
            }
            if (ok) break;
            in.StartingVcn.QuadPart = static_cast<LONGLONG>(vcn);
        }
    }
    if (runs.empty()) return false;

    // One node per MFT record: unused records stay empty and unlinked.
    tree.ReserveNodes(static_cast<u32>(records) - 1); // node 0 (root) already exists
    std::vector<u64> parentOf(records, ~0ull);
    std::vector<unsigned char> keep(records, 0);       // 1 = in-use base record with a usable name

    // Split the MFT into <= 4 MB read jobs that each know their first record number.
    struct Job { u64 offset; DWORD bytes; u64 firstRecord; };
    std::vector<Job> jobs;
    {
        constexpr u64 Chunk = 4 * MiB;
        u64 rec = 0;
        for (const Run& run : runs)
        {
            u64 runBytes = run.clusters * cluster, offset = run.lcn * cluster;
            while (runBytes && rec < records)
            {
                u64 want = std::min<u64>({ Chunk, runBytes, (records - rec) * recordSize });
                want = std::min<u64>(runBytes, (want + cluster - 1) / cluster * cluster); // unbuffered reads: whole clusters
                jobs.push_back({ offset, static_cast<DWORD>(want), rec });
                rec += want / recordSize;
                offset += want;
                runBytes -= want;
            }
        }
    }

    // Parses one record into its node. Extension records (base != 0) describe another file's node, so in
    // the parallel pass they are copied to `deferred` and applied afterwards on one thread.
    using Deferred = std::vector<std::pair<u64, std::vector<BYTE>>>;
    auto parse = [&](BYTE* rec, u64 recordIndex, ScanTree::NameCursor& cursor, Deferred* deferred, bool fixedUp)
    {
        if (!fixedUp && !Fixup(rec, recordSize)) return;
        const u16 flags = Read<u16>(rec, 0x16);
        if (!(flags & RecordInUse)) return;
        const u64 base = Read<u64>(rec, 0x20) & 0x0000FFFFFFFFFFFFull;
        if (base && deferred)
        {
            deferred->emplace_back(recordIndex, std::vector<BYTE>(rec, rec + recordSize));
            return;
        }
        const u64 target = base ? base : recordIndex;
        if (target >= records) return;
        Node& node = tree.At(NodeOf(target));
        if (!base && (flags & RecordIsDirectory)) node.flags |= NF_DIR;

        for (u32 a = Read<u16>(rec, 0x14); a + 16 <= recordSize;)
        {
            const u32 type = Read<u32>(rec, a);
            const u32 len = Read<u32>(rec, a + 4);
            if (type == AttrEnd || len < 16 || a + len > recordSize) break;
            const bool nonResident = rec[a + 8] != 0;
            const u8 nameLen = rec[a + 9];
            const u16 attrFlags = Read<u16>(rec, a + 0x0C);
            if (!nonResident && (type == AttrStandardInfo || type == AttrFileName))
            {
                const BYTE* v = rec + a + Read<u16>(rec, a + 0x14);
                if (type == AttrStandardInfo)
                {
                    node.writeTime = Read<u64>(v, 0x08);
                    node.accessTime = Read<u64>(v, 0x18);
                    node.attributes = Read<u32>(v, 0x20);
                }
                else if (v[0x41] != NamespaceDos && node.nameLen == 0)
                {
                    tree.SetName(node, cursor, reinterpret_cast<const wchar_t*>(v + 0x42), v[0x40]);
                    parentOf[target] = Read<u64>(v, 0) & 0x0000FFFFFFFFFFFFull;
                    keep[target] = 1;
                }
            }
            else if (type == AttrData && nameLen == 0) // unnamed $DATA = file contents
            {
                if (!nonResident)
                {
                    node.size = Read<u32>(rec, a + 0x10);
                    node.alloc = 0; // stored inside the MFT record (resident)
                }
                else if (Read<u64>(rec, a + 0x10) == 0) // first piece holds the sizes
                {
                    node.size = Read<u64>(rec, a + 0x30);
                    node.alloc = (attrFlags & (AttrFlagCompressed | AttrFlagSparse)) && len >= 0x48 ?
                        Read<u64>(rec, a + 0x40) : Read<u64>(rec, a + 0x28);
                }
            }
            else if (type == AttrReparse) node.flags |= NF_REPARSE;
            a += len;
        }
    };

    // Parallel pass: each worker reads its own chunk (unbuffered, overlapped -> deep NVMe queue) and parses it.
    Handle async(CreateFileW(volume.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_NO_BUFFERING | FILE_FLAG_OVERLAPPED, nullptr));
    if (!async.valid()) return false;
    std::atomic<size_t> nextJob{ 0 };
    std::atomic<bool> failed{ false };
    std::mutex deferredLock;
    Deferred deferred;
    const int workers = static_cast<int>(std::clamp(std::thread::hardware_concurrency(), 2u, 8u));
    std::vector<std::thread> pool;
    for (int w = 0; w < workers; ++w)
        pool.emplace_back([&] {
            BYTE* buffer = static_cast<BYTE*>(VirtualAlloc(nullptr, 4 * MiB, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
            HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            ScanTree::NameCursor cursor;
            Deferred local;
            while (buffer && done && !m_cancel && !failed)
            {
                const size_t j = nextJob.fetch_add(1);
                if (j >= jobs.size()) break;
                OVERLAPPED ov = {};
                ov.Offset = static_cast<DWORD>(jobs[j].offset);
                ov.OffsetHigh = static_cast<DWORD>(jobs[j].offset >> 32);
                ov.hEvent = done;
                DWORD read = 0;
                if ((!ReadFile(async.h, buffer, jobs[j].bytes, nullptr, &ov) && GetLastError() != ERROR_IO_PENDING) ||
                    !GetOverlappedResult(async.h, &ov, &read, TRUE))
                {
                    failed = true;
                    break;
                }
                u64 r = jobs[j].firstRecord;
                for (u32 pos = 0; pos + recordSize <= read && r < records; pos += recordSize, ++r)
                    parse(buffer + pos, r, cursor, &local, false);
                m_bytes.fetch_add(read, std::memory_order_relaxed);
            }
            if (!buffer || !done) failed = true;
            if (buffer) VirtualFree(buffer, 0, MEM_RELEASE);
            if (done) CloseHandle(done);
            std::lock_guard<std::mutex> guard(deferredLock);
            for (auto& d : local) deferred.push_back(std::move(d));
        });
    for (std::thread& t : pool) t.join();
    if (m_cancel || failed) return false;
    for (auto& [index, bytes] : deferred) parse(bytes.data(), index, tree.m_mainCursor, nullptr, true);

    // Link children to parents; skip metafiles and anything whose parent is gone.
    Node& root = tree.At(0);
    root.flags |= NF_DIR;
    for (u64 r = FirstUserRecord; r < records; ++r)
    {
        if (!keep[r] || r == RecordRoot) continue;
        const u64 parent = parentOf[r];
        if (parent >= records || (parent != RecordRoot && (!keep[parent] || parent < FirstUserRecord))) continue;
        const u32 n = NodeOf(r), p = NodeOf(parent);
        Node& node = tree.At(n);
        node.parent = p;
        node.nextSibling = tree.At(p).firstChild;
        tree.At(p).firstChild = n;
    }

    u64 files = 0, dirs = 0;
    for (const u32 i : tree.PostOrder())
    {
        if (i == 0) continue;
        Node& n = tree.At(i);
        if (n.flags & NF_DIR)
        {
            ++dirs;
            n.attributes |= FILE_ATTRIBUTE_DIRECTORY;
            if (n.flags & NF_REPARSE) n.firstChild = NoNode; // never follow junctions/mount points
            n.size = n.alloc = 0; // directory index blocks aren't counted by the normal scan either
        }
        else
        {
            ++files;
            n.files = 1;
            if (n.attributes & (FILE_ATTRIBUTE_OFFLINE | FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS)) n.flags |= NF_CLOUD;
        }
    }
    RollUp(tree);
    tree.fileCount = files;
    tree.dirCount = dirs;
    tree.usedMft = true;
    tree.threadsUsed = 1;
    m_files = files;
    m_dirs = dirs;
    return true;
}
}
