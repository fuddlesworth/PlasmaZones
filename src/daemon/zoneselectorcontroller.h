// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../core/interfaces.h"
#include "../core/layoutmanager.h"
#include "../core/layout.h"
#include "../autotile/AlgorithmRegistry.h"
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QPointF>
#include <QRectF>
#include <QVariantList>
#include <QVariantMap>
#include <QScreen>
#include <QQuickItem>

namespace PlasmaZones {

/**
 * @brief Controller for the Zone Selector UI component
 *
 * Manages the zone selector that slides in from the top of the screen
 * when the user drags a window near the top edge. Similar to KZones
 * implementation with three states: hidden, near, expanded.
 *
 * Responsibilities:
 * - Track cursor proximity to trigger edge
 * - Manage selector visibility states
 * - Provide layout list to QML
 * - Handle layout selection
 * - Coordinate with OverlayService
 */
class PLASMAZONES_EXPORT ZoneSelectorController : public QObject
{
    Q_OBJECT

    // State properties
    Q_PROPERTY(QString state READ state WRITE setState NOTIFY stateChanged)
    Q_PROPERTY(bool visible READ isVisible NOTIFY visibilityChanged)
    Q_PROPERTY(bool enabled READ isEnabled WRITE setEnabled NOTIFY enabledChanged)

    // Cursor tracking
    Q_PROPERTY(qreal cursorProximity READ cursorProximity NOTIFY cursorProximityChanged)
    Q_PROPERTY(QPointF cursorPosition READ cursorPosition NOTIFY cursorPositionChanged)

    // Layout data
    Q_PROPERTY(QVariantList layouts READ layouts NOTIFY layoutsChanged)
    Q_PROPERTY(QString activeLayoutId READ activeLayoutId WRITE setActiveLayoutId NOTIFY activeLayoutIdChanged)
    Q_PROPERTY(QString hoveredLayoutId READ hoveredLayoutId WRITE setHoveredLayoutId NOTIFY hoveredLayoutIdChanged)

    // Configuration
    Q_PROPERTY(int triggerDistance READ triggerDistance WRITE setTriggerDistance NOTIFY triggerDistanceChanged)
    Q_PROPERTY(int nearDistance READ nearDistance WRITE setNearDistance NOTIFY nearDistanceChanged)
    Q_PROPERTY(int edgeTriggerZone READ edgeTriggerZone WRITE setEdgeTriggerZone NOTIFY edgeTriggerZoneChanged)

    // Selector geometry (set from QML to prevent hiding while cursor is over selector)
    Q_PROPERTY(QRectF selectorGeometry READ selectorGeometry WRITE setSelectorGeometry NOTIFY selectorGeometryChanged)

public:
    /**
     * @brief Selector states matching the QML component
     */
    enum class State {
        Hidden, // Off-screen, not visible
        Near, // Partially visible (peeking)
        Expanded // Fully visible and interactive
    };
    Q_ENUM(State)

    explicit ZoneSelectorController(QObject* parent = nullptr);
    ~ZoneSelectorController() override;

    // State management
    QString state() const;
    State stateEnum() const
    {
        return m_state;
    }
    void setState(const QString& state);
    void setState(State state);

    bool isVisible() const;
    bool isEnabled() const
    {
        return m_enabled;
    }
    void setEnabled(bool enabled);

    // Cursor tracking
    qreal cursorProximity() const
    {
        return m_cursorProximity;
    }
    QPointF cursorPosition() const
    {
        return m_cursorPosition;
    }

    // Layout data
    QVariantList layouts() const;
    QString activeLayoutId() const
    {
        return m_activeLayoutId;
    }
    void setActiveLayoutId(const QString& layoutId);
    QString hoveredLayoutId() const
    {
        return m_hoveredLayoutId;
    }
    void setHoveredLayoutId(const QString& layoutId);

    // Configuration
    int triggerDistance() const
    {
        return m_triggerDistance;
    }
    void setTriggerDistance(int distance);
    int nearDistance() const
    {
        return m_nearDistance;
    }
    void setNearDistance(int distance);
    int edgeTriggerZone() const
    {
        return m_edgeTriggerZone;
    }
    void setEdgeTriggerZone(int zone);

