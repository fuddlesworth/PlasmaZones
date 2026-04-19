// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"

#include <PhosphorScreens/Manager.h>

class QPoint;
class QScreen;

namespace PlasmaZones {

/**
 * @brief Daemon-side service-locator for the process-global ScreenManager.
 *
 * The library (`Phosphor::Screens::ScreenManager`) is intentionally not a
 * singleton — there's no `instance()` accessor, no static helpers. This
 * file is the daemon's ONE concession to global access: a thin pair of
 * `setScreenManager` / `screenManager` accessors plus a few "with
 * fallback" wrappers that compose the manager with Qt-only defaults for
 * call sites where the manager may not exist yet (very early in start-up,
 * unit tests).
 *
 * If you're writing new code: prefer to be given a `Phosphor::Screens::ScreenManager*`
 * via constructor injection. Reach for `screenManager()` only when the
 * call site is so deep in a utility chain that DI would balloon a dozen
 * signatures for no real benefit.
 */

/// Replaces `Phosphor::Screens::ScreenManager` for in-tree call sites
/// that haven't migrated to the canonical name yet.

/// Register the process-global ScreenManager pointer. Pass nullptr to
/// unregister (called by Daemon's destructor). Subsequent `screenManager()`
/// calls return whatever was last registered.
PLASMAZONES_EXPORT void setScreenManager(Phosphor::Screens::ScreenManager* manager);

/// Returns the registered ScreenManager pointer, or nullptr if none.
PLASMAZONES_EXPORT Phosphor::Screens::ScreenManager* screenManager();

/// Available geometry for @p screen, falling back to Qt's
/// `QScreen::availableGeometry()` when no ScreenManager is registered
/// (cold-start, unit tests).
PLASMAZONES_EXPORT QRect actualAvailableGeometry(QScreen* screen);

/// True if the registered ScreenManager has received its first panel
/// reading. Returns false when no manager is registered (callers gate
/// startup work on this).
PLASMAZONES_EXPORT bool isPanelGeometryReady();

/// Resolve a screen ID (physical or virtual) to the backing QScreen*.
/// Tries the registered manager's `physicalQScreenFor` first, then
/// `Phosphor::Screens::ScreenIdentity::findByIdOrName`, then primary
/// screen. Only returns nullptr if there are no screens at all.
PLASMAZONES_EXPORT QScreen* resolvePhysicalScreen(const QString& screenId);

/// Effective screen IDs from the registered manager, falling back to
/// `QGuiApplication::screens()` identifiers when no manager exists.
PLASMAZONES_EXPORT QStringList effectiveScreenIdsWithFallback();

} // namespace PlasmaZones
