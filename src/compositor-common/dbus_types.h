// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDBusArgument>
#include <QDBusMetaType>
#include <QList>
#include <QRect>
#include <QString>
#include <QStringList>

namespace PlasmaZones {

/// D-Bus struct for batch geometry entries: (siiii)
struct WindowGeometryEntry
{
    QString windowId;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    QRect toRect() const
    {
        return QRect(x, y, width, height);
    }
    static WindowGeometryEntry fromRect(const QString& id, const QRect& r)
    {
        return {id, r.x(), r.y(), r.width(), r.height()};
    }
};

using WindowGeometryList = QList<WindowGeometryEntry>;

/// D-Bus struct for autotile tile requests: (siiiissbb)
struct TileRequestEntry
{
    QString windowId;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    QString zoneId;
    QString screenId;
    bool monocle = false;
    bool floating = false;

    QRect toRect() const
    {
        return QRect(x, y, width, height);
    }
};

using TileRequestList = QList<TileRequestEntry>;

/// D-Bus struct for snap-all result entries: (sssiiii)
/// Carries targetZoneId so the plugin can confirm snaps without a second JSON parse.
struct SnapAllResultEntry
{
    QString windowId;
    QString targetZoneId;
    QString sourceZoneId;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    QRect toRect() const
    {
        return QRect(x, y, width, height);
    }
    WindowGeometryEntry toGeometryEntry() const
    {
        return {windowId, x, y, width, height};
    }
};

using SnapAllResultList = QList<SnapAllResultEntry>;

/// D-Bus struct for batch snap confirmation: (sssb)
struct SnapConfirmationEntry
{
    QString windowId;
    QString zoneId;
    QString screenId;
    bool isRestore = false;
};

using SnapConfirmationList = QList<SnapConfirmationEntry>;

/// D-Bus struct for batch window-opened notification: (ssii)
struct WindowOpenedEntry
{
    QString windowId;
    QString screenId;
    int minWidth = 0;
    int minHeight = 0;
};

using WindowOpenedList = QList<WindowOpenedEntry>;

/// D-Bus struct for window state: (sssbsasb)
struct WindowStateEntry
{
    QString windowId;
    QString zoneId;
    QString screenId;
    bool isFloating = false;
    QString changeType; ///< "snapped", "unsnapped", "floated", "unfloated", "screen_changed"
    QStringList zoneIds; ///< D-Bus type 'as' — all zone IDs for multi-zone span (query only)
    bool isSticky = false; ///< Whether window is on all virtual desktops (query only)
};

using WindowStateList = QList<WindowStateEntry>;

/// D-Bus struct for unfloat restore result: (bassiiii).
/// Intentionally scalar-only — `calculateUnfloatRestore` returns exactly one
/// result per call. No `QList<UnfloatRestoreResult>` metatype is registered;
/// if a batch variant is ever added, register the list type alongside it.
struct UnfloatRestoreResult
{
    bool found = false;
    QStringList zoneIds; ///< D-Bus type 'as'
    QString screenName;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    QRect toRect() const
    {
        return QRect(x, y, width, height);
    }
};

/// D-Bus struct for zone geometry: (iiii)
struct ZoneGeometryRect
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    QRect toRect() const
    {
        return QRect(x, y, width, height);
    }
    static ZoneGeometryRect fromRect(const QRect& r)
    {
        return {r.x(), r.y(), r.width(), r.height()};
    }
};

using ZoneGeometryList = QList<ZoneGeometryRect>;

/// D-Bus struct for empty zone info: (siiiiiibsssdd)
struct EmptyZoneEntry
{
    QString zoneId;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int borderWidth = 0;
    int borderRadius = 0;
    bool useCustomColors = false;
    QString highlightColor;
    QString inactiveColor;
    QString borderColor;
    double activeOpacity = 0.5;
    double inactiveOpacity = 0.3;

    QRect toRect() const
    {
        return QRect(x, y, width, height);
    }
};

using EmptyZoneList = QList<EmptyZoneEntry>;

