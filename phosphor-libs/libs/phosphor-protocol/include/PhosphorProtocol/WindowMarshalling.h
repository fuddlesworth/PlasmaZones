// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorProtocol/WindowTypes.h>
#include <PhosphorProtocol/phosphorprotocol_export.h>

#include <PhosphorDBus/Streaming.h>

#include <QDBusArgument>
#include <QDBusMetaType>

/// D-Bus marshalling for the window-tracking value types (see WindowTypes.h).

namespace PhosphorProtocol {

PHOSPHORPROTOCOL_EXPORT QDBusArgument& operator<<(QDBusArgument& arg, const WindowGeometryEntry& e);
PHOSPHORPROTOCOL_EXPORT const QDBusArgument& operator>>(const QDBusArgument& arg, WindowGeometryEntry& e);
PHOSPHORPROTOCOL_EXPORT QDBusArgument& operator<<(QDBusArgument& arg, const SnapConfirmationEntry& e);
PHOSPHORPROTOCOL_EXPORT const QDBusArgument& operator>>(const QDBusArgument& arg, SnapConfirmationEntry& e);
PHOSPHORPROTOCOL_EXPORT QDBusArgument& operator<<(QDBusArgument& arg, const WindowOpenedEntry& e);
PHOSPHORPROTOCOL_EXPORT const QDBusArgument& operator>>(const QDBusArgument& arg, WindowOpenedEntry& e);
PHOSPHORPROTOCOL_EXPORT QDBusArgument& operator<<(QDBusArgument& arg, const WindowStateEntry& e);
PHOSPHORPROTOCOL_EXPORT const QDBusArgument& operator>>(const QDBusArgument& arg, WindowStateEntry& e);
PHOSPHORPROTOCOL_EXPORT QDBusArgument& operator<<(QDBusArgument& arg, const UnfloatRestoreResult& e);
PHOSPHORPROTOCOL_EXPORT const QDBusArgument& operator>>(const QDBusArgument& arg, UnfloatRestoreResult& e);

static_assert(PhosphorDBus::HasDBusStreaming<WindowGeometryEntry>::value,
              "WindowGeometryEntry missing QDBusArgument operators");
static_assert(PhosphorDBus::HasDBusStreaming<SnapConfirmationEntry>::value,
              "SnapConfirmationEntry missing QDBusArgument operators");
static_assert(PhosphorDBus::HasDBusStreaming<WindowOpenedEntry>::value,
              "WindowOpenedEntry missing QDBusArgument operators");
static_assert(PhosphorDBus::HasDBusStreaming<WindowStateEntry>::value,
              "WindowStateEntry missing QDBusArgument operators");
static_assert(PhosphorDBus::HasDBusStreaming<UnfloatRestoreResult>::value,
              "UnfloatRestoreResult missing QDBusArgument operators");

} // namespace PhosphorProtocol
