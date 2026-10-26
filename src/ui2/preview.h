// SpaceRescue - SPDX-License-Identifier: GPL-3.0-or-later
//
// Phase D design preview: the new navigation and every tab drawn with the Direct2D design system on
// real scan data. Launched with: SpaceRescue.exe /preview [path] [/light|/dark] [/page N]

#pragma once

#include "../core/common.h"

namespace sr::ui2
{
struct PreviewOptions
{
    std::wstring path;     // default: system drive
    int theme = 0;         // 0 system, 1 light, 2 dark
    int page = 0;          // 0..7
};

bool ParsePreviewArgs(int argc, wchar_t** argv, PreviewOptions* options);
int RunPreview(HINSTANCE instance, int show, const PreviewOptions& options);
}
