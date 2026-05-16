// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorProtocol/phosphorprotocoltypes_export.h>

#include <QList>
#include <QMetaType>
#include <QRect>
#include <QString>
#include <QStringList>

class QDebug;

namespace PhosphorProtocol {

/// Why a drag was bypassed from the canonical snap pipeline.
///
/// The daemon's computeDragPolicy picks exactly one of these based on the
/// drag's context, and both sides route behavior off the value. Replaces
/// the free-form `QString bypassReason` that carried `""` / `"autotile_screen"`
/// / `"snapping_disabled"` / `"context_disabled"` as magic strings.
///
/// Wire format: unchanged — serialized as the same legacy string via
/// toWireString()/fromWireString() inside the DragPolicy marshaller, so no
/// ApiVersion bump is required. Unknown wire values parse to None (matches
/// the old behavior where unrecognized strings didn't match the autotile
/// branch and fell through to the canonical snap path).
enum class DragBypassReason : int {
    None = 0, ///< canonical snap path — drag flows through the snap pipeline
    AutotileScreen = 1, ///< drag started/ended on an autotile screen — engine owns placement
    SnappingDisabled = 2, ///< snap mode off globally — dead drag
    ContextDisabled = 3, ///< monitor/desktop/activity excluded in settings — dead drag
};

/// Convert to the legacy wire-format string. Returns an empty QString for None.
PHOSPHORPROTOCOLTYPES_EXPORT QString toWireString(DragBypassReason r);

/// Parse from the legacy wire-format string. Unknown values map to None.
PHOSPHORPROTOCOLTYPES_EXPORT DragBypassReason bypassReasonFromWireString(const QString& s);

/// QDebug streaming for logging. Prints the enum name (e.g. "AutotileScreen").
PHOSPHORPROTOCOLTYPES_EXPORT QDebug operator<<(QDebug debug, DragBypassReason r);

/// D-Bus struct for batch geometry entries: (siiiis)
///
/// `screenId` is the daemon-authoritative target screen for this window after
/// the geometry is applied. The compositor uses it to seed its per-window
/// tracked-screen cache (m_trackedScreenPerWindow) without re-deriving from
/// geometry.center() against m_virtualScreenDefs — eliminating a race during
/// virtual-screen swap/rotate where the cache lags the daemon's authoritative
/// move and a stale interpretation triggers a spurious cross-VS unsnap.
///
/// Empty `screenId` means "no authoritative answer; fall back to geometry-
/// based resolution" (used by the autotile float-restore path which doesn't
/// own snap state).
struct WindowGeometryEntry
{
    QString windowId;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    QString screenId; ///< target VS/physical screen (empty = fall back to geometry resolution)

    QRect toRect() const
    {
        return QRect(x, y, width, height);
    }
    static WindowGeometryEntry fromRect(const QString& id, const QRect& r)
    {
        return {id, r.x(), r.y(), r.width(), r.height(), QString()};
    }
    static WindowGeometryEntry fromRect(const QString& id, const QRect& r, const QString& screenId)
    {
        return {id, r.x(), r.y(), r.width(), r.height(), screenId};
    }
};

using WindowGeometryList = QList<WindowGeometryEntry>;

/// D-Bus struct for autotile tile requests: (siiiissbb)
struct PHOSPHORPROTOCOLTYPES_EXPORT TileRequestEntry
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

    /// Returns empty QString if valid, or a human-readable description of
    /// the invariant violation. Call at every unmarshal site to detect a
    /// garbled payload before acting on it.
    QString validationError() const;
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
        return {windowId, x, y, width, height, QString()};
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
struct PHOSPHORPROTOCOLTYPES_EXPORT BridgeRegistrationResult
{
    QString apiVersion;
    QString bridgeName;
    QString sessionId;

    /// Returns empty QString if valid, or a human-readable description of
    /// the invariant violation. "REJECTED" sessionId is a valid sentinel
    /// signaling version mismatch and is NOT flagged as invalid — callers
    /// must check for it separately before using the result.
    QString validationError() const;
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
struct PHOSPHORPROTOCOLTYPES_EXPORT DragPolicy
{
    bool streamDragMoved = false; ///< effect should send dragMoved D-Bus ticks
    bool showOverlay = false; ///< daemon will show zone overlay during this drag
    bool grabKeyboard = false; ///< effect should grab keyboard (Escape cancel)
    bool captureGeometry = false; ///< effect should capture pre-drag geometry
    bool immediateFloatOnStart = false; ///< effect should call handleDragToFloat(immediate) at drag start
    QString screenId; ///< screen the drag started/currently is on (virtual-screen-aware)
    DragBypassReason bypassReason = DragBypassReason::None; ///< drag routing (None = canonical snap path)

    /// Returns empty QString if valid, or a human-readable description of
    /// the invariant violation. Call at every unmarshal site on the effect
    /// side to catch garbled policies before they perturb drag state.
    QString validationError() const;

