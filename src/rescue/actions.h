// SpaceRescue - SPDX-License-Identifier: GPL-3.0-or-later
//
// Reversible cleanup actions with an undo log. Owner: core-engineer.
// Rules: Recycle Bin first; quarantine copies to the work volume and deletes the source
// only after the copy is verified; permanent delete only after explicit confirmation (UI).

#pragma once

#include "../core/common.h"

#include <deque>
#include <mutex>

namespace sr
{
enum class ActionKind
{
    Recycle,          // SHFileOperation with FOF_ALLOWUNDO. Frees nothing until the bin is emptied!
    Quarantine,       // copy to <work volume>\SpaceRescue-Quarantine\<stamp>\..., verify, delete source
    Compress,         // NTFS compression (FSCTL_SET_COMPRESSION)
    DeletePermanent,  // only after explicit confirmation
};

struct ActionFailure
{
    std::wstring path;
    std::wstring error;
};

struct ActionResult
{
    ActionKind kind = ActionKind::Recycle;
    std::vector<std::wstring> done;        // paths that succeeded
    std::vector<ActionFailure> failed;
    u64 bytesFreed = 0;                    // measured free-space change on the source volume(s)
    int undoId = -1;                       // -1 = cannot be undone
    std::wstring summary;                  // plain-language one-liner for the toast
};

class ActionManager
{
public:
    static ActionManager& Get();

    // `workVolume` ("D:\") is required for Quarantine; ignored otherwise.
    ActionResult Apply(ActionKind kind, const std::vector<std::wstring>& paths, const std::wstring& workVolume);
    bool CanUndo(int undoId) const;
    ActionResult Undo(int undoId);
    // Where the undo log lives ("" = in memory only).
    std::wstring LogLocation() const { return m_logPath; }

private:
    struct Entry
    {
        ActionKind kind;
        std::vector<std::pair<std::wstring, std::wstring>> moves; // original -> quarantine copy (or original only)
        bool undone = false;
    };
    void AppendLog(const Entry& entry, int id);

    mutable std::mutex m_mutex;       // Apply runs on worker threads while the UI calls CanUndo/Undo
    std::deque<Entry> m_entries;      // deque: push_back never invalidates references held by Undo
    std::wstring m_logPath;
};
}
