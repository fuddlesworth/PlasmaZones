// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShell/PlacementMap.h>

#include "placementmap_p.h"

#include <PhosphorShell/Workspaces.h>

#include <QDBusConnectionInterface>
#include <QDBusServiceWatcher>
#include <QQmlEngine>
#include <QSize>

#include <algorithm>
#include <utility>

Q_LOGGING_CATEGORY(lcPlacementMap, "phosphorshell.placementmap")

namespace {
// Layout ids the daemon answers for a non-snapping context; neither is a
// layout document worth fetching.
constexpr QLatin1String AutotilePrefix("autotile:");
constexpr QLatin1String NoneLayout("none");
} // namespace

namespace PhosphorShell {

using namespace PlacementMapParser;
using namespace PlacementMapIface;

// The bus, the call template and the verbs live in placementmap_p.h and
// placementmap_actions.cpp; this file is the screen's data flow and the
// singleton's shared tables.

// =====================================================================
// PlacementMapScreen
// =====================================================================

PlacementMapScreen::PlacementMapScreen(const QString& screenName, PlacementMap* parent)
    : PlacementMapScreen(screenName, -1, parent)
{
}

PlacementMapScreen::PlacementMapScreen(const QString& screenName, int desktopIndex, PlacementMap* parent)
    : QObject(parent)
    , m_map(parent)
    , m_screenName(screenName)
    , m_pinnedDesktop(desktopIndex < 0 ? -1 : desktopIndex)
{
    m_coalesce.setSingleShot(true);
    m_coalesce.setInterval(0);
    connect(&m_coalesce, &QTimer::timeout, this, &PlacementMapScreen::publish);
    m_pinnedRefetch.setSingleShot(true);
    m_pinnedRefetch.setInterval(0);
    connect(&m_pinnedRefetch, &QTimer::timeout, this, &PlacementMapScreen::fetchDesktopWindows);

    if (Workspaces* ws = m_map->workspaces()) {
        connect(ws, &Workspaces::activeChanged, this, &PlacementMapScreen::desktopsChanged);
        connect(ws, &Workspaces::countChanged, this, &PlacementMapScreen::desktopsChanged);
    }
    if (m_map->isAvailable()) {
        reseed();
    }
    desktopsChanged();
}

PlacementMapScreen::~PlacementMapScreen()
{
    // Fire-and-forget: a proxy left behind would keep pointing the
    // compositor's drag at a miniature that is gone.
    unregisterDropProxy();
}

QString PlacementMapScreen::screenName() const
{
    return m_screenName;
}
QString PlacementMapScreen::screenId() const
{
    return m_screenId;
}
int PlacementMapScreen::mode() const
{
    return m_mode;
}
qreal PlacementMapScreen::aspect() const
{
    return m_aspect;
}
QRect PlacementMapScreen::workArea() const
{
    return m_publishedWorkArea;
}
int PlacementMapScreen::stripExtentPx() const
{
    return m_stripExtentPx;
}
QVariantList PlacementMapScreen::cells() const
{
    return m_cells;
}
QVariantMap PlacementMapScreen::lens() const
{
    return m_lens;
}
int PlacementMapScreen::overflowLeft() const
{
    return m_overflowLeft;
}
int PlacementMapScreen::overflowRight() const
{
    return m_overflowRight;
}
int PlacementMapScreen::desktopCount() const
{
    return m_desktopCount;
}
int PlacementMapScreen::currentDesktop() const
{
    return m_currentDesktop;
}
bool PlacementMapScreen::isUrgent() const
{
    return m_urgent;
}
QVariantList PlacementMapScreen::menuModel() const
{
    return m_menu;
}

void PlacementMapScreen::reseed()
{
    ++m_generation;
    m_source.clear();
    m_lastBatch.clear();
    m_sourceLens = QRectF();
    m_sourceOverflowLeft = 0;
    m_sourceOverflowRight = 0;
    m_sourceStripExtentPx = 0;
    m_focusedWindowId.clear();
    m_focusFromDaemon = false;
    clearPinnedSource();
    // A restarted daemon has no proxy for us; the bar re-registers on its
    // next geometry pass.
    m_dropProxyJson.clear();
    resolveScreenId();
}

void PlacementMapScreen::serviceLost()
{
    ++m_generation;
    m_source.clear();
    m_lastBatch.clear();
    m_sourceLens = QRectF();
    m_sourceStripExtentPx = 0;
    m_focusedWindowId.clear();
    m_focusFromDaemon = false;
    clearPinnedSource();
    m_dropProxyJson.clear();
    setState(ScreenState());
    setMenu({});
    rebuildFromSource();
}

void PlacementMapScreen::resolveScreenId()
{
    // The daemon keys screens by an EDID-based id; the bar only knows the
    // connector name. Screen.getScreenId accepts the connector.
    call<QString>(Iface::Screen, QStringLiteral("getScreenId"), {m_screenName}, [this](const QString& id) {
        if (id != m_screenId) {
            m_screenId = id;
            Q_EMIT screenIdChanged();
        }
        refreshGeometry();
        refreshMode();
        fetchFocus();
    });
}

void PlacementMapScreen::refreshGeometry()
{
    if (m_screenId.isEmpty()) {
        return;
    }
    // The screen origin, so workArea and cellRect() can be screen-local.
    call<QRect>(Iface::Screen, QStringLiteral("getScreenGeometry"), {m_screenId}, [this](const QRect& rect) {
        if (rect == m_screenGeometry) {
            return;
        }
        m_screenGeometry = rect;
        schedulePublish();
    });
    call<QRect>(Iface::Screen, QStringLiteral("getAvailableGeometry"), {m_screenId}, [this](const QRect& rect) {
        if (rect == m_workArea) {
            return;
        }
        m_workArea = rect;
        // A tiling batch is in screen pixels; re-normalise it against the new
        // work area. Snapping is relative already; scrolling is re-read, and
        // so is a pinned desktop (its cells are not from a batch).
        if (m_mode == Tiling && !isPinned()) {
            applyTiles(m_lastBatch);
        } else {
            fetchModeData();
        }
        schedulePublish();
    });
}

void PlacementMapScreen::refreshMode()
{
    if (m_screenId.isEmpty()) {
        return;
    }
    if (isPinned()) {
        fetchPinnedMode();
        return;
    }
    call<QString>(Iface::LayoutRegistry, QStringLiteral("getScreenStates"), {}, [this](const QString& json) {
        setState(screenStateFor(json, m_screenId));
        fetchModeData();
    });
}

void PlacementMapScreen::setState(const ScreenState& state)
{
    const bool idsChanged = state.layoutId != m_state.layoutId || state.algorithmId != m_state.algorithmId
        || state.scrollingTemplateId != m_state.scrollingTemplateId;
    m_state = state;
    setMode(state.mode);
    // A menu that is being shown re-reads so its `current` mark follows
    // the assignment; an empty menu is nobody's and stays empty.
    if (idsChanged && !m_menu.isEmpty()) {
        refreshMenu();
    }
}

void PlacementMapScreen::setMode(int mode)
{
    if (mode == m_mode) {
        return;
    }
    m_mode = mode;
    // The drop proxy is a snapping surface; leaving the mode withdraws it.
    if (mode != Snapping) {
        unregisterDropProxy();
    }
    Q_EMIT modeChanged();
    schedulePublish();
}

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
        m_sourceLens = QRectF();
        m_sourceStripExtentPx = 0;
        rebuildFromSource();
        break;
    }
}

