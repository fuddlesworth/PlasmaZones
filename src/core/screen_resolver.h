// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Forwarding shim. The canonical header is <PhosphorScreens/Resolver.h>;
// this file keeps the legacy `core/screen_resolver.h` include path working
// for the editor and any other in-tree caller that hasn't yet migrated to
// the canonical path. New callers should use the canonical path.
//
// The type alias re-exports the static-method facade under
// `PlasmaZones::ScreenResolver` so existing `ScreenResolver::effectiveScreenAt(...)`
// calls keep compiling. The defaulted Endpoint matches the PlasmaZones daemon's
// canonical D-Bus address, so callers do not need to pass anything extra.

#include <PhosphorScreens/Resolver.h>

namespace PlasmaZones {

using ScreenResolver = Phosphor::Screens::ScreenResolver;

} // namespace PlasmaZones
