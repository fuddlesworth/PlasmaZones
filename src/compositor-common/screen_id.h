// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Forwarding shim. The canonical header is <PhosphorIdentity/ScreenId.h>;
// this file keeps the legacy `compositor-common/screen_id.h` include path
// working for the KWin effect and any other in-tree caller still on the
// pre-extraction spelling. New callers should use the canonical path.
//
// The namespace alias re-exports the helpers under
// `PlasmaZones::ScreenIdUtils` so existing call sites compile unchanged.

#include <PhosphorIdentity/ScreenId.h>

namespace PlasmaZones {
namespace ScreenIdUtils = PhosphorIdentity::ScreenId;
} // namespace PlasmaZones
