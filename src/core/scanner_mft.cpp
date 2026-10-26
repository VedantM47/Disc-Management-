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
    ScanTree::NameCursor& cursor = tree.m_mainCursor;

    constexpr u64 Chunk = 4 * MiB;
    std::vector<BYTE> buffer(Chunk);
    u64 recordIndex = 0;
    u64 bytesSeen = 0;
    for (const Run& run : runs)
    {
        u64 runBytes = run.clusters * cluster;
        u64 offset = run.lcn * cluster;
        while (runBytes && recordIndex < records && !m_cancel)
        {
            const DWORD want = static_cast<DWORD>(std::min<u64>(Chunk, runBytes));
            OVERLAPPED ov = {};
            ov.Offset = static_cast<DWORD>(offset);
            ov.OffsetHigh = static_cast<DWORD>(offset >> 32);
            DWORD read = 0;
            if (!ReadFile(vol.h, buffer.data(), want, &read, &ov) || read == 0) return false;
            for (u32 pos = 0; pos + recordSize <= read && recordIndex < records; pos += recordSize, ++recordIndex)
            {
                BYTE* rec = buffer.data() + pos;
                if (!Fixup(rec, recordSize)) continue;
                const u16 flags = Read<u16>(rec, 0x16);
                if (!(flags & RecordInUse)) continue;
                const u64 base = Read<u64>(rec, 0x20) & 0x0000FFFFFFFFFFFFull;
                const u64 target = base ? base : recordIndex;
                if (target >= records) continue;
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
                            const u8 chars = v[0x40];
                            std::wstring name(reinterpret_cast<const wchar_t*>(v + 0x42), chars);
                            tree.SetName(node, cursor, name.c_str(), name.size());
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
            }
            bytesSeen += read;
            offset += read;
            runBytes -= read;
            m_bytes.store(bytesSeen, std::memory_order_relaxed);
        }
    }
    if (m_cancel) return false;

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
