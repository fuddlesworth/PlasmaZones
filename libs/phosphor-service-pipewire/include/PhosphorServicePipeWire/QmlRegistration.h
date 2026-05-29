// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServicePipeWire/phosphorservicepipewire_export.h>

namespace PhosphorServicePipeWire {

/// Register every PhosphorServicePipeWire QML type under the
/// `Phosphor.Service.PipeWire` module at version 1.0. Idempotent on
/// repeat calls: internally guarded by `std::call_once` so a hot-
/// reloading shell that builds a fresh `QQmlEngine` per reload can
/// safely call this from every engine setup without triggering Qt's
/// duplicate-registration warning. Mirrors the sibling phosphor-
/// service-{sni,mpris,upower,icontheme} pattern.
PHOSPHORSERVICEPIPEWIRE_EXPORT void registerQmlTypes();

} // namespace PhosphorServicePipeWire
