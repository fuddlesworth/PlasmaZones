// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dbus_types.h"

#include <QDebug>
#include <QLatin1String>

namespace PlasmaZones {

namespace {
// Wire-format strings for DragBypassReason. Kept as a single source of truth
// so toWireString() and bypassReasonFromWireString() can never drift.
constexpr QLatin1String kBypassAutotileScreen("autotile_screen");
constexpr QLatin1String kBypassSnappingDisabled("snapping_disabled");
constexpr QLatin1String kBypassContextDisabled("context_disabled");
} // namespace

QString toWireString(DragBypassReason r)
{
    switch (r) {
    case DragBypassReason::None:
        return {};
    case DragBypassReason::AutotileScreen:
        return kBypassAutotileScreen;
    case DragBypassReason::SnappingDisabled:
        return kBypassSnappingDisabled;
    case DragBypassReason::ContextDisabled:
        return kBypassContextDisabled;
    }
    return {};
}

DragBypassReason bypassReasonFromWireString(const QString& s)
{
    if (s.isEmpty()) {
        return DragBypassReason::None;
    }
    if (s == kBypassAutotileScreen) {
        return DragBypassReason::AutotileScreen;
    }
    if (s == kBypassSnappingDisabled) {
        return DragBypassReason::SnappingDisabled;
    }
    if (s == kBypassContextDisabled) {
        return DragBypassReason::ContextDisabled;
    }
    qWarning() << "bypassReasonFromWireString: unknown wire value" << s << "— mapping to None";
    return DragBypassReason::None;
}

QDebug operator<<(QDebug debug, DragBypassReason r)
{
    QDebugStateSaver saver(debug);
    debug.nospace();
    switch (r) {
    case DragBypassReason::None:
        debug << "DragBypassReason::None";
        break;
    case DragBypassReason::AutotileScreen:
        debug << "DragBypassReason::AutotileScreen";
        break;
    case DragBypassReason::SnappingDisabled:
        debug << "DragBypassReason::SnappingDisabled";
        break;
    case DragBypassReason::ContextDisabled:
        debug << "DragBypassReason::ContextDisabled";
        break;
    }
    return debug;
}

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
    p.bypassReason = bypassReasonFromWireString(bypassWire);
    arg.endStructure();
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

void registerDBusTypes()
{
    // IMPORTANT: register each type under BOTH its qualified and unqualified
    // names. Q_DECLARE_METATYPE must be at global scope, so it registers under
    // the fully-qualified name "PlasmaZones::Foo". The authoritative fix is to
    // fully-qualify the type in every adaptor slot parameter declaration (see
    // e.g. autotileadaptor.h) so moc records "PlasmaZones::Foo" and matches
    // the qualified registration. This unqualified-alias registration is a
    // defensive belt-and-suspenders so that a future adaptor written with
    // unqualified slot parameters still works rather than crashing D-Bus
    // dispatch at runtime (see dbus_adaptor_routing integration test for the
    // failure mode — "Could not find slot ..." / "demarshalling function for
    // type 'QString' failed" observed in production on 2026-04-10).

#define PZ_REGISTER_DBUS_TYPE(Type)                                                                                    \
    qRegisterMetaType<Type>(#Type);                                                                                    \
    qDBusRegisterMetaType<Type>()

    PZ_REGISTER_DBUS_TYPE(WindowGeometryEntry);
    PZ_REGISTER_DBUS_TYPE(WindowGeometryList);
    PZ_REGISTER_DBUS_TYPE(TileRequestEntry);
    PZ_REGISTER_DBUS_TYPE(TileRequestList);
    PZ_REGISTER_DBUS_TYPE(SnapAllResultEntry);
    PZ_REGISTER_DBUS_TYPE(SnapAllResultList);
    PZ_REGISTER_DBUS_TYPE(SnapConfirmationEntry);
    PZ_REGISTER_DBUS_TYPE(SnapConfirmationList);
    PZ_REGISTER_DBUS_TYPE(WindowOpenedEntry);
    PZ_REGISTER_DBUS_TYPE(WindowOpenedList);
    PZ_REGISTER_DBUS_TYPE(WindowStateEntry);
    PZ_REGISTER_DBUS_TYPE(WindowStateList);
    PZ_REGISTER_DBUS_TYPE(UnfloatRestoreResult);
    PZ_REGISTER_DBUS_TYPE(ZoneGeometryRect);
    PZ_REGISTER_DBUS_TYPE(ZoneGeometryList);
    PZ_REGISTER_DBUS_TYPE(EmptyZoneEntry);
    PZ_REGISTER_DBUS_TYPE(EmptyZoneList);
    PZ_REGISTER_DBUS_TYPE(SnapAssistCandidate);
    PZ_REGISTER_DBUS_TYPE(SnapAssistCandidateList);
    PZ_REGISTER_DBUS_TYPE(NamedZoneGeometry);
    PZ_REGISTER_DBUS_TYPE(NamedZoneGeometryList);
    PZ_REGISTER_DBUS_TYPE(AlgorithmInfoEntry);
    PZ_REGISTER_DBUS_TYPE(AlgorithmInfoList);
    PZ_REGISTER_DBUS_TYPE(BridgeRegistrationResult);
    PZ_REGISTER_DBUS_TYPE(MoveTargetResult);
    PZ_REGISTER_DBUS_TYPE(FocusTargetResult);
    PZ_REGISTER_DBUS_TYPE(CycleTargetResult);
    PZ_REGISTER_DBUS_TYPE(SwapTargetResult);
    PZ_REGISTER_DBUS_TYPE(RestoreTargetResult);
    PZ_REGISTER_DBUS_TYPE(PreTileGeometryEntry);
    PZ_REGISTER_DBUS_TYPE(PreTileGeometryList);
    PZ_REGISTER_DBUS_TYPE(DragPolicy);
    PZ_REGISTER_DBUS_TYPE(DragOutcome);

#undef PZ_REGISTER_DBUS_TYPE
}

} // namespace PlasmaZones
