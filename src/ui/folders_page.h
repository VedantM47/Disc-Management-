// SpaceRescue UI - SPDX-License-Identifier: GPL-3.0-or-later
//
// The "Folders" tab: the scan tree as a lazily populated TreeView sorted by size.

#pragma once

#include "app_window.h"

namespace ui
{
class FoldersPage
{
public:
    HWND Create(HWND parent);
    HWND Hwnd() const { return m_hwnd; }
    HWND TreeHwnd() const { return m_tree; }
    void Reload();                 // rebuild from App::Tree() (keeps the selection when it still exists)
    void Select(sr::u32 node);     // expand the path to `node` and select it
    void Restyle();                // theme / DPI changed

private:
    static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
    LRESULT Handle(UINT, WPARAM, LPARAM);
    void Layout();
    void Paint();
    void Populate(HTREEITEM parent, sr::u32 node);
    HTREEITEM InsertNode(HTREEITEM parent, sr::u32 node, bool isMore, sr::u64 moreCount);
    std::wstring ItemText(sr::u32 node) const;
    sr::u32 NodeOf(HTREEITEM item) const;
    void ContextMenu(int screenX, int screenY, bool fromKeyboard);
    LRESULT CustomDraw(NMTVCUSTOMDRAW* cd);

    HWND m_hwnd = nullptr;
    HWND m_tree = nullptr;
    RECT m_treeRect = {};
};
}
