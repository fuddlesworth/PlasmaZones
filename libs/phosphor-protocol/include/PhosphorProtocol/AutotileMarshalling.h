// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorProtocol/AutotileTypes.h>
#include <PhosphorProtocol/phosphorprotocol_export.h>

#include <PhosphorDBus/Streaming.h>

#include <QDBusArgument>
#include <QDBusMetaType>

/// D-Bus marshalling for the autotile value types (see AutotileTypes.h).

namespace PhosphorProtocol {

PHOSPHORPROTOCOL_EXPORT QDBusArgument& operator<<(QDBusArgument& arg, const TileRequestEntry& e);
PHOSPHORPROTOCOL_EXPORT const QDBusArgument& operator>>(const QDBusArgument& arg, TileRequestEntry& e);
PHOSPHORPROTOCOL_EXPORT QDBusArgument& operator<<(QDBusArgument& arg, const AlgorithmInfoEntry& e);
PHOSPHORPROTOCOL_EXPORT const QDBusArgument& operator>>(const QDBusArgument& arg, AlgorithmInfoEntry& e);
PHOSPHORPROTOCOL_EXPORT QDBusArgument& operator<<(QDBusArgument& arg, const PreTileGeometryEntry& e);
PHOSPHORPROTOCOL_EXPORT const QDBusArgument& operator>>(const QDBusArgument& arg, PreTileGeometryEntry& e);

static_assert(PhosphorDBus::HasDBusStreaming<TileRequestEntry>::value,
              "TileRequestEntry missing QDBusArgument operators");
static_assert(PhosphorDBus::HasDBusStreaming<AlgorithmInfoEntry>::value,
              "AlgorithmInfoEntry missing QDBusArgument operators");
static_assert(PhosphorDBus::HasDBusStreaming<PreTileGeometryEntry>::value,
              "PreTileGeometryEntry missing QDBusArgument operators");

} // namespace PhosphorProtocol
