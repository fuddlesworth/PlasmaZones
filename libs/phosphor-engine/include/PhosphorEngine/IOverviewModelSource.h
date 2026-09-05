// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorEngine/EngineTypes.h>

#include <QList>
#include <QRect>
#include <QString>

#include <optional>

namespace PhosphorEngine {

/// One window as an engine models it under a placement key, for the
/// workspace overview. Rects are in the engine's OWN coordinate space: the
/// global logical pixels it applies geometry in (the same space the
/// screen-geometry provider hands it). The overview model builder translates
/// them into workspace-local space by subtracting the owning physical
/// output's origin; engines never do that themselves, because a virtual
/// screen's engine key is offset inside its physical output and only the
/// builder knows both.
struct OverviewWindowEntry
{
    QString windowId;
    QRect rect;
    bool floating = false;
    /// Minimized per the engine's own model. The scroll and autotile engines
    /// receive a minimize as a float from the daemon, so their entries carry
    /// it through `floating`; a true here is the snap engine's freed-zone
    /// record.
    bool minimized = false;
    /// Scrolling only: the window's column index in strip order and its tile
    /// index inside that column. -1 elsewhere (and for a scrolling float).
    int column = -1;
    int tile = -1;
};

/// One tile of a scrolling column. `rect` is the tile's resolved rect in
/// the engine's coordinate space (see OverviewWindowEntry::rect), view
/// offset already applied, so a parked tile lies outside the work area.
/// Null for a minimized tile. Rects rather than main/cross-axis offsets so
/// a consumer that does not know the strip's axis (the overview model
/// builder, which only translates by the output origin) stays correct on a
/// vertical strip.
struct OverviewStripTile
{
    QString windowId;
    QRect rect;
};

/// One column of a scrolling strip, in strip order. `rect` is the column's
/// resolved bounding rect in the engine's coordinate space, view offset
/// applied. `activeTab` is the index into `tiles` of the column's active
/// tile (the visible tab when `tabbed`).
struct OverviewStripColumn
{
    QRect rect;
    bool tabbed = false;
    int activeTab = 0;
    QList<OverviewStripTile> tiles;
};

/// A scrolling workspace's strip structure, for the overview's strip
/// rendering, its pan verb and its drop-position resolver. `viewOffset` is
/// the viewport's leading edge in strip coordinates along the strip's main
/// axis (ResolvedStrip::viewOffset); the column and tile rects already
/// account for it.
struct OverviewStripEntry
{
    int viewOffset = 0;
    QList<OverviewStripColumn> columns;
};

/// Read surface a placement engine exposes to the workspace overview. The
/// overview shows EVERY workspace of every screen at once, most of them not
/// the screen's current context, so it cannot use the current-context
/// accessors (visibleTiles, stripSnapshot(screenId), zoneForWindow) — it
/// asks per PlacementStateKey.
///
/// Contract for implementers:
/// - Never create state. A key the engine has no state for answers
///   std::nullopt (never visited, stashed by a mode reassignment, or the
///   mode disabled), and the builder falls back to the daemon's tracked
///   window geometry.
/// - Never mutate focus, anchors or the view. The read must be invisible to
///   the engine's own behaviour.
/// - Every window the engine tracks under the key appears exactly once,
///   floats included.
class IOverviewModelSource
{
public:
    virtual ~IOverviewModelSource() = default;

    /// Windows the engine tracks under @p key, with rects in the engine's
    /// coordinate space. std::nullopt when the engine has NO state for the
    /// key. An engaged empty list is a real, empty context.
    virtual std::optional<QList<OverviewWindowEntry>> overviewWindowsFor(const PlacementStateKey& key) const = 0;

    /// Scrolling only: the strip structure under @p key. Every other engine
    /// (and a scrolling key with no state) answers std::nullopt.
    virtual std::optional<OverviewStripEntry> overviewStripFor(const PlacementStateKey& key) const
    {
        Q_UNUSED(key)
        return std::nullopt;
    }
};

} // namespace PhosphorEngine
