// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServiceSni/phosphorservicesni_export.h>

namespace PhosphorServiceSni {

/// Register every PhosphorServiceSni QML type under the
/// `Phosphor.Service.Sni` module at version 1.0. Idempotent on repeat
/// calls — relies on Qt's `qmlRegisterType` no-op behaviour for
/// duplicate registrations.
///
/// Called from the consuming binary (typically `src/shell/main.cpp`)
/// before any `QQmlEngine` loads a `.qml` file. The host application
/// is also expected to install the icon image provider (via
/// `PhosphorServiceIconTheme::installImageProvider`) before SNI
/// publishes URLs to it.
PHOSPHORSERVICESNI_EXPORT void registerQmlTypes();

} // namespace PhosphorServiceSni
