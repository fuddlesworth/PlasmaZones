// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorProtocol/ZoneTypes.h>
#include <PhosphorProtocol/phosphorprotocol_export.h>

#include <PhosphorDBus/Streaming.h>

#include <QDBusArgument>
#include <QDBusMetaType>

/// D-Bus marshalling for the zone / overlay value types (see ZoneTypes.h).

namespace PhosphorProtocol {

PHOSPHORPROTOCOL_EXPORT QDBusArgument& operator<<(QDBusArgument& arg, const ZoneGeometryRect& e);
PHOSPHORPROTOCOL_EXPORT const QDBusArgument& operator>>(const QDBusArgument& arg, ZoneGeometryRect& e);
PHOSPHORPROTOCOL_EXPORT QDBusArgument& operator<<(QDBusArgument& arg, const EmptyZoneEntry& e);
PHOSPHORPROTOCOL_EXPORT const QDBusArgument& operator>>(const QDBusArgument& arg, EmptyZoneEntry& e);
PHOSPHORPROTOCOL_EXPORT QDBusArgument& operator<<(QDBusArgument& arg, const SnapAssistCandidate& e);
PHOSPHORPROTOCOL_EXPORT const QDBusArgument& operator>>(const QDBusArgument& arg, SnapAssistCandidate& e);
PHOSPHORPROTOCOL_EXPORT QDBusArgument& operator<<(QDBusArgument& arg, const NamedZoneGeometry& e);
PHOSPHORPROTOCOL_EXPORT const QDBusArgument& operator>>(const QDBusArgument& arg, NamedZoneGeometry& e);

static_assert(PhosphorDBus::HasDBusStreaming<ZoneGeometryRect>::value,
              "ZoneGeometryRect missing QDBusArgument operators");
static_assert(PhosphorDBus::HasDBusStreaming<EmptyZoneEntry>::value, "EmptyZoneEntry missing QDBusArgument operators");
static_assert(PhosphorDBus::HasDBusStreaming<SnapAssistCandidate>::value,
              "SnapAssistCandidate missing QDBusArgument operators");
static_assert(PhosphorDBus::HasDBusStreaming<NamedZoneGeometry>::value,
              "NamedZoneGeometry missing QDBusArgument operators");

} // namespace PhosphorProtocol
