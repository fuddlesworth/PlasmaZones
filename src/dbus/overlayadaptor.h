// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QObject>
#include <QDBusAbstractAdaptor>
#include <QString>
#include <QHash>

namespace PlasmaZones {

class IOverlayService;
class IZoneDetector;
class ILayoutManager;
class ISettings;
class Zone;

/**
 * @brief D-Bus adaptor for overlay control operations
 *
 * Provides D-Bus interface: org.plasmazones.Overlay
 *  Zone overlay visibility and highlighting only
 *
 * Note: Zone detection and window tracking are handled by separate adaptors
 * (ZoneDetectionAdaptor and WindowTrackingAdaptor).
 *
 * Uses interface types for loose coupling
 */
class PLASMAZONES_EXPORT OverlayAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.plasmazones.Overlay")

public:
    explicit OverlayAdaptor(IOverlayService* overlay, IZoneDetector* detector, ILayoutManager* layoutManager,
                            ISettings* settings, QObject* parent = nullptr);
    ~OverlayAdaptor() override = default;

public Q_SLOTS:
    // Visibility control
    void showOverlay();
    void hideOverlay();
    bool isOverlayVisible();

    // Zone highlighting (requires layout manager for backward compatibility)
    void highlightZone(const QString& zoneId);
    void highlightZones(const QStringList& zoneIds);
    void clearHighlight();

    // Performance constants
    int getPollIntervalMs();
    int getMinimumZoneSizePx();
    int getMinimumZoneDisplaySizePx();

    // Switch to a specific layout
    void switchToLayout(const QString& layoutId);

    // Shader preview overlay (editor Shader Settings dialog)
    void showShaderPreview(int x, int y, int width, int height, const QString& screenName,
                           const QString& shaderId, const QString& shaderParamsJson, const QString& zonesJson);
    void updateShaderPreview(int x, int y, int width, int height,
                             const QString& shaderParamsJson, const QString& zonesJson);
    void hideShaderPreview();

    // Snap Assist overlay (window picker after snapping)
    bool showSnapAssist(const QString& screenName, const QString& emptyZonesJson,
                        const QString& candidatesJson);
    void hideSnapAssist();
    bool isSnapAssistVisible();
    void setSnapAssistThumbnail(const QString& kwinHandle, const QString& dataUrl);

Q_SIGNALS:
    void overlayVisibilityChanged(bool visible);
    void zoneHighlightChanged(const QString& zoneId);
    void layoutSwitched(const QString& layoutId);
    void snapAssistShown(const QString& screenName, const QString& emptyZonesJson,
                         const QString& candidatesJson);

private:
    IOverlayService* m_overlayService; // Interface type (DIP)
    IZoneDetector* m_zoneDetector; // Interface type (DIP) - only for highlighting
    ILayoutManager* m_layoutManager; // Interface type (DIP) - needed for highlightZone by ID
    ISettings* m_settings; // Interface type (DIP) - for configurable constants
};

} // namespace PlasmaZones
