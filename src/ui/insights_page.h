// SpaceRescue UI - SPDX-License-Identifier: GPL-3.0-or-later
//
// The "Insights" tab: drive summary, top space hogs, smart categories, stale files,
// growth since last scan and duplicate search. Custom painted and scrollable.

#pragma once

#include "app_window.h"
#include "widgets.h"

namespace ui
{
class InsightsPage : public Canvas
{
public:
    ~InsightsPage() override { StopBackground(); }
    // A new scan started or finished: forget duplicate results and scroll to the top.
    void Reset();
    // Called by the main window when WM_SR_DUPDONE arrives (takes ownership of `result`).
    void OnDuplicatesDone(sr::CategoryResult* result);
    // Cancels and joins the duplicate search (must run before the scan tree is replaced).
    void StopBackground();
    // Periodic repaint while something is running (progress text).
    void Tick();

protected:
    int OnPaint(Painter& p) override;
    void OnButton(int id) override;

private:
    enum DupState { DupIdle, DupRunning, DupDone, DupCancelled };

    void StartDuplicates();
    void Review(const std::wstring& title, const std::wstring& blurb, const std::vector<sr::u32>& nodes, bool access);
    int Hero(Painter& p, int x, int y, int w);
    int HogRows(Painter& p, int x, int y, int w, const std::vector<sr::Hog>& hogs, int reviewBase, int showBase);
    int Section(Painter& p, int x, int y, const std::wstring& title, const std::wstring& sub, int w);
    int EmptyCard(Painter& p, int x, int y, int w, const std::wstring& title, const std::wstring& body);
    std::wstring DriveLabel() const;
    sr::u64 ShareTotal() const;

    DupState m_dupState = DupIdle;
    std::unique_ptr<sr::CategoryResult> m_dup;
    HANDLE m_dupThread = nullptr;
    volatile bool m_dupCancel = false;
    DWORD m_dupStart = 0;
};
}
