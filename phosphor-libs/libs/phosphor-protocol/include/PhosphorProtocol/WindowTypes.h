// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorProtocol/phosphorprotocoltypes_export.h>

#include <QList>
#include <QMetaType>
#include <QRect>
#include <QString>
#include <QStringList>
#include <cstdlib>

namespace PhosphorProtocol {

/// Magnitude ceilings shared by every geometry-bearing wire struct.
///
/// A consumer builds a QRect from these, and QRect's x/y/w/h constructor
/// computes `x + w - 1` — signed overflow, and undefined, BEFORE any
/// `isValid()` or `width <= 0` guard the consumer writes can run. Those guards
/// are therefore not a defence, and the bound has to be applied here.
///
/// Deliberately generous, and deliberately NOT a screen-bounds check: the
/// scrolling engine parks off-screen columns entirely outside their screen
/// rect, so a legitimate park origin sits far outside every output. An
/// over-strict validator here has already broken that once, dropping every
/// vertical park at its own validationError. These limits are orders of
/// magnitude past any real display and only catch garbling.
inline constexpr int MaxWireExtent = 100000;
inline constexpr int MaxWireOrigin = 1000000;

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

    /// Empty when the entry is safe to consume; a diagnostic otherwise.
    ///
    /// Call this BEFORE toRect() or any other arithmetic on the fields. Same
    /// contract as TileRequestEntry::validationError(), and it exists for the
    /// same reason: the consumer's QRect construction overflows before its own
    /// guards can run. Size is NOT checked for positivity here — callers
    /// legitimately differ on whether a zero extent means "size-only" or
    /// "invalid" — only magnitude, which is the half that is undefined
    /// behaviour rather than a policy question.
    QString validationError() const
    {
        if (windowId.isEmpty()) {
            return QStringLiteral("WindowGeometryEntry: empty windowId");
        }
        // Widened before the absolute value: qAbs(INT_MIN) is itself undefined
        // behaviour in int, and INT_MIN is exactly the sort of value a corrupt
        // wire payload carries.
        if (std::abs(static_cast<qint64>(width)) > MaxWireExtent
            || std::abs(static_cast<qint64>(height)) > MaxWireExtent) {
            return QStringLiteral("WindowGeometryEntry: implausible size (windowId=%1 w=%2 h=%3)")
                .arg(windowId)
                .arg(width)
                .arg(height);
        }
        if (std::abs(static_cast<qint64>(x)) > MaxWireOrigin || std::abs(static_cast<qint64>(y)) > MaxWireOrigin) {
            return QStringLiteral("WindowGeometryEntry: implausible origin (windowId=%1 x=%2 y=%3)")
                .arg(windowId)
                .arg(x)
                .arg(y);
        }
        return {};
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
    /// NOT on the wire (the marshalling carries the four fields above): the
    /// daemon stamps it from the method the entry arrived through, so it
    /// survives the panel-gate queue and the parked-open retry. True only for
    /// Tiling.windowOpened, a genuine open that may take focus; a re-announce
    /// (windowsOpenedBatch) or re-placement (windowReannounced) never does.
    bool focusEligible = false;
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

    /// Returns empty QString if valid, else a human-readable description of the
    /// invariant violation. Called at the windowStateChanged unmarshal site (like
    /// DragPolicy / BridgeRegistrationResult on their paths) so a garbled entry —
    /// one naming no window — can't perturb the effect's zone cache. zoneId is
    /// intentionally unchecked: empty is the valid "unsnapped / floated" signal.
    QString validationError() const
    {
        if (windowId.isEmpty()) {
            return QStringLiteral("WindowStateEntry: empty windowId");
        }
        return QString();
    }
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

/// One window in the effect's output-settle report, sent when a screen change
/// settles that added or removed an output or deferred a crossing:
/// (ssssbiiiiiiiiiiibbbbssiiibiiii). What KWin did to the window since the
/// debounce began, and the state it had before an output it was on went away
/// (S0), which the daemon compares to tell a window KWin returned untouched
/// from one the user touched.
struct OutputSettleRow
{
    QString windowId;
    /// KWin's uuid of the output the window is on now (moveResizeOutput).
    QString outputUuid;
    /// The PlasmaZones physical id of that output.
    QString screenId;
    /// The PlasmaZones physical id of the output it was on when the debounce began.
    QString sourceScreenId;
    /// Whether that output is still connected.
    bool sourceConnected = true;
    int x = 0, y = 0, width = 0, height = 0; ///< moveResizeGeometry now
    int baseX = 0, baseY = 0, baseWidth = 0, baseHeight = 0; ///< the frame when the debounce began
    int moveResizeCount = 0; ///< interactiveMoveResizeCount now
    int maximizeMode = 0;
    int quickTileMode = 0;
    bool fullscreen = false;
    /// Only KWin moved it: no interactive move or resize since the baseline.
    bool kwinOnly = true;
    /// On the current desktop and activity, and not minimized.
    bool placeableNow = true;
    /// The fields below carry S0, the state when its output went away.
    bool hasS0 = false;
    QString s0Uuid;
    QString s0ScreenId;
    int s0MoveResizeCount = 0;
    int s0MaximizeMode = 0;
    int s0QuickTileMode = 0;
    bool s0Fullscreen = false;
    int s0X = 0, s0Y = 0, s0Width = 0, s0Height = 0;

    QRect geometry() const
    {
        return QRect(x, y, width, height);
    }
    QRect baseline() const
    {
        return QRect(baseX, baseY, baseWidth, baseHeight);
    }
    /// Whether the window's KWin state now equals S0 (false without one).
    bool stateEqualsS0() const
    {
        return hasS0 && moveResizeCount == s0MoveResizeCount && maximizeMode == s0MaximizeMode
            && quickTileMode == s0QuickTileMode && fullscreen == s0Fullscreen;
    }
    QString validationError() const
    {
        if (windowId.isEmpty()) {
            return QStringLiteral("OutputSettleRow: empty windowId");
        }
        if (screenId.isEmpty()) {
            return QStringLiteral("OutputSettleRow: empty screenId (windowId=%1)").arg(windowId);
        }
        return QString();
    }
};

using OutputSettleRowList = QList<OutputSettleRow>;

/// The daemon's answer for one OutputSettleRow: (sis).
struct OutputSettleVerdict
{
    enum Kind : int {
        None = 0, ///< nothing to do; the effect replays a deferred crossing as a move
        Readopt = 1, ///< re-seated in its parked place on the output it returned to
        EvacueeFloat = 2, ///< an evacuee: adopted floating where KWin put it
        Reassert = 3, ///< the daemon re-asserted its placement after KWin moved it
        UserMove = 4, ///< touched: the crossing is the user's move
    };
    QString windowId;
    int verdict = None;
    /// The screen the verdict placed the window on, when it placed it.
    QString screenId;

    QString validationError() const
    {
        if (windowId.isEmpty()) {
            return QStringLiteral("OutputSettleVerdict: empty windowId");
        }
        if (verdict < None || verdict > UserMove) {
            return QStringLiteral("OutputSettleVerdict: unknown verdict %1 (windowId=%2)").arg(verdict).arg(windowId);
        }
        return QString();
    }
};

using OutputSettleVerdictList = QList<OutputSettleVerdict>;

/// Why the daemon asks the compositor to place a window, carried by
/// WindowTracking.applyGeometryRequested. A UserVerb places the window a user
/// action is about (a snap key, a drop, Meta+F, a routed open): its maximize and
/// fullscreen end. A Restatement re-states a placement the window already has
/// (a zone re-applied, a swap partner, a minimize return, a remembered float
/// spot): a maximized or fullscreen window keeps that state.
enum class PlacementPurpose : int {
    UserVerb = 0,
    Restatement = 1,
};

/// Clamp an integer wire value to a PlacementPurpose. An unknown value is a
/// UserVerb, the arm every placement took before the purpose existed.
inline PlacementPurpose clampPlacementPurposeFromWire(int wire)
{
    return wire == static_cast<int>(PlacementPurpose::Restatement) ? PlacementPurpose::Restatement
                                                                   : PlacementPurpose::UserVerb;
}

} // namespace PhosphorProtocol

Q_DECLARE_METATYPE(PhosphorProtocol::OutputSettleRow)
Q_DECLARE_METATYPE(PhosphorProtocol::OutputSettleRowList)
Q_DECLARE_METATYPE(PhosphorProtocol::OutputSettleVerdict)
Q_DECLARE_METATYPE(PhosphorProtocol::OutputSettleVerdictList)
Q_DECLARE_METATYPE(PhosphorProtocol::WindowGeometryEntry)
Q_DECLARE_METATYPE(PhosphorProtocol::WindowGeometryList)
Q_DECLARE_METATYPE(PhosphorProtocol::SnapConfirmationEntry)
Q_DECLARE_METATYPE(PhosphorProtocol::SnapConfirmationList)
Q_DECLARE_METATYPE(PhosphorProtocol::WindowOpenedEntry)
Q_DECLARE_METATYPE(PhosphorProtocol::WindowOpenedList)
Q_DECLARE_METATYPE(PhosphorProtocol::WindowStateEntry)
Q_DECLARE_METATYPE(PhosphorProtocol::WindowStateList)
Q_DECLARE_METATYPE(PhosphorProtocol::UnfloatRestoreResult)
