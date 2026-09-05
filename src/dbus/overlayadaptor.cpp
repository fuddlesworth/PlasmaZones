// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "overlayadaptor.h"
#include "dbushelpers.h"
#include "kwinsendertrust.h"
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
#include <QDBusMessage>
#include <QTimer>
#include <QUuid>

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
    connect(m_overlayService, &IOverlayService::snapAssistThumbnailCacheTrimmed, this,
            &OverlayAdaptor::snapAssistThumbnailCacheTrimmed);

    m_kwinTrust = new KwinSenderTrust(this);
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

bool OverlayAdaptor::authenticateKwinSender()
{
    // Resolve the sender's bus name via QDBusContext. Direct (non-D-Bus)
    // calls, such as unit tests invoking the slot via QMetaObject, produce an
    // empty service string; the trust object accepts those because there is
    // no remote peer to authorise.
    if (!calledFromDBus()) {
        return true;
    }
    return m_kwinTrust->isTrustedSender(message().service(), connection());
}

} // namespace PlasmaZones
