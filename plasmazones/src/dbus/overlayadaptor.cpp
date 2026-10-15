// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "overlayadaptor.h"
#include "dbushelpers.h"
#include "core/interfaces/interfaces.h"
#include "core/types/dmabufthumbnail.h"
#include "core/interfaces/ioverlayservice.h"
#include <PhosphorZones/IZoneLayoutRegistry.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>
#include "core/types/constants.h"
#include "core/platform/logging.h"
#include <PhosphorScreens/Manager.h>
#include "core/utils/utils.h"
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorScreens/VirtualScreen.h>
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QTimer>
#include <QUuid>

#include <algorithm>

#include <sys/socket.h>
#include <wayland-client-core.h>

namespace PlasmaZones {

namespace {

/// This process's own Wayland connection, or null off Wayland (the
/// offscreen platform, or before the platform is up).
wl_display* ownWaylandConnection()
{
    auto* const app = qobject_cast<QGuiApplication*>(QCoreApplication::instance());
    auto* const wayland = app ? app->nativeInterface<QNativeInterface::QWaylandApplication>() : nullptr;
    return wayland ? wayland->display() : nullptr;
}

/// The pid at the other end of the socket @p fd, from the kernel's peer
/// credentials, or 0 when it gives none.
uint peerPid(int fd)
{
    ucred credentials{};
    socklen_t size = sizeof(credentials);
    if (fd < 0 || getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credentials, &size) != 0 || size != sizeof(credentials)
        || credentials.pid <= 0) {
        return 0;
    }
    return static_cast<uint>(credentials.pid);
}

/// The parent of @p pid, from the PPid line of /proc/<pid>/status, which the
/// kernel shows for an undumpable process too, or 0. Nothing else of the
/// process is read. The line is the kernel's "PPid:" and decimal digits, and
/// anything else in it answers 0.
uint parentPid(uint pid)
{
    QFile status(QStringLiteral("/proc/%1/status").arg(pid));
    if (!status.open(QIODevice::ReadOnly)) {
        return 0;
    }
    // Read up to a bound rather than until atEnd(): a /proc file stats as a
    // regular file of size 0, so QFile reports it at its end before the first
    // read. PPid is the seventh line, well inside the bound.
    constexpr qint64 MaxBytes = 4096;
    const QByteArray contents = status.read(MaxBytes);
    for (const QByteArray& line : contents.split('\n')) {
        if (!line.startsWith("PPid:")) {
            continue;
        }
        const QByteArray field = line.mid(5).trimmed();
        const bool digits = !field.isEmpty() && std::all_of(field.cbegin(), field.cend(), [](char c) {
            return c >= '0' && c <= '9';
        });
        bool ok = false;
        const uint parent = digits ? field.toUInt(&ok) : 0;
        return ok ? parent : 0;
    }
    return 0;
}

/// The file name of the executable @p pid runs, empty when its
/// /proc/<pid>/exe does not read (an undumpable process).
QString executableName(uint pid)
{
    return QFileInfo(QFile::symLinkTarget(QStringLiteral("/proc/%1/exe").arg(pid))).fileName();
}

} // namespace

