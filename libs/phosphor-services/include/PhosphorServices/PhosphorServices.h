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
 * Sole remaining tenant: StatusNotifierItem (system tray) host +
 * watcher with `com.canonical.dbusmenu` support. Consumes
 * `phosphor-service-icontheme` for XDG icon-theme lookup and image
 * provider plumbing. Sibling services live in their own libraries:
 * `phosphor-service-upower` (`PhosphorServiceUPower::`),
 * `phosphor-service-mpris` (`PhosphorServiceMpris::`),
 * `phosphor-service-icontheme` (`PhosphorServiceIconTheme::`). The
 * umbrella will be deleted once SNI is extracted (Phase 2.0 final
 * step). See `docs/phosphor-shell-design/04-implementation-plan.md`.
 */

#include <PhosphorServices/StatusNotifierItem.h>
#include <PhosphorServices/StatusNotifierItemModel.h>
#include <PhosphorServices/StatusNotifierHost.h>
#include <PhosphorServices/DBusMenuModel.h>
