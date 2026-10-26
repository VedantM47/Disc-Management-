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
    // Phase D design preview of the new UI (see DESIGN.md).
    if (sr::ui2::PreviewOptions preview; argv && sr::ui2::ParsePreviewArgs(argc, argv, &preview))
    {
        LocalFree(argv);
        sr::Settings::Get().Load();
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        return sr::ui2::RunPreview(instance, show, preview);
    }
    if (argv && sr::HandleCommandLine(argc, argv, &exitCode, &scanPath))
    {
        LocalFree(argv);
        return exitCode;
    }
    if (argv) LocalFree(argv);

    sr::Settings::Get().Load();

    INITCOMMONCONTROLSEX icc = {};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_TREEVIEW_CLASSES | ICC_STANDARD_CLASSES | ICC_BAR_CLASSES | ICC_TAB_CLASSES;
    InitCommonControlsEx(&icc);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    const int result = ui::App::Get().Run(instance, show, scanPath);
    CoUninitialize();
    return result;
}