OverlayAdaptor::OverlayAdaptor(IOverlayService* overlay, PhosphorZones::IZoneDetector* detector,
                               PhosphorZones::IZoneLayoutRegistry* layoutRegistry,
                               PhosphorScreens::ScreenManager* screenManager, ISettings* settings, QObject* parent)
    : QDBusAbstractAdaptor(parent)
    , m_overlayService(overlay)
    , m_zoneDetector(detector)
    , m_layoutRegistry(layoutRegistry)
    , m_screenManager(screenManager)
    , m_settings(settings)
{
    Q_ASSERT(overlay);
    Q_ASSERT(detector);
    Q_ASSERT(layoutRegistry);
    Q_ASSERT(settings);

    connect(m_overlayService, &IOverlayService::visibilityChanged, this, &OverlayAdaptor::overlayVisibilityChanged);

    // Connect to interface signals (DIP)
    connect(m_zoneDetector, &PhosphorZones::IZoneDetector::zoneHighlighted, this, [this](PhosphorZones::Zone* zone) {
        Q_EMIT zoneHighlightChanged(zone ? zone->id().toString() : QString());
    });

    connect(m_zoneDetector, &PhosphorZones::IZoneDetector::highlightsCleared, this, [this]() {
        Q_EMIT zoneHighlightChanged(QString());
    });

    connect(m_overlayService, &IOverlayService::snapAssistShown, this,
            [this](const QString& screenId, const PhosphorProtocol::EmptyZoneList& emptyZones,
                   const PhosphorProtocol::SnapAssistCandidateList& candidates) {
                Q_EMIT snapAssistShown(screenId, emptyZones, candidates);
            });

    // Mirror snapAssistShown's path on the way out so external observers
    // (KCMs, debugging tools, the kwin-effect's thumbnail-injection
    // bookkeeping) can track visible/hidden symmetrically without
    // polling isSnapAssistVisible. The internal IOverlayService signal
    // collapses every dismiss reason (pick, Escape, backdrop, screen-
    // change cancel, explicit D-Bus hideSnapAssist) through one emit
    // site, so a parameterless forward is the right shape here.
    connect(m_overlayService, &IOverlayService::snapAssistDismissed, this, &OverlayAdaptor::snapAssistDismissed);
    connect(m_overlayService, &IOverlayService::snapAssistThumbnailCacheTrimmed, this,
            &OverlayAdaptor::snapAssistThumbnailCacheTrimmed);

    // Pre-warm the kwin trust cache so the first @c setSnapAssistThumbnail
    // of a session is a one-set-lookup hit instead of a sync
    // GetConnectionUnixProcessID round-trip from inside the D-Bus method
    // handler. Watcher armed for both directions so a kwin restart re-fires
    // the pre-warm; the existing per-sender unregistration watchers
    // installed by @ref trustIfCompositor handle trust eviction on the
    // way out.
    m_kwinWatcher = new QDBusServiceWatcher(QStringLiteral("org.kde.KWin"), QDBusConnection::sessionBus(),
                                            QDBusServiceWatcher::WatchForRegistration, this);
    QObject::connect(m_kwinWatcher, &QDBusServiceWatcher::serviceRegistered, this, [this](const QString&) {
        prewarmKwinTrust();
    });
    // Initial fire — covers the steady-state case where kwin came up
    // before plasmazones (the universal case for compositor + session
    // services). If kwin isn't running yet, GetNameOwner returns an
    // error, the pre-warm bails, and the WatchForRegistration callback
    // above will retry when kwin lands.
    prewarmKwinTrust();
}

void OverlayAdaptor::showOverlay()
{
    if (!m_overlayService) {
        qCWarning(lcDbus) << "showOverlay: overlay service not wired";
        return;
    }
    m_overlayService->show();
}

void OverlayAdaptor::hideOverlay()
{
    if (!m_overlayService) {
        qCWarning(lcDbus) << "hideOverlay: overlay service not wired";
        return;
    }
    m_overlayService->hide();
}

bool OverlayAdaptor::isOverlayVisible()
{
    return m_overlayService ? m_overlayService->isVisible() : false;
}

void OverlayAdaptor::highlightZone(const QString& zoneId)
{
    if (!m_overlayService || !m_zoneDetector) {
        qCWarning(lcDbus) << "highlightZone: overlay service or zone detector not wired";
        return;
    }
    auto* zone = DbusHelpers::getZoneFromActiveLayout(m_layoutRegistry, zoneId, QStringLiteral("highlight zone"));
    if (!zone) {
        return;
    }

    m_zoneDetector->highlightZone(zone);
    m_overlayService->updateGeometries();
}

void OverlayAdaptor::highlightZones(const QStringList& zoneIds)
{
    if (zoneIds.isEmpty()) {
        qCWarning(lcDbus) << "highlightZones: empty zone ID list";
        return;
    }

    if (!m_layoutRegistry || !m_layoutRegistry->activeLayout()) {
        qCWarning(lcDbus) << "highlightZones: no active layout";
        return;
    }

    if (!m_overlayService || !m_zoneDetector) {
        qCWarning(lcDbus) << "highlightZones: overlay service or zone detector not wired";
        return;
    }

    QVector<PhosphorZones::Zone*> zones;
    for (const auto& id : zoneIds) {
        auto uuidOpt = Utils::parseUuid(id);
        if (uuidOpt) {
            auto* zone = m_layoutRegistry->activeLayout()->zoneById(*uuidOpt);
            if (zone) {
                zones.append(zone);
            }
        }
    }

    if (!zones.isEmpty()) {
        m_zoneDetector->highlightZones(zones);
        m_overlayService->updateGeometries();
    }
}

void OverlayAdaptor::clearHighlight()
{
    if (!m_zoneDetector) {
        return;
    }
    m_zoneDetector->clearHighlights();
}

