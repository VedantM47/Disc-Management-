// SpaceRescue - SPDX-License-Identifier: GPL-3.0-or-later
//
// F4/F5: Explorer context menu and the self-contained per-user installer.
// No WiX/MSI tooling is available locally, so the exe installs itself:
//   SpaceRescue.exe /install [/reserve]  -> copies to %LOCALAPPDATA%\Programs\SpaceRescue,
//                                           Start-menu shortcut, context menu, Apps & features entry,
//                                           optional reserve file
//   SpaceRescue.exe /uninstall           -> removes all of the above (and the reserve file)
//   SpaceRescue.exe /register | /unregister        -> context menu only (HKCU)
//   SpaceRescue.exe /createreserve [MB] | /removereserve
//   SpaceRescue.exe <path>                         -> open and scan <path> (handled by the UI)
// Owner: core-engineer.

#pragma once

#include "../core/common.h"

namespace sr
{
// Registers "Analyze with SpaceRescue" for folders, folder backgrounds and drives (HKCU\Software\Classes).
DWORD RegisterContextMenu(const std::wstring& exePath);
DWORD UnregisterContextMenu();
bool IsContextMenuRegistered();

DWORD Install(bool createReserve);
DWORD Uninstall();

// Handles the switches above. Returns true if a switch was handled (the app should exit
// with *exitCode); false for normal startup. `scanPath` receives a path argument, if any.
bool HandleCommandLine(int argc, wchar_t** argv, int* exitCode, std::wstring* scanPath);
}
