// SpaceRescue - SPDX-License-Identifier: GPL-3.0-or-later
//
// Entry point: command line (install / uninstall / context-menu switches handled by the engine),
// settings, common controls, then the main window.

#include "ui/ui_common.h"
#include "ui/app_window.h"
#include "rescue/setup.h"
#include "ui2/preview.h"

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int show)
{
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    int exitCode = 0;
    std::wstring scanPath;
    // Switches (/install, /tray, /bench, ...) first; then the app.
    if (argv && sr::HandleCommandLine(argc, argv, &exitCode, &scanPath))
    {
        LocalFree(argv);
        return exitCode;
    }
    sr::ui2::PreviewOptions options;
    const bool modern = !argv || sr::ui2::ParsePreviewArgs(argc, argv, &options);
    if (argv) LocalFree(argv);
    sr::Settings::Get().Load();

    // The Direct2D interface is the default; /classic opens the original one.
    if (modern)
    {
        if (options.path.empty()) options.path = scanPath;
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        const int result = sr::ui2::RunPreview(instance, show, options);
        CoUninitialize();
        return result;
    }

    INITCOMMONCONTROLSEX icc = {};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_TREEVIEW_CLASSES | ICC_STANDARD_CLASSES | ICC_BAR_CLASSES | ICC_TAB_CLASSES;
    InitCommonControlsEx(&icc);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    const int result = ui::App::Get().Run(instance, show, scanPath);
    CoUninitialize();
    return result;
}
