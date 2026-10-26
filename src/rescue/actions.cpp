// SpaceRescue - SPDX-License-Identifier: GPL-3.0-or-later

#include "actions.h"

namespace sr
{
namespace
{
bool FreeOf(const std::wstring& root, u64* freeBytes)
{
    ULARGE_INTEGER avail = {}, tot = {}, fr = {};
    if (!GetDiskFreeSpaceExW(root.c_str(), &avail, &tot, &fr)) return false;
    *freeBytes = avail.QuadPart;
    return true;
}

DWORD Attr(const std::wstring& p)
{
    return GetFileAttributesW(LongPath(p).c_str());
}


void MakeDirs(const std::wstring& dir)
{
    if (dir.size() <= 3 || Attr(dir) != INVALID_FILE_ATTRIBUTES) return;
    MakeDirs(ParentPath(dir));
    CreateDirectoryW(LongPath(dir).c_str(), nullptr);
}

// Total bytes and entries of a tree; fails on junctions/symlinks (never followed).
DWORD TreeStats(const std::wstring& path, u64* bytes, u64* entries)
{
    WIN32_FILE_ATTRIBUTE_DATA d = {};
    if (!GetFileAttributesExW(LongPath(path).c_str(), GetFileExInfoStandard, &d)) return GetLastError();
    ++*entries;
    if (!(d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
    {
        *bytes += (static_cast<u64>(d.nFileSizeHigh) << 32) | d.nFileSizeLow;
        return ERROR_SUCCESS;
    }
    if (d.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) return ERROR_NOT_SUPPORTED;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(LongPath(JoinPath(path, L"*")).c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
    {
        const DWORD e = GetLastError();
        return e == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : e;
    }
    DWORD result = ERROR_SUCCESS;
    do
    {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        result = TreeStats(JoinPath(path, fd.cFileName), bytes, entries);
    } while (result == ERROR_SUCCESS && FindNextFileW(h, &fd));
    FindClose(h);
    return result;
}

DWORD CopyTree(const std::wstring& from, const std::wstring& to)
{
    const DWORD a = Attr(from);
    if (a == INVALID_FILE_ATTRIBUTES) return GetLastError();
    if (!(a & FILE_ATTRIBUTE_DIRECTORY))
    {
        MakeDirs(ParentPath(to));
        if (!CopyFileExW(LongPath(from).c_str(), LongPath(to).c_str(), nullptr, nullptr, nullptr, COPY_FILE_FAIL_IF_EXISTS))
            return GetLastError();
        return ERROR_SUCCESS;
    }
    if (a & FILE_ATTRIBUTE_REPARSE_POINT) return ERROR_NOT_SUPPORTED;
    MakeDirs(to);
    if (Attr(to) == INVALID_FILE_ATTRIBUTES) return GetLastError();
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(LongPath(JoinPath(from, L"*")).c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
    {
        const DWORD e = GetLastError();
        return e == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : e;
    }
    DWORD result = ERROR_SUCCESS;
    do
    {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        result = CopyTree(JoinPath(from, fd.cFileName), JoinPath(to, fd.cFileName));
    } while (result == ERROR_SUCCESS && FindNextFileW(h, &fd));
    FindClose(h);
    return result;
}

DWORD DeleteTree(const std::wstring& path)
{
    const std::wstring lp = LongPath(path);
    const DWORD a = GetFileAttributesW(lp.c_str());
    if (a == INVALID_FILE_ATTRIBUTES) return GetLastError() == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : GetLastError();
    if (a & FILE_ATTRIBUTE_READONLY) SetFileAttributesW(lp.c_str(), a & ~FILE_ATTRIBUTE_READONLY);
    if (!(a & FILE_ATTRIBUTE_DIRECTORY)) return DeleteFileW(lp.c_str()) ? ERROR_SUCCESS : GetLastError();
    if (!(a & FILE_ATTRIBUTE_REPARSE_POINT))
    {
        // Real folder: empty it first. Links are removed, never followed.
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(LongPath(JoinPath(path, L"*")).c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE)
        {
            DWORD first = ERROR_SUCCESS;
            do
            {
                if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
                const DWORD e = DeleteTree(JoinPath(path, fd.cFileName));
                if (e != ERROR_SUCCESS && first == ERROR_SUCCESS) first = e;
            } while (FindNextFileW(h, &fd));
            FindClose(h);
            if (first != ERROR_SUCCESS) return first;
        }
    }
    return RemoveDirectoryW(lp.c_str()) ? ERROR_SUCCESS : GetLastError();
}

// Copy + verify (bytes and entry count) + delete the source. Partial copies are cleaned up.
DWORD MoveVerified(const std::wstring& from, const std::wstring& to)
{
    if (Attr(to) != INVALID_FILE_ATTRIBUTES) return ERROR_ALREADY_EXISTS;
    u64 sb = 0, se = 0;
    DWORD e = TreeStats(from, &sb, &se);
    if (e != ERROR_SUCCESS) return e;
    u64 freeBytes = 0;
    if (FreeOf(VolumeOf(to), &freeBytes) && freeBytes < sb + 16 * MiB) return ERROR_DISK_FULL;
    e = CopyTree(from, to);
    if (e == ERROR_SUCCESS)
    {
        u64 db = 0, de = 0;
        e = TreeStats(to, &db, &de);
        if (e == ERROR_SUCCESS && (db != sb || de != se)) e = ERROR_CRC;
    }
    if (e != ERROR_SUCCESS)
    {
        DeleteTree(to);
        return e;
    }
    return DeleteTree(from); // a failure here leaves a verified copy plus the source
}

DWORD SetCompressionOne(const std::wstring& path, USHORT format)
{
    const DWORD a = Attr(path);
    if (a == INVALID_FILE_ATTRIBUTES) return GetLastError();
    if (a & FILE_ATTRIBUTE_REPARSE_POINT && (a & FILE_ATTRIBUTE_DIRECTORY)) return ERROR_SUCCESS; // skip links
    const bool dir = (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
    Handle h(CreateFileW(LongPath(path).c_str(), GENERIC_READ | GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                         FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    if (!h.valid()) return GetLastError();
    DWORD got = 0;
    if (!DeviceIoControl(h.h, FSCTL_SET_COMPRESSION, &format, sizeof(format), nullptr, 0, &got, nullptr))
        return GetLastError();
    (void)dir;
    return ERROR_SUCCESS;
}

DWORD SetCompressionTree(const std::wstring& path, USHORT format)
{
    DWORD first = SetCompressionOne(path, format);
    const DWORD a = Attr(path);
    if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) && !(a & FILE_ATTRIBUTE_REPARSE_POINT))
    {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(LongPath(JoinPath(path, L"*")).c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE)
        {
            do
            {
                if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
                const DWORD e = SetCompressionTree(JoinPath(path, fd.cFileName), format);
                if (e != ERROR_SUCCESS && first == ERROR_SUCCESS) first = e;
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
    }
    return first;
}

std::string Utf8(const std::wstring& w)
{
    if (w.empty()) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

const wchar_t* KindName(ActionKind k)
{
    switch (k)
    {
    case ActionKind::Recycle: return L"Recycle";
    case ActionKind::Quarantine: return L"Quarantine";
    case ActionKind::Compress: return L"Compress";
    default: return L"Delete";
    }
}

// Reads a Recycle Bin $I metadata file: original path of the deleted item.
bool ReadRecycleInfo(const std::wstring& file, std::wstring* original)
{
    Handle h(CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!h.valid()) return false;
    std::vector<char> buf(8192);
    DWORD got = 0;
    if (!ReadFile(h.h, buf.data(), static_cast<DWORD>(buf.size()), &got, nullptr) || got < 28) return false;
    long long version;
    memcpy(&version, buf.data(), 8);
    size_t off;
    size_t maxChars;
    if (version == 2)
    {
        DWORD len;
        memcpy(&len, buf.data() + 24, 4);
        off = 28;
        maxChars = len;
    }
    else if (version == 1)
    {
        off = 24;
        maxChars = 260;
    }
    else
        return false;
    const size_t avail = (got - off) / 2;
    const wchar_t* w = reinterpret_cast<const wchar_t*>(buf.data() + off);
    size_t n = 0;
    while (n < avail && n < maxChars && w[n]) ++n;
    original->assign(w, n);
    return true;
}

// Restores `original` from the Recycle Bin of its volume by renaming the $R file back.
DWORD RestoreFromBin(const std::wstring& original)
{
    const std::wstring root = VolumeOf(original);
    if (root.empty()) return ERROR_PATH_NOT_FOUND;
    const std::wstring bin = root + L"$Recycle.Bin";
    WIN32_FIND_DATAW sid;
    HANDLE hs = FindFirstFileW((bin + L"\\*").c_str(), &sid);
    if (hs == INVALID_HANDLE_VALUE) return ERROR_ACCESS_DENIED;
    std::wstring bestR;
    u64 bestTime = 0;
    do
    {
        if (!(sid.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || sid.cFileName[0] == L'.') continue;
        const std::wstring dir = bin + L"\\" + sid.cFileName;
        WIN32_FIND_DATAW fd;
        HANDLE hf = FindFirstFileW((dir + L"\\$I*").c_str(), &fd);
        if (hf == INVALID_HANDLE_VALUE) continue;
        do
        {
            std::wstring orig;
            if (!ReadRecycleInfo(dir + L"\\" + fd.cFileName, &orig) || _wcsicmp(orig.c_str(), original.c_str()) != 0) continue;
            const std::wstring r = dir + L"\\$R" + (fd.cFileName + 2);
            if (GetFileAttributesW(r.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
            const u64 t = Ticks(fd.ftLastWriteTime); // newest deletion wins
            if (t >= bestTime)
            {
                bestTime = t;
                bestR = r;
            }
        } while (FindNextFileW(hf, &fd));
        FindClose(hf);
    } while (FindNextFileW(hs, &sid));
    FindClose(hs);
    if (bestR.empty()) return ERROR_FILE_NOT_FOUND;
    if (Attr(original) != INVALID_FILE_ATTRIBUTES) return ERROR_ALREADY_EXISTS;
    MakeDirs(ParentPath(original));
    // Same-volume rename: needs no free space. $I metadata is left for the shell to purge.
    if (!MoveFileExW(bestR.c_str(), LongPath(original).c_str(), 0)) return GetLastError();
    DeleteFileW((ParentPath(bestR) + L"\\$I" + (bestR.c_str() + ParentPath(bestR).size() + 3)).c_str());
    return ERROR_SUCCESS;
}

struct VolumeFree
{
    std::vector<std::pair<std::wstring, u64>> before;
    void Snapshot(const std::vector<std::wstring>& paths)
    {
        for (const auto& p : paths)
        {
            const std::wstring v = VolumeOf(p);
            bool seen = false;
            for (const auto& b : before) seen |= _wcsicmp(b.first.c_str(), v.c_str()) == 0;
            u64 f;
            if (!seen && !v.empty() && FreeOf(v, &f)) before.emplace_back(v, f);
        }
    }
    u64 Gained() const
    {
        u64 sum = 0;
        for (const auto& b : before)
        {
            u64 f;
            if (FreeOf(b.first, &f) && f > b.second) sum += f - b.second;
        }
        return sum;
    }
};

std::wstring ErrorOrCode(DWORD e) { return ErrorText(e); }
}

ActionManager& ActionManager::Get()
{
    static ActionManager instance;
    return instance;
}

void ActionManager::AppendLog(const Entry& entry, const int id)
{
    if (m_logPath.empty()) return;
    Handle h(CreateFileW(m_logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!h.valid()) return;
    for (const auto& m : entry.moves)
    {
        const std::string line = Utf8(Fmt(L"%d\t%s\t%s\t%s\r\n", id, KindName(entry.kind), m.first.c_str(), m.second.c_str()));
        DWORD w;
        WriteFile(h.h, line.data(), static_cast<DWORD>(line.size()), &w, nullptr);
    }
}

bool ActionManager::CanUndo(const int undoId) const
{
    return undoId >= 0 && static_cast<size_t>(undoId) < m_entries.size() && !m_entries[undoId].undone &&
           m_entries[undoId].kind != ActionKind::DeletePermanent;
}

ActionResult ActionManager::Apply(const ActionKind kind, const std::vector<std::wstring>& paths, const std::wstring& workVolume)
{
    ActionResult r;
    r.kind = kind;
    Entry entry;
    entry.kind = kind;
    VolumeFree vf;
    vf.Snapshot(paths);

    std::wstring quarantineBase;
    if (kind == ActionKind::Quarantine)
    {
        const std::wstring wv = VolumeOf(workVolume);
        if (wv.empty())
        {
            for (const auto& p : paths) r.failed.push_back({ p, L"No work volume available for quarantine." });
            r.summary = L"Nothing moved: no other drive is available for quarantine.";
            return r;
        }
        SYSTEMTIME st;
        GetLocalTime(&st);
        const std::wstring root = JoinPath(wv, L"SpaceRescue-Quarantine");
        quarantineBase = JoinPath(root, Fmt(L"%04d%02d%02d-%02d%02d%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond));
        MakeDirs(quarantineBase);
        m_logPath = JoinPath(root, L"undo-log.txt");
    }

    for (const std::wstring& path : paths)
    {
        DWORD e = ERROR_SUCCESS;
        std::wstring dest = path;
        switch (kind)
        {
        case ActionKind::Recycle:
        {
            std::wstring from = path;
            from.push_back(L'\0'); // double-NUL terminated list
            SHFILEOPSTRUCTW op = {};
            op.wFunc = FO_DELETE;
            op.pFrom = from.c_str();
            // FOF_WANTNUKEWARNING: if the item is too big for the bin, Windows asks before
            // destroying it instead of silently deleting it permanently.
            op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI | 0x4000 /* FOF_WANTNUKEWARNING */;
            const int rc = SHFileOperationW(&op);
            if (rc != 0) e = static_cast<DWORD>(rc);
            else if (op.fAnyOperationsAborted) e = ERROR_CANCELLED;
            else if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) e = ERROR_SHARING_VIOLATION;
            break;
        }
        case ActionKind::Quarantine:
        {
            const std::wstring tail = path.size() > 3 ? path.substr(3) : std::wstring();
            dest = JoinPath(JoinPath(quarantineBase, std::wstring(1, VolumeOf(path)[0])), tail);
            e = MoveVerified(path, dest);
            break;
        }
        case ActionKind::Compress:
            e = SetCompressionTree(path, COMPRESSION_FORMAT_DEFAULT);
            break;
        case ActionKind::DeletePermanent:
            e = DeleteTree(path);
            break;
        }
        if (e == ERROR_SUCCESS)
        {
            r.done.push_back(path);
            entry.moves.emplace_back(path, dest);
        }
        else
            r.failed.push_back({ path, ErrorOrCode(e) });
    }

    r.bytesFreed = vf.Gained();
    const std::wstring n = FormatCount(r.done.size());
    switch (kind)
    {
    case ActionKind::Recycle:
        r.summary = Fmt(L"Moved %s item(s) to the Recycle Bin. Space is only freed when the Recycle Bin is emptied.", n.c_str());
        break;
    case ActionKind::Quarantine:
        r.summary = Fmt(L"Moved %s item(s) to quarantine on %s, freeing %s.", n.c_str(), VolumeOf(workVolume).c_str(), FormatBytes(r.bytesFreed).c_str());
        break;
    case ActionKind::Compress:
        r.summary = Fmt(L"Compressed %s item(s) with NTFS compression, saving %s.", n.c_str(), FormatBytes(r.bytesFreed).c_str());
        break;
    case ActionKind::DeletePermanent:
        r.summary = Fmt(L"Permanently deleted %s item(s), freeing %s. This cannot be undone.", n.c_str(), FormatBytes(r.bytesFreed).c_str());
        break;
    }
    if (!r.failed.empty()) r.summary += Fmt(L" %s item(s) failed.", FormatCount(r.failed.size()).c_str());

    if (kind != ActionKind::DeletePermanent && !entry.moves.empty())
    {
        r.undoId = static_cast<int>(m_entries.size());
        m_entries.push_back(entry);
        AppendLog(entry, r.undoId);
        if (kind == ActionKind::Recycle) r.summary += L" Undo restores them from the Recycle Bin.";
    }
    return r;
}

ActionResult ActionManager::Undo(const int undoId)
{
    ActionResult r;
    if (!CanUndo(undoId))
    {
        r.summary = L"Nothing to undo.";
        return r;
    }
    Entry& entry = m_entries[undoId];
    r.kind = entry.kind;
    VolumeFree vf;
    std::vector<std::wstring> srcs;
    for (const auto& m : entry.moves) srcs.push_back(entry.kind == ActionKind::Quarantine ? m.second : m.first);
    // Space is consumed by an undo, so measure the change on the volume being written to.
    std::vector<std::wstring> targets;
    for (const auto& m : entry.moves) targets.push_back(m.first);
    vf.Snapshot(targets);

    for (const auto& m : entry.moves)
    {
        DWORD e = ERROR_SUCCESS;
        switch (entry.kind)
        {
        case ActionKind::Recycle: e = RestoreFromBin(m.first); break;
        case ActionKind::Quarantine: e = MoveVerified(m.second, m.first); break;
        case ActionKind::Compress: e = SetCompressionTree(m.first, COMPRESSION_FORMAT_NONE); break;
        default: e = ERROR_NOT_SUPPORTED;
        }
        if (e == ERROR_SUCCESS) r.done.push_back(m.first);
        else r.failed.push_back({ m.first, ErrorOrCode(e) });
    }
    entry.undone = r.failed.empty();
    // Drop restored items so a second undo only retries the failures.
    if (!r.failed.empty())
    {
        std::vector<std::pair<std::wstring, std::wstring>> left;
        for (const auto& m : entry.moves)
            for (const auto& f : r.failed)
                if (f.path == m.first) left.push_back(m);
        entry.moves = left;
    }
    r.undoId = -1;
    r.summary = Fmt(L"Restored %s item(s).", FormatCount(r.done.size()).c_str());
    if (!r.failed.empty()) r.summary += Fmt(L" %s item(s) could not be restored.", FormatCount(r.failed.size()).c_str());
    return r;
}
}
