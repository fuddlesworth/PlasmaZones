// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServiceMpris/phosphorservicempris_export.h>

namespace PhosphorServiceMpris {

/// Register every PhosphorServiceMpris QML type under the
/// `Phosphor.Service.Mpris` module at version 1.0. Intended to be
/// called once per process at startup; `qmlRegisterType` is not a
/// true no-op on repeat calls (the second registration overwrites
/// and Qt logs a debug-level duplicate-registration warning).
PHOSPHORSERVICEMPRIS_EXPORT void registerQmlTypes();

} // namespace PhosphorServiceMpris
