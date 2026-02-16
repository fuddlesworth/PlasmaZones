// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "zoneselectorcontroller.h"
#include "../core/zone.h"
#include "../core/constants.h"
#include "../core/layoututils.h"
#include "../core/utils.h"
#include <QGuiApplication>
#include <QQuickItem>
#include <QScreen>
#include <QUuid>
#include "../core/logging.h"

namespace PlasmaZones {

ZoneSelectorController::ZoneSelectorController(QObject* parent)
    : QObject(parent)
{
    // Configure collapse timer
    m_collapseTimer.setSingleShot(true);
    m_collapseTimer.setInterval(300); // 300ms delay before collapse
    connect(&m_collapseTimer, &QTimer::timeout, this, &ZoneSelectorController::onCollapseTimerTimeout);

    // Configure proximity check timer (only active during drag)
    m_proximityCheckTimer.setInterval(16); // ~60fps updates
    connect(&m_proximityCheckTimer, &QTimer::timeout, this, &ZoneSelectorController::onProximityCheckTimeout);
}

ZoneSelectorController::~ZoneSelectorController() = default;

QString ZoneSelectorController::state() const
{
    return stateToString(m_state);
}

void ZoneSelectorController::setState(const QString& state)
{
    setState(stringToState(state));
}

void ZoneSelectorController::setState(State state)
{
    if (m_state == state) {
        return;
    }

    m_state = state;
    Q_EMIT stateChanged(stateToString(state));
    Q_EMIT visibilityChanged(isVisible());

    // Update QML item if bound
    if (m_qmlItem) {
        QMetaObject::invokeMethod(m_qmlItem, "setState", Q_ARG(QVariant, stateToString(state)));
    }

    qCDebug(lcOverlay) << "State changed to" << stateToString(state);
}

bool ZoneSelectorController::isVisible() const
{
    return m_state != State::Hidden;
}

void ZoneSelectorController::setEnabled(bool enabled)
{
    if (m_enabled == enabled) {
        return;
    }

    m_enabled = enabled;
    Q_EMIT enabledChanged(enabled);

    if (!enabled) {
        hide();
    }
}

QVariantList ZoneSelectorController::layouts() const
{
    // Use shared utility to build filtered layout list for current context
    // and mode-based filtering (manual-only vs autotile-only)
    QString screenName;
    if (m_screen) {
        screenName = m_screen->name();
    }
    const auto entries = LayoutUtils::buildUnifiedLayoutList(
        m_layoutManager, screenName, m_currentVirtualDesktop, m_currentActivity,
        m_includeManualLayouts, m_includeAutotileLayouts);
    return LayoutUtils::toVariantList(entries);
}

void ZoneSelectorController::setActiveLayoutId(const QString& layoutId)
{
    if (m_activeLayoutId == layoutId) {
        return;
    }

    m_activeLayoutId = layoutId;
    Q_EMIT activeLayoutIdChanged(layoutId);
}

void ZoneSelectorController::setHoveredLayoutId(const QString& layoutId)
{
    if (m_hoveredLayoutId == layoutId) {
        return;
    }

    m_hoveredLayoutId = layoutId;
    Q_EMIT hoveredLayoutIdChanged(layoutId);
    Q_EMIT layoutHovered(layoutId);
}

void ZoneSelectorController::setTriggerDistance(int distance)
{
    if (m_triggerDistance == distance) {
        return;
    }

    m_triggerDistance = distance;
    Q_EMIT triggerDistanceChanged(distance);
}

void ZoneSelectorController::setNearDistance(int distance)
{
    if (m_nearDistance == distance) {
        return;
    }

    m_nearDistance = distance;
    Q_EMIT nearDistanceChanged(distance);
}

void ZoneSelectorController::setEdgeTriggerZone(int zone)
{
    if (m_edgeTriggerZone == zone) {
        return;
    }

    m_edgeTriggerZone = zone;
    Q_EMIT edgeTriggerZoneChanged(zone);
}

void ZoneSelectorController::setSelectorGeometry(const QRectF& geometry)
{
    if (m_selectorGeometry == geometry) {
        return;
    }

    m_selectorGeometry = geometry;
    Q_EMIT selectorGeometryChanged(geometry);
}

void ZoneSelectorController::setLayoutManager(LayoutManager* layoutManager)
{
    if (m_layoutManager == layoutManager) {
        return;
    }

    // Disconnect old manager
    if (m_layoutManager) {
        disconnect(m_layoutManager, nullptr, this, nullptr);
    }

    m_layoutManager = layoutManager;

    // Connect new manager using LayoutManager (concrete) - ILayoutManager has no signals
    if (m_layoutManager) {
        connect(m_layoutManager, &LayoutManager::layoutsChanged, this, &ZoneSelectorController::onLayoutsChanged);
        connect(m_layoutManager, &LayoutManager::activeLayoutChanged, this, [this](Layout* layout) {
            // Use screen-specific layout if available, fall back to global active layout
            // Pass current virtual desktop for per-desktop layout lookup
            Layout* effectiveLayout = nullptr;
            if (m_screen) {
                effectiveLayout =
                    m_layoutManager->layoutForScreen(Utils::screenIdentifier(m_screen), m_currentVirtualDesktop, m_currentActivity);
            }
            if (!effectiveLayout) {
                effectiveLayout = layout;
            }
            if (effectiveLayout) {
                setActiveLayoutId(effectiveLayout->id().toString());
            }
        });
        // Also respond to per-screen layout assignments
        // Only update when the popup is actually visible (during drag) to avoid
        // excessive updates during startup or layout switching when popup is hidden
        connect(m_layoutManager, &LayoutManager::layoutAssigned, this,
                [this](const QString& screenName, Layout* layout) {
                    // Only update if:
                    // 1. We're currently dragging (popup may be visible)
                    // 2. This assignment is for our screen
                    // 3. Layout is valid
                    // This prevents cascading updates during startup
                    if (m_isDragging && m_screen && Utils::screenIdentifier(m_screen) == screenName && layout) {
                        setActiveLayoutId(layout->id().toString());
                    }
                });
    }

    Q_EMIT layoutsChanged();
}

void ZoneSelectorController::setSettings(ISettings* settings)
{
    m_settings = settings;
}

void ZoneSelectorController::setScreen(QScreen* screen)
{
    m_screen = screen;

    // Update active layout ID for this screen and current desktop
    if (m_screen && m_layoutManager) {
        Layout* screenLayout = m_layoutManager->layoutForScreen(Utils::screenIdentifier(m_screen), m_currentVirtualDesktop, m_currentActivity);
        if (screenLayout) {
            setActiveLayoutId(screenLayout->id().toString());
        } else if (auto* def = m_layoutManager->defaultLayout()) {
            // Fall back to default layout
            setActiveLayoutId(def->id().toString());
        }
    }
}

void ZoneSelectorController::setCurrentVirtualDesktop(int desktop)
{
    if (m_currentVirtualDesktop != desktop) {
        m_currentVirtualDesktop = desktop;
        // Update active layout ID when desktop changes
        if (m_screen && m_layoutManager) {
            Layout* screenLayout =
                m_layoutManager->layoutForScreen(Utils::screenIdentifier(m_screen), m_currentVirtualDesktop, m_currentActivity);
            if (screenLayout) {
                setActiveLayoutId(screenLayout->id().toString());
            }
        }
    }
}

void ZoneSelectorController::setCurrentActivity(const QString& activity)
{
    if (m_currentActivity != activity) {
        m_currentActivity = activity;
        // Refresh layout list when activity changes
        Q_EMIT layoutsChanged();
    }
}

void ZoneSelectorController::setLayoutFilter(bool includeManual, bool includeAutotile)
{
    if (m_includeManualLayouts == includeManual && m_includeAutotileLayouts == includeAutotile) {
        return;
    }
    m_includeManualLayouts = includeManual;
    m_includeAutotileLayouts = includeAutotile;
    Q_EMIT layoutsChanged();
}

void ZoneSelectorController::startDrag()
{
    if (!m_enabled) {
        return;
    }

    m_isDragging = true;
    m_proximityCheckTimer.start();

    qCDebug(lcOverlay) << "Drag started";
}

void ZoneSelectorController::endDrag()
{
    m_isDragging = false;
    m_proximityCheckTimer.stop();
    hide();

    qCDebug(lcOverlay) << "Drag ended";
}

void ZoneSelectorController::updateCursorPosition(const QPointF& globalPos)
{
    if (m_cursorPosition == globalPos) {
        return;
    }

    m_cursorPosition = globalPos;
    Q_EMIT cursorPositionChanged(globalPos);

    if (m_isDragging) {
        updateProximity();
    }
}

void ZoneSelectorController::updateCursorPosition(qreal x, qreal y)
{
    updateCursorPosition(QPointF(x, y));
}

void ZoneSelectorController::show()
{
    if (!m_enabled || m_state != State::Hidden) {
        return;
    }

    setState(State::Near);
}

void ZoneSelectorController::hide()
{
    if (m_state == State::Hidden) {
        return;
    }

    m_collapseTimer.stop();
    setState(State::Hidden);
}

void ZoneSelectorController::expand()
{
    if (!m_enabled) {
        return;
    }

    setState(State::Expanded);
}

void ZoneSelectorController::toggle()
{
    switch (m_state) {
    case State::Hidden:
        show();
        break;
    case State::Near:
        expand();
        break;
    case State::Expanded:
        hide();
        break;
    }
}

void ZoneSelectorController::selectLayout(const QString& layoutId)
{
    qCInfo(lcOverlay) << "Layout selected:" << layoutId;

    setActiveLayoutId(layoutId);
    Q_EMIT layoutSelected(layoutId);

    // Defensive: set the active layout directly so the switch happens even if the
    // signal chain (QML zoneSelected → OverlayService::onZoneSelected →
    // manualLayoutSelected → daemon handler) is broken. setActiveLayout is
    // idempotent — the daemon handler becomes a no-op for the same layout.
    if (m_layoutManager) {
        QUuid uuid = QUuid::fromString(layoutId);
        if (!uuid.isNull()) {
            Layout* layout = m_layoutManager->layoutById(uuid);
            if (layout && layout != m_layoutManager->activeLayout()) {
                m_layoutManager->setActiveLayout(layout);
            }
        }
    }
}

void ZoneSelectorController::hoverLayout(const QString& layoutId)
{
    setHoveredLayoutId(layoutId);
}

void ZoneSelectorController::setQmlItem(QQuickItem* item)
{
    m_qmlItem = item;
}

void ZoneSelectorController::onLayoutsChanged()
{
    Q_EMIT layoutsChanged();
}

void ZoneSelectorController::onCollapseTimerTimeout()
{
    // Only collapse if still in expanded state and not hovering
    if (m_state == State::Expanded) {
        setState(State::Near);
    }
}

void ZoneSelectorController::onProximityCheckTimeout()
{
    if (!m_isDragging) {
        return;
    }

    // Use already-stored cursor position from D-Bus drag events
    // (QCursor::pos() is unreliable for background daemons on Wayland)
    updateProximity();
}

void ZoneSelectorController::updateProximity()
{
    // Note: Proximity-based triggering during drag is handled by
    // WindowDragAdaptor::isNearTriggerEdge(), which uses per-screen resolved config.
    // This method provides QML property updates for direct cursor-position use cases.

    if (!m_cursorPosition.isNull()) {
        QScreen* cursorScreen = QGuiApplication::screenAt(m_cursorPosition.toPoint());
        if (cursorScreen) {
            m_screen = cursorScreen;
        }
    }

    if (!m_screen) {
        m_screen = Utils::primaryScreen();
    }

    if (!m_screen) {
        return;
    }

    const QRectF screenGeometry = m_screen->geometry();
    const qreal distanceFromTop = m_cursorPosition.y() - screenGeometry.top();

    // Horizontal zone check: cursor must be within the center area
    const qreal hDist = qAbs(m_cursorPosition.x() - screenGeometry.center().x());
    const bool inTriggerZone = hDist < (screenGeometry.width() / 2 - m_edgeTriggerZone);

    // Proximity: 0.0 = at edge, 1.0 = far away
    const qreal proximity = qBound(0.0, distanceFromTop / static_cast<qreal>(m_triggerDistance), 1.0);

    if (m_cursorProximity != proximity) {
        m_cursorProximity = proximity;
        Q_EMIT cursorProximityChanged(proximity);
    }

    bool cursorOverSelector = m_selectorGeometry.isValid() && m_selectorGeometry.contains(m_cursorPosition);

    if (m_isDragging && inTriggerZone) {
        if (proximity < 0.3 && m_state == State::Hidden) {
            show();
        } else if (proximity < 0.1 && m_state == State::Near) {
            expand();
        } else if (proximity > 0.7 && m_state != State::Hidden && !cursorOverSelector) {
            hide();
        }
    }
}

QString ZoneSelectorController::stateToString(State state)
{
    switch (state) {
    case State::Hidden:
        return QStringLiteral("hidden");
    case State::Near:
        return QStringLiteral("near");
    case State::Expanded:
        return QStringLiteral("expanded");
    default:
        return QStringLiteral("hidden");
    }
}

ZoneSelectorController::State ZoneSelectorController::stringToState(const QString& state)
{
    if (state == QLatin1String("near")) {
        return State::Near;
    } else if (state == QLatin1String("expanded")) {
        return State::Expanded;
    }
    return State::Hidden;
}

} // namespace PlasmaZones
