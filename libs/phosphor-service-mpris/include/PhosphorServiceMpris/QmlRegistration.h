// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServiceMpris/phosphorservicempris_export.h>

namespace PhosphorServiceMpris {

/// Register every PhosphorServiceMpris QML type under the
/// `Phosphor.Service.Mpris` module at version 1.0. Idempotent on
/// repeat calls — relies on Qt's `qmlRegisterType` no-op behaviour
/// for duplicate registrations.
PHOSPHORSERVICEMPRIS_EXPORT void registerQmlTypes();

} // namespace PhosphorServiceMpris
