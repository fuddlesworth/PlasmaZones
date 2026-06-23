// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "overlayadaptor.h"
#include "dbushelpers.h"
#include "../core/interfaces.h"
#include "../core/dmabufthumbnail.h"
#include "../core/ioverlayservice.h"
#include <PhosphorZones/IZoneLayoutRegistry.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>
#include "../core/constants.h"
#include "../core/logging.h"
#include <PhosphorScreens/Manager.h>
#include "../core/utils.h"
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorScreens/VirtualScreen.h>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QFile>
#include <QFileInfo>
#include <QLatin1StringView>
#include <QTimer>

#include <algorithm>
#include <array>

namespace PlasmaZones {

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

    // Pre-warm the kwin trust cache so the first @c setSnapAssistThumbnail
    // of a session is a one-set-lookup hit instead of a sync
    // GetConnectionUnixProcessID round-trip from inside the D-Bus method
    // handler. Watcher armed for both directions so a kwin restart re-fires
    // the pre-warm; the existing per-sender unregistration watchers
    // installed by @ref validateExeAndTrust handle trust eviction on the
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

void OverlayAdaptor::showShaderPreview(int x, int y, int width, int height, const QString& screenId,
                                       const QString& shaderId, const QString& shaderParamsJson,
                                       const QString& zonesJson)
{
    if (!m_overlayService) {
        qCWarning(lcDbus) << "showShaderPreview: overlay service not wired";
        return;
    }
    m_overlayService->showShaderPreview(x, y, width, height, screenId, shaderId, shaderParamsJson, zonesJson);
}

void OverlayAdaptor::updateShaderPreview(int x, int y, int width, int height, const QString& shaderParamsJson,
                                         const QString& zonesJson)
{
    if (!m_overlayService) {
        return;
    }
    m_overlayService->updateShaderPreview(x, y, width, height, shaderParamsJson, zonesJson);
}

void OverlayAdaptor::hideShaderPreview()
{
    if (!m_overlayService) {
        return;
    }
    m_overlayService->hideShaderPreview();
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
    // reject a hostile/buggy authenticated sender, mirroring the raw-pixel
    // path's 1024² ceiling.
    static constexpr int MaxDimension = 1024;
    if (width <= 0 || height <= 0 || width > MaxDimension || height > MaxDimension) {
        qCWarning(lcDbus) << "setWindowThumbnailDmabuf: rejecting out-of-range dimensions" << width << "x" << height
                          << "(handle len=" << compositorHandle.size() << ")";
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

bool OverlayAdaptor::authenticateKwinSender()
{
    // Resolve the sender's bus name via QDBusContext. Direct (non-D-Bus) calls
    // — e.g. unit tests that invoke the slot via QMetaObject — produce an
    // empty service string; we accept those because there is no remote peer
    // to authorise.
    if (!calledFromDBus()) {
        return true;
    }
    const QString sender = message().service();
    if (sender.isEmpty()) {
        return true;
    }

    if (m_trustedKwinSenders.contains(sender)) {
        return true;
    }

    // Slow-path fallback. The pre-warm chain (@ref prewarmKwinTrust →
    // @ref resolvePidAndTrust → @ref validateExeAndTrust) primes the
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
    QDBusConnection bus = connection();
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
        // rejections still log at warning level inside @ref validateExeAndTrust.
        qCDebug(lcDbus) << "authenticateKwinSender: GetConnectionUnixProcessID failed for" << sender << "—"
                        << pidReplyMsg.errorMessage();
        return false;
    }
    const uint pid = pidReplyMsg.arguments().constFirst().toUInt();
    if (pid == 0) {
        return false;
    }

    return validateExeAndTrust(sender, pid);
}

bool OverlayAdaptor::validateExeAndTrust(const QString& uniqueName, uint pid)
{
    if (m_trustedKwinSenders.contains(uniqueName)) {
        return true;
    }

    // /proc/<pid>/exe is a kernel-maintained symlink to the actual binary
    // path; unlike /proc/<pid>/comm it cannot be rewritten from userspace
    // (no prctl(PR_SET_NAME) equivalent for the exe link). Compare the
    // basename against the accepted set — full path differs by distro
    // (/usr/bin vs /usr/lib/qt6/bin etc.) so basename matching is the
    // portable form.
    //
    // Project is Wayland-only (CLAUDE.md), but accept the X11 binary too —
    // the effect plugin runs inside whichever kwin variant the user is on,
    // and a bare-metal X11 fallback still produces correctly-built
    // thumbnails. `kwin_wayland_wrapper` is the launcher shim some distros
    // ship (Arch / Fedora's session integration); without it auth fails
    // silently on packaged installs and snap-assist falls back to icons
    // for the daemon's whole session.
    //
    // QLatin1StringView array (rather than function-local static QStringList)
    // so the basenames are zero-allocation constants resolved at compile
    // time. The earlier QStringList form paid one-time heap allocations on
    // first call via static initialisation.
    static constexpr std::array<QLatin1StringView, 3> AcceptedExeBasenames = {
        QLatin1StringView("kwin_wayland"),
        QLatin1StringView("kwin_wayland_wrapper"),
        QLatin1StringView("kwin_x11"),
    };
    const QString exePath = QFile::symLinkTarget(QStringLiteral("/proc/%1/exe").arg(pid));
    if (exePath.isEmpty()) {
        qCWarning(lcDbus) << "validateExeAndTrust: cannot resolve /proc/" << pid << "/exe — rejecting" << uniqueName;
        return false;
    }
    const QString exeBasename = QFileInfo(exePath).fileName();
    const bool accepted =
        std::any_of(AcceptedExeBasenames.begin(), AcceptedExeBasenames.end(), [&exeBasename](QLatin1StringView v) {
            return exeBasename == v;
        });
    if (!accepted) {
        qCWarning(lcDbus) << "validateExeAndTrust: rejecting non-kwin sender" << uniqueName << "pid=" << pid
                          << "exe=" << exePath;
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
    qCDebug(lcDbus) << "validateExeAndTrust: admitted" << uniqueName << "pid=" << pid << "exe=" << exePath;
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
        validateExeAndTrust(uniqueName, pid);
    });
}

} // namespace PlasmaZones
