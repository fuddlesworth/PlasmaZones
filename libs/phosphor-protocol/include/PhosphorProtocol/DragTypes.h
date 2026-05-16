// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorProtocol/ZoneTypes.h>
#include <PhosphorProtocol/phosphorprotocoltypes_export.h>

#include <QMetaType>
#include <QRect>
#include <QString>

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

} // namespace PhosphorProtocol

Q_DECLARE_METATYPE(PhosphorProtocol::DragPolicy)
Q_DECLARE_METATYPE(PhosphorProtocol::DragOutcome)
