// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../core/interfaces.h"
#include "../core/layout.h"
#include <QObject>
#include <QHash>
#include <QPointer>
#include <memory>

class QQmlEngine;
class QQuickWindow;
class QScreen;

namespace PlasmaZones {

class Zone;

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
    void setCurrentVirtualDesktop(int desktop);

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

public Q_SLOTS:
    void onZoneSelected(const QString& layoutId, int zoneIndex, const QVariant& relativeGeometry);

private:
    void createOverlayWindow(QScreen* screen);
    void destroyOverlayWindow(QScreen* screen);
    void updateOverlayWindow(QScreen* screen);
    QVariantList buildZonesList(QScreen* screen) const;
    QVariantList buildLayoutsList() const;
    QVariantMap layoutToVariantMap(Layout* layout) const;
    QVariantList zonesToVariantList(Layout* layout) const;
    QVariantMap zoneToVariantMap(Zone* zone, QScreen* screen, Layout* layout = nullptr) const;

    std::unique_ptr<QQmlEngine> m_engine;
    QHash<QScreen*, QQuickWindow*> m_overlayWindows;
    QHash<QScreen*, QQuickWindow*> m_zoneSelectorWindows;
    QPointer<Layout> m_layout;
    QPointer<ISettings> m_settings;
    ILayoutManager* m_layoutManager = nullptr;
    int m_currentVirtualDesktop = 1; // Current virtual desktop (1-based)
    bool m_visible = false;
    bool m_zoneSelectorVisible = false;

    // Zone selector selection tracking
    QString m_selectedLayoutId;
    int m_selectedZoneIndex = -1;
    QRectF m_selectedZoneRelGeo;

    void createZoneSelectorWindow(QScreen* screen);
    void destroyZoneSelectorWindow(QScreen* screen);
    void updateZoneSelectorWindow(QScreen* screen);
};

} // namespace PlasmaZones
