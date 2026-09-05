// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// PlacementMapScreen pinned to a desktop (A3 §7): the dashboard draws
// every desktop's placement map at once, so a screen needs a view of a
// desktop that is not current. The daemon resolves mode and snapping
// layout per (screen, desktop), and WindowTracking.getWindowStatesForDesktop
// says which windows are there; neither engine replays the tiles or the
// strip of a desktop it is not showing, so those modes draw what is known
// (the window count as equal columns). The live screen (forScreen) stays
// the current desktop's view and the singleton hands it back for the
// current index, so a dashboard cell never draws a stale copy of the
// desktop the user is on.

#include <PhosphorShell/PlacementMap.h>

#include "placementmap_p.h"

#include <PhosphorShell/Workspaces.h>

#include <QQmlEngine>

namespace PhosphorShell {

using namespace PlacementMapParser;
using namespace PlacementMapIface;

int PlacementMapScreen::pinnedDesktop() const
{
    return m_pinnedDesktop;
}

bool PlacementMapScreen::isPinned() const
{
    return m_pinnedDesktop >= 0;
}

void PlacementMapScreen::clearPinnedSource()
{
    m_pinnedOccupants.clear();
    m_pinnedWindowIds.clear();
    m_pinnedRefetch.stop();
}

void PlacementMapScreen::fetchPinnedMode()
{
    // getScreenStates carries the CURRENT context only; the per-desktop
    // read is the assignment cascade resolved for this desktop.
    const int desktop = m_pinnedDesktop + 1;
    call<int>(Iface::LayoutRegistry, QStringLiteral("getModeForScreenDesktop"), {m_screenId, desktop},
              [this, desktop](int mode) {
                  ScreenState state;
                  state.mode = mode >= Snapping && mode <= Scrolling ? mode : None;
                  state.virtualDesktop = desktop;
                  state.activity = m_map->currentActivity();
                  // The resolved ids are read by the mode's own fetch; keep
                  // whatever the last one found so the menu's current mark
                  // survives a mode re-read that lands on the same mode.
                  state.layoutId = m_state.layoutId;
                  state.algorithmId = m_state.algorithmId;
                  state.scrollingTemplateId = m_state.scrollingTemplateId;
                  setState(state);
                  fetchModeData();
              });
}

void PlacementMapScreen::fetchPinnedSnappingLayout()
{
    const int desktop = m_pinnedDesktop + 1;
    call<QString>(Iface::LayoutRegistry, QStringLiteral("getLayoutForScreenDesktop"), {m_screenId, desktop},
                  [this](const QString& layoutId) {
                      // "none", the autotile sentinel, or nothing: an empty map.
                      if (layoutId.isEmpty() || layoutId == QLatin1String("none")
                          || layoutId.startsWith(QLatin1String("autotile:"))) {
                          m_state.layoutId.clear();
                          m_source.clear();
                          rebuildFromSource();
                          return;
                      }
                      m_state.layoutId = layoutId;
                      call<QString>(Iface::LayoutRegistry, QStringLiteral("getLayout"), {layoutId},
                                    [this](const QString& json) {
                                        m_source = parseSnappingLayout(json, m_workArea.size());
                                        m_sourceLens = QRectF();
                                        rebuildFromSource();
                                    });
                  });
    // The occupancy is the desktop's own read, fetched alongside.
    fetchDesktopWindows();
}

void PlacementMapScreen::fetchDesktopWindows()
{
    if (!isPinned() || m_screenId.isEmpty()) {
        return;
    }
    // Older daemon: no per-desktop read. Nothing to draw for this desktop
    // beyond the snapping layout's empty cells; the live screen's
    // occupancy is the current desktop's and would mislabel this one.
    if (!m_map->m_caps.windowStatesForDesktop) {
        applyDesktopWindows({});
        return;
    }
    const int desktop = m_pinnedDesktop + 1;
    call<PhosphorProtocol::WindowStateList>(
        Iface::WindowTracking, QStringLiteral("getWindowStatesForDesktop"), {m_screenId, desktop},
        [this](const PhosphorProtocol::WindowStateList& states) {
            applyDesktopWindows(states);
        },
        [this](const QDBusError& error) {
            latchUnknownMethod(m_map->m_caps.windowStatesForDesktop, error);
            applyDesktopWindows({});
        });
}

void PlacementMapScreen::applyDesktopWindows(const PhosphorProtocol::WindowStateList& states)
{
    // Snapping: the desktop's non-floating windows, in the daemon's order
    // (the last row of a zone is drawn as its topmost occupant, the same
    // convention the singleton's table keeps). Tiling and scrolling: the
    // same windows as the cells themselves.
    QList<Occupant> occupants;
    QStringList windowIds;
    const QSet<QString>& urgent = m_map->urgentWindows();
    for (const auto& e : states) {
        if (!e.validationError().isEmpty() || e.isFloating) {
            continue;
        }
        QStringList zones;
        const QStringList& ids = e.zoneIds.isEmpty() ? QStringList{e.zoneId} : e.zoneIds;
        for (const QString& z : ids) {
            if (!z.isEmpty()) {
                zones.append(z);
            }
        }
        if (!zones.isEmpty()) {
            occupants.append(Occupant{e.windowId, zones, urgent.contains(e.windowId)});
        }
        windowIds.append(e.windowId);
    }
    m_pinnedOccupants = occupants;
    m_pinnedWindowIds = windowIds;
    if (m_mode == Tiling || m_mode == Scrolling) {
        // [NEW] per-desktop tile replay: the engine has no batch or strip
        // for a desktop it is not showing, so the count stands in as
        // equal columns until the daemon can replay that desktop's tiles.
        m_source = stackedColumns(m_pinnedWindowIds);
        m_sourceLens = QRectF();
        m_sourceStripExtentPx = 0;
    }
    rebuildFromSource();
}

void PlacementMapScreen::windowStatesChanged()
{
    if (!isPinned()) {
        occupancyChanged();
        return;
    }
    // Coalesced: a seed lands one edge per window.
    m_pinnedRefetch.start();
}

// =====================================================================
// PlacementMap
// =====================================================================

QString PlacementMap::screenKey(const QString& screenName, int desktopIndex)
{
    if (desktopIndex < 0) {
        return screenName;
    }
    return screenName + QLatin1Char('\n') + QString::number(desktopIndex);
}

PlacementMapScreen* PlacementMap::forScreenDesktop(const QString& screenName, int desktopIndex)
{
    // The current desktop's map is the live one: it follows the engine's
    // focus and tile batches, which a pinned copy could not.
    const int current = m_workspaces ? m_workspaces->activeIndex() - 1 : -1;
    if (desktopIndex < 0 || desktopIndex == current) {
        return forScreen(screenName);
    }
    const QString key = screenKey(screenName, desktopIndex);
    if (auto it = m_screens.find(key); it != m_screens.end()) {
        return it.value();
    }
    // Same ownership rule as forScreen: parented here, C++-owned, so the
    // QML engine never collects it.
    auto* screen = new PlacementMapScreen(screenName, desktopIndex, this);
    QQmlEngine::setObjectOwnership(screen, QQmlEngine::CppOwnership);
    m_screens.insert(key, screen);
    return screen;
}

} // namespace PhosphorShell
