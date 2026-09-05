// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShell/PlacementMapParser.h>
#include <PhosphorShell/phosphorshell_export.h>

#include <QHash>
#include <QList>
#include <QObject>
#include <QRect>
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
 * `id, x, y, w, h, t, occupied, focused, label, zoneNumber, stack`, where
 * the rect is 0..1 of the work area and `t` is the hue axis (A2 §1.2).
 * `lens` is `{x, w}` in scrolling mode and empty otherwise.
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
    Q_PROPERTY(QVariantList cells READ cells NOTIFY cellsChanged)
    Q_PROPERTY(QVariantMap lens READ lens NOTIFY lensChanged)
    Q_PROPERTY(int overflowLeft READ overflowLeft NOTIFY overflowChanged)
    Q_PROPERTY(int overflowRight READ overflowRight NOTIFY overflowChanged)
    Q_PROPERTY(int desktopCount READ desktopCount NOTIFY desktopCountChanged)
    Q_PROPERTY(int currentDesktop READ currentDesktop NOTIFY currentDesktopChanged)

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
    [[nodiscard]] QVariantList cells() const;
    [[nodiscard]] QVariantMap lens() const;
    [[nodiscard]] int overflowLeft() const;
    [[nodiscard]] int overflowRight() const;
    [[nodiscard]] int desktopCount() const;
    [[nodiscard]] int currentDesktop() const;

    /// Click on a cell. Snapping: snap the focused window into an empty
    /// zone. Tiling: no daemon surface exists yet to focus a window by id.
    /// Scrolling: focus one column toward the cell.
    Q_INVOKABLE void activate(const QString& id);
    /// Wheel over the map. Scrolling pans the strip; snapping and tiling
    /// move focus one cell in reading order.
    Q_INVOKABLE void scrollView(int delta);
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
    void desktopsChanged();

Q_SIGNALS:
    void screenIdChanged();
    void modeChanged();
    void aspectChanged();
    void cellsChanged();
    void lensChanged();
    void overflowChanged();
    void desktopCountChanged();
    void currentDesktopChanged();
    /// Coalesced: once per event-loop turn after any of the above.
    void changed();

private:
    void resolveScreenId();
    void fetchModeData();
    void fetchSnappingLayout();
    void fetchStrip();
    void rebuildFromSource();
    void setMode(int mode);
    void schedulePublish();
    void publish();
    [[nodiscard]] const PlacementMapParser::Cell* cellById(const QString& id) const;
    /// Async daemon call whose reply is dropped if the screen has been
    /// reseeded since it was sent.
    template<typename Reply, typename Fn>
    void call(const QString& interface, const QString& method, const QVariantList& args, Fn&& onReply);

    PlacementMap* m_map = nullptr;
    QString m_screenName;
    QString m_screenId;
    QRect m_workArea;
    int m_mode = None;
    // Bumped by reseed()/serviceLost(); in-flight replies carry the value
    // they were sent under and are dropped on mismatch.
    int m_generation = 0;

    // Source data, before occupancy and focus are layered on.
    QList<PlacementMapParser::Cell> m_source;
    QList<PlacementMapParser::TileRect> m_lastBatch;
    QRectF m_sourceLens;
    int m_sourceOverflowLeft = 0;
    int m_sourceOverflowRight = 0;

    // Resolved cells (occupancy + focus applied), the input to publish().
    QList<PlacementMapParser::Cell> m_resolved;

    // Published values, change-gated.
    QVariantList m_cells;
    QVariantMap m_lens;
    qreal m_aspect = 0.0;
    int m_overflowLeft = 0;
    int m_overflowRight = 0;
    int m_desktopCount = 0;
    int m_currentDesktop = -1;

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
    /// The window the daemon last asked the compositor to focus or
    /// activate, or empty. Best available focus proxy; see the .cpp.
    [[nodiscard]] QString focusedWindowId() const;

Q_SIGNALS:
    void availableChanged();

private:
    // Screens issue their daemon calls through the singleton's bus.
    friend class PlacementMapScreen;

    struct WindowOccupancy
    {
        QString screenId;
        QStringList zoneIds;
    };

    void setAvailable(bool available);
    void seedWindowStates();
    void applyWindowState(const QString& windowId, const QString& screenId, const QStringList& zoneIds, bool floating);
    void forEachScreen(void (PlacementMapScreen::*fn)());

    PlacementMapBus* m_bus = nullptr;
    QDBusServiceWatcher* m_watcher = nullptr;
    Workspaces* m_workspaces = nullptr;
    QHash<QString, PlacementMapScreen*> m_screens;
    QHash<QString, WindowOccupancy> m_occupancy;
    QString m_focusedWindowId;
    bool m_available = false;
};

} // namespace PhosphorShell