// Window tracking and zone detection methods moved to separate adaptors
// See WindowTrackingAdaptor and ZoneDetectionAdaptor

int OverlayAdaptor::getPollIntervalMs()
{
    return m_settings ? m_settings->pollIntervalMs() : Defaults::PollIntervalMs;
}

int OverlayAdaptor::getMinimumZoneSizePx()
{
    return m_settings ? m_settings->minimumZoneSizePx() : Defaults::MinimumZoneSizePx;
}

int OverlayAdaptor::getMinimumZoneDisplaySizePx()
{
    return m_settings ? m_settings->minimumZoneDisplaySizePx() : Defaults::MinimumZoneDisplaySizePx;
}

bool OverlayAdaptor::showSnapAssist(const QString& screenId, const PhosphorProtocol::EmptyZoneList& emptyZones,
                                    const PhosphorProtocol::SnapAssistCandidateList& candidates)
{
    if (!m_overlayService) {
        qCWarning(lcDbus) << "showSnapAssist: overlay service not wired";
        return false;
    }
    // Respect master toggle — don't show snap assist when the feature is disabled
    if (m_settings && !m_settings->snapAssistFeatureEnabled()) {
        return false;
    }
    // Return false when we know overlay won't be shown (avoids misleading "success")
    if (emptyZones.isEmpty() || candidates.isEmpty()) {
        return false;
    }
    // When the effect sends a physical screen ID for a subdivided monitor,
    // resolve to the correct virtual screen so snap assist appears on the
    // right side.  Use the first empty zone's center to determine which
    // virtual screen to target.
    QString resolvedScreen = screenId;
    if (!PhosphorIdentity::VirtualScreenId::isVirtual(screenId)) {
        auto* mgr = m_screenManager;
        if (mgr && mgr->hasVirtualScreens(screenId) && !emptyZones.isEmpty()) {
            const PhosphorProtocol::EmptyZoneEntry& first = emptyZones.first();
            QPoint center(first.x + first.width / 2, first.y + first.height / 2);
            QString vsId = mgr->effectiveScreenAt(center);
            if (!vsId.isEmpty()) {
                resolvedScreen = vsId;
            }
        }
    }

    // Defer actual work so we return immediately — the KWin effect blocks on this D-Bus
    // call; returning quickly prevents compositor freeze during overlay creation.
    // Note: Return value means "request accepted for deferred processing", not "overlay shown".
    QTimer::singleShot(0, m_overlayService, [this, resolvedScreen, emptyZones, candidates]() {
        m_overlayService->showSnapAssist(resolvedScreen, emptyZones, candidates);
    });
    return true;
}

void OverlayAdaptor::hideSnapAssist()
{
    if (!m_overlayService) {
        return;
    }
    m_overlayService->hideSnapAssist();
}

bool OverlayAdaptor::isSnapAssistVisible()
{
    return m_overlayService ? m_overlayService->isSnapAssistVisible() : false;
}

bool OverlayAdaptor::setSnapAssistThumbnail(const QString& compositorHandle, int width, int height,
                                            const QByteArray& pixels)
{
    if (!m_overlayService) {
        return false;
    }
    // Hard byte cap matching a 1024² ARGB32 image (4 MiB) plus framing
    // overhead, generous for the 256² steady state (256 KiB). Note: by the
    // time this slot runs, QtDBus has already deserialised the QByteArray —
    // the marshaller cost is paid regardless. What this guard actually
    // bounds is (a) downstream validation cost in OverlayService against
    // an authenticated kwin-effect bug producing a too-large buffer, and
    // (b) the daemon's QImage.copy() walk if the buffer slipped past auth.
    // A finer-grained dimension/byte-count match runs in
    // OverlayService::setSnapAssistThumbnail.
    static constexpr int MaxPixelBytes = 4 * 1024 * 1024 + 64;
    if (pixels.size() > MaxPixelBytes) {
        // Don't log @c compositorHandle — this branch fires before
        // @ref authenticateKwinSender, so the field is attacker-controlled
        // and a hostile peer could steer arbitrary content (long strings,
        // ANSI sequences) into the warning channel via a 5 MB blob. Length
        // is enough to distinguish "empty handle" from "well-formed-looking
        // handle" without quoting the bytes.
        qCWarning(lcDbus) << "setSnapAssistThumbnail: rejecting oversize payload" << pixels.size()
                          << "bytes (handle len=" << compositorHandle.size() << ")";
        return false;
    }
    if (!authenticateKwinSender()) {
        return false;
    }
    // Handle must be a QUuid spelling (the producer always sends
    // EffectWindow::internalId().toString()). The providers key their caches
    // on the handle AND parse it back out of an `image://.../<handle>/<gen>`
    // URL with a first-'/' split, so a handle carrying URL-reserved
    // characters would be storable but never resolvable — reject it at the
    // boundary per the input-validation rule.
    if (QUuid::fromString(compositorHandle).isNull()) {
        qCWarning(lcDbus) << "setSnapAssistThumbnail: rejecting non-UUID compositor handle (len="
                          << compositorHandle.size() << ")";
        return false;
    }
    // Forward the service's accepted/rejected bool verbatim so the kwin-
    // effect's recently-posted dedup window only marks handles the daemon
    // actually stored. Treating any silent rejection as success would
    // strand snap-assist on icons until the dedup FIFO rolls past.
    return m_overlayService->setSnapAssistThumbnail(compositorHandle, width, height, pixels);
}

