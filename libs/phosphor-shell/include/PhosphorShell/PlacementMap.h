// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShell/PlacementMapParser.h>
#include <PhosphorShell/phosphorshell_export.h>

#include <QHash>
#include <QList>
#include <QObject>
#include <QRect>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

QT_BEGIN_NAMESPACE
class QQmlEngine;
class QJSEngine;
class QDBusServiceWatcher;
QT_END_NAMESPACE

namespace PhosphorShell {

class PlacementMap;
class PlacementMapBus;
class Workspaces;

/**
 * @brief One screen's view of the placement engine, in work-area fractions.
 *
 * Vended by `PlacementMap.forScreen(name)` and owned by that singleton;
 * QML never constructs one. Every property has its own NOTIFY, and
 * `changed()` fires once per event-loop turn after any burst of updates,
 * so a painter can bind the properties and repaint on `changed`.
 *
 * `mode` is 0 snapping, 1 tiling, 2 scrolling, and -1 when the daemon is
 * absent or does not list this screen. `cells` is a list of maps with
 * `id, x, y, w, h, t, occupied, focused, label, zoneNumber, stack, stripT,
 * columnIndex, windowId, appId, title, urgent`, where the rect is 0..1 of
 * the work area and `t` is the hue axis (A2 §1.2). `lens` is `{x, w}` in
 * scrolling mode and empty otherwise; `stripExtentPx` is the strip's
 * length in scrolling mode (0 against a daemon without `stripModelJson`).
 * `urgent` is true while any cell demands attention (the rail thickening
 * of A2 §3.3).
 *
 * `menuModel` is the right-click menu (A2 §1.5): after `refreshMenu()` it
 * holds one `{kind, id, name, current}` map per choice for this mode
 * (`kind` "layout", "algorithm" or "template") followed by `{kind:
 * "verb", id}` entries (snapping: editLayout, snapAll; tiling: retile,
 * promoteToMaster; scrolling: toggleMaximizeColumn). `applyMenuChoice`
 * assigns the choice for this screen and desktop, or runs the verb.
 *
 * `workArea` and `cellRect(id)` are in this screen's own pixels, origin
 * at the screen's top-left, so a full-screen surface on the screen can
 * place an item on a cell directly (the OSD and toast bands, A3 §3–§4).
 *
 * `currentDesktop` and `switchDesktop(index)` are both 0-based, matching
 * the row index of `Workspaces.model`.
 */
class PHOSPHORSHELL_EXPORT PlacementMapScreen : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString screenName READ screenName CONSTANT)
    Q_PROPERTY(QString screenId READ screenId NOTIFY screenIdChanged)
    Q_PROPERTY(int mode READ mode NOTIFY modeChanged)
    Q_PROPERTY(qreal aspect READ aspect NOTIFY aspectChanged)
    Q_PROPERTY(QRect workArea READ workArea NOTIFY workAreaChanged)
    Q_PROPERTY(QVariantList cells READ cells NOTIFY cellsChanged)
    Q_PROPERTY(QVariantMap lens READ lens NOTIFY lensChanged)
    Q_PROPERTY(int overflowLeft READ overflowLeft NOTIFY overflowChanged)
    Q_PROPERTY(int overflowRight READ overflowRight NOTIFY overflowChanged)
    Q_PROPERTY(int stripExtentPx READ stripExtentPx NOTIFY stripExtentChanged)
    Q_PROPERTY(int desktopCount READ desktopCount NOTIFY desktopCountChanged)
    Q_PROPERTY(int currentDesktop READ currentDesktop NOTIFY currentDesktopChanged)
    Q_PROPERTY(bool urgent READ isUrgent NOTIFY urgentChanged)
    Q_PROPERTY(QVariantList menuModel READ menuModel NOTIFY menuModelChanged)

