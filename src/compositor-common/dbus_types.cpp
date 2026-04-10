// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dbus_types.h"

namespace PlasmaZones {

QDBusArgument& operator<<(QDBusArgument& arg, const WindowGeometryEntry& e)
{
    arg.beginStructure();
    arg << e.windowId << e.x << e.y << e.width << e.height;
    arg.endStructure();
    return arg;
}

const QDBusArgument& operator>>(const QDBusArgument& arg, WindowGeometryEntry& e)
{
    arg.beginStructure();
    arg >> e.windowId >> e.x >> e.y >> e.width >> e.height;
    arg.endStructure();
    return arg;
}

QDBusArgument& operator<<(QDBusArgument& arg, const TileRequestEntry& e)
{
    arg.beginStructure();
    arg << e.windowId << e.x << e.y << e.width << e.height << e.zoneId << e.screenId << e.monocle << e.floating;
    arg.endStructure();
    return arg;
}

const QDBusArgument& operator>>(const QDBusArgument& arg, TileRequestEntry& e)
{
    arg.beginStructure();
    arg >> e.windowId >> e.x >> e.y >> e.width >> e.height >> e.zoneId >> e.screenId >> e.monocle >> e.floating;
    arg.endStructure();
    return arg;
}

QDBusArgument& operator<<(QDBusArgument& arg, const SnapAllResultEntry& e)
{
    arg.beginStructure();
    arg << e.windowId << e.targetZoneId << e.sourceZoneId << e.x << e.y << e.width << e.height;
    arg.endStructure();
    return arg;
}

const QDBusArgument& operator>>(const QDBusArgument& arg, SnapAllResultEntry& e)
{
    arg.beginStructure();
    arg >> e.windowId >> e.targetZoneId >> e.sourceZoneId >> e.x >> e.y >> e.width >> e.height;
    arg.endStructure();
    return arg;
}

QDBusArgument& operator<<(QDBusArgument& arg, const SnapConfirmationEntry& e)
{
    arg.beginStructure();
    arg << e.windowId << e.zoneId << e.screenId << e.isRestore;
    arg.endStructure();
    return arg;
}

const QDBusArgument& operator>>(const QDBusArgument& arg, SnapConfirmationEntry& e)
{
    arg.beginStructure();
    arg >> e.windowId >> e.zoneId >> e.screenId >> e.isRestore;
    arg.endStructure();
    return arg;
}

QDBusArgument& operator<<(QDBusArgument& arg, const WindowOpenedEntry& e)
{
    arg.beginStructure();
    arg << e.windowId << e.screenId << e.minWidth << e.minHeight;
    arg.endStructure();
    return arg;
}

const QDBusArgument& operator>>(const QDBusArgument& arg, WindowOpenedEntry& e)
{
    arg.beginStructure();
    arg >> e.windowId >> e.screenId >> e.minWidth >> e.minHeight;
    arg.endStructure();
    return arg;
}

QDBusArgument& operator<<(QDBusArgument& arg, const WindowStateEntry& e)
{
    arg.beginStructure();
    arg << e.windowId << e.zoneId << e.screenId << e.isFloating << e.changeType << e.zoneIds << e.isSticky;
    arg.endStructure();
    return arg;
}

const QDBusArgument& operator>>(const QDBusArgument& arg, WindowStateEntry& e)
{
    arg.beginStructure();
    arg >> e.windowId >> e.zoneId >> e.screenId >> e.isFloating >> e.changeType >> e.zoneIds >> e.isSticky;
    arg.endStructure();
    return arg;
}

QDBusArgument& operator<<(QDBusArgument& arg, const UnfloatRestoreResult& e)
{
    arg.beginStructure();
    arg << e.found << e.zoneIds << e.screenName << e.x << e.y << e.width << e.height;
    arg.endStructure();
    return arg;
}

const QDBusArgument& operator>>(const QDBusArgument& arg, UnfloatRestoreResult& e)
{
    arg.beginStructure();
    arg >> e.found >> e.zoneIds >> e.screenName >> e.x >> e.y >> e.width >> e.height;
    arg.endStructure();
    return arg;
}

QDBusArgument& operator<<(QDBusArgument& arg, const ZoneGeometryRect& e)
{
    arg.beginStructure();
    arg << e.x << e.y << e.width << e.height;
    arg.endStructure();
    return arg;
}

const QDBusArgument& operator>>(const QDBusArgument& arg, ZoneGeometryRect& e)
{
    arg.beginStructure();
    arg >> e.x >> e.y >> e.width >> e.height;
    arg.endStructure();
    return arg;
}

QDBusArgument& operator<<(QDBusArgument& arg, const EmptyZoneEntry& e)
{
    arg.beginStructure();
    arg << e.zoneId << e.x << e.y << e.width << e.height << e.borderWidth << e.borderRadius << e.useCustomColors
        << e.highlightColor << e.inactiveColor << e.borderColor << e.activeOpacity << e.inactiveOpacity;
    arg.endStructure();
    return arg;
}

const QDBusArgument& operator>>(const QDBusArgument& arg, EmptyZoneEntry& e)
{
    arg.beginStructure();
    arg >> e.zoneId >> e.x >> e.y >> e.width >> e.height >> e.borderWidth >> e.borderRadius >> e.useCustomColors
        >> e.highlightColor >> e.inactiveColor >> e.borderColor >> e.activeOpacity >> e.inactiveOpacity;
    arg.endStructure();
    return arg;
}

QDBusArgument& operator<<(QDBusArgument& arg, const SnapAssistCandidate& e)
{
    arg.beginStructure();
    arg << e.windowId << e.compositorHandle << e.icon << e.caption;
    arg.endStructure();
    return arg;
}

const QDBusArgument& operator>>(const QDBusArgument& arg, SnapAssistCandidate& e)
{
    arg.beginStructure();
    arg >> e.windowId >> e.compositorHandle >> e.icon >> e.caption;
    arg.endStructure();
    return arg;
}

QDBusArgument& operator<<(QDBusArgument& arg, const NamedZoneGeometry& e)
{
    arg.beginStructure();
    arg << e.zoneId << e.x << e.y << e.width << e.height;
    arg.endStructure();
    return arg;
}

const QDBusArgument& operator>>(const QDBusArgument& arg, NamedZoneGeometry& e)
{
    arg.beginStructure();
    arg >> e.zoneId >> e.x >> e.y >> e.width >> e.height;
    arg.endStructure();
    return arg;
}

QDBusArgument& operator<<(QDBusArgument& arg, const AlgorithmInfoEntry& e)
{
    arg.beginStructure();
    arg << e.id << e.name << e.description << e.supportsMasterCount << e.supportsSplitRatio << e.centerLayout
        << e.producesOverlappingZones << e.defaultSplitRatio << e.defaultMaxWindows << e.isScripted
        << e.zoneNumberDisplay << e.isUserScript << e.supportsMemory;
    arg.endStructure();
    return arg;
}

const QDBusArgument& operator>>(const QDBusArgument& arg, AlgorithmInfoEntry& e)
{
    arg.beginStructure();
    arg >> e.id >> e.name >> e.description >> e.supportsMasterCount >> e.supportsSplitRatio >> e.centerLayout
        >> e.producesOverlappingZones >> e.defaultSplitRatio >> e.defaultMaxWindows >> e.isScripted
        >> e.zoneNumberDisplay >> e.isUserScript >> e.supportsMemory;
    arg.endStructure();
    return arg;
}

QDBusArgument& operator<<(QDBusArgument& arg, const BridgeRegistrationResult& e)
{
    arg.beginStructure();
    arg << e.apiVersion << e.bridgeName << e.sessionId;
    arg.endStructure();
    return arg;
}

const QDBusArgument& operator>>(const QDBusArgument& arg, BridgeRegistrationResult& e)
{
    arg.beginStructure();
    arg >> e.apiVersion >> e.bridgeName >> e.sessionId;
    arg.endStructure();
    return arg;
}

QDBusArgument& operator<<(QDBusArgument& arg, const MoveTargetResult& e)
{
    arg.beginStructure();
    arg << e.success << e.reason << e.zoneId << e.x << e.y << e.width << e.height << e.sourceZoneId << e.screenName;
    arg.endStructure();
    return arg;
}

const QDBusArgument& operator>>(const QDBusArgument& arg, MoveTargetResult& e)
{
    arg.beginStructure();
    arg >> e.success >> e.reason >> e.zoneId >> e.x >> e.y >> e.width >> e.height >> e.sourceZoneId >> e.screenName;
    arg.endStructure();
    return arg;
}

QDBusArgument& operator<<(QDBusArgument& arg, const FocusTargetResult& e)
{
    arg.beginStructure();
    arg << e.success << e.reason << e.windowIdToActivate << e.sourceZoneId << e.targetZoneId << e.screenName;
    arg.endStructure();
    return arg;
}

const QDBusArgument& operator>>(const QDBusArgument& arg, FocusTargetResult& e)
{
    arg.beginStructure();
    arg >> e.success >> e.reason >> e.windowIdToActivate >> e.sourceZoneId >> e.targetZoneId >> e.screenName;
    arg.endStructure();
    return arg;
}

QDBusArgument& operator<<(QDBusArgument& arg, const CycleTargetResult& e)
{
    arg.beginStructure();
    arg << e.success << e.reason << e.windowIdToActivate << e.zoneId << e.screenName;
    arg.endStructure();
    return arg;
}

const QDBusArgument& operator>>(const QDBusArgument& arg, CycleTargetResult& e)
{
    arg.beginStructure();
    arg >> e.success >> e.reason >> e.windowIdToActivate >> e.zoneId >> e.screenName;
    arg.endStructure();
    return arg;
}

QDBusArgument& operator<<(QDBusArgument& arg, const SwapTargetResult& e)
{
    arg.beginStructure();
    arg << e.success << e.reason << e.windowId1 << e.x1 << e.y1 << e.w1 << e.h1 << e.zoneId1 << e.windowId2 << e.x2
        << e.y2 << e.w2 << e.h2 << e.zoneId2 << e.screenName << e.sourceZoneId << e.targetZoneId;
    arg.endStructure();
    return arg;
}

const QDBusArgument& operator>>(const QDBusArgument& arg, SwapTargetResult& e)
{
    arg.beginStructure();
    arg >> e.success >> e.reason >> e.windowId1 >> e.x1 >> e.y1 >> e.w1 >> e.h1 >> e.zoneId1 >> e.windowId2 >> e.x2
        >> e.y2 >> e.w2 >> e.h2 >> e.zoneId2 >> e.screenName >> e.sourceZoneId >> e.targetZoneId;
    arg.endStructure();
    return arg;
}

QDBusArgument& operator<<(QDBusArgument& arg, const RestoreTargetResult& e)
{
    arg.beginStructure();
    arg << e.success << e.found << e.x << e.y << e.width << e.height;
    arg.endStructure();
    return arg;
}

const QDBusArgument& operator>>(const QDBusArgument& arg, RestoreTargetResult& e)
{
    arg.beginStructure();
    arg >> e.success >> e.found >> e.x >> e.y >> e.width >> e.height;
    arg.endStructure();
    return arg;
}

void registerDBusTypes()
{
    qDBusRegisterMetaType<WindowGeometryEntry>();
    qDBusRegisterMetaType<WindowGeometryList>();
    qDBusRegisterMetaType<TileRequestEntry>();
    qDBusRegisterMetaType<TileRequestList>();
    qDBusRegisterMetaType<SnapAllResultEntry>();
    qDBusRegisterMetaType<SnapAllResultList>();
    qDBusRegisterMetaType<SnapConfirmationEntry>();
    qDBusRegisterMetaType<SnapConfirmationList>();
    qDBusRegisterMetaType<WindowOpenedEntry>();
    qDBusRegisterMetaType<WindowOpenedList>();
    qDBusRegisterMetaType<WindowStateEntry>();
    qDBusRegisterMetaType<WindowStateList>();
    qDBusRegisterMetaType<UnfloatRestoreResult>();
    qDBusRegisterMetaType<ZoneGeometryRect>();
    qDBusRegisterMetaType<ZoneGeometryList>();
    qDBusRegisterMetaType<EmptyZoneEntry>();
    qDBusRegisterMetaType<EmptyZoneList>();
    qDBusRegisterMetaType<SnapAssistCandidate>();
    qDBusRegisterMetaType<SnapAssistCandidateList>();
    qDBusRegisterMetaType<NamedZoneGeometry>();
    qDBusRegisterMetaType<NamedZoneGeometryList>();
    qDBusRegisterMetaType<AlgorithmInfoEntry>();
    qDBusRegisterMetaType<AlgorithmInfoList>();
    qDBusRegisterMetaType<BridgeRegistrationResult>();
    qDBusRegisterMetaType<MoveTargetResult>();
    qDBusRegisterMetaType<FocusTargetResult>();
    qDBusRegisterMetaType<CycleTargetResult>();
    qDBusRegisterMetaType<SwapTargetResult>();
    qDBusRegisterMetaType<RestoreTargetResult>();
}

} // namespace PlasmaZones