bool OverlayAdaptor::setWindowThumbnailDmabuf(const QString& compositorHandle, int width, int height, uint drmFormat,
                                              qulonglong modifier, uint stride, uint offset,
                                              const QDBusUnixFileDescriptor& fd, const QDBusUnixFileDescriptor& fenceFd)
{
    if (!m_overlayService) {
        return false;
    }
    // Authenticate the sender BEFORE inspecting the payload, so an
    // unauthenticated peer can't drive the validation logic. (The raw-pixel
    // sibling deliberately bounds its payload size pre-auth to cap marshalling
    // cost of a multi-MB blob; a dma-buf carries no inline payload, so there is
    // no reason to validate before authenticating here.)
    if (!authenticateKwinSender()) {
        return false;
    }
    // No marshalling-size guard is needed here (unlike the raw-pixel path):
    // a dma-buf is a kernel handle, not an inline byte array, so there is no
    // large payload to deserialise. Bound the dimensions before import to
    // reject a hostile/buggy authenticated sender — the ceiling is the
    // shared protocol constant, so the effect's capture clamp, this check,
    // and the service-boundary re-validation can never drift apart.
    static constexpr int MaxDimension = PhosphorProtocol::Service::SnapAssistThumbnailMaxDimension;
    if (width <= 0 || height <= 0 || width > MaxDimension || height > MaxDimension) {
        qCWarning(lcDbus) << "setWindowThumbnailDmabuf: rejecting out-of-range dimensions" << width << "x" << height
                          << "(handle len=" << compositorHandle.size() << ")";
        return false;
    }
    // Same UUID-spelling requirement as the raw-pixel sibling — see the
    // comment there for why URL-reserved characters must never reach the
    // provider key space.
    if (QUuid::fromString(compositorHandle).isNull()) {
        qCWarning(lcDbus) << "setWindowThumbnailDmabuf: rejecting non-UUID compositor handle (len="
                          << compositorHandle.size() << ")";
        return false;
    }
    if (!fd.isValid() || !fenceFd.isValid()) {
        qCWarning(lcDbus) << "setWindowThumbnailDmabuf: invalid dma-buf or fence fd (handle len="
                          << compositorHandle.size() << ")";
        return false;
    }
    // fd / fenceFd are BORROWED: the QDBusUnixFileDescriptors own them and close
    // them when this call returns. The service's importer dup()s whatever it
    // needs past the call (the GPU import dups the dma-buf; the fence is waited
    // on during import); we hand the borrowed fds through unchanged.
    DmabufThumbnailDesc desc;
    desc.fd = fd.fileDescriptor();
    desc.fenceFd = fenceFd.fileDescriptor();
    desc.width = width;
    desc.height = height;
    desc.fourcc = drmFormat;
    desc.modifier = modifier;
    desc.stride = stride;
    desc.offset = offset;
    return m_overlayService->setWindowThumbnailDmabuf(compositorHandle, desc);
}

void OverlayAdaptor::setCompositorResolverForTesting(CompositorResolver resolver)
{
    m_compositorResolver = std::move(resolver);
}

OverlayAdaptor::Compositor OverlayAdaptor::compositor()
{
    if (m_compositorResolver) {
        return m_compositorResolver();
    }
    wl_display* const display = ownWaylandConnection();
    const int fd = display ? wl_display_get_fd(display) : -1;
    if (display != m_compositorDisplay || fd != m_compositorFd) {
        m_compositorDisplay = display;
        m_compositorFd = fd;
        m_compositor.pid = peerPid(fd);
        m_compositor.trustsChildren =
            m_compositor.pid != 0 && executableName(m_compositor.pid) == QLatin1String("kwin_wayland_wrapper");
        qCDebug(lcDbus) << "compositor: pid" << m_compositor.pid << "trusts its children"
                        << m_compositor.trustsChildren;
    }
    return m_compositor;
}

