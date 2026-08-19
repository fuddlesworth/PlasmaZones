// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorProtocol/phosphorprotocoltypes_export.h>

#include <QList>
#include <QMetaType>
#include <QRect>
#include <QString>

namespace PhosphorProtocol {

/// D-Bus struct for the shared tiling-family tile requests (autotile +
/// scrolling ride the same windowsTileRequested pipeline): (siiiissbbbssiiibsb)
struct PHOSPHORPROTOCOLTYPES_EXPORT TileRequestEntry
{
    QString windowId;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    /// Reserved and currently always empty on this wire: no producer writes
    /// it and no consumer reads it (it predates the autotile-to-tiling
    /// generalization). Kept because removing a mid-struct field is another
    /// wire break for zero functional gain.
    QString zoneId;
    QString screenId;
    bool monocle = false;
    bool floating = false;
    /// Scrolling mode: windowed fullscreen (niri toggle-windowed-fullscreen).
    /// The effect tells the client it is fullscreen (KWin fullscreen state)
    /// while committing the column rect above — the tile never leaves its
    /// slot. False for every other placement.
    bool windowedFullscreen = false;
    /// Overlap-layout stacking direction: "firstOnTop" or "lastOnTop".
    /// Empty for non-overlap layouts (the effect leaves z-order alone).
    QString stacking;
    /// Scrolling mode: which screen edge this window's motion is anchored to,
    /// "left" or "right". Empty for every other placement.
    ///
    /// This exists because a scrolling strip's off-viewport columns have to be
    /// committed somewhere, and where they are committed must NOT decide which
    /// way they appear to move. Parking a column just past the edge it left by
    /// encodes the direction in the position, which forces a choice between a
    /// believable animation and keeping the rect off a neighbouring output —
    /// on a horizontally-adjacent monitor pair those two demands are in direct
    /// conflict and the position can only satisfy one. Carrying the edge as
    /// data lets the engine park wherever is safe while the effect still
    /// animates from the side the user scrolled from.
    QString scrollEdge;
    /// Scrolling mode: how far the whole VIEW slid since the last batch, in
    /// logical pixels, for the screen this entry belongs to. Zero for every
    /// other placement, and zero within scrolling for a window the view does
    /// not carry.
    ///
    /// It is a property of the batch rather than of the window, carried
    /// per-entry so a batch spanning several screens stays unambiguous. The
    /// effect springs it ONCE per output and lets every carrying window ride
    /// it, which is what makes the strip move as one object instead of as N
    /// windows that each started their own spring a moment apart.
    ///
    /// Zero means "not carried". A parked column is the case that matters:
    /// its committed rect is off below the union of all outputs, so no
    /// translation puts it back on screen, and it keeps the edge-anchored
    /// slide-out built from `scrollEdge` instead.
    int viewDeltaX = 0;
    /// Scrolling strip: where this window really sits on the strip, when that
    /// differs from the rect committed above. Only a PARKED column sets it.
    ///
    /// A parked column is committed below the union of all outputs, because a
    /// rect is the only clip every present path honours and an off-view column
    /// must not land on a neighbouring monitor. But the park is not where the
    /// column IS on the strip, and while the view slides the column has to be
    /// SEEN travelling past — otherwise the columns whizzing by during a fast
    /// scroll are exactly the ones that have parked, and the screen goes empty
    /// instead of showing the strip move.
    ///
    /// So the two answers are separated: the rect above stays the safe commit,
    /// and this is the position to paint at. The compositor translates by the
    /// difference and then adds the view offset on top, which puts the column
    /// back in lockstep with the rest of the strip.
    ///
    /// `hasVisualPos` rather than a zero sentinel: (0, 0) is a legal position
    /// on a strip whose screen starts at the origin.
    int visualX = 0;
    int visualY = 0;
    bool hasVisualPos = false;
    /// Scrolling mode: the window id of the tab this one is REPLACING in a
    /// tabbed column, when this entry is a tab being activated. Empty for
    /// every other placement.
    ///
    /// A tab switch is two commits sharing one rect — the outgoing tab parks,
    /// the incoming tab takes the rect it just vacated — and the pair is not
    /// recoverable from the rects alone: both are ordinary placements, and
    /// matching them by coincident geometry would also fire on a column that
    /// merely re-laid out. Only the engine knows the two entries are the same
    /// swap, so it says so, and the compositor cross-fades the outgoing tab's
    /// content into the incoming one instead of hard-cutting between two
    /// different windows in the same rectangle.
    ///
    /// Named on the ARRIVING entry: that is the window still on screen when
    /// the animation runs, and the one the transition is installed on.
    QString tabFrom;
    /// Scrolling mode: this batch's view travel is USER-DRIVEN continuous
    /// motion (the drag edge auto-scroll heartbeat, ~60 Hz), not a discrete
    /// scroll verb. The effect must apply `viewDeltaX` directly — no view
    /// animation leg, no strip shader pass — because the per-tick commits ARE
    /// the motion. Animating on top retargets a leg every 16 ms, which on a
    /// stateless (duration) curve resets its clock and zeroes its velocity
    /// each tick, so the painted strip stalls behind the committed geometry
    /// and then glides once when the ticks stop. Meaningless without a
    /// non-zero `viewDeltaX`; false for every discrete scroll. Trailing, like
    /// every widening since v10, so aggregate-initialized fixtures stay
    /// aligned.
    bool viewImmediate = false;

    QRect toRect() const
    {
        return QRect(x, y, width, height);
    }

    /// Returns empty QString if valid, or a human-readable description of
    /// the invariant violation. Call at every unmarshal site for THIS type to
    /// detect a garbled payload before acting on it.
    ///
    /// Not every wire type in this header has one — AlgorithmInfoEntry and
    /// PreTileGeometryEntry are unmarshalled unvalidated. That is a gap rather
    /// than a policy: the types that carry a validator are the ones whose
    /// garbled values were seen to do damage.
    QString validationError() const;
};

using TileRequestList = QList<TileRequestEntry>;

/// D-Bus struct for algorithm metadata: (sssbbbbdibsbbb)
struct PHOSPHORPROTOCOLTYPES_EXPORT AlgorithmInfoEntry
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
    bool supportsSingleWindow = false;
};

using AlgorithmInfoList = QList<AlgorithmInfoEntry>;

/// D-Bus struct for pre-tile geometry entries: (siiiis)
/// Replaces the JSON blob previously returned by getPreTileGeometriesJson.
struct PHOSPHORPROTOCOLTYPES_EXPORT PreTileGeometryEntry
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

Q_DECLARE_METATYPE(PhosphorProtocol::TileRequestEntry)
Q_DECLARE_METATYPE(PhosphorProtocol::TileRequestList)
Q_DECLARE_METATYPE(PhosphorProtocol::AlgorithmInfoEntry)
Q_DECLARE_METATYPE(PhosphorProtocol::AlgorithmInfoList)
Q_DECLARE_METATYPE(PhosphorProtocol::PreTileGeometryEntry)
Q_DECLARE_METATYPE(PhosphorProtocol::PreTileGeometryList)
