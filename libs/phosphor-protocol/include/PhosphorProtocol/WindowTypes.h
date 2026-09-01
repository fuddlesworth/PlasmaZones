// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorProtocol/phosphorprotocoltypes_export.h>

#include <QList>
#include <QMetaType>
#include <QRect>
#include <QString>
#include <QStringList>

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
        if (qAbs(width) > MaxWireExtent || qAbs(height) > MaxWireExtent) {
            return QStringLiteral("WindowGeometryEntry: implausible size (windowId=%1 w=%2 h=%3)")
                .arg(windowId)
                .arg(width)
                .arg(height);
        }
        if (qAbs(x) > MaxWireOrigin || qAbs(y) > MaxWireOrigin) {
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

} // namespace PhosphorProtocol

Q_DECLARE_METATYPE(PhosphorProtocol::WindowGeometryEntry)
Q_DECLARE_METATYPE(PhosphorProtocol::WindowGeometryList)
Q_DECLARE_METATYPE(PhosphorProtocol::SnapConfirmationEntry)
Q_DECLARE_METATYPE(PhosphorProtocol::SnapConfirmationList)
Q_DECLARE_METATYPE(PhosphorProtocol::WindowOpenedEntry)
Q_DECLARE_METATYPE(PhosphorProtocol::WindowOpenedList)
Q_DECLARE_METATYPE(PhosphorProtocol::WindowStateEntry)
Q_DECLARE_METATYPE(PhosphorProtocol::WindowStateList)
Q_DECLARE_METATYPE(PhosphorProtocol::UnfloatRestoreResult)