/// D-Bus struct for snap assist candidate: (ssss)
struct SnapAssistCandidate
{
    QString windowId;
    QString compositorHandle;
    QString icon;
    QString caption;
};

using SnapAssistCandidateList = QList<SnapAssistCandidate>;

/// D-Bus struct for named zone geometry: (siiii)
struct NamedZoneGeometry
{
    QString zoneId;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    QRect toRect() const
    {
        return QRect(x, y, width, height);
    }
};

using NamedZoneGeometryList = QList<NamedZoneGeometry>;

/// D-Bus struct for algorithm metadata: (sssbbbbdibsbb)
struct AlgorithmInfoEntry
{
    QString id;
    QString name;
    QString description;
    bool supportsMasterCount = false;
    bool supportsSplitRatio = false;
    bool centerLayout = false;
    bool producesOverlappingZones = false;
    double defaultSplitRatio = 0.5;
    int defaultMaxWindows = 0;
    bool isScripted = false;
    QString zoneNumberDisplay;
    bool isUserScript = false;
    bool supportsMemory = false;
};

using AlgorithmInfoList = QList<AlgorithmInfoEntry>;

/// D-Bus struct for bridge registration result: (sss)
struct BridgeRegistrationResult
{
    QString apiVersion;
    QString bridgeName;
    QString sessionId;
};

/// D-Bus struct for move/push/zone-number navigation result: (bssiiiiss)
struct MoveTargetResult
{
    bool success = false;
    QString reason;
    QString zoneId;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    QString sourceZoneId;
    QString screenName;

    QRect toRect() const
    {
        return QRect(x, y, width, height);
    }
};

/// D-Bus struct for focus navigation result: (bsssss)
struct FocusTargetResult
{
    bool success = false;
    QString reason;
    QString windowIdToActivate;
    QString sourceZoneId;
    QString targetZoneId;
    QString screenName;
};

/// D-Bus struct for cycle navigation result: (bssss)
struct CycleTargetResult
{
    bool success = false;
    QString reason;
    QString windowIdToActivate;
    QString zoneId;
    QString screenName;
};

/// D-Bus struct for swap navigation result: (bssiiiissiiiissss)
struct SwapTargetResult
{
    bool success = false;
    QString reason;
    QString windowId1;
    int x1 = 0;
    int y1 = 0;
    int w1 = 0;
    int h1 = 0;
    QString zoneId1;
    QString windowId2;
    int x2 = 0;
    int y2 = 0;
    int w2 = 0;
    int h2 = 0;
    QString zoneId2;
    QString screenName;
    QString sourceZoneId;
    QString targetZoneId;
};

/// Drag policy — daemon-authoritative decision about how a drag should be
/// handled. Returned from WindowDragAdaptor::beginDrag at drag start and
/// re-emitted via dragPolicyChanged if the cursor crosses a screen boundary
/// that flips the mode (autotile↔snap). The compositor plugin uses this to
/// decide whether to stream dragMoved, grab keyboard, show an overlay, and
/// whether to apply an immediate float transition for an autotile drag.
///
/// Wire: (bbbbbss)  —  5 bools + 2 strings
///
/// Single source of truth replaces the effect-side
/// m_dragBypassedForAutotile / m_cachedZoneSelectorEnabled cache that went
/// stale after every settings reload.
struct DragPolicy
{
    bool streamDragMoved = false; ///< effect should send dragMoved D-Bus ticks
    bool showOverlay = false; ///< daemon will show zone overlay during this drag
    bool grabKeyboard = false; ///< effect should grab keyboard (Escape cancel)
    bool captureGeometry = false; ///< effect should capture pre-drag geometry
    bool immediateFloatOnStart = false; ///< effect should call handleDragToFloat(immediate) at drag start
    QString screenId; ///< screen the drag started/currently is on (virtual-screen-aware)
    QString bypassReason; ///< empty if snap path; "autotile_screen" / "snapping_disabled" / "context_disabled"
};

