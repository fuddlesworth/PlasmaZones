// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <PhosphorProtocol/ZoneMarshalling.h>
#include <QObject>
#include <QDBusAbstractAdaptor>
#include <QDBusContext>
#include <QDBusUnixFileDescriptor>
#include <QSet>
#include <QString>

namespace PhosphorScreens {
class ScreenManager;
}

namespace PhosphorZones {
class IZoneLayoutRegistry;
class IZoneDetector;
}

namespace PlasmaZones {

class KwinSenderTrust;

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
class PLASMAZONES_EXPORT OverlayAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.plasmazones.Overlay")

public:
    explicit OverlayAdaptor(IOverlayService* overlay, PhosphorZones::IZoneDetector* detector,
                            PhosphorZones::IZoneLayoutRegistry* layoutRegistry,
                            PhosphorScreens::ScreenManager* screenManager, ISettings* settings,
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
    bool showSnapAssist(const QString& screenId, const PhosphorProtocol::EmptyZoneList& emptyZones,
                        const PhosphorProtocol::SnapAssistCandidateList& candidates);
    void hideSnapAssist();
    bool isSnapAssistVisible();
    bool setSnapAssistThumbnail(const QString& compositorHandle, int width, int height, const QByteArray& pixels);
    bool setWindowThumbnailDmabuf(const QString& compositorHandle, int width, int height, uint drmFormat,
                                  qulonglong modifier, uint stride, uint offset, const QDBusUnixFileDescriptor& fd,
                                  const QDBusUnixFileDescriptor& fenceFd);

Q_SIGNALS:
    void overlayVisibilityChanged(bool visible);
    void zoneHighlightChanged(const QString& zoneId);
    void snapAssistShown(const QString& screenId, const PhosphorProtocol::EmptyZoneList& emptyZones,
                         const PhosphorProtocol::SnapAssistCandidateList& candidates);
    /**
     * @brief Emitted when the Snap Assist overlay closes.
     *
     * Forwarded directly from `IOverlayService::snapAssistDismissed`,
     * which fires regardless of dismiss source (user pick, Escape,
     * backdrop click, screen-change cancel, or an explicit
     * `hideSnapAssist()` D-Bus call). Pairs with @ref snapAssistShown
     * so external observers can mirror visibility without polling
     * @ref isSnapAssistVisible. No payload — the dismiss reason is
     * intentionally not surfaced because the internal signal collapses
     * every path through the same emit site.
     */
    void snapAssistDismissed();
    /**
     * @brief Emitted when the daemon's idle-grace trim empties its
     * snap-assist thumbnail stores.
     *
     * Forwarded from `IOverlayService::snapAssistThumbnailCacheTrimmed`.
     * The kwin-effect subscribes and drops its recently-posted dedup set so
     * the next snap-assist re-captures thumbnails the daemon no longer
     * holds instead of stranding on icons.
     */
    void snapAssistThumbnailCacheTrimmed();

private:
    /**
     * @brief Authorise the caller of @ref setSnapAssistThumbnail.
     *
     * The thumbnail-injection method is a UI-spoofing primitive in the
     * wrong hands: any peer on the session bus could otherwise feed
     * arbitrary 256² ARGB32 buffers into the daemon's bounded thumbnail
     * cache. The check is the shared kwin-sender trust (KwinSenderTrust):
     * the sender's unique name resolved to a PID and its
     * @c /proc/<pid>/exe basename matched against the accepted kwin
     * binaries, pre-warmed so the steady-state cost is one set lookup.
     * Direct (non-D-Bus) calls are accepted because there is no remote
     * peer to authorise.
     *
     * @return true if the sender has been authenticated as kwin.
     */
    bool authenticateKwinSender();

    IOverlayService* m_overlayService; // Interface type (DIP)
    PhosphorZones::IZoneDetector* m_zoneDetector; // Interface type (DIP) - only for highlighting
    // Narrow to IZoneLayoutRegistry — overlay adaptor only reads the active
    // layout, never per-context assignments / quick slots / persistence.
    PhosphorZones::IZoneLayoutRegistry* m_layoutRegistry;
    PhosphorScreens::ScreenManager* m_screenManager;
    ISettings* m_settings; // Interface type (DIP) - for configurable constants

    /// Shared kwin-sender authentication (see KwinSenderTrust). Parent-owned.
    KwinSenderTrust* m_kwinTrust = nullptr;
};

} // namespace PlasmaZones
