// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorProtocol/NavigationTypes.h>
#include <PhosphorProtocol/phosphorprotocol_export.h>

#include <PhosphorDBus/Streaming.h>

#include <QDBusArgument>
#include <QDBusMetaType>

/// D-Bus marshalling for the snap-navigation value types (see NavigationTypes.h).

namespace PhosphorProtocol {

PHOSPHORPROTOCOL_EXPORT QDBusArgument& operator<<(QDBusArgument& arg, const SnapAllResultEntry& e);
PHOSPHORPROTOCOL_EXPORT const QDBusArgument& operator>>(const QDBusArgument& arg, SnapAllResultEntry& e);
PHOSPHORPROTOCOL_EXPORT QDBusArgument& operator<<(QDBusArgument& arg, const MoveTargetResult& e);
PHOSPHORPROTOCOL_EXPORT const QDBusArgument& operator>>(const QDBusArgument& arg, MoveTargetResult& e);
PHOSPHORPROTOCOL_EXPORT QDBusArgument& operator<<(QDBusArgument& arg, const FocusTargetResult& e);
PHOSPHORPROTOCOL_EXPORT const QDBusArgument& operator>>(const QDBusArgument& arg, FocusTargetResult& e);
PHOSPHORPROTOCOL_EXPORT QDBusArgument& operator<<(QDBusArgument& arg, const CycleTargetResult& e);
PHOSPHORPROTOCOL_EXPORT const QDBusArgument& operator>>(const QDBusArgument& arg, CycleTargetResult& e);
PHOSPHORPROTOCOL_EXPORT QDBusArgument& operator<<(QDBusArgument& arg, const SwapTargetResult& e);
PHOSPHORPROTOCOL_EXPORT const QDBusArgument& operator>>(const QDBusArgument& arg, SwapTargetResult& e);
PHOSPHORPROTOCOL_EXPORT QDBusArgument& operator<<(QDBusArgument& arg, const RestoreTargetResult& e);
PHOSPHORPROTOCOL_EXPORT const QDBusArgument& operator>>(const QDBusArgument& arg, RestoreTargetResult& e);

static_assert(PhosphorDBus::HasDBusStreaming<SnapAllResultEntry>::value,
              "SnapAllResultEntry missing QDBusArgument operators");
static_assert(PhosphorDBus::HasDBusStreaming<MoveTargetResult>::value,
              "MoveTargetResult missing QDBusArgument operators");
static_assert(PhosphorDBus::HasDBusStreaming<FocusTargetResult>::value,
              "FocusTargetResult missing QDBusArgument operators");
static_assert(PhosphorDBus::HasDBusStreaming<CycleTargetResult>::value,
              "CycleTargetResult missing QDBusArgument operators");
static_assert(PhosphorDBus::HasDBusStreaming<SwapTargetResult>::value,
              "SwapTargetResult missing QDBusArgument operators");
static_assert(PhosphorDBus::HasDBusStreaming<RestoreTargetResult>::value,
              "RestoreTargetResult missing QDBusArgument operators");

} // namespace PhosphorProtocol