/// Drag outcome — daemon-authoritative decision about what to apply at drag
/// end. Returned from WindowDragAdaptor::endDrag. The compositor plugin
/// executes exactly the action specified; no further decisions on the effect
/// side. Wire: (issiiiisb)  —  int action + 2 strings + 4 ints + string + bool
struct DragOutcome
{
    enum Action : int {
        NoOp = 0, ///< drag produced no state change (e.g. cancelled with no prior snap)
        ApplyFloat = 1, ///< autotile path: mark window floating at current position
        ApplySnap = 2, ///< snap path: apply snap geometry to a zone
        RestoreSize = 3, ///< drag-out unsnap: restore original size, keep current position
        CancelSnap = 4, ///< snap cancelled via Escape
        NotifyDragOutUnsnap = 5 ///< drag ended without activation trigger on a previously-snapped window
    };

    int action = NoOp;
    QString windowId;
    QString targetScreenId;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    QString zoneId; ///< populated for ApplySnap
    bool skipAnimation = false;
    bool requestSnapAssist = false; ///< true → plugin should show snap-assist window picker
    EmptyZoneList emptyZones; ///< candidate zones for snap assist (empty unless requestSnapAssist)

    QRect toRect() const
    {
        return QRect(x, y, width, height);
    }
};

/// D-Bus struct for restore navigation result: (bbiiii)
struct RestoreTargetResult
{
    bool success = false;
    bool found = false;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    QRect toRect() const
    {
        return QRect(x, y, width, height);
    }
};

// QDBusArgument streaming operators (implemented in dbus_types.cpp)
QDBusArgument& operator<<(QDBusArgument& arg, const WindowGeometryEntry& e);
const QDBusArgument& operator>>(const QDBusArgument& arg, WindowGeometryEntry& e);
QDBusArgument& operator<<(QDBusArgument& arg, const TileRequestEntry& e);
const QDBusArgument& operator>>(const QDBusArgument& arg, TileRequestEntry& e);
QDBusArgument& operator<<(QDBusArgument& arg, const SnapAllResultEntry& e);
const QDBusArgument& operator>>(const QDBusArgument& arg, SnapAllResultEntry& e);
QDBusArgument& operator<<(QDBusArgument& arg, const SnapConfirmationEntry& e);
const QDBusArgument& operator>>(const QDBusArgument& arg, SnapConfirmationEntry& e);
QDBusArgument& operator<<(QDBusArgument& arg, const WindowOpenedEntry& e);
const QDBusArgument& operator>>(const QDBusArgument& arg, WindowOpenedEntry& e);
QDBusArgument& operator<<(QDBusArgument& arg, const WindowStateEntry& e);
const QDBusArgument& operator>>(const QDBusArgument& arg, WindowStateEntry& e);
QDBusArgument& operator<<(QDBusArgument& arg, const UnfloatRestoreResult& e);
const QDBusArgument& operator>>(const QDBusArgument& arg, UnfloatRestoreResult& e);
QDBusArgument& operator<<(QDBusArgument& arg, const ZoneGeometryRect& e);
const QDBusArgument& operator>>(const QDBusArgument& arg, ZoneGeometryRect& e);
QDBusArgument& operator<<(QDBusArgument& arg, const EmptyZoneEntry& e);
const QDBusArgument& operator>>(const QDBusArgument& arg, EmptyZoneEntry& e);
QDBusArgument& operator<<(QDBusArgument& arg, const SnapAssistCandidate& e);
const QDBusArgument& operator>>(const QDBusArgument& arg, SnapAssistCandidate& e);
QDBusArgument& operator<<(QDBusArgument& arg, const NamedZoneGeometry& e);
const QDBusArgument& operator>>(const QDBusArgument& arg, NamedZoneGeometry& e);
QDBusArgument& operator<<(QDBusArgument& arg, const AlgorithmInfoEntry& e);
const QDBusArgument& operator>>(const QDBusArgument& arg, AlgorithmInfoEntry& e);
QDBusArgument& operator<<(QDBusArgument& arg, const BridgeRegistrationResult& e);
const QDBusArgument& operator>>(const QDBusArgument& arg, BridgeRegistrationResult& e);
QDBusArgument& operator<<(QDBusArgument& arg, const MoveTargetResult& e);
const QDBusArgument& operator>>(const QDBusArgument& arg, MoveTargetResult& e);
QDBusArgument& operator<<(QDBusArgument& arg, const FocusTargetResult& e);
const QDBusArgument& operator>>(const QDBusArgument& arg, FocusTargetResult& e);
QDBusArgument& operator<<(QDBusArgument& arg, const CycleTargetResult& e);
const QDBusArgument& operator>>(const QDBusArgument& arg, CycleTargetResult& e);
QDBusArgument& operator<<(QDBusArgument& arg, const SwapTargetResult& e);
const QDBusArgument& operator>>(const QDBusArgument& arg, SwapTargetResult& e);
QDBusArgument& operator<<(QDBusArgument& arg, const RestoreTargetResult& e);
const QDBusArgument& operator>>(const QDBusArgument& arg, RestoreTargetResult& e);
QDBusArgument& operator<<(QDBusArgument& arg, const DragPolicy& p);
const QDBusArgument& operator>>(const QDBusArgument& arg, DragPolicy& p);
QDBusArgument& operator<<(QDBusArgument& arg, const DragOutcome& o);
const QDBusArgument& operator>>(const QDBusArgument& arg, DragOutcome& o);

