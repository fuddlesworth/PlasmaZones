// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

/**
 * @file PhosphorServiceSni.h
 * @brief Umbrella header for the PhosphorServiceSni library.
 *
 * PhosphorServiceSni implements the `org.kde.StatusNotifierItem` /
 * `org.kde.StatusNotifierWatcher` system-tray protocol plus the
 * `com.canonical.dbusmenu` context-menu model that tray items use to
 * expose their right-click menus. The shell registers a
 * `StatusNotifierHost`, binds its QML tray view to
 * `StatusNotifierItemModel`, and the library handles watcher
 * registration, item discovery, icon URL publishing, and menu
 * activation.
 *
 * Icon resolution + the `image://phosphor-service-icontheme/` provider
 * are provided by the sibling `phosphor-service-icontheme` library.
 *
 * Extracted from the legacy `phosphor-services` umbrella as the last
 * of the Phase 2.0 splits documented in
 * `docs/phosphor-shell-design/04-implementation-plan.md`. With this
 * extraction the umbrella is deleted; no compat shim per the
 * `feedback_no_legacy_shims` memory.
 */

#include <PhosphorServiceSni/StatusNotifierItem.h>
#include <PhosphorServiceSni/StatusNotifierItemModel.h>
#include <PhosphorServiceSni/StatusNotifierHost.h>
#include <PhosphorServiceSni/DBusMenuModel.h>
#include <PhosphorServiceSni/QmlRegistration.h>
