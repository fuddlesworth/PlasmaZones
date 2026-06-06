// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServicePolkit/phosphorservicepolkit_export.h>

namespace PhosphorServicePolkit {

/// Register every PhosphorServicePolkit QML type under the
/// `Phosphor.Service.Polkit` module at version 1.0. Idempotent on repeat calls:
/// internally guarded by `std::call_once` so a hot-reloading shell that builds a
/// fresh `QQmlEngine` per reload can safely call this from every engine setup
/// without triggering Qt's duplicate-registration warning.
///
/// Called from the consuming binary (typically `src/shell/main.cpp`) before any
/// `QQmlEngine` loads a `.qml` file.
PHOSPHORSERVICEPOLKIT_EXPORT void registerQmlTypes();

} // namespace PhosphorServicePolkit