bool OverlayAdaptor::authenticateKwinSender()
{
    // The call's context is on the object this adaptor is exported through,
    // never on the adaptor itself (DbusHelpers::busContext). A direct
    // (non-D-Bus) call, as the unit tests make through QMetaObject, has no
    // remote peer to authorise.
    const QDBusContext* const context = DbusHelpers::busContext(this);
    if (!context) {
        return true;
    }
    const QString sender = context->message().service();
    const auto refuse = [this, &sender] {
        DbusHelpers::sendBusError(this, QDBusError::AccessDenied,
                                  QStringLiteral("only the compositor may push snap assist thumbnails"));
        qCDebug(lcDbus) << "authenticateKwinSender: refused" << sender;
        return false;
    };
    // Every message the bus daemon routes names its sender, so an empty one
    // has no identity to check.
    if (sender.isEmpty()) {
        return refuse();
    }

    if (m_trustedKwinSenders.contains(sender)) {
        return true;
    }

    // Slow-path fallback. The pre-warm chain (@ref prewarmKwinTrust →
    // @ref resolvePidAndTrust → @ref trustIfCompositor) primes the
    // cache before any thumbnail call lands in steady state, so this
    // sync block runs only when a thumbnail call races a fresh pre-warm
    // (e.g. the very first call after a kwin restart). Acceptable cost:
    // one ~1ms GetConnectionUnixProcessID round-trip; subsequent calls
    // hit the cache.
    //
    // Bounded with the shared @c SyncCallTimeoutMs (500 ms) — the
    // dbus-daemon's @c GetConnectionUnixProcessID is a hash lookup, so
    // 500 ms is "definitely something is wrong" rather than a meaningful
    // expected latency. Qt's default 25 s timeout would freeze the
    // daemon's main thread (and every overlay it drives) under
    // dbus-daemon stress; capping here keeps degradation graceful.
    QDBusConnection bus = context->connection();
    QDBusMessage pidMsg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
        QStringLiteral("org.freedesktop.DBus"), QStringLiteral("GetConnectionUnixProcessID"));
    pidMsg << sender;
    const QDBusMessage pidReplyMsg = bus.call(pidMsg, QDBus::Block, PhosphorProtocol::Service::SyncCallTimeoutMs);
    if (pidReplyMsg.type() != QDBusMessage::ReplyMessage || pidReplyMsg.arguments().isEmpty()) {
        // Most commonly hit when a thumbnail call beats the pre-warm reply
        // and KWin disconnects mid-flight (PID has gone, GetConnectionUnixProcessID
        // returns NameHasNoOwner). Benign and self-healing — kwin's next
        // registration re-fires the pre-warm. Demoted from qCWarning to keep
        // routine session churn out of the warning channel; actual auth
        // rejections still log at warning level inside @ref trustIfCompositor.
        qCDebug(lcDbus) << "authenticateKwinSender: GetConnectionUnixProcessID failed for" << sender << "—"
                        << pidReplyMsg.errorMessage();
        return refuse();
    }
    const uint pid = pidReplyMsg.arguments().constFirst().toUInt();
    if (pid == 0 || !trustIfCompositor(sender, pid)) {
        return refuse();
    }
    return true;
}