public:
    enum Mode {
        None = -1,
        Snapping = 0,
        Tiling = 1,
        Scrolling = 2,
    };
    Q_ENUM(Mode)

    PlacementMapScreen(const QString& screenName, PlacementMap* parent);
    ~PlacementMapScreen() override;

    [[nodiscard]] QString screenName() const;
    [[nodiscard]] QString screenId() const;
    [[nodiscard]] int mode() const;
    [[nodiscard]] qreal aspect() const;
    [[nodiscard]] QRect workArea() const;
    [[nodiscard]] QVariantList cells() const;
    [[nodiscard]] QVariantMap lens() const;
    [[nodiscard]] int overflowLeft() const;
    [[nodiscard]] int overflowRight() const;
    [[nodiscard]] int stripExtentPx() const;
    [[nodiscard]] int desktopCount() const;
    [[nodiscard]] int currentDesktop() const;
    [[nodiscard]] bool isUrgent() const;
    [[nodiscard]] QVariantList menuModel() const;

    /// The cell's rect in screen pixels (work-area fractions scaled onto
    /// `workArea`), or a null rect for an unknown id.
    Q_INVOKABLE QRect cellRect(const QString& id) const;
    /// The id of the cell holding the focused window, or empty.
    Q_INVOKABLE QString focusedCellId() const;

    /// Click on a cell. Snapping: activate the zone's topmost window
    /// (`WindowTracking.activateWindow`), or snap the focused window into
    /// an empty zone. Tiling: activate that window. Scrolling:
    /// `Scrolling.focusColumnAt`, stepping one column at a time toward the
    /// cell on a daemon without it.
    Q_INVOKABLE void activate(const QString& id);
    /// Drag of one cell onto another (A2 §1.5). Snapping:
    /// `Snap.swapWindowsById` when the target is occupied, else
    /// `Snap.moveWindowToZone`. Tiling: `Autotile.swapWindows`. Scrolling:
    /// `Scrolling.moveColumnTo`, inert on a daemon without it.
    Q_INVOKABLE void moveCell(const QString& fromId, const QString& toId);
    /// Middle-click: float the cell's window. Snapping:
    /// `Snap.toggleFloatForWindow`. Tiling and scrolling:
    /// `WindowTracking.setWindowFloatingForScreen`, falling back to the
    /// Snap verb on a daemon without it.
    Q_INVOKABLE void toggleFloat(const QString& id);
    /// Move the cell's window to the desktop at this 0-based row of
    /// `Workspaces.model` (`WindowTracking.moveWindowToDesktop`, 1-based on
    /// the wire). Inert on a daemon without it.
    Q_INVOKABLE void moveToDesktop(const QString& id, int index);
    /// Register the miniature as a drop proxy for the compositor's own
    /// window drag (`WindowDrag.registerDropProxy`). `miniatureScreenRect`
    /// is the miniature's rect and `cellScreenRects` one `{id, x, y, w, h}`
    /// map per cell, both in this screen's pixels. Snapping only; a no-op
    /// otherwise and on a daemon without it. Change-gated: re-sent only
    /// when the payload differs.
    Q_INVOKABLE void registerDropProxy(const QRect& miniatureScreenRect, const QVariantList& cellScreenRects);
    /// Withdraw the drop proxy, if one is registered.
    Q_INVOKABLE void unregisterDropProxy();
    /// Re-fetch `menuModel` for the current mode. Async; `menuModelChanged`
    /// fires when the list is in.
    Q_INVOKABLE void refreshMenu();
    /// Apply one `menuModel` entry: assign the layout / algorithm /
    /// template to this screen and desktop, or run the verb.
    Q_INVOKABLE void applyMenuChoice(const QString& kind, const QString& id);
    /// Wheel over the map. Scrolling pans the strip; snapping and tiling
    /// move focus one cell in reading order.
    Q_INVOKABLE void scrollView(int delta);
    /// Drag of the lens: pan the strip's view by `px` in strip space
    /// (`Scrolling.scrollViewByPx`). Scrolling only.
    Q_INVOKABLE void scrollViewByPx(int px);
    /// Switch to the desktop at this 0-based row of `Workspaces.model`.
    Q_INVOKABLE void switchDesktop(int index);

    /// Forget everything and re-resolve from the daemon. Called by the
    /// singleton when the service (re)appears.
    void reseed();
    /// The daemon left the bus: mode -1, no cells.
    void serviceLost();
    /// Re-read the mode for the current (screen, desktop, activity).
    void refreshMode();
    /// Re-read the work area.
    void refreshGeometry();
    /// Occupancy (snapping) or focus (any mode) changed upstream.
    void occupancyChanged();
    /// A `windowsTileRequested` batch arrived; ignored unless it names us.
    void tileBatchReceived(const QList<PlacementMapParser::TileRect>& tiles);
    /// `Tiling.tilingChanged` for this screen.
    void tilingChanged();
    /// `Scrolling.stripChanged` / `stripContextChanged` for this screen.
    void stripChanged();
    /// `Tiling.focusedWindowChanged` for this screen: the engine's own
    /// focus, which supersedes the singleton's focus-request proxy.
    void focusedWindowChanged(const QString& windowId);
    void desktopsChanged();