void PlacementMapScreen::fetchSnappingLayout()
{
    call<QString>(
        Iface::LayoutRegistry, QStringLiteral("getLayoutForScreen"), {m_screenId}, [this](const QString& layoutId) {
            if (layoutId.isEmpty() || layoutId == NoneLayout || layoutId.startsWith(AutotilePrefix)) {
                m_source.clear();
                rebuildFromSource();
                return;
            }
            call<QString>(Iface::LayoutRegistry, QStringLiteral("getLayout"), {layoutId}, [this](const QString& json) {
                m_source = parseSnappingLayout(json, m_workArea.size());
                m_sourceLens = QRectF();
                rebuildFromSource();
            });
        });
}

void PlacementMapScreen::fetchStrip()
{
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
            if (error.type() == QDBusError::UnknownMethod) {
                m_map->m_caps.stripModel = false;
            }
            fetchVisibleStrip();
        });
}

void PlacementMapScreen::fetchVisibleStrip()
{
    call<QString>(Iface::Scrolling, QStringLiteral("visibleStripJson"), {m_screenId}, [this](const QString& json) {
        applyStrip(parseVisibleStrip(json));
    });
}

void PlacementMapScreen::applyStrip(const StripParse& parse)
{
    m_source = parse.cells;
    m_sourceLens = parse.lens;
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

void PlacementMapScreen::focusedWindowChanged(const QString& windowId)
{
    const bool changed = !m_focusFromDaemon || windowId != m_focusedWindowId;
    m_focusFromDaemon = true;
    m_focusedWindowId = windowId;
    if (changed) {
        rebuildFromSource();
    }
}

QString PlacementMapScreen::effectiveFocusedWindowId() const
{
    return m_focusFromDaemon ? m_focusedWindowId : m_map->focusedWindowId();
}

void PlacementMapScreen::tileBatchReceived(const QList<TileRect>& tiles)
{
    // Keep every batch that names this screen, even while another mode is
    // resolved: a mode switch to tiling then has something to draw.
    bool namesUs = false;
    for (const TileRect& t : tiles) {
        if (t.screenId == m_screenId) {
            namesUs = true;
            break;
        }
    }
    if (namesUs) {
        m_lastBatch = tiles;
    }
    applyTiles(m_lastBatch);
}

void PlacementMapScreen::tilingChanged()
{
    if (m_mode != Tiling) {
        return;
    }
    if (isPinned()) {
        // A retile on this screen may be a window arriving on or leaving
        // the pinned desktop; the desktop read is what settles it.
        fetchDesktopWindows();
    } else {
        fetchCurrentTiles();
    }
}

void PlacementMapScreen::stripChanged()
{
    if (m_mode == Scrolling) {
        if (isPinned()) {
            fetchDesktopWindows();
        } else {
            fetchStrip();
        }
    } else {
        // A context epoch change can mean the desktop switched onto a
        // scrolling context; the mode read is what settles it.
        refreshMode();
    }
}

void PlacementMapScreen::occupancyChanged()
{
    rebuildFromSource();
}

void PlacementMapScreen::desktopsChanged()
{
    Workspaces* ws = m_map->workspaces();
    const int count = ws ? ws->count() : 0;
    const int current = ws ? ws->activeIndex() - 1 : -1;
    bool changed = false;
    if (count != m_desktopCount) {
        m_desktopCount = count;
        Q_EMIT desktopCountChanged();
        changed = true;
    }
    if (current != m_currentDesktop) {
        m_currentDesktop = current;
        Q_EMIT currentDesktopChanged();
        changed = true;
        // Assignments resolve per desktop; the mode may differ over there.
        // A pinned screen's desktop never moves, so it has nothing to
        // re-read.
        if (m_map->isAvailable() && !m_screenId.isEmpty() && !isPinned()) {
            refreshMode();
        }
    }
    if (changed) {
        schedulePublish();
    }
}

void PlacementMapScreen::rebuildFromSource()
{
    m_resolved = m_source;
    // The engine's focus is the current desktop's; a pinned screen shows
    // no focus, and its snapping occupancy is its own desktop's read.
    const QString focused = isPinned() ? QString() : effectiveFocusedWindowId();
    switch (m_mode) {
    case Snapping:
        applyOccupancy(m_resolved, isPinned() ? m_pinnedOccupants : m_map->occupantsForScreen(m_screenId), focused);
        break;
    case Tiling:
        applyFocusByWindowId(m_resolved, focused);
        applyUrgency(m_resolved, m_map->urgentWindows());
        break;
    case Scrolling:
        // The strip model marks activeColumn focused itself, and the
        // visible-cut fallback carries no window id to match, so the
        // source focus stands either way.
        applyUrgency(m_resolved, m_map->urgentWindows());
        break;
    default:
        break;
    }
    // Labels: app id and title per window, fetched once per window and
    // shared across screens. Cells whose window has not answered yet are
    // published unlabelled and re-resolved when the answer lands.
    applyMetadata(m_resolved, m_map->windowMetadata());
    QSet<QString> referenced;
    for (const Cell& cell : std::as_const(m_resolved)) {
        if (!cell.windowId.isEmpty()) {
            referenced.insert(cell.windowId);
            m_map->requestMetadata(cell.windowId);
        }
    }
    m_map->noteReferencedWindows(this, referenced);
    schedulePublish();
}

void PlacementMapScreen::schedulePublish()
{
    m_coalesce.start();
}

void PlacementMapScreen::publish()
{
    const QVariantList cells = toVariantList(m_resolved);
    if (cells != m_cells) {
        m_cells = cells;
        Q_EMIT cellsChanged();
    }
    const QVariantMap lens = m_mode == Scrolling ? lensToVariant(m_sourceLens) : QVariantMap();
    if (lens != m_lens) {
        m_lens = lens;
        Q_EMIT lensChanged();
    }
    const int left = m_mode == Scrolling ? m_sourceOverflowLeft : 0;
    const int right = m_mode == Scrolling ? m_sourceOverflowRight : 0;
    if (left != m_overflowLeft || right != m_overflowRight) {
        m_overflowLeft = left;
        m_overflowRight = right;
        Q_EMIT overflowChanged();
    }
    const int extent = m_mode == Scrolling ? m_sourceStripExtentPx : 0;
    if (extent != m_stripExtentPx) {
        m_stripExtentPx = extent;
        Q_EMIT stripExtentChanged();
    }
    const QRect workArea = m_workArea.translated(-m_screenGeometry.topLeft());
    if (workArea != m_publishedWorkArea) {
        m_publishedWorkArea = workArea;
        Q_EMIT workAreaChanged();
    }
    const qreal aspect = m_workArea.height() > 0 ? qreal(m_workArea.width()) / m_workArea.height() : 0.0;
    if (!qFuzzyCompare(aspect, m_aspect)) {
        m_aspect = aspect;
        Q_EMIT aspectChanged();
    }
    const bool urgent = std::any_of(m_resolved.cbegin(), m_resolved.cend(), [](const Cell& c) {
        return c.urgent;
    });
    if (urgent != m_urgent) {
        m_urgent = urgent;
        Q_EMIT urgentChanged();
    }
    Q_EMIT changed();
}

const Cell* PlacementMapScreen::cellById(const QString& id) const
{
    for (const Cell& c : m_resolved) {
        if (c.id == id) {
            return &c;
        }
    }
    return nullptr;
}

QRect PlacementMapScreen::cellRect(const QString& id) const
{
    const Cell* cell = cellById(id);
    if (!cell || m_publishedWorkArea.isEmpty()) {
        return QRect();
    }
    const QRect& area = m_publishedWorkArea;
    return QRect(qRound(area.x() + cell->rect.x() * area.width()), qRound(area.y() + cell->rect.y() * area.height()),
                 qRound(cell->rect.width() * area.width()), qRound(cell->rect.height() * area.height()));
}

QString PlacementMapScreen::focusedCellId() const
{
    for (const Cell& c : m_resolved) {
        if (c.focused) {
            return c.id;
        }
    }
    return QString();
}

// =====================================================================
// PlacementMap (singleton)
// =====================================================================

PlacementMap::PlacementMap(QObject* parent)
    : QObject(parent)
    , m_bus(new PlacementMapBus(this))
    , m_watcher(new QDBusServiceWatcher(
          Name, QDBusConnection::sessionBus(),
          QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration, this))
    , m_workspaces(new Workspaces(this))
{
    connect(m_watcher, &QDBusServiceWatcher::serviceRegistered, this, [this] {
        setAvailable(true);
    });
    connect(m_watcher, &QDBusServiceWatcher::serviceUnregistered, this, [this] {
        setAvailable(false);
    });

    connect(m_bus, &PlacementMapBus::layoutChanged, this, [this](const QString& screenId) {
        for (PlacementMapScreen* s : std::as_const(m_screens)) {
            if (s->screenId() == screenId || screenId.isEmpty()) {
                s->refreshMode();
            }
        }
    });
    connect(m_bus, &PlacementMapBus::windowStateChanged, this, [this](const PhosphorProtocol::WindowStateEntry& e) {
        applyWindowState(e.windowId, e.screenId, e.zoneIds.isEmpty() ? QStringList{e.zoneId} : e.zoneIds, e.isFloating);
        forEachScreen(&PlacementMapScreen::windowStatesChanged);
    });
    connect(m_bus, &PlacementMapBus::windowMetadataChanged, this,
            [this](const QString& windowId, const QString& appId, const QString& title) {
                const WindowMeta meta{appId, title};
                auto it = m_metadata.find(windowId);
                if (it != m_metadata.end() && it->appId == meta.appId && it->title == meta.title) {
                    return;
                }
                m_metadata.insert(windowId, meta);
                m_metadataPending.remove(windowId);
                forEachScreen(&PlacementMapScreen::occupancyChanged);
            });
    connect(m_bus, &PlacementMapBus::windowUrgencyChanged, this, [this](const QString& windowId, bool urgent) {
        const bool was = m_urgent.contains(windowId);
        if (was == urgent) {
            return;
        }
        if (urgent) {
            m_urgent.insert(windowId);
        } else {
            m_urgent.remove(windowId);
        }
        forEachScreen(&PlacementMapScreen::occupancyChanged);
    });
    connect(m_bus, &PlacementMapBus::currentActivityChanged, this, [this](const QString& activityId) {
        m_activity = activityId;
    });
    // Metadata for windows no screen draws any more is dropped once the
    // burst of rebuilds that dereferenced them has settled.
    m_prune.setSingleShot(true);
    m_prune.setInterval(0);
    connect(&m_prune, &QTimer::timeout, this, &PlacementMap::pruneMetadata);
    connect(m_bus, &PlacementMapBus::tileBatch, this, [this](const QList<TileRect>& tiles) {
        for (PlacementMapScreen* s : std::as_const(m_screens)) {
            s->tileBatchReceived(tiles);
        }
    });
    connect(m_bus, &PlacementMapBus::tilingChanged, this, [this](const QString& screenId) {
        for (PlacementMapScreen* s : std::as_const(m_screens)) {
            if (s->screenId() == screenId) {
                s->tilingChanged();
            }
        }
    });
    connect(m_bus, &PlacementMapBus::stripChanged, this, [this](const QString& screenId) {
        for (PlacementMapScreen* s : std::as_const(m_screens)) {
            if (s->screenId() == screenId) {
                s->stripChanged();
            }
        }
    });
    // The engine's own focus per screen (Tiling.focusedWindowChanged).
    connect(m_bus, &PlacementMapBus::focusedWindowChanged, this,
            [this](const QString& screenId, const QString& windowId) {
                for (PlacementMapScreen* s : std::as_const(m_screens)) {
                    if (s->screenId() == screenId) {
                        s->focusedWindowChanged(windowId);
                    }
                }
            });
    // Focus proxy for a daemon without focusedWindowChanged: the window it
    // asks the compositor to focus. It misses focus changes the user makes
    // with the pointer. A screen that has heard from the daemon ignores it.
    connect(m_bus, &PlacementMapBus::focusRequested, this, [this](const QString& windowId) {
        if (windowId == m_focusedWindowId) {
            return;
        }
        m_focusedWindowId = windowId;
        forEachScreen(&PlacementMapScreen::occupancyChanged);
    });

    setAvailable(QDBusConnection::sessionBus().interface()->isServiceRegistered(Name));
}

PlacementMap::~PlacementMap()
{
    // The screens have to die BEFORE this object's own members and before
    // ~QObject reaps the children. Every PlacementMapScreen destructor calls
    // unregisterDropProxy(), which reads m_caps and calls through m_bus — and
    // m_bus is built in the init list, so it is child index 0 and ~QObject
    // would delete it first, leaving those reads pointing at freed memory.
    // Only reachable with a proxy registered (snapping with the bar up), which
    // is why it has not shown up as a routine crash.
    for (PlacementMapScreen* screen : std::as_const(m_screens)) {
        delete screen;
    }
    m_screens.clear();
}

PlacementMap* PlacementMap::create(QQmlEngine* engine, QJSEngine* scriptEngine)
{
    Q_UNUSED(engine)
    Q_UNUSED(scriptEngine)
    return new PlacementMap();
}

PlacementMapScreen* PlacementMap::forScreen(const QString& screenName)
{
    const QString key = screenKey(screenName, -1);
    if (auto it = m_screens.find(key); it != m_screens.end()) {
        return it.value();
    }
    // Parented here and pinned to C++ ownership: a parentless QObject
    // handed back from a Q_INVOKABLE is collected by the QML engine.
    auto* screen = new PlacementMapScreen(screenName, this);
    QQmlEngine::setObjectOwnership(screen, QQmlEngine::CppOwnership);
    m_screens.insert(key, screen);
    return screen;
}

bool PlacementMap::isAvailable() const
{
    return m_available;
}

Workspaces* PlacementMap::workspaces() const
{
    return m_workspaces;
}

QHash<QString, QStringList> PlacementMap::occupancyForScreen(const QString& screenId) const
{
    QHash<QString, QStringList> out;
    for (auto it = m_occupancy.cbegin(); it != m_occupancy.cend(); ++it) {
        if (it.value().screenId == screenId) {
            out.insert(it.key(), it.value().zoneIds);
        }
    }
    return out;
}

QList<Occupant> PlacementMap::occupantsForScreen(const QString& screenId) const
{
    struct Ranked
    {
        quint64 seq;
        Occupant occupant;
    };
    QList<Ranked> ranked;
    for (auto it = m_occupancy.cbegin(); it != m_occupancy.cend(); ++it) {
        if (it.value().screenId == screenId) {
            ranked.append(Ranked{it.value().seq, Occupant{it.key(), it.value().zoneIds, m_urgent.contains(it.key())}});
        }
    }
    std::sort(ranked.begin(), ranked.end(), [](const Ranked& a, const Ranked& b) {
        return a.seq < b.seq;
    });
    QList<Occupant> out;
    out.reserve(ranked.size());
    for (const Ranked& r : std::as_const(ranked)) {
        out.append(r.occupant);
    }
    return out;
}

QString PlacementMap::focusedWindowId() const
{
    return m_focusedWindowId;
}

const QHash<QString, WindowMeta>& PlacementMap::windowMetadata() const
{
    return m_metadata;
}

const QSet<QString>& PlacementMap::urgentWindows() const
{
    return m_urgent;
}

QString PlacementMap::currentActivity() const
{
    return m_activity;
}

void PlacementMap::setAvailable(bool available)
{
    if (available == m_available) {
        return;
    }
    m_available = available;
    Q_EMIT availableChanged();
    if (available) {
        // A restarted daemon may be a newer one: probe every surface again.
        m_caps = Capabilities();
        m_metadata.clear();
        m_metadataPending.clear();
        m_urgent.clear();
        seedWindowStates();
        seedUrgentWindows();
        seedActivity();
        forEachScreen(&PlacementMapScreen::reseed);
    } else {
        m_occupancy.clear();
        m_focusedWindowId.clear();
        m_activity.clear();
        m_metadata.clear();
        m_metadataPending.clear();
        m_urgent.clear();
        forEachScreen(&PlacementMapScreen::serviceLost);
    }
}

void PlacementMap::seedUrgentWindows()
{
    if (!m_caps.urgentWindows) {
        return;
    }
    auto* watcher =
        new QDBusPendingCallWatcher(m_bus->call(Iface::WindowTracking, QStringLiteral("getUrgentWindows"), {}), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        QDBusPendingReply<QStringList> reply = *w;
        if (reply.isError()) {
            // Older daemon: urgency never fires (A2 §1.4).
            latchUnknownMethod(m_caps.urgentWindows, reply.error());
            return;
        }
        const QStringList ids = reply.value();
        QSet<QString> urgent(ids.cbegin(), ids.cend());
        urgent.remove(QString());
        if (urgent == m_urgent) {
            return;
        }
        m_urgent = urgent;
        forEachScreen(&PlacementMapScreen::occupancyChanged);
    });
}

