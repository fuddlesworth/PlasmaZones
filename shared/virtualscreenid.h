// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Forwarding shim. The canonical header is <PhosphorIdentity/VirtualScreenId.h>;
// this file keeps the legacy `shared/virtualscreenid.h` include path working
// for the KWin effect (which adds ${CMAKE_SOURCE_DIR} to its include path
// instead of the per-library include) and for any other caller still on the
// pre-extraction spelling. New callers should use the canonical path.
//
// The namespace alias re-exports the helpers under `PlasmaZones::VirtualScreenId`
// so the existing call sites (~10 across daemon, effect, editor) keep
// compiling without a sweeping rename in this PR. A follow-up cleanup can
// migrate them to the canonical `PhosphorIdentity::VirtualScreenId` spelling.

#include <PhosphorIdentity/VirtualScreenId.h>

namespace PlasmaZones {
namespace VirtualScreenId = PhosphorIdentity::VirtualScreenId;
} // namespace PlasmaZones
