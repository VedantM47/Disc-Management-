// SpaceRescue UI - SPDX-License-Identifier: GPL-3.0-or-later
// Shared UI includes and string-table helpers.

#pragma once

#ifndef _WIN32_IE
#define _WIN32_IE 0x0600  // exposes the ListView_/TreeView_ macros in the old MinGW headers
#endif
#include "../core/common.h"
#include "../../res/resource.h"

// Custom window messages (all posted to / handled by the UI thread).
#define WM_SR_SCANDONE   (WM_APP + 1)  // Scanner finished
#define WM_SR_ACTIONDONE (WM_APP + 2)  // lParam = heap ActionResult*
#define WM_SR_ANALYSIS   (WM_APP + 3)  // analysis finished
#define WM_SR_FRAGDONE   (WM_APP + 4)  // lParam = heap vector<FragmentedFile>*
#define WM_SR_DUPDONE    (WM_APP + 5)  // lParam = heap CategoryResult*
#define WM_SR_UNDODONE   (WM_APP + 6)  // lParam = heap ActionResult*
#define WM_SR_THEME      (WM_APP + 20) // theme / DPI changed: restyle yourself
#define WM_SR_KEY        (WM_APP + 30) // canvas forwards keys it did not handle (wParam = VK)

#ifndef LVS_EX_DOUBLEBUFFER
#define LVS_EX_DOUBLEBUFFER 0x00010000
#endif
#ifndef TVS_EX_DOUBLEBUFFER
#define TVS_EX_DOUBLEBUFFER 0x0004
#endif
#ifndef TVM_SETEXTENDEDSTYLE
#define TVM_SETEXTENDEDSTYLE (TV_FIRST + 44)
#endif

namespace ui
{
// Loads a string from the resource STRINGTABLE. The tokens {ndash} {mdash} {dot} {ell}
// are replaced with the real characters (the .rc file stays pure ASCII).
std::wstring Str(UINT id);

// Str() + printf-style formatting; all placeholders are %ls (wide string) or %d.
template <class... A>
std::wstring Strf(UINT id, A... args)
{
    return sr::Fmt(Str(id).c_str(), args...);
}
}
