// SpaceRescue UI - SPDX-License-Identifier: GPL-3.0-or-later
//
// The review list: the only place destructive actions start (dashboard -> review -> confirm).

#pragma once

#include "app_window.h"

namespace ui
{
// Modal: returns when the dialog is closed. Applies the chosen action on a worker thread and
// reports the result through App::OnActionApplied().
void ShowReviewDialog(HWND owner, const ReviewRequest& request);
}