void PlacementMap::seedActivity()
{
    auto* watcher =
        new QDBusPendingCallWatcher(m_bus->call(Iface::LayoutRegistry, QStringLiteral("getCurrentActivity"), {}), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        QDBusPendingReply<QString> reply = *w;
        if (reply.isError()) {
            qCDebug(lcPlacementMap) << "getCurrentActivity:" << reply.error().message();
            return;
        }
        m_activity = reply.value();
    });
}

void PlacementMap::requestMetadata(const QString& windowId)
{
    if (!m_available || !m_caps.windowMetadata || windowId.isEmpty() || m_metadata.contains(windowId)
        || m_metadataPending.contains(windowId)) {
        return;
    }
    m_metadataPending.insert(windowId);
    auto* watcher = new QDBusPendingCallWatcher(
        m_bus->call(Iface::WindowTracking, QStringLiteral("getWindowMetadata"), {windowId}), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, windowId](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        m_metadataPending.remove(windowId);
        // (appId, title), plus whatever a later daemon appends after them.
        QDBusPendingReply<QString, QString> reply = *w;
        if (reply.isError()) {
            // Older daemon: cells stay unlabelled, and nothing is asked again.
            latchUnknownMethod(m_caps.windowMetadata, reply.error());
            return;
        }
        m_metadata.insert(windowId, WindowMeta{reply.argumentAt<0>(), reply.argumentAt<1>()});
        forEachScreen(&PlacementMapScreen::occupancyChanged);
    });
}

