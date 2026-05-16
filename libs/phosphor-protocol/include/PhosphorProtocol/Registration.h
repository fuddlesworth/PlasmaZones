// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorProtocol/phosphorprotocol_export.h>

namespace PhosphorProtocol {

/// Register every PhosphorProtocol wire type with the Qt D-Bus metatype
/// system. Call once at startup (daemon and compositor plugin) before any
/// adaptor is registered on the bus.
PHOSPHORPROTOCOL_EXPORT void registerWireTypes();

} // namespace PhosphorProtocol
