// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorProtocol/Types.h>
#include <PhosphorProtocol/phosphorprotocol_export.h>

#include <PhosphorDBus/Streaming.h>

#include <QDBusArgument>
#include <QDBusMetaType>

/// D-Bus transport layer for the PhosphorProtocol value types.
///
/// `Types.h` defines the value objects with no QtDBus dependency, so domain
/// libraries can name them without linking the bus. This header adds the
/// `QDBusArgument` marshallers and the one-shot metatype registration on top.
/// Include it only in code that actually serializes types over D-Bus
/// (adaptors, the daemon entry point, transport round-trip tests).
namespace PhosphorProtocol {

/// The generic compile-time marshalling check, from PhosphorDBus.
using PhosphorDBus::HasDBusStreaming;

// QDBusArgument streaming operators (implemented in marshalling.cpp)
PHOSPHORPROTOCOL_EXPORT QDBusArgument& operator<<(QDBusArgument& arg, const WindowGeometryEntry& e);
PHOSPHORPROTOCOL_EXPORT const QDBusArgument& operator>>(const QDBusArgument& arg, WindowGeometryEntry& e);
PHOSPHORPROTOCOL_EXPORT QDBusArgument& operator<<(QDBusArgument& arg, const TileRequestEntry& e);
PHOSPHORPROTOCOL_EXPORT const QDBusArgument& operator>>(const QDBusArgument& arg, TileRequestEntry& e);
PHOSPHORPROTOCOL_EXPORT QDBusArgument& operator<<(QDBusArgument& arg, const SnapAllResultEntry& e);
PHOSPHORPROTOCOL_EXPORT const QDBusArgument& operator>>(const QDBusArgument& arg, SnapAllResultEntry& e);
PHOSPHORPROTOCOL_EXPORT QDBusArgument& operator<<(QDBusArgument& arg, const SnapConfirmationEntry& e);
PHOSPHORPROTOCOL_EXPORT const QDBusArgument& operator>>(const QDBusArgument& arg, SnapConfirmationEntry& e);
PHOSPHORPROTOCOL_EXPORT QDBusArgument& operator<<(QDBusArgument& arg, const WindowOpenedEntry& e);
PHOSPHORPROTOCOL_EXPORT const QDBusArgument& operator>>(const QDBusArgument& arg, WindowOpenedEntry& e);
PHOSPHORPROTOCOL_EXPORT QDBusArgument& operator<<(QDBusArgument& arg, const WindowStateEntry& e);
PHOSPHORPROTOCOL_EXPORT const QDBusArgument& operator>>(const QDBusArgument& arg, WindowStateEntry& e);
PHOSPHORPROTOCOL_EXPORT QDBusArgument& operator<<(QDBusArgument& arg, const UnfloatRestoreResult& e);
PHOSPHORPROTOCOL_EXPORT const QDBusArgument& operator>>(const QDBusArgument& arg, UnfloatRestoreResult& e);
PHOSPHORPROTOCOL_EXPORT QDBusArgument& operator<<(QDBusArgument& arg, const ZoneGeometryRect& e);
PHOSPHORPROTOCOL_EXPORT const QDBusArgument& operator>>(const QDBusArgument& arg, ZoneGeometryRect& e);
PHOSPHORPROTOCOL_EXPORT QDBusArgument& operator<<(QDBusArgument& arg, const EmptyZoneEntry& e);
PHOSPHORPROTOCOL_EXPORT const QDBusArgument& operator>>(const QDBusArgument& arg, EmptyZoneEntry& e);
PHOSPHORPROTOCOL_EXPORT QDBusArgument& operator<<(QDBusArgument& arg, const SnapAssistCandidate& e);
PHOSPHORPROTOCOL_EXPORT const QDBusArgument& operator>>(const QDBusArgument& arg, SnapAssistCandidate& e);
PHOSPHORPROTOCOL_EXPORT QDBusArgument& operator<<(QDBusArgument& arg, const NamedZoneGeometry& e);
PHOSPHORPROTOCOL_EXPORT const QDBusArgument& operator>>(const QDBusArgument& arg, NamedZoneGeometry& e);
PHOSPHORPROTOCOL_EXPORT QDBusArgument& operator<<(QDBusArgument& arg, const AlgorithmInfoEntry& e);
PHOSPHORPROTOCOL_EXPORT const QDBusArgument& operator>>(const QDBusArgument& arg, AlgorithmInfoEntry& e);
PHOSPHORPROTOCOL_EXPORT QDBusArgument& operator<<(QDBusArgument& arg, const BridgeRegistrationResult& e);
PHOSPHORPROTOCOL_EXPORT const QDBusArgument& operator>>(const QDBusArgument& arg, BridgeRegistrationResult& e);
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
PHOSPHORPROTOCOL_EXPORT QDBusArgument& operator<<(QDBusArgument& arg, const PreTileGeometryEntry& e);
PHOSPHORPROTOCOL_EXPORT const QDBusArgument& operator>>(const QDBusArgument& arg, PreTileGeometryEntry& e);
PHOSPHORPROTOCOL_EXPORT QDBusArgument& operator<<(QDBusArgument& arg, const DragPolicy& p);
PHOSPHORPROTOCOL_EXPORT const QDBusArgument& operator>>(const QDBusArgument& arg, DragPolicy& p);
PHOSPHORPROTOCOL_EXPORT QDBusArgument& operator<<(QDBusArgument& arg, const DragOutcome& o);
PHOSPHORPROTOCOL_EXPORT const QDBusArgument& operator>>(const QDBusArgument& arg, DragOutcome& o);

// Compile-time verification that all D-Bus struct types have streaming operators.
// If you add a new struct to Types.h and forget the operator<</>> declarations
// above, the build fails here with a clear message instead of crashing at
// runtime.
static_assert(HasDBusStreaming<WindowGeometryEntry>::value, "WindowGeometryEntry missing QDBusArgument operators");
static_assert(HasDBusStreaming<TileRequestEntry>::value, "TileRequestEntry missing QDBusArgument operators");
static_assert(HasDBusStreaming<SnapAllResultEntry>::value, "SnapAllResultEntry missing QDBusArgument operators");
static_assert(HasDBusStreaming<SnapConfirmationEntry>::value, "SnapConfirmationEntry missing QDBusArgument operators");
static_assert(HasDBusStreaming<WindowOpenedEntry>::value, "WindowOpenedEntry missing QDBusArgument operators");
static_assert(HasDBusStreaming<WindowStateEntry>::value, "WindowStateEntry missing QDBusArgument operators");
static_assert(HasDBusStreaming<UnfloatRestoreResult>::value, "UnfloatRestoreResult missing QDBusArgument operators");
static_assert(HasDBusStreaming<ZoneGeometryRect>::value, "ZoneGeometryRect missing QDBusArgument operators");
static_assert(HasDBusStreaming<EmptyZoneEntry>::value, "EmptyZoneEntry missing QDBusArgument operators");
static_assert(HasDBusStreaming<SnapAssistCandidate>::value, "SnapAssistCandidate missing QDBusArgument operators");
static_assert(HasDBusStreaming<NamedZoneGeometry>::value, "NamedZoneGeometry missing QDBusArgument operators");
static_assert(HasDBusStreaming<AlgorithmInfoEntry>::value, "AlgorithmInfoEntry missing QDBusArgument operators");
static_assert(HasDBusStreaming<BridgeRegistrationResult>::value,
              "BridgeRegistrationResult missing QDBusArgument operators");
static_assert(HasDBusStreaming<MoveTargetResult>::value, "MoveTargetResult missing QDBusArgument operators");
static_assert(HasDBusStreaming<FocusTargetResult>::value, "FocusTargetResult missing QDBusArgument operators");
static_assert(HasDBusStreaming<CycleTargetResult>::value, "CycleTargetResult missing QDBusArgument operators");
static_assert(HasDBusStreaming<SwapTargetResult>::value, "SwapTargetResult missing QDBusArgument operators");
static_assert(HasDBusStreaming<RestoreTargetResult>::value, "RestoreTargetResult missing QDBusArgument operators");
static_assert(HasDBusStreaming<PreTileGeometryEntry>::value, "PreTileGeometryEntry missing QDBusArgument operators");
static_assert(HasDBusStreaming<DragPolicy>::value, "DragPolicy missing QDBusArgument operators");
static_assert(HasDBusStreaming<DragOutcome>::value, "DragOutcome missing QDBusArgument operators");

/// Call once at startup (daemon and plugin) to register types with Qt D-Bus
PHOSPHORPROTOCOL_EXPORT void registerWireTypes();

} // namespace PhosphorProtocol
