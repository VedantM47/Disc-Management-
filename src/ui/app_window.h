// SpaceRescue UI - SPDX-License-Identifier: GPL-3.0-or-later
//
// The main window and the application state shared by all pages.

#pragma once

#include "ui_common.h"
#include "../core/scanner.h"
#include "../rescue/rescue.h"
#include "../rescue/actions.h"
#include "../insights/insights.h"
#include "../internals/internals.h"

namespace ui
{
// What the review dialog is asked to show.
struct ReviewRequest
{
    std::wstring title;          // "Developer caches"
    std::wstring blurb;          // one plain-language line under the title
    std::vector<sr::u32> nodes;  // scan-tree nodes (files or folders)
    bool preselect = false;      // tick every row (used for single-item requests)
    bool useAccessTime = false;  // "Last used" column uses access time instead of modified time
};

enum class View { Home, Scanning, Results };

class App
{
public:
    static App& Get();

    int Run(HINSTANCE instance, int show, const std::wstring& scanPath);
    HWND Hwnd() const { return m_hwnd; }

    // --- State for pages -------------------------------------------------------------
    const sr::ScanTree* Tree() const { return m_tree.get(); }
    sr::ScanTree* MutableTree() { return m_tree.get(); }
    const sr::InsightsReport* Report() const { return m_report.get(); }
    bool Analyzing() const { return m_analysisRunning; }
    const std::vector<sr::DriveInfo>& Drives() const { return m_drives; }
    // Drive that holds the scanned folder, or nullptr when unknown.
    const sr::DriveInfo* TreeDrive() const;
    // "D:\" or "" when there is no other usable drive (changes then stay in memory).
    const std::wstring& WorkVolume() const { return m_workVolume; }
    // Folder where scan snapshots are kept (never on a low drive); "" = none available.
    const std::wstring& SnapshotDir() const { return m_snapshotDir; }
    bool AnyDriveLow() const { return m_anyLow; }

    // --- Actions ----------------------------------------------------------------------
    void StartScan(const std::wstring& path);
    void ChooseFolderAndScan();
    void OpenReview(const ReviewRequest& request);
    void SelectTab(int tab);           // 0 Insights, 1 Folders, 2 Disk Internals
    void ShowFileInInternals(const std::wstring& path);
    void ShowNodeInFolders(sr::u32 node);
    // Called by the review dialog after ActionManager::Apply finished.
    void OnActionApplied(const sr::ActionResult& result, const std::vector<sr::u32>& detached);
    void ShowToast(const std::wstring& text, int undoId);
    void RefreshAfterChange();
    void RefreshDrives();

private:
    App() = default;
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);
    void OnScanDone();
    void OnAnalysisDone(int generation, sr::InsightsReport* report);
    void OnUndoDone(sr::ActionResult* result);
    void BeginAnalysis(bool saveSnapshot);
    void EndAnalysis();
    bool PreTranslate(MSG& msg);
    HWND m_hwnd = nullptr;
    std::unique_ptr<sr::ScanTree> m_tree;
    std::unique_ptr<sr::InsightsReport> m_report;
    std::vector<sr::DriveInfo> m_drives;
    std::wstring m_workVolume;
    std::wstring m_snapshotDir;
    bool m_anyLow = false;
    bool m_analysisRunning = false;
};
}
