// SpaceRescue UI - SPDX-License-Identifier: GPL-3.0-or-later
//
// The "Disk Internals" tab: volume facts (clusters, MFT, slack), a per-file extents strip and
// the most fragmented files.

#pragma once

#include "app_window.h"

namespace ui
{
class InternalsPage
{
public:
    HWND Create(HWND parent);
    HWND Hwnd() const { return m_hwnd; }
    void Reload();                           // new scan: re-query the volume, restart the fragmentation scan
    void ShowFile(const std::wstring& path); // fill the file panel
    void StopBackground();                   // cancel + join the fragmentation worker
    void Tick();                             // periodic repaint while loading
    void Restyle();

    // Data shown by the info canvas (written on the UI thread only).
    sr::VolumeInternals vol;
    sr::SlackSummary slack;
    sr::FileInternals file;
    bool haveVolume = false;
    bool haveFile = false;
    bool fragLoading = false;
    bool fragDone = false;
    size_t fragShown = 0;

    void Browse();

private:
    static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
    LRESULT Handle(UINT, WPARAM, LPARAM);
    void Layout();
    void Paint();
    void FillList(const std::vector<sr::FragmentedFile>& frags);

    HWND m_hwnd = nullptr;
    HWND m_list = nullptr;
    class InfoCanvas* m_info = nullptr;
    HANDLE m_thread = nullptr;
    volatile bool m_cancel = false;
    std::vector<sr::FragmentedFile> m_frags;
    RECT m_labelRect = {};
};
}
