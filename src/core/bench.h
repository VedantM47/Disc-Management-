// SpaceRescue - SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common.h"

namespace sr
{
// SpaceRescue.exe /bench [path]: scan timing (and MFT comparison when elevated) printed to the console.
int RunBench(std::wstring path);
}
