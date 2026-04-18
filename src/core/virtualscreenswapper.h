// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Forwarding shim. The canonical header is <PhosphorScreens/Swapper.h>;
// this file keeps the legacy `core/virtualscreenswapper.h` include path
// working for the in-tree call sites (D-Bus adaptor, tests) that have not
// yet migrated to the canonical path. New callers should use the canonical
// path together with `<PhosphorScreens/IConfigStore.h>` and the daemon's
// `SettingsConfigStore` adapter.

#include <PhosphorScreens/Swapper.h>

namespace PlasmaZones {

using VirtualScreenSwapper = Phosphor::Screens::VirtualScreenSwapper;

} // namespace PlasmaZones