Q_SIGNALS:
    void screenIdChanged();
    void modeChanged();
    void aspectChanged();
    void workAreaChanged();
    void cellsChanged();
    void lensChanged();
    void overflowChanged();
    void stripExtentChanged();
    void desktopCountChanged();
    void currentDesktopChanged();
    void urgentChanged();
    void menuModelChanged();
    /// Coalesced: once per event-loop turn after any of the above.
    void changed();

private:
    void resolveScreenId();
    void fetchModeData();
    void fetchSnappingLayout();
    void fetchStrip();
    void fetchVisibleStrip();
    void fetchCurrentTiles();
    void fetchFocus();
    void applyStrip(const PlacementMapParser::StripParse& parse);
    void applyTiles(const QList<PlacementMapParser::TileRect>& tiles);
    void stepFocusToward(const PlacementMapParser::Cell* cell);
    void rebuildFromSource();
    void setMode(int mode);
    void setState(const PlacementMapParser::ScreenState& state);
    void schedulePublish();
    void publish();
    // placementmap_actions.cpp
    void activateWindow(const QString& windowId);
    void setWindowFloating(const QString& windowId);
    void fetchSnappingMenu();
    void fetchTilingMenu();
    void fetchScrollingMenu();
    void setMenu(const QVariantList& menu);
    void runVerb(const QString& id);
    void sendDropProxy(const QString& json);
    [[nodiscard]] int wireDesktop() const;
    [[nodiscard]] const PlacementMapParser::Cell* cellById(const QString& id) const;
    [[nodiscard]] QString effectiveFocusedWindowId() const;
    /// Async daemon call whose reply is dropped if the screen has been
    /// reseeded since it was sent. `onError` runs for a D-Bus error (an
    /// older daemon without the method, typically) under the same
    /// generation guard.
    template<typename Reply, typename Fn, typename ErrFn>
    void call(const QString& interface, const QString& method, const QVariantList& args, Fn&& onReply, ErrFn&& onError);
    template<typename Reply, typename Fn>
    void call(const QString& interface, const QString& method, const QVariantList& args, Fn&& onReply);

    PlacementMap* m_map = nullptr;
    QString m_screenName;
    QString m_screenId;
    // Both in the daemon's global coordinates; the published workArea and
    // cellRect() subtract the screen origin.
    QRect m_workArea;
    QRect m_screenGeometry;
    int m_mode = None;
    // The engine's own focus for this screen (Tiling.managedFocusedWindow
    // / focusedWindowChanged). Authoritative once the daemon has answered;
    // until then the singleton's focus-request proxy stands in.
    QString m_focusedWindowId;
    bool m_focusFromDaemon = false;
    // Bumped by reseed()/serviceLost(); in-flight replies carry the value
    // they were sent under and are dropped on mismatch.
    int m_generation = 0;
    // The screen's getScreenStates row: the resolved ids behind `mode`
    // and the daemon's (desktop, activity) for the assignment verbs.
    PlacementMapParser::ScreenState m_state;
    // Bumped per refreshMenu(); a fetch that started under an older value
    // publishes nothing.
    int m_menuGeneration = 0;
    // The last drop-proxy payload sent, empty while none is registered.
    QString m_dropProxyJson;

    // Source data, before occupancy and focus are layered on.
    QList<PlacementMapParser::Cell> m_source;
    QList<PlacementMapParser::TileRect> m_lastBatch;
    QRectF m_sourceLens;
    int m_sourceOverflowLeft = 0;
    int m_sourceOverflowRight = 0;
    int m_sourceStripExtentPx = 0;

    // Resolved cells (occupancy + focus applied), the input to publish().
    QList<PlacementMapParser::Cell> m_resolved;

    // Published values, change-gated.
    QVariantList m_cells;
    QVariantMap m_lens;
    QRect m_publishedWorkArea;
    qreal m_aspect = 0.0;
    int m_overflowLeft = 0;
    int m_overflowRight = 0;
    int m_stripExtentPx = 0;
    int m_desktopCount = 0;
    int m_currentDesktop = -1;
    bool m_urgent = false;
    QVariantList m_menu;

    QTimer m_coalesce;
};

/**
 * @brief QML singleton: the placement engine's geometry per screen.
 *
 *     import Phosphor.Shell
 *     readonly property var map: PlacementMap.forScreen(screen.name)
 *
 * One `PlacementMapScreen` per QScreen name, created on first request,
 * parented here and handed to QML under C++ ownership so the engine never
 * collects it. The singleton holds the process's daemon subscriptions
 * (one per QML engine) and the shared occupancy table; the screens hold
 * their own geometry.
 *
 * Registered imperatively in ShellEngine::load(), like Workspaces.
 */
