// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <PhosphorProtocol/ZoneMarshalling.h>
#include <QObject>
#include <QDBusAbstractAdaptor>
#include <QDBusUnixFileDescriptor>
#include <QSet>
#include <QString>

#include <functional>

class QDBusConnection;
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
 * The two thumbnail entries authenticate their caller: they accept an
 * attacker-influenceable image payload from the unauthenticated session bus,
 * and only @c kwin_wayland is meant to invoke them. The caller is read from
 * the context of the object this adaptor is exported through
 * (DbusHelpers::busContext), since Qt never sets one on the adaptor itself.
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

    /// The process a thumbnail sender is held to (@ref trustIfCompositor).
    struct Compositor
    {
        uint pid = 0; ///< 0 when there is none, which trusts nobody
        bool trustsChildren = false; ///< a direct child of @c pid passes too
    };
    /// Replaces the production source, which reads the peer credentials of
    /// this process's own Wayland connection (@ref compositor). A unit test,
    /// which runs on no Wayland display, names the compositor with it.
    /// Everything else in the check runs as it does in the daemon.
    using CompositorResolver = std::function<Compositor()>;
    void setCompositorResolverForTesting(CompositorResolver resolver);

    /// Whether @p uniqueName, a unique name on the session bus, is the
    /// compositor's own connection (@ref trustIfCompositor). A name not
    /// trusted yet has its pid resolved with a bounded blocking call. The
    /// compositor bridge's registration is held to it as well as the
    /// thumbnail entries (#1155), so no other peer can register first and
    /// keep the compositor out.
    bool isCompositorPeer(const QString& uniqueName);

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
     * @brief Authorise the caller of @ref setSnapAssistThumbnail and
     *        @ref setWindowThumbnailDmabuf.
     *
     * The thumbnail-injection methods are a UI-spoofing primitive in the
     * wrong hands: any peer on the session bus could otherwise feed
     * arbitrary 256² ARGB32 buffers into the daemon's bounded thumbnail
     * cache, keyed on a compositor handle leaked through the
     * @c snapAssistShown signal. We bind the call to the compositor by
     * resolving the sender's bus name to a PID via
     * @c GetConnectionUnixProcessID and holding it to the compositor's pid
     * (@ref trustIfCompositor). Verified bus names are cached so the
     * steady-state cost is one set lookup; @c NameOwnerChanged
     * invalidates the cache so a kwin restart followed by a PID reuse
     * cannot inherit trust.
     *
     * Slow-path fallback only: in steady state @ref prewarmKwinTrust has
     * already populated the cache before any thumbnail call arrives, so
     * this path runs only if a call races the pre-warm reply (or the
     * pre-warm failed because @c org.kde.KWin wasn't yet registered).
     *
     * A refused caller is answered with an AccessDenied error, so the slot's
     * own return value never reaches it. A direct (non-D-Bus) call has no
     * remote peer to authorise and passes.
     *
     * @return true if the sender has been authenticated as kwin.
     */
    bool authenticateKwinSender();

    /// isCompositorPeer, resolving the pid on @p bus, the connection the
    /// call being checked arrived on.
    bool isCompositorPeerOn(const QDBusConnection& bus, const QString& uniqueName);

    /**
     * @brief Async pre-warm the trusted-sender cache against the current
     *        owner of @c org.kde.KWin.
     *
     * Inverts the original "first thumbnail pays the auth cost" pattern:
     * resolve kwin's unique bus name and hold its pid to the compositor's
     * (@ref trustIfCompositor) at adaptor construction (and again on any
     * future kwin restart, via @ref m_kwinWatcher). By the time the kwin-effect posts
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
    /// @ref trustIfCompositor. Factored out so the @c GetNameOwner
    /// reply lambda stays narrow.
    void resolvePidAndTrust(const QString& uniqueName);

    /**
     * @brief Admit @p uniqueName to @ref m_trustedKwinSenders, with an
     *        arming watcher, when @p pid is the compositor's.
     *
     * The compositor is the process serving this daemon's own Wayland
     * connection, read from the kernel's peer credentials on its socket,
     * which no other process can forge (@ref compositor). @p pid passes when
     * it is that process, or a direct child of it when the compositor
     * trusts its children, which only @c kwin_wayland_wrapper does: Plasma
     * starts the wrapper, which holds the Wayland socket and runs
     * @c kwin_wayland (where the effect lives) as its only child, and starts
     * it again if it crashes. A grandchild never passes. With no compositor
     * nothing passes.
     *
     * Of the sender's own @c /proc entry only the PPid line of
     * @c /proc/<pid>/status is read, strictly parsed. Its executable is not
     * consulted: @c kwin_wayland ships with a file capability
     * (@c cap_sys_nice), which leaves it undumpable and its
     * @c /proc/<pid>/exe unreadable to the daemon, so no check on it can
     * admit the real compositor. Its status stays readable.
     *
     * Single source of truth for the trust-admission step shared by the
     * pre-warm path (@ref resolvePidAndTrust) and the sync fallback
     * (@ref isCompositorPeerOn). Idempotent against duplicate
     * admissions for the same unique name.
     *
     * @return true iff @p uniqueName is now in the trust cache.
     */
    bool trustIfCompositor(const QString& uniqueName, uint pid);

    /**
     * @brief The compositor the thumbnail senders are held to.
     *
     * The process at the other end of this daemon's own Wayland socket, from
     * @c SO_PEERCRED, and whether it trusts its children: only when its
     * executable is @c kwin_wayland_wrapper (the wrapper carries no file
     * capability, so its executable reads). Read once per Wayland
     * connection: before the platform is up there is none and nothing is
     * trusted, and when Qt reconnects to a restarted compositor
     * (@c QT_WAYLAND_RECONNECT, which Plasma sets) the new connection is read
     * again. Under the wrapper that answers the same process, which holds the
     * socket across the restart, and the restarted @c kwin_wayland is again
     * its child. Fails closed: no Wayland connection, or a socket that gives
     * no peer credentials, answers pid 0.
     */
    Compositor compositor();

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

    /// setCompositorResolverForTesting; empty reads the Wayland
    /// connection's peer credentials.
    CompositorResolver m_compositorResolver;

    /// The compositor read from the Wayland connection, and the connection
    /// it was read from (its wl_display and socket fd). @ref compositor reads
    /// it again only when the connection changed: once the platform is up,
    /// and after Qt reconnected to a restarted compositor.
    Compositor m_compositor;
    const void* m_compositorDisplay = nullptr;
    int m_compositorFd = -1;

    /// The refusal warnings go out once each, not per call: a bus client
    /// can call as often as it likes, and the AccessDenied it gets back is
    /// the per-call answer. A compositor found again re-arms the first.
    bool m_noCompositorLogged = false;
    bool m_refusalLogged = false;
};

} // namespace PlasmaZones
