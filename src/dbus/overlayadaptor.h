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

class QDBusServiceWatcher;

namespace PhosphorScreens {
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

private:
    /**
     * @brief Authorise the caller of @ref setSnapAssistThumbnail.
     *
     * The thumbnail-injection method is a UI-spoofing primitive in the
     * wrong hands: any peer on the session bus could otherwise feed
     * arbitrary 256² ARGB32 buffers into the daemon's bounded thumbnail
     * cache, keyed on a compositor handle leaked through the
     * @c snapAssistShown signal. We bind the call to @c kwin_wayland by
     * resolving the sender's bus name to a PID via
     * @c GetConnectionUnixProcessID and checking the basename of
     * @c /proc/<pid>/exe against the accepted set. The @c /proc/<pid>/exe
     * symlink is kernel-maintained and cannot be rewritten from userspace
     * (unlike @c /proc/<pid>/comm, which @c prctl(PR_SET_NAME) can
     * trivially spoof — that earlier comm-based check was replaced for
     * exactly that reason). Verified bus names are cached so the
     * steady-state cost is one set lookup; @c NameOwnerChanged
     * invalidates the cache so a kwin restart followed by a PID reuse
     * cannot inherit trust.
     *
     * Slow-path fallback only: in steady state @ref prewarmKwinTrust has
     * already populated the cache before any thumbnail call arrives, so
     * this path runs only if a call races the pre-warm reply (or the
     * pre-warm failed because @c org.kde.KWin wasn't yet registered).
     *
     * @return true if the sender has been authenticated as kwin.
     */
    bool authenticateKwinSender();

    /**
     * @brief Async pre-warm the trusted-sender cache against the current
     *        owner of @c org.kde.KWin.
     *
     * Inverts the original "first thumbnail pays the auth cost" pattern:
     * resolve kwin's unique bus name and validate its @c /proc/<pid>/exe
     * basename at adaptor construction (and again on any future kwin
     * restart, via @ref m_kwinWatcher). By the time the kwin-effect posts
     * the first thumbnail of a snap-assist invocation, the sender's
     * unique name is already in @ref m_trustedKwinSenders and
     * @ref setSnapAssistThumbnail returns synchronously after a single
     * set lookup — no sync @c GetConnectionUnixProcessID round-trip from
     * inside the D-Bus method handler.
     *
     * Failure modes are bounded: GetNameOwner failure logs and bails
     * (kwin will register later → watcher fires → pre-warm retries);
     * a thumbnail that races the pre-warm falls through to
     * @ref authenticateKwinSender's existing sync path.
     */
    void prewarmKwinTrust();

    /// Async second leg of the pre-warm chain. Resolves @p uniqueName
    /// to a PID via @c GetConnectionUnixProcessID, then hands off to
    /// @ref validateExeAndTrust. Factored out so the @c GetNameOwner
    /// reply lambda stays narrow.
    void resolvePidAndTrust(const QString& uniqueName);

    /**
     * @brief Validate @c /proc/<pid>/exe and (on success) admit
     *        @p uniqueName to @ref m_trustedKwinSenders with an arming
     *        watcher.
     *
     * Single source of truth for the trust-admission step shared by the
     * pre-warm path (@ref resolvePidAndTrust) and the sync fallback
     * (@ref authenticateKwinSender). Idempotent against duplicate
     * admissions for the same unique name.
     *
     * @return true iff @p uniqueName is now in the trust cache.
     */
    bool validateExeAndTrust(const QString& uniqueName, uint pid);

    IOverlayService* m_overlayService; // Interface type (DIP)
    PhosphorZones::IZoneDetector* m_zoneDetector; // Interface type (DIP) - only for highlighting
    // Narrow to IZoneLayoutRegistry — overlay adaptor only reads the active
    // layout, never per-context assignments / quick slots / persistence.
    PhosphorZones::IZoneLayoutRegistry* m_layoutRegistry;
    PhosphorScreens::ScreenManager* m_screenManager;
    ISettings* m_settings; // Interface type (DIP) - for configurable constants

    /// Set of session-bus unique names previously verified as belonging to
    /// kwin_wayland. Populated eagerly via @ref prewarmKwinTrust at
    /// construction (and on every @c org.kde.KWin re-registration), with
    /// the lazy @ref authenticateKwinSender path retained as a slow-path
    /// fallback for the narrow window where a thumbnail call races the
    /// pre-warm reply. Entries are evicted via per-sender
    /// @c QDBusServiceWatcher when their owner unregisters, so a PID
    /// reuse after kwin_wayland exits cannot inherit trust.
    QSet<QString> m_trustedKwinSenders;

    /// Watches @c org.kde.KWin so kwin restarts re-fire the pre-warm
    /// chain. Parent-owned (this); destroyed automatically with the
    /// adaptor.
    QDBusServiceWatcher* m_kwinWatcher = nullptr;
};

} // namespace PlasmaZones
