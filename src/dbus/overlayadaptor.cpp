// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "overlayadaptor.h"
#include "dbushelpers.h"
#include "../core/interfaces.h"
#include <PhosphorZones/IZoneLayoutRegistry.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>
#include "../core/constants.h"
#include "../core/logging.h"
#include <PhosphorScreens/Manager.h>
#include "../core/utils.h"
#include <PhosphorScreens/VirtualScreen.h>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusServiceWatcher>
#include <QFile>
#include <QTimer>

namespace PlasmaZones {

OverlayAdaptor::OverlayAdaptor(IOverlayService* overlay, PhosphorZones::IZoneDetector* detector,
                               PhosphorZones::IZoneLayoutRegistry* layoutRegistry,
                               Phosphor::Screens::ScreenManager* screenManager, ISettings* settings, QObject* parent)
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

    connect(
        m_overlayService, &IOverlayService::snapAssistShown, this,
        [this](const QString& screenId, const EmptyZoneList& emptyZones, const SnapAssistCandidateList& candidates) {
            Q_EMIT snapAssistShown(screenId, emptyZones, candidates);
        });
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

bool OverlayAdaptor::showSnapAssist(const QString& screenId, const EmptyZoneList& emptyZones,
                                    const SnapAssistCandidateList& candidates)
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
            const EmptyZoneEntry& first = emptyZones.first();
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

void OverlayAdaptor::setSnapAssistThumbnail(const QString& compositorHandle, const QString& dataUrl)
{
    if (!m_overlayService) {
        return;
    }
    if (!authenticateKwinSender()) {
        return;
    }
    m_overlayService->setSnapAssistThumbnail(compositorHandle, dataUrl);
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

    QDBusConnection bus = connection();
    auto* iface = bus.interface();
    if (!iface) {
        qCWarning(lcDbus) << "setSnapAssistThumbnail: no D-Bus interface for sender PID lookup; rejecting" << sender;
        return false;
    }

    QDBusReply<uint> pidReply = iface->servicePid(sender);
    if (!pidReply.isValid() || pidReply.value() == 0) {
        qCWarning(lcDbus) << "setSnapAssistThumbnail: GetConnectionUnixProcessID failed for" << sender << "—"
                          << pidReply.error().message();
        return false;
    }

    const uint pid = pidReply.value();
    // /proc/<pid>/comm is truncated to TASK_COMM_LEN (16) bytes by the kernel,
    // which fits the kwin process names we accept. Reading the file is one
    // syscall and the kernel guarantees per-PID liveness while we hold a
    // reference via the bus connection's keepalive (the lookup above
    // refcounted the credential).
    QFile commFile(QStringLiteral("/proc/%1/comm").arg(pid));
    if (!commFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCWarning(lcDbus) << "setSnapAssistThumbnail: cannot read /proc/" << pid << "/comm — rejecting" << sender;
        return false;
    }
    const QByteArray commLine = commFile.readLine().trimmed();
    commFile.close();

    // Project is Wayland-only (CLAUDE.md), but accept the X11 binary too —
    // the effect plugin runs inside whichever kwin variant the user is on,
    // and a bare-metal X11 fallback still produces correctly-built thumbnails.
    static const QByteArrayList kAcceptedComms = {
        QByteArrayLiteral("kwin_wayland"),
        QByteArrayLiteral("kwin_x11"),
    };
    if (!kAcceptedComms.contains(commLine)) {
        qCWarning(lcDbus) << "setSnapAssistThumbnail: rejecting non-kwin sender" << sender << "pid=" << pid
                          << "comm=" << commLine;
        return false;
    }

    // Cache the trusted bus name and arm a watcher that drops it from the
    // trust set the moment the bus name's owner disappears. Without this,
    // a kwin restart followed by a (rapid) PID reuse on a new short-lived
    // process binding the same unique-name could inherit trust.
    m_trustedKwinSenders.insert(sender);
    auto* watcher = new QDBusServiceWatcher(sender, bus, QDBusServiceWatcher::WatchForUnregistration, this);
    QObject::connect(watcher, &QDBusServiceWatcher::serviceUnregistered, this, [this, watcher](const QString& service) {
        m_trustedKwinSenders.remove(service);
        watcher->deleteLater();
    });
    return true;
}

} // namespace PlasmaZones
