// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

/**
 * @file PhosphorServices.h
 * @brief Umbrella header for the PhosphorServices library.
 *
 * PhosphorServices provides DBus + platform-integration primitives for
 * Phosphor-based desktop shells. The library has no UI of its own —
 * shells (and downstream apps) consume the C++/QML APIs and render the
 * results however they like.
 *
 * First tenant: StatusNotifierItem (system tray) host + watcher with
 * full XDG icon-theme spec lookup and com.canonical.dbusmenu support.
 * MPRIS and the icon-theme resolver are co-tenants. Other desktop
 * services are extracted per-domain into `phosphor-service-*` sibling
 * libraries (see docs/phosphor-shell-design/04-implementation-plan.md);
 * UPower is the first such extraction (`PhosphorServiceUPower::`).
 */

#include <PhosphorServices/StatusNotifierItem.h>
#include <PhosphorServices/StatusNotifierItemModel.h>
#include <PhosphorServices/StatusNotifierHost.h>
#include <PhosphorServices/DBusMenuModel.h>
#include <PhosphorServices/IconThemeResolver.h>
