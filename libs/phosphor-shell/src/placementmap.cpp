// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShell/PlacementMap.h>

#include <PhosphorShell/Workspaces.h>

#include <PhosphorProtocol/AutotileMarshalling.h>
#include <PhosphorProtocol/AutotileTypes.h>
#include <PhosphorProtocol/Registration.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorProtocol/WindowMarshalling.h>
#include <PhosphorProtocol/WindowTypes.h>

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QLoggingCategory>
#include <QQmlEngine>
#include <QSize>

#include <functional>
#include <type_traits>

namespace {
Q_LOGGING_CATEGORY(lcPlacementMap, "phosphorshell.placementmap")

using PhosphorProtocol::Service::Name;
using PhosphorProtocol::Service::ObjectPath;
namespace Iface = PhosphorProtocol::Service::Interface;

// Layout ids the daemon answers for a non-snapping context; neither is a
// layout document worth fetching.
constexpr QLatin1String AutotilePrefix("autotile:");
constexpr QLatin1String NoneLayout("none");
} // namespace

namespace PhosphorShell {

using namespace PlacementMapParser;

// =====================================================================
// PlacementMapBus: one set of session-bus subscriptions per process.
// =====================================================================

/**
 * The daemon's signal fan-in. Owned by the PlacementMap singleton; every
 * PlacementMapScreen connects to the typed signals here rather than to
 * the bus, so hot-reloading the QML engine never re-subscribes.
 */
class PlacementMapBus : public QObject
{
    Q_OBJECT

public:
    explicit PlacementMapBus(QObject* parent)
        : QObject(parent)
    {
        PhosphorProtocol::registerWireTypes();
        auto bus = QDBusConnection::sessionBus();
        bus.connect(Name, ObjectPath, Iface::LayoutRegistry, QStringLiteral("screenLayoutChanged"), this,
                    SLOT(onScreenLayoutChanged(QString)));
        bus.connect(Name, ObjectPath, Iface::LayoutRegistry, QStringLiteral("activeLayoutForScreenChanged"), this,
                    SLOT(onScreenLayoutChanged(QString)));
        bus.connect(Name, ObjectPath, Iface::LayoutRegistry, QStringLiteral("assignmentChangesApplied"), this,
                    SLOT(onAssignmentsApplied(QStringList)));
        bus.connect(Name, ObjectPath, Iface::WindowTracking, QStringLiteral("windowStateChanged"), this,
                    SLOT(onWindowStateChanged(QString, PhosphorProtocol::WindowStateEntry)));
        bus.connect(Name, ObjectPath, Iface::Tiling, QStringLiteral("windowsTileRequested"), this,
                    SLOT(onWindowsTileRequested(PhosphorProtocol::TileRequestList)));
        bus.connect(Name, ObjectPath, Iface::Tiling, QStringLiteral("tilingChanged"), this,
                    SLOT(onTilingChanged(QString)));
        bus.connect(Name, ObjectPath, Iface::Tiling, QStringLiteral("focusWindowRequested"), this,
                    SLOT(onFocusWindowRequested(QString)));
        // Phase-2 surface: an older daemon never emits it, which is harmless.
        bus.connect(Name, ObjectPath, Iface::Tiling, QStringLiteral("focusedWindowChanged"), this,
                    SLOT(onFocusedWindowChanged(QString, QString)));
        bus.connect(Name, ObjectPath, Iface::Scrolling, QStringLiteral("stripChanged"), this,
                    SLOT(onStripChanged(QString)));
        bus.connect(Name, ObjectPath, Iface::Scrolling, QStringLiteral("stripContextChanged"), this,
                    SLOT(onStripChanged(QString)));
    }

