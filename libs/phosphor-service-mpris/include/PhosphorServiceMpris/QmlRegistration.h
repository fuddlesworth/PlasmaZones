// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServiceMpris/phosphorservicempris_export.h>

namespace PhosphorServiceMpris {

/// Register every PhosphorServiceMpris QML type under the
/// `Phosphor.Service.Mpris` module at version 1.0. Idempotent on
/// repeat calls: internally guarded by `std::call_once` so a hot-
/// reloading shell that builds a fresh `QQmlEngine` per reload can
/// safely call this from every engine setup without triggering Qt's
/// duplicate-registration warning.
PHOSPHORSERVICEMPRIS_EXPORT void registerQmlTypes();

} // namespace PhosphorServiceMpris
