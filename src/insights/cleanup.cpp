// SpaceRescue - SPDX-License-Identifier: GPL-3.0-or-later

#include "cleanup.h"

#include <map>

namespace sr
{
namespace
{
// Anchors for rule paths.
enum class Base { LocalAppData, RoamingAppData, Profile, Windows, ProgramData, Temp, DriveRoot, ProgramFilesX86 };

struct Rule
{
    CleanSection section;
    const wchar_t* app;
    Base base;
    const wchar_t* path;     // relative, '\'-separated; '*' matches one folder name (prefix* / *suffix allowed)
    Safety safety;
    const wchar_t* why;
    const wchar_t* tool = L"";
};

const Rule kRules[] = {
    // --- Windows temp ---
    { CleanSection::WindowsTemp, L"Temporary files", Base::Temp, L"", Safety::Safe, L"Scratch files apps leave behind. Files in use are skipped." },
    { CleanSection::WindowsTemp, L"Windows temp", Base::Windows, L"Temp", Safety::Safe, L"Windows' own scratch folder." },
    { CleanSection::WindowsTemp, L"Windows Update downloads", Base::Windows, L"SoftwareDistribution\\Download", Safety::Safe,
      L"Update packages already installed. Windows downloads them again if needed.", L"Disk Cleanup" },
    { CleanSection::WindowsTemp, L"Delivery Optimization", Base::Windows,
      L"ServiceProfiles\\NetworkService\\AppData\\Local\\Microsoft\\Windows\\DeliveryOptimization", Safety::Safe,
      L"Update pieces shared with other PCs.", L"Disk Cleanup" },
    { CleanSection::WindowsTemp, L"Crash dumps", Base::LocalAppData, L"CrashDumps", Safety::Safe, L"Memory dumps from crashed apps." },
    { CleanSection::WindowsTemp, L"Crash dumps", Base::Windows, L"Minidump", Safety::Safe, L"Small dumps from blue screens." },
    { CleanSection::WindowsTemp, L"Error reports", Base::ProgramData, L"Microsoft\\Windows\\WER", Safety::Safe, L"Windows Error Reporting queue." },
    { CleanSection::WindowsTemp, L"Thumbnail cache", Base::LocalAppData, L"Microsoft\\Windows\\Explorer", Safety::Safe,
      L"Picture previews. Explorer rebuilds them." },
    { CleanSection::WindowsTemp, L"Recycle Bin", Base::DriveRoot, L"$Recycle.Bin", Safety::Review,
      L"Deleted files waiting in the bin. Emptying it is permanent." },
    // --- App caches ---
    { CleanSection::AppCaches, L"Google Chrome", Base::LocalAppData, L"Google\\Chrome\\User Data\\*\\Cache", Safety::Safe, L"Web page cache. Sign-ins and history stay." },
    { CleanSection::AppCaches, L"Google Chrome", Base::LocalAppData, L"Google\\Chrome\\User Data\\*\\Code Cache", Safety::Safe, L"Compiled script cache." },
    { CleanSection::AppCaches, L"Google Chrome", Base::LocalAppData, L"Google\\Chrome\\User Data\\*\\Service Worker\\CacheStorage", Safety::Safe, L"Offline site data cache." },
    { CleanSection::AppCaches, L"Microsoft Edge", Base::LocalAppData, L"Microsoft\\Edge\\User Data\\*\\Cache", Safety::Safe, L"Web page cache. Sign-ins and history stay." },
    { CleanSection::AppCaches, L"Microsoft Edge", Base::LocalAppData, L"Microsoft\\Edge\\User Data\\*\\Code Cache", Safety::Safe, L"Compiled script cache." },
    { CleanSection::AppCaches, L"Microsoft Edge", Base::LocalAppData, L"Microsoft\\Edge\\User Data\\*\\Service Worker\\CacheStorage", Safety::Safe, L"Offline site data cache." },
    { CleanSection::AppCaches, L"Brave", Base::LocalAppData, L"BraveSoftware\\Brave-Browser\\User Data\\*\\Cache", Safety::Safe, L"Web page cache." },
    { CleanSection::AppCaches, L"Firefox", Base::LocalAppData, L"Mozilla\\Firefox\\Profiles\\*\\cache2", Safety::Safe, L"Web page cache." },
    { CleanSection::AppCaches, L"Microsoft Teams", Base::LocalAppData, L"Packages\\MSTeams_*\\LocalCache", Safety::Safe, L"Teams cache; chats stay in the cloud." },
    { CleanSection::AppCaches, L"Microsoft Teams", Base::RoamingAppData, L"Microsoft\\Teams\\Cache", Safety::Safe, L"Classic Teams cache." },
    { CleanSection::AppCaches, L"Discord", Base::RoamingAppData, L"discord\\Cache", Safety::Safe, L"Images and media Discord already showed you." },
    { CleanSection::AppCaches, L"Discord", Base::RoamingAppData, L"discord\\Code Cache", Safety::Safe, L"Compiled script cache." },
    { CleanSection::AppCaches, L"Slack", Base::RoamingAppData, L"Slack\\Cache", Safety::Safe, L"Slack media cache." },
    { CleanSection::AppCaches, L"Slack", Base::RoamingAppData, L"Slack\\Service Worker\\CacheStorage", Safety::Safe, L"Slack offline cache." },
    { CleanSection::AppCaches, L"Zoom", Base::RoamingAppData, L"Zoom\\data\\WebviewCacheX64", Safety::Safe, L"Zoom web view cache." },
    { CleanSection::AppCaches, L"Spotify", Base::LocalAppData, L"Spotify\\Storage", Safety::Safe, L"Streaming cache. Downloads for offline play are separate." },
    { CleanSection::AppCaches, L"WhatsApp", Base::LocalAppData, L"Packages\\5319275A.WhatsAppDesktop_*\\LocalState\\shared\\transfers", Safety::Review,
      L"Media you received. Check you don't need them first." },
    { CleanSection::AppCaches, L"Telegram", Base::RoamingAppData, L"Telegram Desktop\\tdata\\user_data", Safety::Review, L"Cached media from chats." },
    { CleanSection::AppCaches, L"Steam", Base::ProgramFilesX86, L"Steam\\steamapps\\shadercache", Safety::Safe, L"Precompiled game shaders, rebuilt on launch." },
    { CleanSection::AppCaches, L"Steam", Base::ProgramFilesX86, L"Steam\\steamapps\\downloading", Safety::Review, L"Unfinished game downloads." },
    { CleanSection::AppCaches, L"Epic Games", Base::LocalAppData, L"EpicGamesLauncher\\Saved\\webcache*", Safety::Safe, L"Launcher web cache." },
    { CleanSection::AppCaches, L"Adobe", Base::RoamingAppData, L"Adobe\\Common\\Media Cache Files", Safety::Safe, L"Premiere/After Effects media cache." },
    { CleanSection::AppCaches, L"Adobe", Base::RoamingAppData, L"Adobe\\Common\\Media Cache", Safety::Safe, L"Media cache database." },
    { CleanSection::AppCaches, L"Microsoft Office", Base::LocalAppData, L"Microsoft\\Office\\16.0\\OfficeFileCache", Safety::Review,
      L"Offline copies of cloud documents. Make sure they're synced." },
    { CleanSection::AppCaches, L"Visual Studio Code", Base::RoamingAppData, L"Code\\Cache", Safety::Safe, L"Editor web cache." },
    { CleanSection::AppCaches, L"Visual Studio Code", Base::RoamingAppData, L"Code\\CachedData", Safety::Safe, L"Compiled editor code." },
    { CleanSection::AppCaches, L"Visual Studio Code", Base::RoamingAppData, L"Code\\CachedExtensionVSIXs", Safety::Safe, L"Old extension installers." },
    { CleanSection::AppCaches, L"Graphics shader cache", Base::LocalAppData, L"NVIDIA\\DXCache", Safety::Safe, L"GPU shader cache, rebuilt automatically." },
    { CleanSection::AppCaches, L"Graphics shader cache", Base::LocalAppData, L"D3DSCache", Safety::Safe, L"DirectX shader cache." },
    // --- Developer (global caches) ---
    { CleanSection::Developer, L"npm cache", Base::LocalAppData, L"npm-cache", Safety::Safe, L"Downloaded npm packages. npm fetches them again." },
    { CleanSection::Developer, L"npm cache", Base::RoamingAppData, L"npm-cache", Safety::Safe, L"Downloaded npm packages." },
    { CleanSection::Developer, L"pnpm store", Base::LocalAppData, L"pnpm\\store", Safety::Review, L"Shared pnpm store; projects relink on install." },
    { CleanSection::Developer, L"Yarn cache", Base::LocalAppData, L"Yarn\\Cache", Safety::Safe, L"Downloaded Yarn packages." },
    { CleanSection::Developer, L"pip cache", Base::LocalAppData, L"pip\\cache", Safety::Safe, L"Downloaded Python wheels." },
    { CleanSection::Developer, L"Conda packages", Base::Profile, L"anaconda3\\pkgs", Safety::Safe, L"Package tarballs; `conda clean -a` does the same." },
    { CleanSection::Developer, L"Conda packages", Base::Profile, L"miniconda3\\pkgs", Safety::Safe, L"Package tarballs." },
    { CleanSection::Developer, L"Gradle", Base::Profile, L".gradle\\caches", Safety::Safe, L"Build cache and downloaded dependencies." },
    { CleanSection::Developer, L"Gradle", Base::Profile, L".gradle\\wrapper\\dists", Safety::Review, L"Gradle versions downloaded by wrappers." },
    { CleanSection::Developer, L"Maven", Base::Profile, L".m2\\repository", Safety::Safe, L"Downloaded Maven artifacts." },
    { CleanSection::Developer, L"NuGet", Base::Profile, L".nuget\\packages", Safety::Safe, L"Downloaded NuGet packages." },
    { CleanSection::Developer, L"Cargo", Base::Profile, L".cargo\\registry", Safety::Safe, L"Downloaded Rust crates." },
    { CleanSection::Developer, L"Go", Base::Profile, L"go\\pkg\\mod", Safety::Safe, L"Go module cache (`go clean -modcache`)." },
    { CleanSection::Developer, L"Go", Base::LocalAppData, L"go-build", Safety::Safe, L"Go build cache." },
    { CleanSection::Developer, L"Android SDK", Base::LocalAppData, L"Android\\Sdk\\system-images", Safety::Review, L"Emulator system images; reinstall from SDK Manager." },
    { CleanSection::Developer, L"Android emulators", Base::Profile, L".android\\avd", Safety::Review, L"Virtual devices and their disks." },
    { CleanSection::Developer, L"JetBrains", Base::LocalAppData, L"JetBrains\\*\\caches", Safety::Safe, L"IDE index caches, rebuilt on open." },
    { CleanSection::Developer, L"Docker / WSL disks", Base::LocalAppData, L"Docker\\wsl", Safety::Caution,
      L"Virtual disks for Linux and containers. Don't delete; compact them instead.", L"Optimize VHD" },
    { CleanSection::Developer, L"Docker / WSL disks", Base::LocalAppData, L"wsl", Safety::Caution,
      L"WSL distribution disks. Don't delete; compact them instead.", L"Optimize VHD" },
};

// Folder-name rules for project build outputs and caches at any depth.
struct NameRule
{
    const wchar_t* name;
    const wchar_t* sibling;  // required marker in the parent folder ("" = none); '*' suffix match allowed
    Safety safety;
    const wchar_t* why;
};

const NameRule kNameRules[] = {
    { L"node_modules", L"package.json", Safety::Review, L"Installed npm packages; `npm install` restores them." },
    { L"__pycache__", L"", Safety::Safe, L"Compiled Python bytecode, recreated on run." },
    { L".venv", L"", Safety::Review, L"Python virtual environment; recreate from requirements." },
    { L"venv", L"", Safety::Review, L"Python virtual environment; recreate from requirements." },
    { L".vs", L"", Safety::Safe, L"Visual Studio cache and IntelliSense database." },
    { L"bin", L"*.csproj", Safety::Safe, L".NET build output, rebuilt by Build." },
    { L"obj", L"*.csproj", Safety::Safe, L".NET intermediate files." },
    { L"target", L"Cargo.toml", Safety::Safe, L"Rust build output (`cargo clean`)." },
    { L"target", L"pom.xml", Safety::Safe, L"Maven build output (`mvn clean`)." },
    { L"build", L"build.gradle", Safety::Safe, L"Gradle build output (`gradle clean`)." },
    { L"build", L"build.gradle.kts", Safety::Safe, L"Gradle build output." },
    { L"dist", L"package.json", Safety::Review, L"Built web bundle." },
    { L".next", L"package.json", Safety::Safe, L"Next.js build cache." },
    { L".nuxt", L"package.json", Safety::Safe, L"Nuxt build cache." },
    { L".turbo", L"", Safety::Safe, L"Turborepo cache." },
    { L".parcel-cache", L"", Safety::Safe, L"Parcel bundler cache." },
    { L".gradle", L"settings.gradle", Safety::Safe, L"Per-project Gradle cache." },
    { L"Library", L"ProjectSettings", Safety::Review, L"Unity import cache; Unity rebuilds it (slow)." },
};

bool GlobPart(const std::wstring& pattern, const std::wstring& name)
{
    if (pattern == L"*") return true;
    if (!pattern.empty() && pattern.back() == L'*')
        return name.size() >= pattern.size() - 1 && _wcsnicmp(name.c_str(), pattern.c_str(), pattern.size() - 1) == 0;
    if (!pattern.empty() && pattern.front() == L'*')
        return EndsWithI(name, pattern.c_str() + 1);
    return _wcsicmp(pattern.c_str(), name.c_str()) == 0;
}

std::vector<std::wstring> Split(const std::wstring& path)
{
    std::vector<std::wstring> parts;
    size_t start = 0;
    while (start <= path.size())
    {
        const size_t end = path.find(L'\\', start);
        const std::wstring part = path.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
        if (!part.empty()) parts.push_back(part);
        if (end == std::wstring::npos) break;
        start = end + 1;
    }
    return parts;
}

// All nodes under `from` matching the glob parts.
void Match(const ScanTree& tree, u32 from, const std::vector<std::wstring>& parts, size_t i, std::vector<u32>& out)
{
    if (i == parts.size())
    {
        out.push_back(from);
        return;
    }
    for (u32 c = tree.At(from).firstChild; c != NoNode; c = tree.At(c).nextSibling)
        if (GlobPart(parts[i], tree.Name(c))) Match(tree, c, parts, i + 1, out);
}

std::wstring BasePath(Base b, const std::wstring& root)
{
    switch (b)
    {
    case Base::LocalAppData: return KnownFolder(CSIDL_LOCAL_APPDATA);
    case Base::RoamingAppData: return KnownFolder(CSIDL_APPDATA);
    case Base::Profile: return KnownFolder(CSIDL_PROFILE);
    case Base::Windows: return KnownFolder(CSIDL_WINDOWS);
    case Base::ProgramData: return KnownFolder(CSIDL_COMMON_APPDATA);
    case Base::ProgramFilesX86: return KnownFolder(CSIDL_PROGRAM_FILESX86);
    case Base::DriveRoot: return root.size() >= 3 ? root.substr(0, 3) : root;
    case Base::Temp:
    {
        wchar_t temp[MAX_PATH] = {}, longTemp[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, temp);
        if (GetLongPathNameW(temp, longTemp, MAX_PATH)) wcscpy(temp, longTemp);
        std::wstring t = temp;
        if (t.size() > 3 && t.back() == L'\\') t.pop_back();
        return t;
    }
    }
    return L"";
}

bool Within(const std::wstring& path, const std::wstring& root)
{
    return path.size() >= root.size() && _wcsnicmp(path.c_str(), root.c_str(), root.size()) == 0;
}

const wchar_t* SectionTitle(CleanSection s)
{
    static const wchar_t* t[] = { L"Windows temp", L"App caches", L"Developer", L"Old and unused" };
    return t[static_cast<size_t>(s)];
}

void AddToGroup(std::map<std::wstring, CleanGroup>& groups, const std::wstring& key, const CleanItem& item, const wchar_t* why,
    const wchar_t* tool)
{
    CleanGroup& g = groups[key];
    if (g.name.empty())
    {
        g.name = key;
        g.why = why;
        g.tool = tool ? tool : L"";
        g.safety = item.safety;
    }
    if (static_cast<int>(item.safety) > static_cast<int>(g.safety)) g.safety = item.safety;
    g.bytes += item.bytes;
    g.items.push_back(item);
}

bool HasChild(const ScanTree& tree, u32 dir, const wchar_t* marker)
{
    const std::wstring m = marker;
    for (u32 c = tree.At(dir).firstChild; c != NoNode; c = tree.At(c).nextSibling)
        if (GlobPart(m, tree.Name(c))) return true;
    return false;
}
}

CleanupReport BuildCleanup(const ScanTree& tree, const InsightsReport* insights)
{
    CleanupReport rep;
    for (size_t i = 0; i < rep.sections.size(); ++i)
    {
        rep.sections[i].id = static_cast<CleanSection>(i);
        rep.sections[i].title = SectionTitle(static_cast<CleanSection>(i));
    }
    const std::wstring root = tree.Path(tree.Root());
    std::array<std::map<std::wstring, CleanGroup>, static_cast<size_t>(CleanSection::Count)> groups;
    std::vector<char> claimed(tree.Count(), 0); // nodes already counted (no double counting across rules)
    auto claimedAbove = [&](u32 n) {
        for (u32 p = n; p != NoNode; p = tree.At(p).parent) if (claimed[p]) return true;
        return false;
    };

    // 1. Anchored rules.
    for (const Rule& r : kRules)
    {
        const std::wstring base = BasePath(r.base, root);
        if (base.empty()) continue;
        std::wstring anchor = base;
        std::vector<std::wstring> parts = Split(r.path);
        // Resolve the longest wildcard-free prefix with Find, then glob the rest.
        size_t fixed = 0;
        while (fixed < parts.size() && parts[fixed].find(L'*') == std::wstring::npos) anchor = JoinPath(anchor, parts[fixed++]);
        if (!Within(anchor, root) && !Within(root, anchor)) continue;
        const u32 start = tree.Find(anchor);
        if (start == NoNode) continue;
        std::vector<u32> hits;
        Match(tree, start, parts, fixed, hits);
        for (u32 h : hits)
        {
            if (claimedAbove(h) || !tree.At(h).size) continue;
            claimed[h] = 1;
            CleanItem item{ h, tree.Name(h), r.safety, tree.At(h).size };
            AddToGroup(groups[static_cast<size_t>(r.section)], r.app, item, r.why, r.tool);
        }
    }

    // 2. Project build outputs and caches at any depth (one pass over folders, skipping Windows).
    const std::wstring win = KnownFolder(CSIDL_WINDOWS);
    const u32 winNode = tree.Find(win);
    std::vector<u32> stack{ tree.Root() };
    std::map<std::wstring, CleanGroup>& dev = groups[static_cast<size_t>(CleanSection::Developer)];
    while (!stack.empty())
    {
        const u32 dir = stack.back();
        stack.pop_back();
        for (u32 c = tree.At(dir).firstChild; c != NoNode; c = tree.At(c).nextSibling)
        {
            const Node& n = tree.At(c);
            if (!(n.flags & NF_DIR) || (n.flags & NF_REPARSE) || c == winNode || claimed[c]) continue;
            const std::wstring name = tree.Name(c);
            const NameRule* rule = nullptr;
            for (const NameRule& nr : kNameRules)
                if (_wcsicmp(nr.name, name.c_str()) == 0 && (!*nr.sibling || HasChild(tree, dir, nr.sibling))) { rule = &nr; break; }
            if (!rule)
            {
                stack.push_back(c);
                continue;
            }
            if (!n.size || claimedAbove(c)) continue;
            claimed[c] = 1;
            const std::wstring project = tree.Path(dir);
            CleanItem item{ c, name, rule->safety, n.size };
            AddToGroup(dev, project, item, rule->why, L"");
        }
    }
    // "Last opened" for projects: newest change in the project outside its cache folders.
    for (auto& [key, g] : dev)
    {
        const u32 proj = tree.Find(key);
        if (proj == NoNode) continue;
        u64 newest = 0;
        for (u32 c = tree.At(proj).firstChild; c != NoNode; c = tree.At(c).nextSibling)
            if (!claimed[c] && IsPlausibleTime(tree.At(c).writeTime)) newest = std::max(newest, tree.At(c).writeTime);
        g.lastOpened = newest;
    }

    // 3. Old and unused, from the insights report.
    if (insights)
    {
        auto& old = groups[static_cast<size_t>(CleanSection::OldUnused)];
        const struct { Category c; Safety s; } pick[] = {
            { Category::OldDownloads, Safety::Review }, { Category::Installers, Safety::Review },
            { Category::Duplicates, Safety::Review }, { Category::Videos, Safety::Caution } };
        for (const auto& p : pick)
        {
            const CategoryResult& cr = insights->categories[static_cast<size_t>(p.c)];
            for (u32 n : cr.nodes)
            {
                if (n >= claimed.size() || claimedAbove(n)) continue;
                claimed[n] = 1; // an installer in Downloads counts once, not as both
                CleanItem item{ n, tree.Name(n), p.s, tree.At(n).size };
                AddToGroup(old, cr.title, item, cr.explanation.c_str(), L"");
            }
        }
        // Stale files (12 months bucket), grouped by folder.
        const StaleResult& st = insights->stale;
        for (const StaleGroup& sg : st.groups[1])
        {
            if (sg.folder == NoNode || claimed[sg.folder]) continue;
            for (u32 f : sg.files)
            {
                if (f >= claimed.size() || claimedAbove(f)) continue;
                CleanItem item{ f, tree.Name(f), Safety::Caution, tree.At(f).size };
                AddToGroup(old, L"Not used for a year", item, st.signalReason.c_str(), L"");
            }
        }
    }

    for (size_t s = 0; s < groups.size(); ++s)
    {
        CleanSectionResult& sec = rep.sections[s];
        // Many tiny projects (e.g. __pycache__ everywhere) would drown the list: fold them into one group.
        constexpr u64 SmallGroup = 16 * MiB;
        CleanGroup small;
        size_t smallCount = 0;
        for (auto it = groups[s].begin(); it != groups[s].end();)
        {
            if (it->second.bytes >= SmallGroup || groups[s].size() <= 8) { ++it; continue; }
            ++smallCount;
            small.bytes += it->second.bytes;
            if (static_cast<int>(it->second.safety) > static_cast<int>(small.safety)) small.safety = it->second.safety;
            small.items.insert(small.items.end(), it->second.items.begin(), it->second.items.end());
            it = groups[s].erase(it);
        }
        if (smallCount)
        {
            small.name = Fmt(L"%s smaller items", FormatCount(smallCount).c_str());
            small.why = L"Each is under 16 MB. Review them together.";
            groups[s][L"\xFFFF"] = std::move(small); // sorts last; renamed by its label
        }
        for (auto& [key, g] : groups[s])
        {
            std::sort(g.items.begin(), g.items.end(), [](const CleanItem& a, const CleanItem& b) { return a.bytes > b.bytes; });
            sec.bytes += g.bytes;
            for (const CleanItem& it : g.items)
            {
                if (it.safety == Safety::Safe) rep.safeBytes += it.bytes;
                else if (it.safety == Safety::Review) rep.reviewBytes += it.bytes;
                else rep.cautionBytes += it.bytes;
            }
            sec.groups.push_back(std::move(g));
        }
        std::sort(sec.groups.begin(), sec.groups.end(), [](const CleanGroup& a, const CleanGroup& b) { return a.bytes > b.bytes; });
    }
    return rep;
}

// --- System ------------------------------------------------------------------------

SystemReport AnalyzeSystem(const ScanTree& tree)
{
    SystemReport rep;
    const std::wstring win = KnownFolder(CSIDL_WINDOWS);
    const std::wstring sysRoot = VolumeOf(win);
    const std::wstring root = tree.Path(tree.Root());
    if (_wcsicmp(root.c_str(), sysRoot.c_str()) != 0) return rep;
    rep.available = true;
    struct Spec { const wchar_t* path; const wchar_t* label; const wchar_t* why; const wchar_t* tool; const wchar_t* cmd; double reducible; };
    const Spec specs[] = {
        { L"hiberfil.sys", L"Hibernation file", L"Holds memory contents for hibernate and fast startup.", L"Turn off hibernation",
          L"powercfg /hibernate off  (admin)", 1.0 },
        { L"pagefile.sys", L"Page file", L"Virtual memory. Windows sizes it automatically.", L"Adjust virtual memory",
          L"SystemPropertiesPerformance.exe  (Advanced > Virtual memory)", 0.0 },
        { L"swapfile.sys", L"Swap file", L"Used by Store apps when memory is tight.", L"Managed by Windows", L"", 0.0 },
        { L"Windows\\WinSxS", L"Component store (WinSxS)", L"Every Windows update keeps older components here. Many files are hard links, so the real size is smaller.",
          L"Clean up components", L"Dism.exe /Online /Cleanup-Image /StartComponentCleanup  (admin)", 0.25 },
        { L"Windows\\System32\\DriverStore", L"Driver store", L"Copies of every installed driver.", L"Managed by Windows", L"", 0.0 },
        { L"Windows\\Installer", L"Windows Installer cache", L"Needed to repair and uninstall programs.", L"Managed by Windows", L"", 0.0 },
        { L"Windows\\SoftwareDistribution", L"Windows Update cache", L"Downloaded update packages.", L"Disk Cleanup", L"cleanmgr.exe /d C:", 0.9 },
        { L"System Volume Information", L"Restore points", L"Snapshots for System Restore. Needs admin to measure.", L"Shrink restore points",
          L"SystemPropertiesProtection.exe", 0.0 },
        { L"Program Files", L"Installed apps (64-bit)", L"Programs you installed.", L"Apps & features", L"ms-settings:appsfeatures", 0.0 },
        { L"Program Files (x86)", L"Installed apps (32-bit)", L"Programs you installed.", L"Apps & features", L"ms-settings:appsfeatures", 0.0 },
    };
    for (const Spec& s : specs)
    {
        SystemItem it;
        it.label = s.label;
        it.why = s.why;
        it.tool = s.tool;
        it.command = s.cmd;
        it.node = tree.Find(JoinPath(root, s.path));
        it.bytes = it.node == NoNode ? 0 : tree.At(it.node).size;
        it.reducible = static_cast<u64>(static_cast<double>(it.bytes) * s.reducible);
        rep.total += it.bytes;
        rep.reducible += it.reducible;
        rep.items.push_back(it);
    }
    return rep;
}

bool IsProtectedPath(const std::wstring& path)
{
    const std::wstring win = KnownFolder(CSIDL_WINDOWS);
    const std::wstring sys = VolumeOf(win);
    const std::wstring p = ToLower(path);
    const std::wstring lw = ToLower(win);
    if (Within(p, lw + L"\\") || p == lw) return true;
    static const wchar_t* rootFiles[] = { L"pagefile.sys", L"hiberfil.sys", L"swapfile.sys", L"bootmgr", L"bootnxt", L"dumpstack.log" };
    for (const wchar_t* f : rootFiles)
        if (p == ToLower(sys) + f) return true;
    static const wchar_t* rootDirs[] = { L"system volume information", L"recovery", L"boot", L"program files", L"program files (x86)" };
    for (const wchar_t* d : rootDirs)
    {
        const std::wstring dir = ToLower(sys) + d;
        if (p == dir || Within(p, dir + L"\\")) return true;
    }
    const std::wstring pd = ToLower(JoinPath(KnownFolder(CSIDL_COMMON_APPDATA), L"Microsoft"));
    return p == pd || Within(p, pd + L"\\");
}
}