    QDBusPendingCall call(const QString& interface, const QString& method, const QVariantList& args) const
    {
        QDBusMessage msg = QDBusMessage::createMethodCall(Name, ObjectPath, interface, method);
        msg.setArguments(args);
        return QDBusConnection::sessionBus().asyncCall(msg);
    }

Q_SIGNALS:
    void layoutChanged(const QString& screenId);
    void windowStateChanged(const PhosphorProtocol::WindowStateEntry& entry);
    void tileBatch(const QList<PlacementMapParser::TileRect>& tiles);
    void tilingChanged(const QString& screenId);
    void focusRequested(const QString& windowId);
    void focusedWindowChanged(const QString& screenId, const QString& windowId);
    void stripChanged(const QString& screenId);

private Q_SLOTS:
    void onScreenLayoutChanged(const QString& screenId)
    {
        Q_EMIT layoutChanged(screenId);
    }
    void onAssignmentsApplied(const QStringList& screenIds)
    {
        for (const QString& id : screenIds) {
            Q_EMIT layoutChanged(id);
        }
    }
    void onWindowStateChanged(const QString& windowId, const PhosphorProtocol::WindowStateEntry& entry)
    {
        Q_UNUSED(windowId)
        if (!entry.validationError().isEmpty()) {
            return;
        }
        Q_EMIT windowStateChanged(entry);
    }
    void onWindowsTileRequested(const PhosphorProtocol::TileRequestList& requests)
    {
        QList<TileRect> tiles;
        tiles.reserve(requests.size());
        for (const auto& r : requests) {
            if (!r.validationError().isEmpty()) {
                continue;
            }
            TileRect t;
            t.windowId = r.windowId;
            t.screenId = r.screenId;
            t.rect = r.toRect();
            t.floating = r.floating;
            t.monocle = r.monocle;
            tiles.append(t);
        }
        Q_EMIT tileBatch(tiles);
    }
    void onTilingChanged(const QString& screenId)
    {
        Q_EMIT tilingChanged(screenId);
    }
    void onFocusWindowRequested(const QString& windowId)
    {
        Q_EMIT focusRequested(windowId);
    }
    void onFocusedWindowChanged(const QString& screenId, const QString& windowId)
    {
        Q_EMIT focusedWindowChanged(screenId, windowId);
    }
    void onStripChanged(const QString& screenId)
    {
        Q_EMIT stripChanged(screenId);
    }
};

// =====================================================================
// PlacementMapScreen
// =====================================================================

PlacementMapScreen::PlacementMapScreen(const QString& screenName, PlacementMap* parent)
    : QObject(parent)
    , m_map(parent)
    , m_screenName(screenName)
{
    m_coalesce.setSingleShot(true);
    m_coalesce.setInterval(0);
    connect(&m_coalesce, &QTimer::timeout, this, &PlacementMapScreen::publish);

    if (Workspaces* ws = m_map->workspaces()) {
        connect(ws, &Workspaces::activeChanged, this, &PlacementMapScreen::desktopsChanged);
        connect(ws, &Workspaces::countChanged, this, &PlacementMapScreen::desktopsChanged);
    }
    if (m_map->isAvailable()) {
        reseed();
    }
    desktopsChanged();
}

PlacementMapScreen::~PlacementMapScreen() = default;

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

template<typename Reply, typename Fn, typename ErrFn>
void PlacementMapScreen::call(const QString& interface, const QString& method, const QVariantList& args, Fn&& onReply,
                              ErrFn&& onError)
{
    const int generation = m_generation;
    auto* watcher = new QDBusPendingCallWatcher(m_map->m_bus->call(interface, method, args), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, generation, fn = std::forward<Fn>(onReply),
             err = std::forward<ErrFn>(onError)](QDBusPendingCallWatcher* w) {
                w->deleteLater();
                if (generation != m_generation) {
                    return;
                }
                using ReplyT = std::conditional_t<std::is_void_v<Reply>, QDBusPendingReply<>, QDBusPendingReply<Reply>>;
                ReplyT reply = *w;
                if (reply.isError()) {
                    qCDebug(lcPlacementMap) << m_screenName << reply.error().message();
                    err(reply.error());
                    return;
                }
                if constexpr (std::is_void_v<Reply>) {
                    fn();
                } else {
                    fn(reply.value());
                }
            });
}

