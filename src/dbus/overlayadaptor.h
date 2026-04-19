// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <dbus_types.h>
#include <QObject>
#include <QDBusAbstractAdaptor>
#include <QString>

namespace Phosphor::Screens {
class ScreenManager;
}

namespace PhosphorZones {
class IZoneLayoutRegistry;
class IZoneDetector;
}

namespace PlasmaZones {

class IOverlayService;
class ISettings;

/**
 * @brief D-Bus adaptor for overlay control operations
 *
 * Provides D-Bus interface: org.plasmazones.Overlay
 *  PhosphorZones::Zone overlay visibility and highlighting only
 *
 * Note: PhosphorZones::Zone detection and window tracking are handled by separate adaptors
 * (ZoneDetectionAdaptor and WindowTrackingAdaptor).
 *
 * Uses interface types for loose coupling
 */
class PLASMAZONES_EXPORT OverlayAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.plasmazones.Overlay")

public:
    explicit OverlayAdaptor(IOverlayService* overlay, PhosphorZones::IZoneDetector* detector,
                            PhosphorZones::IZoneLayoutRegistry* layoutRegistry,
                            Phosphor::Screens::ScreenManager* screenManager, ISettings* settings,
                            QObject* parent = nullptr);
    ~OverlayAdaptor() override = default;

public Q_SLOTS:
    // Visibility control
    void showOverlay();
    void hideOverlay();
    bool isOverlayVisible();

    // PhosphorZones::Zone highlighting (requires layout manager for backward compatibility)
    void highlightZone(const QString& zoneId);
    void highlightZones(const QStringList& zoneIds);
    void clearHighlight();

    // Performance constants
    int getPollIntervalMs();
    int getMinimumZoneSizePx();
    int getMinimumZoneDisplaySizePx();

    // Shader preview overlay (editor Shader Settings dialog)
    void showShaderPreview(int x, int y, int width, int height, const QString& screenId, const QString& shaderId,
                           const QString& shaderParamsJson, const QString& zonesJson);
    void updateShaderPreview(int x, int y, int width, int height, const QString& shaderParamsJson,
                             const QString& zonesJson);
    void hideShaderPreview();

    // Snap Assist overlay (window picker after snapping)
    bool showSnapAssist(const QString& screenId, const PlasmaZones::EmptyZoneList& emptyZones,
                        const PlasmaZones::SnapAssistCandidateList& candidates);
    void hideSnapAssist();
    bool isSnapAssistVisible();
    void setSnapAssistThumbnail(const QString& compositorHandle, const QString& dataUrl);

Q_SIGNALS:
    void overlayVisibilityChanged(bool visible);
    void zoneHighlightChanged(const QString& zoneId);
    void snapAssistShown(const QString& screenId, const PlasmaZones::EmptyZoneList& emptyZones,
                         const PlasmaZones::SnapAssistCandidateList& candidates);

private:
    IOverlayService* m_overlayService; // Interface type (DIP)
    PhosphorZones::IZoneDetector* m_zoneDetector; // Interface type (DIP) - only for highlighting
    // Narrow to IZoneLayoutRegistry — overlay adaptor only reads the active
    // layout, never per-context assignments / quick slots / persistence.
    PhosphorZones::IZoneLayoutRegistry* m_layoutRegistry;
    Phosphor::Screens::ScreenManager* m_screenManager;
    ISettings* m_settings; // Interface type (DIP) - for configurable constants
};

} // namespace PlasmaZones
