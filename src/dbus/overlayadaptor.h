// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <PhosphorProtocol/WireTypes.h>
#include <QObject>
#include <QDBusAbstractAdaptor>
#include <QDBusContext>
#include <QSet>
#include <QString>

namespace Phosphor::Screens {
class ScreenManager;
}

namespace PhosphorZones {
class IZoneLayoutRegistry;
class IZoneDetector;
}

namespace PlasmaZones {

using PhosphorProtocol::EmptyZoneEntry;
using PhosphorProtocol::EmptyZoneList;
using PhosphorProtocol::SnapAssistCandidateList;

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
/**
 * @brief D-Bus adaptor for overlay control operations.
 *
 * Inherits @c QDBusContext (in addition to @c QDBusAbstractAdaptor) so the
 * @c setSnapAssistThumbnail entry can authenticate its caller — that method
 * accepts an attacker-influenceable image payload from the unauthenticated
 * session bus and only @c kwin_wayland is meant to invoke it.
 */
class PLASMAZONES_EXPORT OverlayAdaptor : public QDBusAbstractAdaptor, public QDBusContext
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
    void setSnapAssistThumbnail(const QString& compositorHandle, int width, int height, const QByteArray& pixels);

Q_SIGNALS:
    void overlayVisibilityChanged(bool visible);
    void zoneHighlightChanged(const QString& zoneId);
    void snapAssistShown(const QString& screenId, const PlasmaZones::EmptyZoneList& emptyZones,
                         const PlasmaZones::SnapAssistCandidateList& candidates);

private:
    /**
     * @brief Authorise the caller of @ref setSnapAssistThumbnail.
     *
     * The thumbnail-injection method is a UI-spoofing primitive in the
     * wrong hands: any peer on the session bus could otherwise feed
     * arbitrary 256² PNGs into the daemon's bounded thumbnail cache,
     * keyed on a compositor handle leaked through the @c snapAssistShown
     * signal. We bind the call to @c kwin_wayland by resolving the
     * sender's bus name to a PID via @c GetConnectionUnixProcessID and
     * checking @c /proc/<pid>/comm. Verified bus names are cached so
     * the steady-state cost is one map lookup; @c NameOwnerChanged
     * invalidates the cache.
     *
     * @return true if the sender has been authenticated as kwin.
     */
    bool authenticateKwinSender();

    IOverlayService* m_overlayService; // Interface type (DIP)
    PhosphorZones::IZoneDetector* m_zoneDetector; // Interface type (DIP) - only for highlighting
    // Narrow to IZoneLayoutRegistry — overlay adaptor only reads the active
    // layout, never per-context assignments / quick slots / persistence.
    PhosphorZones::IZoneLayoutRegistry* m_layoutRegistry;
    Phosphor::Screens::ScreenManager* m_screenManager;
    ISettings* m_settings; // Interface type (DIP) - for configurable constants

    /// Set of session-bus unique names previously verified as belonging to
    /// kwin_wayland. Populated lazily by @ref authenticateKwinSender. Entries
    /// are evicted when @c NameOwnerChanged reports the name's owner went
    /// away, so a PID reuse after kwin_wayland exits cannot inherit trust.
    QSet<QString> m_trustedKwinSenders;
};

} // namespace PlasmaZones