bool OverlayAdaptor::trustIfCompositor(const QString& uniqueName, uint pid)
{
    if (m_trustedKwinSenders.contains(uniqueName)) {
        return true;
    }

    // The compositor is whatever serves this daemon's own Wayland socket,
    // named by the kernel's peer credentials, so no other process can claim
    // it. The sender's executable is never read: kwin_wayland carries a
    // file capability, which makes it undumpable, and its /proc/<pid>/exe
    // is unreadable to the daemon, so a check on the sender's executable
    // refuses the real compositor.
    const Compositor trusted = compositor();
    if (trusted.pid == 0) {
        // Fail closed, and say so once rather than once per call.
        if (!m_noCompositorLogged) {
            m_noCompositorLogged = true;
            qCWarning(lcDbus) << "trustIfCompositor: no Wayland compositor to authenticate against, so no thumbnail "
                                 "sender is trusted";
        }
        qCDebug(lcDbus) << "trustIfCompositor: no compositor - rejecting" << uniqueName << "pid=" << pid;
        return false;
    }
    m_noCompositorLogged = false;
    // Plasma's kwin_wayland_wrapper holds the socket and runs kwin_wayland,
    // the process the effect lives in, as its only child. A compositor that
    // is not the wrapper lends its children nothing: kwin itself starts
    // Xwayland and the input method. A grandchild never passes.
    const bool isCompositor = pid == trusted.pid;
    const uint parent = isCompositor ? 0 : parentPid(pid);
    if (!isCompositor && !(trusted.trustsChildren && parent != 0 && parent == trusted.pid)) {
        if (!m_refusalLogged) {
            m_refusalLogged = true;
            qCWarning(lcDbus) << "trustIfCompositor: rejecting" << uniqueName << "pid=" << pid << "parent=" << parent
                              << "- the compositor is pid" << trusted.pid << "(further refusals log at debug)";
        } else {
            qCDebug(lcDbus) << "trustIfCompositor: rejecting" << uniqueName << "pid=" << pid << "parent=" << parent;
        }
        return false;
    }

    // Cache the trusted bus name and arm a watcher that drops it from the
    // trust set the moment the bus name's owner disappears. Without this,
    // a kwin restart followed by a (rapid) PID reuse on a new short-lived
    // process binding the same unique-name could inherit trust. The kwin
    // well-known-name watcher armed in the constructor handles the
    // re-prewarm side; this per-unique-name watcher handles trust eviction.
    m_trustedKwinSenders.insert(uniqueName);
    auto* watcher = new QDBusServiceWatcher(uniqueName, QDBusConnection::sessionBus(),
                                            QDBusServiceWatcher::WatchForUnregistration, this);
    QObject::connect(watcher, &QDBusServiceWatcher::serviceUnregistered, this, [this, watcher](const QString& service) {
        m_trustedKwinSenders.remove(service);
        watcher->deleteLater();
    });
    qCDebug(lcDbus) << "trustIfCompositor: admitted" << uniqueName << "pid=" << pid << "compositor=" << trusted.pid;
    return true;
}

void OverlayAdaptor::prewarmKwinTrust()
{
    // Async leg 1: resolve org.kde.KWin → unique bus name. KWin is the
    // compositor and the universal pattern is for it to register before
    // plasmazones starts; pre-warm at construction is a cache hit in
    // steady state, and a no-op (logged at debug) when kwin isn't up yet
    // — m_kwinWatcher's WatchForRegistration callback retries on
    // registration.
    QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusMessage msg =
        QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
                                       QStringLiteral("org.freedesktop.DBus"), QStringLiteral("GetNameOwner"));
    msg << QStringLiteral("org.kde.KWin");
    auto* watcher = new QDBusPendingCallWatcher(bus.asyncCall(msg), this);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        QDBusPendingReply<QString> reply = *w;
        if (reply.isError()) {
            qCDebug(lcDbus) << "prewarmKwinTrust: GetNameOwner(org.kde.KWin) failed —" << reply.error().message()
                            << "(retrying on next NameOwnerChanged)";
            return;
        }
        const QString uniqueName = reply.value();
        if (uniqueName.isEmpty()) {
            return;
        }
        resolvePidAndTrust(uniqueName);
    });
}

void OverlayAdaptor::resolvePidAndTrust(const QString& uniqueName)
{
    if (m_trustedKwinSenders.contains(uniqueName)) {
        return;
    }
    // Async leg 2: resolve unique name → PID. Both legs use asyncCall so
    // the daemon's main thread never blocks on the dbus-daemon for the
    // pre-warm path — the sync fallback in @ref authenticateKwinSender
    // remains for the race window where a thumbnail beats the pre-warm.
    QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
        QStringLiteral("org.freedesktop.DBus"), QStringLiteral("GetConnectionUnixProcessID"));
    msg << uniqueName;
    auto* watcher = new QDBusPendingCallWatcher(bus.asyncCall(msg), this);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, uniqueName](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        QDBusPendingReply<uint> reply = *w;
        if (reply.isError()) {
            qCDebug(lcDbus) << "prewarmKwinTrust: GetConnectionUnixProcessID failed for" << uniqueName << ":"
                            << reply.error().message();
            return;
        }
        const uint pid = reply.value();
        if (pid == 0) {
            return;
        }
        trustIfCompositor(uniqueName, pid);
    });
}

} // namespace PlasmaZones