    /// Full structural equality. Used by WindowDragAdaptor::updateDragCursor
    /// to detect per-screen policy changes during a drag — any field that
    /// participates in routing (bypass reason, screen id, the behavior
    /// flags) is compared, so future fields are picked up automatically
    /// without touching the comparator.
    bool operator==(const DragPolicy&) const = default;
};

/// Drag outcome — daemon-authoritative decision about what to apply at drag
/// end. Returned from WindowDragAdaptor::endDrag. The compositor plugin
/// executes exactly the action specified; no further decisions on the effect
/// side. Wire: (issiiiisbba(...))  —  int + 2 strings + 4 ints + string + 2 bools + EmptyZoneList
struct PHOSPHORPROTOCOLTYPES_EXPORT DragOutcome
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

    /// Returns empty QString if valid, or a human-readable description of
    /// the invariant violation. Enforces action enum range and per-action
    /// cross-field invariants (ApplySnap requires non-empty zoneId + non-
    /// zero size, ApplyFloat/RestoreSize/NotifyDragOutUnsnap require
    /// windowId, etc).
    QString validationError() const;
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

/// D-Bus struct for pre-tile geometry entries: (siiiiis)
/// Replaces the JSON blob previously returned by getPreTileGeometriesJson.
struct PreTileGeometryEntry
{
    QString appId;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    QString screenId;

    QRect toRect() const
    {
        return QRect(x, y, width, height);
    }
};

using PreTileGeometryList = QList<PreTileGeometryEntry>;

} // namespace PhosphorProtocol

// Must be outside namespace for Qt meta-type system. Q_DECLARE_METATYPE only
// needs <QMetaType> (QtCore) — these declarations let the value types travel
// in a QVariant without dragging in QtDBus. The matching QDBusArgument
// marshallers live in Marshalling.h.
Q_DECLARE_METATYPE(PhosphorProtocol::WindowGeometryEntry)
Q_DECLARE_METATYPE(PhosphorProtocol::WindowGeometryList)
Q_DECLARE_METATYPE(PhosphorProtocol::TileRequestEntry)
Q_DECLARE_METATYPE(PhosphorProtocol::TileRequestList)
Q_DECLARE_METATYPE(PhosphorProtocol::SnapAllResultEntry)
Q_DECLARE_METATYPE(PhosphorProtocol::SnapAllResultList)
Q_DECLARE_METATYPE(PhosphorProtocol::SnapConfirmationEntry)
Q_DECLARE_METATYPE(PhosphorProtocol::SnapConfirmationList)
Q_DECLARE_METATYPE(PhosphorProtocol::WindowOpenedEntry)
Q_DECLARE_METATYPE(PhosphorProtocol::WindowOpenedList)
Q_DECLARE_METATYPE(PhosphorProtocol::WindowStateEntry)
Q_DECLARE_METATYPE(PhosphorProtocol::WindowStateList)
Q_DECLARE_METATYPE(PhosphorProtocol::UnfloatRestoreResult)
Q_DECLARE_METATYPE(PhosphorProtocol::ZoneGeometryRect)
Q_DECLARE_METATYPE(PhosphorProtocol::ZoneGeometryList)
Q_DECLARE_METATYPE(PhosphorProtocol::EmptyZoneEntry)
Q_DECLARE_METATYPE(PhosphorProtocol::EmptyZoneList)
Q_DECLARE_METATYPE(PhosphorProtocol::SnapAssistCandidate)
Q_DECLARE_METATYPE(PhosphorProtocol::SnapAssistCandidateList)
Q_DECLARE_METATYPE(PhosphorProtocol::NamedZoneGeometry)
Q_DECLARE_METATYPE(PhosphorProtocol::NamedZoneGeometryList)
Q_DECLARE_METATYPE(PhosphorProtocol::AlgorithmInfoEntry)
Q_DECLARE_METATYPE(PhosphorProtocol::AlgorithmInfoList)
Q_DECLARE_METATYPE(PhosphorProtocol::BridgeRegistrationResult)
Q_DECLARE_METATYPE(PhosphorProtocol::MoveTargetResult)
Q_DECLARE_METATYPE(PhosphorProtocol::FocusTargetResult)
Q_DECLARE_METATYPE(PhosphorProtocol::CycleTargetResult)
Q_DECLARE_METATYPE(PhosphorProtocol::SwapTargetResult)
Q_DECLARE_METATYPE(PhosphorProtocol::RestoreTargetResult)
Q_DECLARE_METATYPE(PhosphorProtocol::PreTileGeometryEntry)
Q_DECLARE_METATYPE(PhosphorProtocol::PreTileGeometryList)
Q_DECLARE_METATYPE(PhosphorProtocol::DragPolicy)
Q_DECLARE_METATYPE(PhosphorProtocol::DragOutcome)
