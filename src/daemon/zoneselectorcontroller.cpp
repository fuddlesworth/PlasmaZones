// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "zoneselectorcontroller.h"
#include "../core/zone.h"
#include "../core/constants.h"
#include "../core/utils.h"
#include "../autotile/TilingAlgorithm.h"
#include "../autotile/TilingState.h"
#include <QScreen>
#include <QQuickItem>
#include <QCursor>
#include <QGuiApplication>
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
    QVariantList result;

    if (!m_layoutManager) {
        return result;
    }

    // Get all manual layouts from the layout manager
    const auto layoutList = m_layoutManager->layouts();
    for (Layout* layout : layoutList) {
        result.append(layoutToVariantMap(layout));
    }

    // Append autotile algorithms as layouts
    auto* registry = AlgorithmRegistry::instance();
    if (registry) {
        const QStringList algorithmIds = registry->availableAlgorithms();
        for (const QString& algorithmId : algorithmIds) {
            TilingAlgorithm* algorithm = registry->algorithm(algorithmId);
            if (algorithm) {
                result.append(autotileToVariantMap(algorithm, algorithmId));
            }
        }
    }

    return result;
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
                    m_layoutManager->layoutForScreen(m_screen->name(), m_currentVirtualDesktop, QString());
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
                    if (m_isDragging && m_screen && m_screen->name() == screenName && layout) {
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
        Layout* screenLayout = m_layoutManager->layoutForScreen(m_screen->name(), m_currentVirtualDesktop, QString());
        if (screenLayout) {
            setActiveLayoutId(screenLayout->id().toString());
        } else if (m_layoutManager->activeLayout()) {
            // Fall back to global active layout
            setActiveLayoutId(m_layoutManager->activeLayout()->id().toString());
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
                m_layoutManager->layoutForScreen(m_screen->name(), m_currentVirtualDesktop, QString());
            if (screenLayout) {
                setActiveLayoutId(screenLayout->id().toString());
            }
        }
    }
}

void ZoneSelectorController::startDrag()
{
    if (!m_enabled) {
        return;
    }

    m_isDragging = true;
    m_proximityCheckTimer.start();
    Q_EMIT dragStarted();

    qCDebug(lcOverlay) << "Drag started";
}

