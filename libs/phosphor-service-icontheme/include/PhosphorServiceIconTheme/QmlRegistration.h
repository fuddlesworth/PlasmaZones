// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServiceIconTheme/phosphorserviceicontheme_export.h>

#include <QtCore/qtconfigmacros.h>

QT_BEGIN_NAMESPACE
class QQmlEngine;
QT_END_NAMESPACE

namespace PhosphorServiceIconTheme {

/// Register the `IconThemeResolver` QML singleton under the
/// `Phosphor.Service.IconTheme` module at version 1.0. Idempotent on
/// repeat calls: internally guarded by `std::call_once` so a hot-
/// reloading shell that builds a fresh `QQmlEngine` per reload can
/// safely call this from every engine setup without triggering Qt's
/// duplicate-registration warning. Must be called after
/// `QCoreApplication::instance()` exists (the singleton instance is
/// parented to it); calling earlier is a logged no-op.
PHOSPHORSERVICEICONTHEME_EXPORT void registerQmlTypes();

/// Mount the icon image provider on `engine` under the URL scheme
/// `image://phosphor-service-icontheme/`. The provider is what makes
/// raw `QImage` payloads (e.g. SNI tray icons that arrive as IconPixmap
/// over D-Bus) reachable from QML's `Image.source` (a `QUrl` property;
/// QImage doesn't auto-convert). Per-engine because `QQmlEngine` takes
/// ownership of its providers and tears them down with itself.
PHOSPHORSERVICEICONTHEME_EXPORT void installImageProvider(QQmlEngine* engine);

/// Stable URL host segment for the image provider, exported so
/// publishing call sites (`StatusNotifierItemModel::iconUrlFor()`,
/// future per-icon publishers) can construct the URL without
/// hard-coding the string. Bump on any rename so consumers fail to
/// link rather than fail silently at runtime.
PHOSPHORSERVICEICONTHEME_EXPORT const char* imageProviderUrlHost();

} // namespace PhosphorServiceIconTheme
