// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServices/phosphorservices_export.h>

namespace PhosphorServices {

/// Register every PhosphorServices QML type under the `Phosphor.Services`
/// module at version 1.0. Idempotent on repeat calls — relies on Qt's
/// `qmlRegisterType` no-op behaviour for duplicate registrations.
///
/// Called from the consuming binary (typically `src/shell/main.cpp`)
/// before any `QQmlEngine` loads a `.qml` file. The split — registration
/// here, invocation in main — keeps phosphor-shell free of a hard
/// dependency on phosphor-services for shells that don't ship a tray.
///
/// The icon image provider that was historically mounted from this
/// header moved to phosphor-service-icontheme along with the resolver;
/// consumers now call `PhosphorServiceIconTheme::installImageProvider`
/// directly (see that library's `QmlRegistration.h`).
PHOSPHORSERVICES_EXPORT void registerQmlTypes();

} // namespace PhosphorServices
