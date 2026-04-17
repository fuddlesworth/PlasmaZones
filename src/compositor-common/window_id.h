// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Backward-compat shim — actual definition lives in libs/phosphor-identity/.
// Existing `#include "window_id.h"` consumers (KWin effect, daemon, tests)
// keep working; new code should prefer `#include <PhosphorIdentity/WindowId.h>`
// directly.

#include <PhosphorIdentity/WindowId.h>
