// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorProtocol/AutotileMarshalling.h>
#include <PhosphorProtocol/BridgeMarshalling.h>
#include <PhosphorProtocol/DragMarshalling.h>
#include <PhosphorProtocol/NavigationMarshalling.h>
#include <PhosphorProtocol/Registration.h>
#include <PhosphorProtocol/WindowMarshalling.h>
#include <PhosphorProtocol/ZoneMarshalling.h>

namespace PhosphorProtocol {

QDBusArgument& operator<<(QDBusArgument& arg, const WindowGeometryEntry& e)
{
    arg.beginStructure();
    arg << e.windowId << e.x << e.y << e.width << e.height << e.screenId;
    arg.endStructure();
    return arg;
}

const QDBusArgument& operator>>(const QDBusArgument& arg, WindowGeometryEntry& e)
{
    arg.beginStructure();
    arg >> e.windowId >> e.x >> e.y >> e.width >> e.height >> e.screenId;
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
        << e.zoneNumberDisplay << e.isUserScript << e.supportsMemory << e.supportsSingleWindow;
    arg.endStructure();
    return arg;
}

const QDBusArgument& operator>>(const QDBusArgument& arg, AlgorithmInfoEntry& e)
{
    arg.beginStructure();
    arg >> e.id >> e.name >> e.description >> e.supportsMasterCount >> e.supportsSplitRatio >> e.centerLayout
        >> e.producesOverlappingZones >> e.defaultSplitRatio >> e.defaultMaxWindows >> e.isScripted
        >> e.zoneNumberDisplay >> e.isUserScript >> e.supportsMemory >> e.supportsSingleWindow;
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
        << e.y2 << e.w2 << e.h2 << e.zoneId2 << e.screenName << e.sourceZoneId << e.targetZoneId << e.screenName2;
    arg.endStructure();
    return arg;
}

const QDBusArgument& operator>>(const QDBusArgument& arg, SwapTargetResult& e)
{
    arg.beginStructure();
    arg >> e.success >> e.reason >> e.windowId1 >> e.x1 >> e.y1 >> e.w1 >> e.h1 >> e.zoneId1 >> e.windowId2 >> e.x2
        >> e.y2 >> e.w2 >> e.h2 >> e.zoneId2 >> e.screenName >> e.sourceZoneId >> e.targetZoneId >> e.screenName2;
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

QDBusArgument& operator<<(QDBusArgument& arg, const PreTileGeometryEntry& e)
{
    arg.beginStructure();
    arg << e.appId << e.x << e.y << e.width << e.height << e.screenId;
    arg.endStructure();
    return arg;
}

const QDBusArgument& operator>>(const QDBusArgument& arg, PreTileGeometryEntry& e)
{
    arg.beginStructure();
    arg >> e.appId >> e.x >> e.y >> e.width >> e.height >> e.screenId;
    arg.endStructure();
    return arg;
}

QDBusArgument& operator<<(QDBusArgument& arg, const DragPolicy& p)
{
    // Wire format kept as `(bbbbbss)` — bypassReason is serialized as its
    // legacy string representation so effects/daemons built at different
    // revisions still interoperate within the same ApiVersion.
    arg.beginStructure();
    arg << p.streamDragMoved << p.showOverlay << p.grabKeyboard << p.captureGeometry << p.immediateFloatOnStart
        << p.screenId << toWireString(p.bypassReason);
    arg.endStructure();
    return arg;
}

const QDBusArgument& operator>>(const QDBusArgument& arg, DragPolicy& p)
{
    arg.beginStructure();
    QString bypassWire;
    arg >> p.streamDragMoved >> p.showOverlay >> p.grabKeyboard >> p.captureGeometry >> p.immediateFloatOnStart
        >> p.screenId >> bypassWire;
    arg.endStructure();
    p.bypassReason = bypassReasonFromWireString(bypassWire);
    return arg;
}

QDBusArgument& operator<<(QDBusArgument& arg, const DragOutcome& o)
{
    arg.beginStructure();
    arg << o.action << o.windowId << o.targetScreenId << o.x << o.y << o.width << o.height << o.zoneId
        << o.skipAnimation << o.requestSnapAssist << o.emptyZones;
    arg.endStructure();
    return arg;
}

const QDBusArgument& operator>>(const QDBusArgument& arg, DragOutcome& o)
{
    arg.beginStructure();
    arg >> o.action >> o.windowId >> o.targetScreenId >> o.x >> o.y >> o.width >> o.height >> o.zoneId
        >> o.skipAnimation >> o.requestSnapAssist >> o.emptyZones;
    arg.endStructure();
    return arg;
}

void registerWireTypes()
{
    // Each type is registered under its fully-qualified name
    // "PhosphorProtocol::Foo" — the name Q_DECLARE_METATYPE records at global
    // scope, and the name moc records for every adaptor slot/signal parameter
    // (all such declarations are fully qualified). The two agree, so D-Bus
    // dispatch resolves the slot and its demarshaller without an unqualified
    // alias. Keep adaptor type spellings fully qualified — an unqualified slot
    // parameter would make moc record "Foo", which is not registered, and
    // crash dispatch at runtime (see the dbus_adaptor_routing integration
    // test for that failure mode).

#define P_REGISTER_DBUS_TYPE(Type) qDBusRegisterMetaType<Type>()

    P_REGISTER_DBUS_TYPE(WindowGeometryEntry);
    P_REGISTER_DBUS_TYPE(WindowGeometryList);
    P_REGISTER_DBUS_TYPE(TileRequestEntry);
    P_REGISTER_DBUS_TYPE(TileRequestList);
    P_REGISTER_DBUS_TYPE(SnapAllResultEntry);
    P_REGISTER_DBUS_TYPE(SnapAllResultList);
    P_REGISTER_DBUS_TYPE(SnapConfirmationEntry);
    P_REGISTER_DBUS_TYPE(SnapConfirmationList);
    P_REGISTER_DBUS_TYPE(WindowOpenedEntry);
    P_REGISTER_DBUS_TYPE(WindowOpenedList);
    P_REGISTER_DBUS_TYPE(WindowStateEntry);
    P_REGISTER_DBUS_TYPE(WindowStateList);
    P_REGISTER_DBUS_TYPE(UnfloatRestoreResult);
    P_REGISTER_DBUS_TYPE(ZoneGeometryRect);
    P_REGISTER_DBUS_TYPE(ZoneGeometryList);
    P_REGISTER_DBUS_TYPE(EmptyZoneEntry);
    P_REGISTER_DBUS_TYPE(EmptyZoneList);
    P_REGISTER_DBUS_TYPE(SnapAssistCandidate);
    P_REGISTER_DBUS_TYPE(SnapAssistCandidateList);
    P_REGISTER_DBUS_TYPE(NamedZoneGeometry);
    P_REGISTER_DBUS_TYPE(NamedZoneGeometryList);
    P_REGISTER_DBUS_TYPE(AlgorithmInfoEntry);
    P_REGISTER_DBUS_TYPE(AlgorithmInfoList);
    P_REGISTER_DBUS_TYPE(BridgeRegistrationResult);
    P_REGISTER_DBUS_TYPE(MoveTargetResult);
    P_REGISTER_DBUS_TYPE(FocusTargetResult);
    P_REGISTER_DBUS_TYPE(CycleTargetResult);
    P_REGISTER_DBUS_TYPE(SwapTargetResult);
    P_REGISTER_DBUS_TYPE(RestoreTargetResult);
    P_REGISTER_DBUS_TYPE(PreTileGeometryEntry);
    P_REGISTER_DBUS_TYPE(PreTileGeometryList);
    P_REGISTER_DBUS_TYPE(DragPolicy);
    P_REGISTER_DBUS_TYPE(DragOutcome);

#undef P_REGISTER_DBUS_TYPE
}

} // namespace PhosphorProtocol
