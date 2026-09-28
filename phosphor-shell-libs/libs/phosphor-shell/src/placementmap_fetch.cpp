// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// PlacementMapScreen's daemon reads: the mode data for the screen's current
// mode (the snapping layout, the scrolling strip, the tiling rects) and the
// engine's own focus. Split out of placementmap.cpp at the file-size ceiling;
// the screen's state, rebuild and publish stay there.

#include <PhosphorShell/PlacementMap.h>

#include "placementmap_p.h"

#include <QDBusError>
#include <QList>
#include <QRectF>
#include <QString>

namespace {
// Layout ids the daemon answers for a non-snapping context; neither is a
// layout document worth fetching.
constexpr QLatin1String AutotilePrefix("autotile:");
constexpr QLatin1String NoneLayout("none");
} // namespace

namespace PhosphorShell {

using namespace PlacementMapParser;
using namespace PlacementMapIface;

void PlacementMapScreen::fetchModeData()
{
    switch (m_mode) {
    case Snapping:
        if (isPinned()) {
            fetchPinnedSnappingLayout();
        } else {
            fetchSnappingLayout();
        }
        break;
    case Tiling:
    case Scrolling:
        // A non-current desktop has no engine replay to read: its cells
        // are the desktop's windows (placementmap_desktop.cpp).
        if (isPinned()) {
            fetchDesktopWindows();
        } else if (m_mode == Tiling) {
            fetchCurrentTiles();
        } else {
            fetchStrip();
        }
        break;
    default:
        m_source.clear();
        m_sourceWindows.clear();
        m_sourceLens = QRectF();
        m_sourceStripExtentPx = 0;
        rebuildFromSource();
        break;
    }
}

void PlacementMapScreen::fetchSnappingLayout()
{
    // Mode-guarded replies: a late getLayout answer after a mode flip must not
    // publish zone cells under Tiling or None.
    call<QString>(
        Iface::LayoutRegistry, QStringLiteral("getLayoutForScreen"), {m_screenId}, [this](const QString& layoutId) {
            if (m_mode != Snapping || isPinned()) {
                return;
            }
            if (layoutId.isEmpty() || layoutId == NoneLayout || layoutId.startsWith(AutotilePrefix)) {
                m_source.clear();
                m_sourceWindows.clear();
                rebuildFromSource();
                return;
            }
            call<QString>(Iface::LayoutRegistry, QStringLiteral("getLayout"), {layoutId}, [this](const QString& json) {
                if (m_mode != Snapping || isPinned()) {
                    return;
                }
                m_source = parseSnappingLayout(json, m_workArea.size());
                m_sourceLens = QRectF();
                rebuildFromSource();
            });
        });
}

void PlacementMapScreen::fetchStrip()
{
    // The timer slot can fire after a mode flip inside the settle.
    if (m_mode != Scrolling || isPinned()) {
        return;
    }
    // One read in flight at a time: each stripModelJson answer costs the
    // daemon a relayout, so a wake-up that lands mid-read is remembered and
    // re-armed once the reply is in rather than stacked behind it.
    if (m_stripFetchInFlight) {
        m_stripRefetchWanted = true;
        return;
    }
    m_stripFetchInFlight = true;
    // The strip model carries the structure axis, the lens and the overflow
    // counts. A daemon without it answers UnknownMethod and the visible
    // cut stands in (full lens, hue sampled inside the cut).
    if (!m_map->m_caps.stripModel) {
        fetchVisibleStrip();
        return;
    }
    call<QString>(
        Iface::Scrolling, QStringLiteral("stripModelJson"), {m_screenId},
        [this](const QString& json) {
            applyStrip(parseStripModel(json));
        },
        [this](const QDBusError& error) {
            latchUnknownMethod(m_map->m_caps.stripModel, error);
            fetchVisibleStrip();
        });
}

void PlacementMapScreen::fetchVisibleStrip()
{
    call<QString>(
        Iface::Scrolling, QStringLiteral("visibleStripJson"), {m_screenId},
        [this](const QString& json) {
            applyStrip(parseVisibleStrip(json));
        },
        [this](const QDBusError&) {
            stripFetchFinished();
        });
}

void PlacementMapScreen::stripFetchFinished()
{
    m_stripFetchInFlight = false;
    if (m_stripRefetchWanted) {
        m_stripRefetchWanted = false;
        m_stripRefetch.start();
    }
}

void PlacementMapScreen::applyStrip(const StripParse& parse)
{
    if (m_mode != Scrolling || isPinned()) {
        // A reply for the mode this screen has left: finish the read without
        // re-arming it and leave the current mode's source alone.
        m_stripRefetchWanted = false;
        stripFetchFinished();
        return;
    }
    stripFetchFinished();
    m_source = parse.cells;
    m_sourceWindows = parse.windows;
    m_sourceLens = parse.lens;
    m_sourceVertical = parse.vertical;
    m_sourceOverflowLeft = parse.overflowLeft;
    m_sourceOverflowRight = parse.overflowRight;
    m_sourceStripExtentPx = parse.stripExtentPx;
    rebuildFromSource();
}

void PlacementMapScreen::fetchCurrentTiles()
{
    // Replay of the engine's current tiles, so the map is never blank
    // after a reseed or a mode switch. Without it (older daemon) the last
    // batch seen on the bus stands until the next retile.
    if (!m_map->m_caps.currentTiles) {
        applyTiles(m_lastBatch);
        return;
    }
    call<QString>(
        Iface::Tiling, QStringLiteral("currentTilesJson"), {m_screenId},
        [this](const QString& json) {
            m_lastBatch = tileRectsFromJson(json);
            applyTiles(m_lastBatch);
        },
        [this](const QDBusError& error) {
            if (error.type() == QDBusError::UnknownMethod) {
                m_map->m_caps.currentTiles = false;
            }
            applyTiles(m_lastBatch);
        });
}

void PlacementMapScreen::applyTiles(const QList<TileRect>& tiles)
{
    // A batch is the current desktop's; a pinned screen never draws one.
    if (m_mode != Tiling || isPinned()) {
        return;
    }
    m_source = parseTileBatch(tiles, m_screenId, m_workArea);
    auto navigationTiles = tiles;
    for (auto& tile : navigationTiles) {
        tile.monocle = false;
    }
    m_sourceWindows = parseTileBatch(navigationTiles, m_screenId, m_workArea);
    m_sourceLens = QRectF();
    m_sourceStripExtentPx = 0;
    rebuildFromSource();
}

void PlacementMapScreen::fetchFocus()
{
    // The engine's own focus for this screen; mode-agnostic. Without it
    // (older daemon) the singleton's focus-request proxy stands in, which
    // misses focus changes made with the pointer. Focus is the current
    // desktop's; a pinned screen has none to read.
    if (m_screenId.isEmpty() || !m_map->m_caps.focusQuery || isPinned()) {
        return;
    }
    call<QString>(
        Iface::Tiling, QStringLiteral("managedFocusedWindow"), {m_screenId},
        [this](const QString& windowId) {
            focusedWindowChanged(windowId);
        },
        [this](const QDBusError& error) {
            if (error.type() == QDBusError::UnknownMethod) {
                m_map->m_caps.focusQuery = false;
            }
        });
}

} // namespace PhosphorShell