    // Selector geometry
    QRectF selectorGeometry() const
    {
        return m_selectorGeometry;
    }
    void setSelectorGeometry(const QRectF& geometry);

    // Layout management (concrete type for signal connections)
    void setLayoutManager(LayoutManager* layoutManager);
    void setSettings(ISettings* settings);

    // Screen management
    void setScreen(QScreen* screen);
    QScreen* screen() const
    {
        return m_screen;
    }

    // Virtual desktop management
    void setCurrentVirtualDesktop(int desktop);
    int currentVirtualDesktop() const
    {
        return m_currentVirtualDesktop;
    }

    // Drag state management
    Q_INVOKABLE void startDrag();
    Q_INVOKABLE void endDrag();
    Q_INVOKABLE bool isDragging() const
    {
        return m_isDragging;
    }

    // Cursor position updates (called by Daemon during drag)
    Q_INVOKABLE void updateCursorPosition(const QPointF& globalPos);
    Q_INVOKABLE void updateCursorPosition(qreal x, qreal y);

    // Public control methods
    Q_INVOKABLE void show();
    Q_INVOKABLE void hide();
    Q_INVOKABLE void expand();
    Q_INVOKABLE void toggle();

    // Layout selection
    Q_INVOKABLE void selectLayout(const QString& layoutId);
    Q_INVOKABLE void hoverLayout(const QString& layoutId);

    // QML item binding (for direct communication)
    void setQmlItem(QQuickItem* item);
    QQuickItem* qmlItem() const
    {
        return m_qmlItem;
    }

Q_SIGNALS:
    void stateChanged(const QString& state);
    void visibilityChanged(bool visible);
    void enabledChanged(bool enabled);
    void cursorProximityChanged(qreal proximity);
    void cursorPositionChanged(const QPointF& position);
    void layoutsChanged();
    void activeLayoutIdChanged(const QString& layoutId);
    void hoveredLayoutIdChanged(const QString& layoutId);
    void triggerDistanceChanged(int distance);
    void nearDistanceChanged(int distance);
    void edgeTriggerZoneChanged(int zone);
    void selectorGeometryChanged(const QRectF& geometry);

    // Layout selection signal (for external handlers)
    void layoutSelected(const QString& layoutId);
    void layoutHovered(const QString& layoutId);

    // Autotile layout selection (emitted when user selects an autotile algorithm from zone selector)
    void autotileLayoutSelected(const QString& algorithmId);

    // Drag state signals
    void dragStarted();
    void dragEnded();

private Q_SLOTS:
    void onLayoutsChanged();
    void onCollapseTimerTimeout();
    void onProximityCheckTimeout();

private:
    void updateProximity();
    void checkEdgeProximity();
    QString stateToString(State state) const;
    State stringToState(const QString& state) const;
    QVariantMap layoutToVariantMap(Layout* layout) const;
    QVariantList zonesToVariantList(Layout* layout) const;
    QVariantMap zoneToVariantMap(Zone* zone) const;

    // State
    State m_state = State::Hidden;
    bool m_enabled = true;
    bool m_isDragging = false;

    // Cursor tracking
    QPointF m_cursorPosition;
    qreal m_cursorProximity = 1.0; // 0.0 = at edge, 1.0 = far away

    // Configuration (in pixels)
    int m_triggerDistance = 100; // Distance from top edge to activate
    int m_nearDistance = 50; // Distance for "near" state
    int m_edgeTriggerZone = 150; // Horizontal zone width for edge detection

    // Selector geometry (global coordinates, set from QML)
    QRectF m_selectorGeometry;

    // Layout data
    QString m_activeLayoutId;
    QString m_hoveredLayoutId;

    // References (concrete type for signal connections)
    QPointer<LayoutManager> m_layoutManager;
    QPointer<ISettings> m_settings;
    QPointer<QScreen> m_screen;
    QPointer<QQuickItem> m_qmlItem;
    int m_currentVirtualDesktop = 1; // Current virtual desktop (1-based)

    // Timers
    QTimer m_collapseTimer; // Delay before collapsing from expanded
    QTimer m_proximityCheckTimer; // Periodic proximity check during drag
};

} // namespace PlasmaZones