template<typename Reply, typename Fn>
void PlacementMapScreen::call(const QString& interface, const QString& method, const QVariantList& args, Fn&& onReply)
{
    call<Reply>(interface, method, args, std::forward<Fn>(onReply), [](const QDBusError&) { });
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
    setMode(None);
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
        // work area. Snapping is relative already; scrolling is re-read.
        if (m_mode == Tiling) {
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
    call<QString>(Iface::LayoutRegistry, QStringLiteral("getScreenStates"), {}, [this](const QString& json) {
        setMode(modeForScreen(json, m_screenId));
        fetchModeData();
    });
}

void PlacementMapScreen::setMode(int mode)
{
    if (mode == m_mode) {
        return;
    }
    m_mode = mode;
    Q_EMIT modeChanged();
    schedulePublish();
}

void PlacementMapScreen::fetchModeData()
{
    switch (m_mode) {
    case Snapping:
        fetchSnappingLayout();
        break;
    case Tiling:
        fetchCurrentTiles();
        break;
    case Scrolling:
        fetchStrip();
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
    if (m_mode != Tiling) {
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
    // misses focus changes made with the pointer.
    if (m_screenId.isEmpty() || !m_map->m_caps.focusQuery) {
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
    if (m_mode == Tiling) {
        fetchCurrentTiles();
    }
}

void PlacementMapScreen::stripChanged()
{
    if (m_mode == Scrolling) {
        fetchStrip();
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
        if (m_map->isAvailable() && !m_screenId.isEmpty()) {
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
    const QString focused = effectiveFocusedWindowId();
    switch (m_mode) {
    case Snapping:
        applyOccupancy(m_resolved, m_map->occupancyForScreen(m_screenId), focused);
        break;
    case Tiling:
        applyFocusByWindowId(m_resolved, focused);
        break;
    default:
        // Scrolling: the strip model marks activeColumn focused itself, and
        // the visible-cut fallback carries no window id to match, so the
        // source focus stands either way.
        break;
    }
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

void PlacementMapScreen::activate(const QString& id)
{
    const Cell* cell = cellById(id);
    if (!cell || !m_map->isAvailable()) {
        return;
    }
    switch (m_mode) {
    case Snapping:
        if (cell->occupied) {
            // [NEW] Snap.activateWindowInZone(zoneId): no verb focuses the
            // topmost window of a zone by zone id today.
            return;
        }
        if (cell->zoneNumber > 0) {
            m_map->m_bus->call(Iface::Snap, QStringLiteral("snapToZoneByNumber"), {cell->zoneNumber, m_screenId});
        }
        break;
    case Tiling:
        // [NEW] Tiling.focusWindow(windowId): focusNext/focusPrevious only
        // step; a direct focus-by-id verb is needed for a click.
        break;
    case Scrolling:
        if (cell->focused) {
            return;
        }
        if (cell->columnIndex >= 0 && m_map->m_caps.focusColumnAt) {
            // The stepping fallback needs the cell after the reply, and
            // the resolved list may have been rebuilt by then, so it is
            // re-found by id.
            const QString cellId = cell->id;
            call<void>(
                Iface::Scrolling, QStringLiteral("focusColumnAt"), {m_screenId, cell->columnIndex}, [] { },
                [this, cellId](const QDBusError& error) {
                    if (error.type() != QDBusError::UnknownMethod) {
                        return;
                    }
                    m_map->m_caps.focusColumnAt = false;
                    stepFocusToward(cellById(cellId));
                });
            return;
        }
        stepFocusToward(cell);
        break;
    default:
        break;
    }
}

void PlacementMapScreen::stepFocusToward(const Cell* cell)
{
    // Older daemon: only ±1 focus steps exist, so walk from the focused
    // column to the clicked one.
    if (!cell) {
        return;
    }
    int from = -1;
    int to = -1;
    for (int i = 0; i < m_resolved.size(); ++i) {
        if (m_resolved[i].focused) {
            from = i;
        }
        if (&m_resolved[i] == cell) {
            to = i;
        }
    }
    if (from < 0 || to < 0 || from == to) {
        return;
    }
    const int step = to > from ? 1 : -1;
    for (int i = from; i != to; i += step) {
        m_map->m_bus->call(Iface::Scrolling, QStringLiteral("focusColumn"), {m_screenId, step});
    }
}

void PlacementMapScreen::scrollViewByPx(int px)
{
    if (!m_map->isAvailable() || m_mode != Scrolling || px == 0 || !m_map->m_caps.scrollViewByPx) {
        return;
    }
    // No pixel-precise pan exists on an older daemon; the lens drag is
    // simply inert there rather than approximated with whole-column steps.
    call<void>(
        Iface::Scrolling, QStringLiteral("scrollViewByPx"), {m_screenId, px}, [] { },
        [this](const QDBusError& error) {
            if (error.type() == QDBusError::UnknownMethod) {
                m_map->m_caps.scrollViewByPx = false;
            }
        });
}

void PlacementMapScreen::scrollView(int delta)
{
    if (!m_map->isAvailable() || delta == 0) {
        return;
    }
    const int step = delta > 0 ? 1 : -1;
    switch (m_mode) {
    case Scrolling:
        m_map->m_bus->call(Iface::Scrolling, QStringLiteral("scrollView"), {m_screenId, step});
        break;
    case Snapping:
        m_map->m_bus->call(Iface::Snap, QStringLiteral("focusAdjacentZone"),
                           {step > 0 ? QStringLiteral("right") : QStringLiteral("left")});
        break;
    case Tiling:
        m_map->m_bus->call(Iface::Autotile, step > 0 ? QStringLiteral("focusNext") : QStringLiteral("focusPrevious"),
                           {});
        break;
    default:
        break;
    }
}

void PlacementMapScreen::switchDesktop(int index)
{
    Workspaces* ws = m_map->workspaces();
    if (!ws || index < 0) {
        return;
    }
    const QModelIndex mi = ws->model()->index(index, 0);
    if (!mi.isValid()) {
        return;
    }
    ws->switchTo(ws->model()->data(mi, WorkspaceListModel::IdRole).toString());
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
        forEachScreen(&PlacementMapScreen::occupancyChanged);
    });
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

PlacementMap::~PlacementMap() = default;

PlacementMap* PlacementMap::create(QQmlEngine* engine, QJSEngine* scriptEngine)
{
    Q_UNUSED(engine)
    Q_UNUSED(scriptEngine)
    return new PlacementMap();
}

PlacementMapScreen* PlacementMap::forScreen(const QString& screenName)
{
    if (auto it = m_screens.find(screenName); it != m_screens.end()) {
        return it.value();
    }
    // Parented here and pinned to C++ ownership: a parentless QObject
    // handed back from a Q_INVOKABLE is collected by the QML engine.
    auto* screen = new PlacementMapScreen(screenName, this);
    QQmlEngine::setObjectOwnership(screen, QQmlEngine::CppOwnership);
    m_screens.insert(screenName, screen);
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

QString PlacementMap::focusedWindowId() const
{
    return m_focusedWindowId;
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
        seedWindowStates();
        forEachScreen(&PlacementMapScreen::reseed);
    } else {
        m_occupancy.clear();
        m_focusedWindowId.clear();
        forEachScreen(&PlacementMapScreen::serviceLost);
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
        forEachScreen(&PlacementMapScreen::occupancyChanged);
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
    m_occupancy.insert(windowId, WindowOccupancy{screenId, zones});
}

void PlacementMap::forEachScreen(void (PlacementMapScreen::*fn)())
{
    for (PlacementMapScreen* s : std::as_const(m_screens)) {
        (s->*fn)();
    }
}

} // namespace PhosphorShell

#include "placementmap.moc"
