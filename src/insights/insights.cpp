// SpaceRescue - SPDX-License-Identifier: GPL-3.0-or-later

#include "insights.h"

#include <map>
#include <set>
#include <unordered_set>

namespace sr
{
namespace
{
const size_t kMaxListNodes = 5000;
const size_t kMaxGroups = 200;
const size_t kMaxFilesPerGroup = 50;

std::wstring TrimSlash(std::wstring s)
{
    while (s.size() > 3 && s.back() == L'\\') s.pop_back();
    return s;
}

bool StartsWithDir(const std::wstring& path, const std::wstring& dir)
{
    if (dir.empty() || path.size() < dir.size() || path.compare(0, dir.size(), dir) != 0) return false;
    return path.size() == dir.size() || path[dir.size()] == L'\\' || dir.back() == L'\\';
}

std::wstring ResolveLong(const std::wstring& p)
{
    wchar_t buf[MAX_PATH * 2] = {};
    const DWORD n = GetLongPathNameW(p.c_str(), buf, MAX_PATH * 2);
    return (n > 0 && n < MAX_PATH * 2) ? std::wstring(buf, n) : p;
}

std::wstring ExtOf(const std::wstring& name)
{
    const size_t dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos || dot + 1 >= name.size()) return std::wstring();
    return ToLower(name.substr(dot));
}

bool InList(const std::wstring& v, const wchar_t* const* list, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        if (v == list[i]) return true;
    return false;
}
#define IN_LIST(v, arr) InList(v, arr, sizeof(arr) / sizeof(arr[0]))

const wchar_t* kVideoExt[] = { L".mp4", L".mkv", L".mov", L".avi", L".wmv", L".flv", L".webm", L".m4v", L".mpg", L".mpeg", L".ts", L".vob" };
const wchar_t* kInstallerExt[] = { L".msi", L".iso", L".msix", L".msixbundle", L".appx" };
const wchar_t* kDevNames[] = { L"node_modules", L".gradle", L".m2", L"__pycache__", L"npm-cache", L".cache" };
const wchar_t* kDevSuffixes[] = { L"\\.nuget\\packages", L"\\pip\\cache", L"\\yarn\\cache" };

struct ExplanationEntry { Category c; const wchar_t* title; const wchar_t* text; bool reclaimable; };
const ExplanationEntry kCategories[] = {
    { Category::OldDownloads, L"Old downloads", L"Files in your Downloads folder you have not touched for a while. Review before deleting - some may be the only copy.", false },
    { Category::Installers, L"Installers and disk images", L"Setup programs and disk images (.msi, .iso, installer .exe). Usually safe to remove once the program is installed.", false },
    { Category::DevCaches, L"Developer caches", L"Package and build caches (node_modules, .gradle, .m2, pip, npm, NuGet). Safe to delete; tools download them again when needed.", true },
    { Category::TempFiles, L"Temporary files", L"Scratch files left by programs in the Temp folders. Safe to delete; files in use are skipped.", true },
    { Category::BrowserCaches, L"Browser caches", L"Cached web pages and images from Chrome, Edge and Firefox. Safe to delete; pages load a little slower once.", true },
    { Category::WindowsUpdate, L"Windows Update leftovers", L"Downloaded update packages. Best removed with Disk Cleanup (cleanmgr) so Windows stays consistent.", true },
    { Category::Videos, L"Videos", L"Video files are usually the biggest personal files. Only you know which ones to keep.", false },
    { Category::RecycleBin, L"Recycle Bin", L"Files you already deleted but that still occupy space. Empty the Recycle Bin to get it back.", true },
    { Category::Duplicates, L"Duplicate files", L"Identical copies of the same file. Keep one copy of each and remove the others.", false },
};

const ExplanationEntry& Info(Category c) { return kCategories[static_cast<int>(c)]; }

struct Candidate { u32 node; u64 size; };

struct Ctx
{
    const ScanTree& t;
    InsightsReport& rep;
    int downloadsDays;
    u64 now;
    std::wstring profile, downloads, desktop, windir, programFiles;
    std::vector<std::wstring> tempDirs;
    std::wstring wuDir;
    bool useAccess;
    std::vector<Candidate> topFiles;
    Ctx(const ScanTree& tree, InsightsReport& r) : t(tree), rep(r) {}
};

void AddNode(Ctx& c, Category cat, u32 node, u64 bytes, u64 count)
{
    CategoryResult& r = c.rep.categories[static_cast<int>(cat)];
    r.bytes += bytes;
    r.count += count;
    r.nodes.push_back(node);
}

// Which category (if any) claims this folder as a whole.
int ClassifyDir(const Ctx& c, const std::wstring& lp, const std::wstring& ln, u32 parent)
{
    if (ln == L"$recycle.bin" && parent == c.t.Root()) return static_cast<int>(Category::RecycleBin);
    for (const auto& td : c.tempDirs)
        if (lp == td) return static_cast<int>(Category::TempFiles);
    if (!c.wuDir.empty() && lp == c.wuDir) return static_cast<int>(Category::WindowsUpdate);
    if (ln == L"cache" && (lp.find(L"\\google\\chrome\\user data\\") != std::wstring::npos ||
                           lp.find(L"\\microsoft\\edge\\user data\\") != std::wstring::npos))
        return static_cast<int>(Category::BrowserCaches);
    if (ln == L"cache2" && lp.find(L"mozilla\\firefox\\profiles\\") != std::wstring::npos)
        return static_cast<int>(Category::BrowserCaches);
    if (IN_LIST(ln, kDevNames)) return static_cast<int>(Category::DevCaches);
    for (const wchar_t* s : kDevSuffixes)
        if (EndsWithI(lp, s)) return static_cast<int>(Category::DevCaches);
    return -1;
}

void AddStale(Ctx& c, const Node& n, u32 idx, StaleGroup* local, bool aggregate)
{
    const u64 t = c.useAccess ? n.accessTime : n.writeTime;
    if (t == 0 || t > c.now) return;
    const double days = static_cast<double>(c.now - t) / static_cast<double>(TicksPerDay);
    StaleResult& s = c.rep.stale;
    for (int b = 0; b < StaleResult::Buckets; ++b)
    {
        if (days < s.months[b] * 30.4375) continue;
        s.bytes[b] += n.size;
        s.count[b] += aggregate ? n.files : 1;
        local[b].bytes += n.size; // StaleGroup used as accumulator
        local[b].count += aggregate ? n.files : 1;
        if (!aggregate) local[b].files.push_back(idx);
    }
}

void Prune(std::vector<StaleGroup>& g, size_t keep)
{
    std::sort(g.begin(), g.end(), [](const StaleGroup& a, const StaleGroup& b) { return a.bytes > b.bytes; });
    if (g.size() > keep) g.resize(keep);
}

void Visit(Ctx& c, u32 dir, const std::wstring& lp, int claimed, bool skipStale)
{
    const ScanTree& t = c.t;
    StaleGroup local[StaleResult::Buckets];
    const bool inDownloads = StartsWithDir(lp, c.downloads);
    const bool inProfile = StartsWithDir(lp, c.profile);
    const bool inAppData = inProfile && lp.find(L"\\appdata\\") != std::wstring::npos;
    const bool inDesktopOrDownloads = inDownloads || StartsWithDir(lp, c.desktop);

    for (u32 ch = t.At(dir).firstChild; ch != NoNode; ch = t.At(ch).nextSibling)
    {
        const Node& n = t.At(ch);
        if (n.flags & NF_DIR)
        {
            if (n.flags & NF_REPARSE) continue;
            const std::wstring ln = ToLower(t.Name(ch));
            const std::wstring clp = lp.empty() || lp.back() == L'\\' ? lp + ln : lp + L'\\' + ln;
            int cl = claimed;
            if (claimed < 0)
            {
                cl = ClassifyDir(c, clp, ln, dir);
                if (cl >= 0) AddNode(c, static_cast<Category>(cl), ch, n.size, n.files);
            }
            const bool skip = skipStale || (!c.windir.empty() && clp == c.windir) || clp == c.programFiles ||
                              ln == L"$recycle.bin" || ln == L"system volume information";
            Visit(c, ch, clp, cl, skip);
            continue;
        }
        if (n.flags & NF_CLOUD) continue;
        const bool aggregate = (n.flags & NF_AGGREGATE) != 0;
        if (!skipStale) AddStale(c, n, ch, local, aggregate);
        if (aggregate) continue;

        // Ten largest files.
        if (c.topFiles.size() < 10 || n.size > c.topFiles.back().size)
        {
            c.topFiles.push_back({ ch, n.size });
            std::sort(c.topFiles.begin(), c.topFiles.end(), [](const Candidate& a, const Candidate& b) { return a.size > b.size; });
            if (c.topFiles.size() > 10) c.topFiles.pop_back();
        }
        if (claimed >= 0) continue;

        const std::wstring name = t.Name(ch);
        const std::wstring ext = ExtOf(name);
        if (IN_LIST(ext, kVideoExt) && !(ext == L".ts" && n.size < 50 * MiB)) // .ts is also TypeScript
        {
            AddNode(c, Category::Videos, ch, n.size, 1);
            continue;
        }
        if (inProfile && !inAppData)
        {
            bool installer = IN_LIST(ext, kInstallerExt);
            if (ext == L".exe" && inDesktopOrDownloads) installer = true;
            if (!installer && ext == L".exe")
            {
                const std::wstring ln = ToLower(name);
                installer = ln.find(L"setup") != std::wstring::npos || ln.find(L"install") != std::wstring::npos;
            }
            if (installer)
            {
                AddNode(c, Category::Installers, ch, n.size, 1);
                continue;
            }
        }
        if (inDownloads && n.writeTime && n.writeTime < c.now &&
            (c.now - n.writeTime) / TicksPerDay >= static_cast<u64>(c.downloadsDays))
            AddNode(c, Category::OldDownloads, ch, n.size, 1);
    }

    for (int b = 0; b < StaleResult::Buckets; ++b)
    {
        if (!local[b].count) continue;
        StaleGroup g;
        g.folder = dir;
        g.bytes = local[b].bytes;
        g.count = local[b].count;
        g.files = local[b].files;
        if (g.files.size() > 1)
            std::sort(g.files.begin(), g.files.end(), [&](u32 x, u32 y) { return t.At(x).size > t.At(y).size; });
        if (g.files.size() > kMaxFilesPerGroup) g.files.resize(kMaxFilesPerGroup);
        auto& vec = c.rep.stale.groups[b];
        vec.push_back(std::move(g));
        if (vec.size() > kMaxGroups * 20) Prune(vec, kMaxGroups);
    }
}

// --- Explanations for hogs ---------------------------------------------------------

const wchar_t* KindOfExt(const std::wstring& ext)
{
    static const wchar_t* images[] = { L".jpg", L".jpeg", L".png", L".gif", L".bmp", L".tif", L".tiff", L".webp", L".heic", L".raw" };
    static const wchar_t* archives[] = { L".zip", L".7z", L".rar", L".gz", L".tar", L".cab" };
    static const wchar_t* audio[] = { L".mp3", L".flac", L".wav", L".aac", L".m4a", L".ogg" };
    static const wchar_t* code[] = { L".dll", L".exe", L".sys", L".lib", L".obj", L".pdb" };
    static const wchar_t* docs[] = { L".pdf", L".docx", L".doc", L".xlsx", L".pptx" };
    static const wchar_t* disks[] = { L".iso", L".vhd", L".vhdx", L".vmdk", L".img" };
    if (IN_LIST(ext, kVideoExt)) return L"Video files";
    if (IN_LIST(ext, images)) return L"Pictures";
    if (IN_LIST(ext, archives)) return L"Compressed archives";
    if (IN_LIST(ext, audio)) return L"Music and audio";
    if (IN_LIST(ext, code)) return L"Program and library files";
    if (IN_LIST(ext, docs)) return L"Documents";
    if (IN_LIST(ext, disks)) return L"Disk images";
    if (ext == L".msi" || ext == L".msp") return L"Installer packages";
    if (ext == L".log" || ext == L".etl") return L"Log files";
    return nullptr;
}

void ExtSums(const ScanTree& t, u32 dir, std::map<std::wstring, u64>& sums, int budget[1])
{
    for (u32 ch = t.At(dir).firstChild; ch != NoNode; ch = t.At(ch).nextSibling)
    {
        const Node& n = t.At(ch);
        if (n.flags & NF_DIR)
        {
            if (!(n.flags & NF_REPARSE)) ExtSums(t, ch, sums, budget);
        }
        else if (n.flags & NF_AGGREGATE)
            sums[L"(small files)"] += n.size;
        else
            sums[ExtOf(t.Name(ch))] += n.size;
    }
}

std::wstring KnownFolderNote(const std::wstring& ln)
{
    struct K { const wchar_t* n; const wchar_t* t; };
    static const K known[] = {
        { L"windows", L"Windows itself - do not delete by hand; use Disk Cleanup" },
        { L"winsxs", L"Windows component store - managed by Windows (DISM cleanup)" },
        { L"program files", L"Installed programs" },
        { L"program files (x86)", L"Installed 32-bit programs" },
        { L"programdata", L"Shared application data and caches" },
        { L"appdata", L"Per-user application settings and caches" },
        { L"$recycle.bin", L"Deleted files waiting in the Recycle Bin" },
        { L"system volume information", L"System Restore points and shadow copies" },
        { L"softwaredistribution", L"Windows Update downloads" },
        { L"installer", L"Windows Installer cache - do not delete" },
        { L"node_modules", L"JavaScript packages (safe to re-download)" },
        { L".gradle", L"Gradle build cache (safe to re-download)" },
        { L".m2", L"Maven repository cache (safe to re-download)" },
        { L"downloads", L"Your Downloads folder" },
        { L"onedrive", L"OneDrive synced files" },
        { L"temp", L"Temporary files" },
        { L"users", L"User profiles and their documents" },
        { L".git", L"Git repository history" },
    };
    for (const K& k : known)
        if (ln == k.n) return k.t;
    return std::wstring();
}

std::wstring ExplainFolder(const ScanTree& t, u32 node)
{
    std::wstring note = KnownFolderNote(ToLower(t.Name(node)));
    if (!note.empty()) return note;
    std::map<std::wstring, u64> sums;
    int budget[1] = { 0 };
    ExtSums(t, node, sums, budget);
    u64 total = 0, best = 0;
    std::wstring bestExt;
    for (const auto& kv : sums)
    {
        total += kv.second;
        if (kv.second > best)
        {
            best = kv.second;
            bestExt = kv.first;
        }
    }
    if (!total) return L"Empty or unreadable folder";
    const std::wstring share = FormatPercent(static_cast<double>(best) / static_cast<double>(total));
    if (bestExt == L"(small files)") return L"Many small files";
    const wchar_t* kind = KindOfExt(bestExt);
    if (kind) return Fmt(L"%s, mostly %s (%s of this folder)", kind, bestExt.c_str(), share.c_str());
    if (bestExt.empty()) return L"Mostly files without an extension";
    return Fmt(L"Mostly %s files (%s of this folder)", bestExt.c_str(), share.c_str());
}

std::wstring ExplainFile(const ScanTree& t, u32 node)
{
    const std::wstring ext = ExtOf(t.Name(node));
    const wchar_t* kind = KindOfExt(ext);
    const std::wstring when = FormatAge(t.At(node).writeTime);
    if (kind) return Fmt(L"%s (%s), last changed %s", kind, ext.c_str(), when.c_str());
    if (ext.empty()) return Fmt(L"File without an extension, last changed %s", when.c_str());
    return Fmt(L"%s file, last changed %s", ext.c_str(), when.c_str());
}

void PickTopFolders(const ScanTree& t, std::vector<Hog>& out)
{
    auto size = [&](u32 n) { return t.At(n).size; };
    auto dirKids = [&](u32 n) {
        std::vector<u32> v;
        for (u32 ch = t.At(n).firstChild; ch != NoNode; ch = t.At(ch).nextSibling)
            if ((t.At(ch).flags & NF_DIR) && !(t.At(ch).flags & NF_REPARSE)) v.push_back(ch);
        std::sort(v.begin(), v.end(), [&](u32 a, u32 b) { return size(a) > size(b); });
        return v;
    };
    std::vector<u32> picks;
    std::vector<u32> cands = dirKids(t.Root());
    // Prefer the deepest folder that still holds at least half of its parent's bytes.
    auto descend = [&](u32 n) {
        for (;;)
        {
            const std::vector<u32> k = dirKids(n);
            if (!k.empty() && size(k[0]) * 2 >= size(n) && size(k[0]) > 0) n = k[0];
            else return n;
        }
    };
    size_t next = 0;
    while (picks.size() < 10)
    {
        if (next < cands.size())
        {
            if (size(cands[next]) > 0) picks.push_back(descend(cands[next]));
            ++next;
            continue;
        }
        // Out of candidates: split the largest pick that has subfolders.
        int idx = -1;
        for (size_t i = 0; i < picks.size(); ++i)
            if (!dirKids(picks[i]).empty() && (idx < 0 || size(picks[i]) > size(picks[idx]))) idx = static_cast<int>(i);
        if (idx < 0) break;
        cands = dirKids(picks[idx]);
        picks.erase(picks.begin() + idx);
        next = 0;
        if (cands.empty()) break;
    }
    std::sort(picks.begin(), picks.end(), [&](u32 a, u32 b) { return size(a) > size(b); });
    for (u32 p : picks)
    {
        if (out.size() >= 10) break;
        Hog h;
        h.node = p;
        h.explanation = ExplainFolder(t, p);
        out.push_back(h);
    }
}

// --- Snapshots ---------------------------------------------------------------------

u64 Fnv1a(const void* data, size_t len, u64 h = 14695981039346656037ull)
{
    const unsigned char* p = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < len; ++i)
    {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

std::wstring SnapshotFile(const ScanTree& tree, const std::wstring& dir)
{
    std::wstring root = TrimSlash(tree.rootPath);
    std::wstring id;
    if (root.size() == 2 && root[1] == L':') id = std::wstring(1, static_cast<wchar_t>(towupper(root[0])));
    else
    {
        const std::wstring low = ToLower(root);
        id = Fmt(L"%016llx", Fnv1a(low.data(), low.size() * sizeof(wchar_t)));
    }
    return JoinPath(dir, L"snapshot-" + id + L".txt");
}

void CollectFolders(const ScanTree& t, u32 dir, const std::wstring& path, int depth, std::vector<std::pair<std::wstring, u64>>& out)
{
    out.emplace_back(path, t.At(dir).size);
    if (depth >= 3) return;
    for (u32 ch = t.At(dir).firstChild; ch != NoNode; ch = t.At(ch).nextSibling)
    {
        const Node& n = t.At(ch);
        if ((n.flags & NF_DIR) && !(n.flags & NF_REPARSE)) CollectFolders(t, ch, JoinPath(path, t.Name(ch)), depth + 1, out);
    }
}

std::string Utf8(const std::wstring& w)
{
    if (w.empty()) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring FromUtf8(const char* s, size_t len)
{
    if (!len) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s, static_cast<int>(len), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, static_cast<int>(len), &w[0], n);
    return w;
}

bool ReadFileBytes(const std::wstring& path, std::string* out)
{
    Handle h(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!h.valid()) return false;
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(h.h, &sz) || sz.QuadPart > 256 * 1024 * 1024) return false;
    out->resize(static_cast<size_t>(sz.QuadPart));
    size_t done = 0;
    while (done < out->size())
    {
        DWORD got = 0;
        if (!ReadFile(h.h, &(*out)[done], static_cast<DWORD>(std::min<size_t>(out->size() - done, 1 << 20)), &got, nullptr) || !got) break;
        done += got;
    }
    out->resize(done);
    return true;
}

// Loads previous snapshot: ticks + map path(lower) -> size.
bool LoadSnapshot(const std::wstring& file, u64* ticks, std::map<std::wstring, u64>* sizes)
{
    std::string data;
    if (!ReadFileBytes(file, &data) || data.empty()) return false;
    size_t pos = 0;
    bool first = true;
    while (pos < data.size())
    {
        size_t eol = data.find('\n', pos);
        if (eol == std::string::npos) eol = data.size();
        size_t e2 = eol;
        if (e2 > pos && data[e2 - 1] == '\r') --e2;
        if (first)
        {
            *ticks = strtoull(data.substr(pos, e2 - pos).c_str(), nullptr, 10);
            first = false;
        }
        else
        {
            const size_t tab = data.find('\t', pos);
            if (tab != std::string::npos && tab < e2)
                (*sizes)[ToLower(FromUtf8(data.data() + tab + 1, e2 - tab - 1))] = strtoull(data.substr(pos, tab - pos).c_str(), nullptr, 10);
        }
        pos = eol + 1;
    }
    return !first;
}

// --- Duplicates --------------------------------------------------------------------

struct FileList
{
    std::vector<u32> files;
};

void CollectFiles(const ScanTree& t, u32 dir, u64 minSize, std::unordered_map<u64, std::vector<u32>>& bySize, volatile bool* cancel)
{
    for (u32 ch = t.At(dir).firstChild; ch != NoNode; ch = t.At(ch).nextSibling)
    {
        if (cancel && *cancel) return;
        const Node& n = t.At(ch);
        if (n.flags & NF_DIR)
        {
            if (!(n.flags & NF_REPARSE)) CollectFiles(t, ch, minSize, bySize, cancel);
        }
        else if (!(n.flags & (NF_AGGREGATE | NF_CLOUD | NF_REPARSE)) && n.size >= minSize &&
                 !(n.attributes & (FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS | FILE_ATTRIBUTE_RECALL_ON_OPEN)))
            bySize[n.size].push_back(ch);
    }
}

// Hashes a file (or its first+last 64 KB when `partial`). Returns false on any error.
bool HashFile(const std::wstring& path, u64 size, bool partial, u64* hash, volatile bool* cancel)
{
    Handle h(CreateFileW(LongPath(path).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (!h.valid()) return false;
    std::vector<char> buf(1 << 20);
    u64 v = Fnv1a(&size, sizeof(size));
    auto readAt = [&](u64 off, u64 len) {
        LARGE_INTEGER p;
        p.QuadPart = static_cast<LONGLONG>(off);
        if (!SetFilePointerEx(h.h, p, nullptr, FILE_BEGIN)) return false;
        while (len)
        {
            if (cancel && *cancel) return false;
            DWORD want = static_cast<DWORD>(std::min<u64>(len, buf.size())), got = 0;
            if (!ReadFile(h.h, buf.data(), want, &got, nullptr) || got != want) return false;
            v = Fnv1a(buf.data(), got, v);
            len -= got;
        }
        return true;
    };
    const u64 piece = 64 * KiB;
    if (partial)
    {
        if (size <= 2 * piece) { if (!readAt(0, size)) return false; }
        else if (!readAt(0, piece) || !readAt(size - piece, piece)) return false;
    }
    else if (!readAt(0, size))
        return false;
    *hash = v;
    return true;
}
}

std::wstring CategoryTitle(const Category c)
{
    return c == Category::Count ? std::wstring() : std::wstring(Info(c).title);
}

bool LastAccessUpdatesEnabled(std::wstring* reason)
{
    HKEY key = nullptr;
    DWORD value = 0, size = sizeof(value), type = 0;
    bool found = false;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\FileSystem", 0, KEY_READ, &key) == ERROR_SUCCESS)
    {
        found = RegQueryValueExW(key, L"NtfsDisableLastAccessUpdate", nullptr, &type, reinterpret_cast<BYTE*>(&value), &size) == ERROR_SUCCESS &&
                type == REG_DWORD;
        RegCloseKey(key);
    }
    std::wstring r;
    bool enabled;
    if (!found)
    {
        // Windows 10/11 default: system-managed, which turns updates off on large volumes.
        enabled = false;
        r = L"NTFS last-access updates are managed by Windows (not set explicitly) and are normally off on large drives, so last-modified dates are used";
    }
    else if (value & 0x80000000u)
    {
        enabled = (value & 1) == 0;
        r = enabled ? L"NTFS last-access updates are on (system-managed), so last-access dates are used"
                    : L"NTFS last-access updates are off (system-managed), so last-modified dates are used";
    }
    else
    {
        enabled = (value & 1) == 0;
        r = enabled ? L"NTFS last-access updates are enabled on this PC, so last-access dates are used"
                    : L"NTFS last-access updates are disabled on this PC, so last-modified dates are used";
    }
    if (reason) *reason = r;
    return enabled;
}

InsightsReport Analyze(const ScanTree& tree, const InsightsOptions& options)
{
    InsightsReport rep;
    rep.categories.resize(static_cast<size_t>(Category::Count));
    for (int i = 0; i < static_cast<int>(Category::Count); ++i)
    {
        CategoryResult& r = rep.categories[i];
        r.id = static_cast<Category>(i);
        r.title = kCategories[i].title;
        r.explanation = kCategories[i].text;
        r.reclaimable = kCategories[i].reclaimable;
    }

    Ctx c(tree, rep);
    c.downloadsDays = options.downloadsAgeDays;
    c.now = NowTicks();
    c.profile = ToLower(TrimSlash(ResolveLong(KnownFolder(CSIDL_PROFILE))));
    c.downloads = c.profile.empty() ? std::wstring() : c.profile + L"\\downloads";
    c.desktop = c.profile.empty() ? std::wstring() : c.profile + L"\\desktop";
    wchar_t buf[MAX_PATH * 2] = {};
    GetWindowsDirectoryW(buf, MAX_PATH);
    c.windir = ToLower(TrimSlash(buf));
    c.programFiles = ToLower(TrimSlash(KnownFolder(CSIDL_PROGRAM_FILES)));
    c.wuDir = c.windir + L"\\softwaredistribution\\download";
    c.tempDirs.push_back(c.windir + L"\\temp");
    if (GetTempPathW(MAX_PATH, buf)) c.tempDirs.push_back(ToLower(TrimSlash(ResolveLong(buf))));

    rep.stale.signal = LastAccessUpdatesEnabled(&rep.stale.signalReason) ? StaleSignal::LastAccess : StaleSignal::LastModified;
    c.useAccess = rep.stale.signal == StaleSignal::LastAccess;

    const std::wstring rootLower = ToLower(TrimSlash(tree.rootPath));
    Visit(c, tree.Root(), rootLower, -1, false);

    for (auto& cat : rep.categories)
    {
        std::sort(cat.nodes.begin(), cat.nodes.end(), [&](u32 a, u32 b) { return tree.At(a).size > tree.At(b).size; });
        if (cat.nodes.size() > kMaxListNodes) cat.nodes.resize(kMaxListNodes);
        if (cat.reclaimable) rep.reclaimable += cat.bytes;
    }
    for (int b = 0; b < StaleResult::Buckets; ++b) Prune(rep.stale.groups[b], kMaxGroups);

    PickTopFolders(tree, rep.topFolders);
    for (const Candidate& f : c.topFiles)
    {
        Hog h;
        h.node = f.node;
        h.explanation = ExplainFile(tree, f.node);
        rep.topFiles.push_back(h);
    }

    // Growth since the previous snapshot of this same root.
    if (!options.snapshotDir.empty())
    {
        u64 ticks = 0;
        std::map<std::wstring, u64> prev;
        if (LoadSnapshot(SnapshotFile(tree, options.snapshotDir), &ticks, &prev))
        {
            rep.hasSnapshot = true;
            rep.snapshotTicks = ticks;
            std::vector<std::pair<std::wstring, u64>> now;
            CollectFolders(tree, tree.Root(), TrimSlash(tree.rootPath), 0, now);
            for (const auto& kv : now)
            {
                auto it = prev.find(ToLower(kv.first));
                const long long delta = static_cast<long long>(kv.second) - (it == prev.end() ? 0ll : static_cast<long long>(it->second));
                if (delta > 0 && kv.first != TrimSlash(tree.rootPath))
                {
                    GrowthEntry g;
                    g.path = kv.first;
                    g.delta = delta;
                    g.now = kv.second;
                    rep.growth.push_back(g);
                }
            }
            std::sort(rep.growth.begin(), rep.growth.end(), [](const GrowthEntry& a, const GrowthEntry& b) { return a.delta > b.delta; });
            if (rep.growth.size() > 10) rep.growth.resize(10);
        }
    }
    return rep;
}

bool SaveSnapshot(const ScanTree& tree, const std::wstring& snapshotDir)
{
    if (snapshotDir.empty()) return false;
    CreateDirectoryW(snapshotDir.c_str(), nullptr);
    std::vector<std::pair<std::wstring, u64>> folders;
    CollectFolders(tree, tree.Root(), TrimSlash(tree.rootPath), 0, folders);
    std::string text = Utf8(Fmt(L"%llu\n", NowTicks()));
    for (const auto& f : folders) text += Utf8(Fmt(L"%llu\t", f.second) + f.first + L"\n");
    Handle h(CreateFileW(SnapshotFile(tree, snapshotDir).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!h.valid()) return false;
    DWORD w = 0;
    return WriteFile(h.h, text.data(), static_cast<DWORD>(text.size()), &w, nullptr) && w == text.size();
}

CategoryResult FindDuplicates(const ScanTree& tree, const u64 minSize, volatile bool* cancel)
{
    CategoryResult res;
    const ExplanationEntry& info = Info(Category::Duplicates);
    res.id = Category::Duplicates;
    res.title = info.title;
    res.explanation = info.text;
    res.reclaimable = false;

    std::unordered_map<u64, std::vector<u32>> bySize;
    CollectFiles(tree, tree.Root(), minSize ? minSize : 1, bySize, cancel);

    struct Group { u64 size; std::vector<u32> files; };
    std::vector<Group> groups;
    for (auto& kv : bySize)
    {
        if (cancel && *cancel) return res;
        if (kv.second.size() < 2) continue;
        // Stage 2: first+last 64 KB; stage 3: whole file.
        std::map<u64, std::vector<u32>> partial;
        for (u32 f : kv.second)
        {
            u64 h;
            if (HashFile(tree.Path(f), kv.first, true, &h, cancel)) partial[h].push_back(f);
            if (cancel && *cancel) return res;
        }
        for (auto& p : partial)
        {
            if (p.second.size() < 2) continue;
            std::map<u64, std::vector<u32>> full;
            for (u32 f : p.second)
            {
                u64 h;
                if (HashFile(tree.Path(f), kv.first, false, &h, cancel)) full[h].push_back(f);
                if (cancel && *cancel) return res;
            }
            for (auto& g : full)
                if (g.second.size() >= 2) groups.push_back({ kv.first, g.second });
        }
    }
    std::sort(groups.begin(), groups.end(), [](const Group& a, const Group& b) { return a.size * a.files.size() > b.size * b.files.size(); });
    for (const Group& g : groups)
    {
        res.bytes += g.size * (g.files.size() - 1);
        res.count += g.files.size();
        for (u32 f : g.files) res.nodes.push_back(f);
    }
    return res;
}
}
