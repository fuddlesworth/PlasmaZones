// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QMutex>
#include <QObject>
#include <QPointer>
#include <QString>
#include <atomic>
#include <memory>

#include "../core/interfaces.h"
#include "../core/layout.h"

class QQmlEngine;
class QQuickWindow;
class QScreen;
class QTimer;

namespace PlasmaZones {

class Zone;
class TilingAlgorithm;

/**
 * @brief Manages zone overlay windows
 *
 * This class separates UI/overlay concerns from the Daemon,
 * following the Single Responsibility Principle.
 * It handles:
 * - Creating and managing overlay windows per screen
 * - Updating overlay appearance from settings
 * - Zone highlighting and visual feedback
 */
class OverlayService : public IOverlayService
{
    Q_OBJECT

    Q_PROPERTY(bool visible READ isVisible NOTIFY visibilityChanged)
    Q_PROPERTY(bool zoneSelectorVisible READ isZoneSelectorVisible NOTIFY zoneSelectorVisibilityChanged)

public:
    explicit OverlayService(QObject* parent = nullptr);
    ~OverlayService() override;

    // IOverlayService interface
    bool isVisible() const override;
    void show() override;
    void showAtPosition(int cursorX, int cursorY) override;
    void hide() override;
    void toggle() override;

    void updateLayout(Layout* layout) override;
    void updateSettings(ISettings* settings) override;
    void updateGeometries() override;

    // Zone highlighting for overlay display (IOverlayService interface)
    void highlightZone(const QString& zoneId) override;
    void highlightZones(const QStringList& zoneIds) override;
    void clearHighlight() override;

    // Additional methods
    void setLayout(Layout* layout);
    Layout* layout() const
    {
        return m_layout;
    }

    void setSettings(ISettings* settings);
    void setLayoutManager(ILayoutManager* layoutManager);
    void setAutotileEngine(class AutotileEngine* engine);
    void setCurrentVirtualDesktop(int desktop);
    void setCurrentActivity(const QString& activityId);

    // Screen management
    void setupForScreen(QScreen* screen);
    void removeScreen(QScreen* screen);
    void handleScreenAdded(QScreen* screen);
    void handleScreenRemoved(QScreen* screen);

    // Zone selector management (IOverlayService interface)
    bool isZoneSelectorVisible() const override;
    void showZoneSelector() override;
    void hideZoneSelector() override;
    void updateSelectorPosition(int cursorX, int cursorY) override;

    // Mouse position for shader effects
    void updateMousePosition(int cursorX, int cursorY) override;

    // Selected zone from zone selector (IOverlayService interface)
    bool hasSelectedZone() const override;
    QString selectedLayoutId() const override
    {
        return m_selectedLayoutId;
    }
    int selectedZoneIndex() const override
    {
        return m_selectedZoneIndex;
    }
    QRect getSelectedZoneGeometry(QScreen* screen) const override;
    void clearSelectedZone() override;

    // Layout OSD (visual preview when switching layouts)
    void showLayoutOsd(Layout* layout);
    // Overload for showing autotile algorithms as layouts (unified layout model)
    void showLayoutOsd(const QString& id, const QString& name, const QVariantList& zones, int category);

    // Navigation OSD (feedback for keyboard navigation)
    void showNavigationOsd(bool success, const QString& action, const QString& reason);

public Q_SLOTS:
    void hideLayoutOsd();
    void hideNavigationOsd();
    void onZoneSelected(const QString& layoutId, int zoneIndex, const QVariant& relativeGeometry);

    // Shader error reporting from QML
    void onShaderError(const QString& errorLog);

private:
    void createOverlayWindow(QScreen* screen);
    void destroyOverlayWindow(QScreen* screen);
    void updateOverlayWindow(QScreen* screen);
    QVariantList buildZonesList(QScreen* screen) const;
    QVariantList buildLayoutsList() const;
    QVariantMap zoneToVariantMap(Zone* zone, QScreen* screen, Layout* layout = nullptr) const;

    std::unique_ptr<QQmlEngine> m_engine;
    QHash<QScreen*, QQuickWindow*> m_overlayWindows;
    QHash<QScreen*, QQuickWindow*> m_zoneSelectorWindows;
    QPointer<Layout> m_layout;
    QPointer<ISettings> m_settings;
    ILayoutManager* m_layoutManager = nullptr;
    class AutotileEngine* m_autotileEngine = nullptr;
    int m_currentVirtualDesktop = 1; // Current virtual desktop (1-based)
    QString m_currentActivity; // Current KDE activity (empty = all activities)
    bool m_visible = false;
    bool m_zoneSelectorVisible = false;

    // Zone selector selection tracking
    QString m_selectedLayoutId;
    int m_selectedZoneIndex = -1;
    QRectF m_selectedZoneRelGeo;

    // Layout OSD windows
    QHash<QScreen*, QQuickWindow*> m_layoutOsdWindows;

    // Navigation OSD windows
    QHash<QScreen*, QQuickWindow*> m_navigationOsdWindows;
    // Track screens with failed window creation to prevent log spam
    QHash<QScreen*, bool> m_navigationOsdCreationFailed;
    // Deduplicate navigation feedback (prevent duplicate OSDs from Qt signal + D-Bus signal)
    QString m_lastNavigationAction;
    QString m_lastNavigationReason;
    QElapsedTimer m_lastNavigationTime;

    void createZoneSelectorWindow(QScreen* screen);
    void destroyZoneSelectorWindow(QScreen* screen);
    void updateZoneSelectorWindow(QScreen* screen);
    void createLayoutOsdWindow(QScreen* screen);
    void destroyLayoutOsdWindow(QScreen* screen);
    void createNavigationOsdWindow(QScreen* screen);
    void destroyNavigationOsdWindow(QScreen* screen);

    /**
     * @brief Create a QML window from a resource URL (DRY helper)
     * @param qmlUrl QML resource URL (e.g., "qrc:/ui/ZoneOverlay.qml")
     * @param screen Screen to assign the window to
     * @param windowType Description for logging (e.g., "overlay", "zone selector")
     * @return Created window with C++ ownership, or nullptr on failure
     *
     * Handles common QML window creation: component loading, error checking,
     * QQuickWindow casting, ownership, and screen assignment.
     */
    QQuickWindow* createQmlWindow(const QUrl& qmlUrl, QScreen* screen, const char* windowType);

    // Shader support methods
    bool useShaderOverlay() const;
    bool canUseShaders() const;
    void startShaderAnimation();
    void stopShaderAnimation();
    void updateShaderUniforms();
    void updateZonesForAllWindows();

    /**
     * @brief Initialize and show overlay for a given screen or cursor position
     * @param cursorScreen Screen where cursor is located (nullptr = show on all monitors)
     *
     * This is the common implementation for show() and showAtPosition().
     * Extracts ~100 lines of duplicate code from both methods.
     */
    void initializeOverlay(QScreen* cursorScreen);

private Q_SLOTS:
    // System sleep/resume handler (connected to logind PrepareForSleep signal)
    void onPrepareForSleep(bool goingToSleep);

private:
    // Shader timing (shared across all monitors for synchronized effects)
    QElapsedTimer m_shaderTimer;
    std::atomic<qint64> m_lastFrameTime{0};
    std::atomic<int> m_frameCount{0};
    QTimer* m_shaderUpdateTimer = nullptr;
    mutable QMutex m_shaderTimerMutex;

    // Shader state
    bool m_zoneDataDirty = true;
    bool m_shaderErrorPending = false;
    QString m_pendingShaderError;

    // Zone data version for shader synchronization
    int m_zoneDataVersion = 0;
};

} // namespace PlasmaZones
