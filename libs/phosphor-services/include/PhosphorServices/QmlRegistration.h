// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServices/phosphorservices_export.h>

#include <QtCore/qtconfigmacros.h>

QT_BEGIN_NAMESPACE
class QQmlEngine;
QT_END_NAMESPACE

namespace PhosphorServices {

/// Register every PhosphorServices QML type under the `Phosphor.Services`
/// module at version 1.0. Idempotent on repeat calls — relies on Qt's
/// `qmlRegisterType` no-op behaviour for duplicate registrations.
///
/// Called from the consuming binary (typically `src/shell/main.cpp`)
/// before any `QQmlEngine` loads a `.qml` file. The split — registration
/// here, invocation in main — keeps phosphor-shell free of a hard
/// dependency on phosphor-services for shells that don't ship a tray.
PHOSPHORSERVICES_EXPORT void registerQmlTypes();

/// Mount the icon image provider on `engine` under the URL scheme
/// `image://phosphor-services/`. The provider is what makes tray-icon
/// QImages reachable from QML's `Image.source` (a QUrl property —
/// QImage doesn't auto-convert). Per-engine because QQmlEngine takes
/// ownership of its providers and tears them down with itself.
PHOSPHORSERVICES_EXPORT void installImageProvider(QQmlEngine* engine);

} // namespace PhosphorServices
