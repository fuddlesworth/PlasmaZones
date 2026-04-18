// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Forwarding shim. The canonical header is <PhosphorScreens/VirtualScreen.h>;
// this file keeps the legacy `core/virtualscreen.h` include path working for
// the dozens of daemon, KCM, editor, and test call sites that have not yet
// migrated to the canonical path. New callers should use the canonical path.
//
// The type aliases re-export the POD types under `PlasmaZones::*` so existing
// call sites (~30 across daemon, KCM, editor, tests) keep compiling without
// a sweeping rename in this PR. A follow-up cleanup can migrate them to the
// canonical `Phosphor::Screens::*` spelling.

#include <PhosphorScreens/VirtualScreen.h>

// Existing call sites of `core/virtualscreen.h` rely on getting
// `PlasmaZones::VirtualScreenId::*` transitively (the pre-extraction header
// included `shared/virtualscreenid.h` directly). Preserve that contract via
// the shared shim, which now namespace-aliases to PhosphorIdentity.
#include "shared/virtualscreenid.h"

namespace PlasmaZones {

using VirtualScreenDef = Phosphor::Screens::VirtualScreenDef;
using VirtualScreenConfig = Phosphor::Screens::VirtualScreenConfig;

} // namespace PlasmaZones