class PHOSPHORSHELL_EXPORT PlacementMap : public QObject
{
    Q_OBJECT

    /// Whether the daemon is on the session bus right now.
    Q_PROPERTY(bool available READ isAvailable NOTIFY availableChanged)

public:
    explicit PlacementMap(QObject* parent = nullptr);
    ~PlacementMap() override;

    /// QML-singleton factory hook.
    [[nodiscard]] static PlacementMap* create(QQmlEngine* engine, QJSEngine* scriptEngine);

    /// The map for a QScreen name (`PanelWindow.screen.name`). Same
    /// object every call; owned by this singleton.
    Q_INVOKABLE PhosphorShell::PlacementMapScreen* forScreen(const QString& screenName);

    [[nodiscard]] bool isAvailable() const;
    [[nodiscard]] Workspaces* workspaces() const;

    /// windowId → zone ids for non-floating windows on `screenId`.
    [[nodiscard]] QHash<QString, QStringList> occupancyForScreen(const QString& screenId) const;
    /// The non-floating windows on `screenId`, oldest state change first,
    /// so the last occupant of a zone is its topmost window.
    [[nodiscard]] QList<PlacementMapParser::Occupant> occupantsForScreen(const QString& screenId) const;
    /// The window the daemon last asked the compositor to focus or
    /// activate, or empty. The focus proxy a screen falls back on until
    /// `Tiling.managedFocusedWindow` has answered for it; see the .cpp.
    [[nodiscard]] QString focusedWindowId() const;
    /// `WindowTracking.getWindowMetadata` answers so far, by window id.
    [[nodiscard]] const QHash<QString, PlacementMapParser::WindowMeta>& windowMetadata() const;
    /// Windows demanding attention (`getUrgentWindows` seeded, then
    /// `windowUrgencyChanged`).
    [[nodiscard]] const QSet<QString>& urgentWindows() const;
    /// The current activity id (`LayoutRegistry.getCurrentActivity`).
    [[nodiscard]] QString currentActivity() const;

Q_SIGNALS:
    void availableChanged();

private:
    // Screens issue their daemon calls through the singleton's bus.
    friend class PlacementMapScreen;

    struct WindowOccupancy
    {
        QString screenId;
        QStringList zoneIds;
        // Order of the last state change; higher is more recent.
        quint64 seq = 0;
    };

    /// Phase-2 and phase-3 daemon surfaces, each assumed present until a
    /// call fails with UnknownMethod; then the screens use the fallback.
    /// Reset when the service (re)appears, since it may have been upgraded.
    struct Capabilities
    {
        bool stripModel = true;
        bool currentTiles = true;
        bool focusQuery = true;
        bool focusColumnAt = true;
        bool scrollViewByPx = true;
        bool windowMetadata = true;
        bool urgentWindows = true;
        bool activateWindow = true;
        bool setWindowFloatingForScreen = true;
        bool moveWindowToDesktop = true;
        bool moveColumnTo = true;
        bool dropProxy = true;
    };

    void setAvailable(bool available);
    void seedWindowStates();
    void seedUrgentWindows();
    void seedActivity();
    void applyWindowState(const QString& windowId, const QString& screenId, const QStringList& zoneIds, bool floating);
    void forEachScreen(void (PlacementMapScreen::*fn)());
    /// Ask the daemon for a window's app id and title once; the answer
    /// lands in `windowMetadata()` and rebuilds every screen.
    void requestMetadata(const QString& windowId);
    /// A screen's resolved cells reference these windows; metadata for
    /// windows no screen references any more is dropped (coalesced).
    void noteReferencedWindows(PlacementMapScreen* screen, const QSet<QString>& windowIds);
    void pruneMetadata();

    PlacementMapBus* m_bus = nullptr;
    QDBusServiceWatcher* m_watcher = nullptr;
    Workspaces* m_workspaces = nullptr;
    QHash<QString, PlacementMapScreen*> m_screens;
    QHash<QString, WindowOccupancy> m_occupancy;
    quint64 m_occupancySeq = 0;
    QString m_focusedWindowId;
    QString m_activity;
    QHash<QString, PlacementMapParser::WindowMeta> m_metadata;
    QSet<QString> m_metadataPending;
    QHash<PlacementMapScreen*, QSet<QString>> m_referenced;
    QSet<QString> m_urgent;
    QTimer m_prune;
    Capabilities m_caps;
    bool m_available = false;
};

} // namespace PhosphorShell