void PlacementMap::noteReferencedWindows(PlacementMapScreen* screen, const QSet<QString>& windowIds)
{
    m_referenced.insert(screen, windowIds);
    m_prune.start();
}

void PlacementMap::pruneMetadata()
{
    QSet<QString> keep;
    for (auto it = m_referenced.cbegin(); it != m_referenced.cend(); ++it) {
        if (m_screens.contains(screenKey(it.key()->screenName(), it.key()->pinnedDesktop()))) {
            keep.unite(it.value());
        }
    }
    for (auto it = m_occupancy.cbegin(); it != m_occupancy.cend(); ++it) {
        keep.insert(it.key());
    }
    for (auto it = m_metadata.begin(); it != m_metadata.end();) {
        it = keep.contains(it.key()) ? std::next(it) : m_metadata.erase(it);
    }
}

void PlacementMap::seedWindowStates()
{
    auto* watcher =
        new QDBusPendingCallWatcher(m_bus->call(Iface::WindowTracking, QStringLiteral("getAllWindowStates"), {}), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        QDBusPendingReply<PhosphorProtocol::WindowStateList> reply = *w;
        if (reply.isError()) {
            qCDebug(lcPlacementMap) << "getAllWindowStates:" << reply.error().message();
            return;
        }
        m_occupancy.clear();
        const auto states = reply.value();
        for (const auto& e : states) {
            if (!e.validationError().isEmpty()) {
                continue;
            }
            applyWindowState(e.windowId, e.screenId, e.zoneIds.isEmpty() ? QStringList{e.zoneId} : e.zoneIds,
                             e.isFloating);
        }
        forEachScreen(&PlacementMapScreen::windowStatesChanged);
    });
}

void PlacementMap::applyWindowState(const QString& windowId, const QString& screenId, const QStringList& zoneIds,
                                    bool floating)
{
    QStringList zones;
    for (const QString& z : zoneIds) {
        if (!z.isEmpty()) {
            zones.append(z);
        }
    }
    // Float is per-mode state and the map is the engine's view: a floating
    // window occupies nothing.
    if (floating || zones.isEmpty()) {
        m_occupancy.remove(windowId);
        return;
    }
    // The latest state change ranks topmost in its zone (A2 §1.3: the
    // label is the topmost window's).
    m_occupancy.insert(windowId, WindowOccupancy{screenId, zones, ++m_occupancySeq});
}

void PlacementMap::forEachScreen(void (PlacementMapScreen::*fn)())
{
    for (PlacementMapScreen* s : std::as_const(m_screens)) {
        (s->*fn)();
    }
}

} // namespace PhosphorShell