void ZoneSelectorController::endDrag()
{
    m_isDragging = false;
    m_proximityCheckTimer.stop();
    hide();
    Q_EMIT dragEnded();

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
    qCDebug(lcOverlay) << "Layout selected:" << layoutId;

    // Check if this is an autotile algorithm selection
    if (LayoutId::isAutotile(layoutId)) {
        QString algorithmId = LayoutId::extractAlgorithmId(layoutId);
        qCInfo(lcOverlay) << "Autotile layout selected:" << algorithmId;
        Q_EMIT autotileLayoutSelected(algorithmId);
        return;
    }

    // Regular manual layout selection
    setActiveLayoutId(layoutId);
    Q_EMIT layoutSelected(layoutId);

    // Notify layout manager to switch layout
    if (m_layoutManager) {
        auto uuidOpt = Utils::parseUuid(layoutId);
        if (uuidOpt) {
            Layout* layout = m_layoutManager->layoutById(*uuidOpt);
            if (layout) {
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

    // Get current cursor position
    QPointF globalPos = QCursor::pos();
    updateCursorPosition(globalPos);
}

void ZoneSelectorController::updateProximity()
{
    if (!m_screen) {
        m_screen = Utils::primaryScreen();
    }

    if (!m_screen) {
        return;
    }

    // Get screen geometry
    QRectF screenGeometry = m_screen->geometry();
    qreal screenTop = screenGeometry.top();
    qreal screenCenterX = screenGeometry.center().x();

    // Calculate distance from top edge
    qreal distanceFromTop = m_cursorPosition.y() - screenTop;

    // Check if cursor is within horizontal trigger zone (centered area)
    qreal horizontalDistance = qAbs(m_cursorPosition.x() - screenCenterX);
    bool inHorizontalZone = horizontalDistance < (screenGeometry.width() / 2 - m_edgeTriggerZone);

    // Calculate proximity (0.0 = at edge, 1.0 = far away)
    qreal proximity = qBound(0.0, distanceFromTop / static_cast<qreal>(m_triggerDistance), 1.0);

    if (m_cursorProximity != proximity) {
        m_cursorProximity = proximity;
        Q_EMIT cursorProximityChanged(proximity);
    }

    // State transitions based on proximity
    if (m_isDragging && inHorizontalZone) {
        if (proximity < 0.3 && m_state == State::Hidden) {
            // Close to edge - show selector
            show();
        } else if (proximity < 0.1 && m_state == State::Near) {
            // Very close - expand
            expand();
        } else if (proximity > 0.7 && m_state != State::Hidden) {
            // Far from edge - hide
            hide();
        }
    }
}

void ZoneSelectorController::checkEdgeProximity()
{
    updateProximity();
}

QString ZoneSelectorController::stateToString(State state) const
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

ZoneSelectorController::State ZoneSelectorController::stringToState(const QString& state) const
{
    if (state == QLatin1String("near")) {
        return State::Near;
    } else if (state == QLatin1String("expanded")) {
        return State::Expanded;
    }
    return State::Hidden;
}

QVariantMap ZoneSelectorController::layoutToVariantMap(Layout* layout) const
{
    QVariantMap map;

    if (!layout) {
        return map;
    }

    map[QStringLiteral("id")] = layout->id().toString();
    map[QStringLiteral("name")] = layout->name();
    map[QStringLiteral("description")] = layout->description();
    map[QStringLiteral("type")] = static_cast<int>(layout->type());
    map[QStringLiteral("zoneCount")] = layout->zoneCount();
    map[QStringLiteral("zones")] = zonesToVariantList(layout);
    map[QStringLiteral("category")] = static_cast<int>(LayoutCategory::Manual);

    return map;
}

QVariantMap ZoneSelectorController::autotileToVariantMap(TilingAlgorithm* algorithm, const QString& algorithmId) const
{
    QVariantMap map;

    if (!algorithm) {
        return map;
    }

    // Use autotile: prefix for ID to distinguish from manual layout UUIDs
    map[QStringLiteral("id")] = LayoutId::makeAutotileId(algorithmId);
    map[QStringLiteral("name")] = algorithm->name();
    map[QStringLiteral("description")] = algorithm->description();
    map[QStringLiteral("type")] = -1; // Not a standard LayoutType
    map[QStringLiteral("zoneCount")] = 0; // Dynamic based on windows
    map[QStringLiteral("zones")] = autotilePreviewZones(algorithm);
    map[QStringLiteral("category")] = static_cast<int>(LayoutCategory::Autotile);

    return map;
}

QVariantList ZoneSelectorController::autotilePreviewZones(TilingAlgorithm* algorithm) const
{
    QVariantList list;

    if (!algorithm) {
        return list;
    }

    // Generate preview zones for a representative window count (e.g., 3 windows)
    // This gives users a sense of how the algorithm arranges windows
    const int previewWindowCount = 3;
    const QRect previewRect(0, 0, 1000, 1000); // Normalized space

    // Create a minimal TilingState for preview
    // We use relative geometry so actual screen size doesn't matter
    TilingState previewState(QStringLiteral("preview"));
    previewState.setMasterCount(1);
    previewState.setSplitRatio(0.6);

    QVector<QRect> zones = algorithm->calculateZones(previewWindowCount, previewRect, previewState);

    for (int i = 0; i < zones.size(); ++i) {
        const QRect& zone = zones[i];
        QVariantMap zoneMap;

        // Generate a stable ID for the preview zone
        zoneMap[QStringLiteral("id")] = QString::number(i);
        zoneMap[QStringLiteral("name")] = QString();
        zoneMap[QStringLiteral("zoneNumber")] = i + 1;

        // Convert to relative geometry (0.0 - 1.0)
        QVariantMap relGeoMap;
        relGeoMap[QStringLiteral("x")] = static_cast<qreal>(zone.x()) / previewRect.width();
        relGeoMap[QStringLiteral("y")] = static_cast<qreal>(zone.y()) / previewRect.height();
        relGeoMap[QStringLiteral("width")] = static_cast<qreal>(zone.width()) / previewRect.width();
        relGeoMap[QStringLiteral("height")] = static_cast<qreal>(zone.height()) / previewRect.height();
        zoneMap[QStringLiteral("relativeGeometry")] = relGeoMap;

        // No custom colors for autotile previews - use theme defaults
        zoneMap[QStringLiteral("useCustomColors")] = false;

        list.append(zoneMap);
    }

    return list;
}

QVariantList ZoneSelectorController::zonesToVariantList(Layout* layout) const
{
    QVariantList list;

    if (!layout) {
        return list;
    }

    const auto zones = layout->zones();
    for (Zone* zone : zones) {
        // Skip null zones to prevent crashes
        if (!zone) {
            qCWarning(lcOverlay) << "Encountered null zone";
            continue;
        }
        list.append(zoneToVariantMap(zone));
    }

    return list;
}

QVariantMap ZoneSelectorController::zoneToVariantMap(Zone* zone) const
{
    QVariantMap map;

    if (!zone) {
        return map;
    }

    using namespace JsonKeys;

    map[Id] = zone->id().toString();
    map[Name] = zone->name();
    map[ZoneNumber] = zone->zoneNumber();

    // Convert relative geometry to QVariantMap
    QRectF relGeo = zone->relativeGeometry();
    QVariantMap relGeoMap;
    relGeoMap[X] = relGeo.x();
    relGeoMap[Y] = relGeo.y();
    relGeoMap[Width] = relGeo.width();
    relGeoMap[Height] = relGeo.height();
    map[RelativeGeometry] = relGeoMap;

    // Always include useCustomColors flag so QML can check it
    map[UseCustomColors] = zone->useCustomColors();

    // Always include zone colors as hex strings (ARGB format) so QML can use them
    // when useCustomColors is true. QML expects color strings, not QColor objects.
    // This allows QML to always have access to zone colors and decide whether to use them.
    map[HighlightColor] = zone->highlightColor().name(QColor::HexArgb);
    map[InactiveColor] = zone->inactiveColor().name(QColor::HexArgb);
    map[BorderColor] = zone->borderColor().name(QColor::HexArgb);

    // Always include appearance properties so QML can use them when useCustomColors is true
    map[ActiveOpacity] = zone->activeOpacity();
    map[InactiveOpacity] = zone->inactiveOpacity();
    map[BorderWidth] = zone->borderWidth();
    map[BorderRadius] = zone->borderRadius();

    return map;
}

} // namespace PlasmaZones