/// Call once at startup (daemon and plugin) to register types with Qt D-Bus
void registerDBusTypes();

} // namespace PlasmaZones

// Must be outside namespace for Qt meta-type system
Q_DECLARE_METATYPE(PlasmaZones::WindowGeometryEntry)
Q_DECLARE_METATYPE(PlasmaZones::WindowGeometryList)
Q_DECLARE_METATYPE(PlasmaZones::TileRequestEntry)
Q_DECLARE_METATYPE(PlasmaZones::TileRequestList)
Q_DECLARE_METATYPE(PlasmaZones::SnapAllResultEntry)
Q_DECLARE_METATYPE(PlasmaZones::SnapAllResultList)
Q_DECLARE_METATYPE(PlasmaZones::SnapConfirmationEntry)
Q_DECLARE_METATYPE(PlasmaZones::SnapConfirmationList)
Q_DECLARE_METATYPE(PlasmaZones::WindowOpenedEntry)
Q_DECLARE_METATYPE(PlasmaZones::WindowOpenedList)
Q_DECLARE_METATYPE(PlasmaZones::WindowStateEntry)
Q_DECLARE_METATYPE(PlasmaZones::WindowStateList)
Q_DECLARE_METATYPE(PlasmaZones::UnfloatRestoreResult)
Q_DECLARE_METATYPE(PlasmaZones::ZoneGeometryRect)
Q_DECLARE_METATYPE(PlasmaZones::ZoneGeometryList)
Q_DECLARE_METATYPE(PlasmaZones::EmptyZoneEntry)
Q_DECLARE_METATYPE(PlasmaZones::EmptyZoneList)
Q_DECLARE_METATYPE(PlasmaZones::SnapAssistCandidate)
Q_DECLARE_METATYPE(PlasmaZones::SnapAssistCandidateList)
Q_DECLARE_METATYPE(PlasmaZones::NamedZoneGeometry)
Q_DECLARE_METATYPE(PlasmaZones::NamedZoneGeometryList)
Q_DECLARE_METATYPE(PlasmaZones::AlgorithmInfoEntry)
Q_DECLARE_METATYPE(PlasmaZones::AlgorithmInfoList)
Q_DECLARE_METATYPE(PlasmaZones::BridgeRegistrationResult)
Q_DECLARE_METATYPE(PlasmaZones::MoveTargetResult)
Q_DECLARE_METATYPE(PlasmaZones::FocusTargetResult)
Q_DECLARE_METATYPE(PlasmaZones::CycleTargetResult)
Q_DECLARE_METATYPE(PlasmaZones::SwapTargetResult)
Q_DECLARE_METATYPE(PlasmaZones::RestoreTargetResult)
Q_DECLARE_METATYPE(PlasmaZones::DragPolicy)
Q_DECLARE_METATYPE(PlasmaZones::DragOutcome)
