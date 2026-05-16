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
 * Future siblings live behind the same `PhosphorServices::` namespace:
 * `org.freedesktop.Notifications`, MPRIS, UPower, NetworkManager,
 * logind, ext-session-lock-v1, ext-idle-notify-v1.
 */

#include <PhosphorServices/StatusNotifierItem.h>
#include <PhosphorServices/StatusNotifierItemModel.h>
#include <PhosphorServices/StatusNotifierHost.h>
#include <PhosphorServices/DBusMenuModel.h>
#include <PhosphorServices/IconThemeResolver.h>
