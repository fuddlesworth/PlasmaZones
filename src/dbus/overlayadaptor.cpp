// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "overlayadaptor.h"
#include "dbushelpers.h"
#include "../core/interfaces.h"
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>
#include "../core/constants.h"
#include "../core/logging.h"
#include "../core/screenmanager.h"
#include "../core/utils.h"
#include "../core/virtualscreen.h"
#include <QTimer>

namespace PlasmaZones {

OverlayAdaptor::OverlayAdaptor(IOverlayService* overlay, PhosphorZones::IZoneDetector* detector,
                               PhosphorZones::ILayoutManager* layoutManager, ISettings* settings, QObject* parent)
    : QDBusAbstractAdaptor(parent)
    , m_overlayService(overlay)
    , m_zoneDetector(detector)
    , m_layoutManager(layoutManager)
    , m_settings(settings)
{
    Q_ASSERT(overlay);
    Q_ASSERT(detector);
    Q_ASSERT(layoutManager);
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
    m_overlayService->show();
}

void OverlayAdaptor::hideOverlay()
{
    m_overlayService->hide();
}

bool OverlayAdaptor::isOverlayVisible()
{
    return m_overlayService->isVisible();
}

void OverlayAdaptor::highlightZone(const QString& zoneId)
{
    auto* zone = DbusHelpers::getZoneFromActiveLayout(m_layoutManager, zoneId, QStringLiteral("highlight zone"));
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

    if (!m_layoutManager || !m_layoutManager->activeLayout()) {
        qCWarning(lcDbus) << "highlightZones: no active layout";
        return;
    }

    QVector<PhosphorZones::Zone*> zones;
    for (const auto& id : zoneIds) {
        auto uuidOpt = Utils::parseUuid(id);
        if (uuidOpt) {
            auto* zone = m_layoutManager->activeLayout()->zoneById(*uuidOpt);
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
    m_overlayService->showShaderPreview(x, y, width, height, screenId, shaderId, shaderParamsJson, zonesJson);
}

void OverlayAdaptor::updateShaderPreview(int x, int y, int width, int height, const QString& shaderParamsJson,
                                         const QString& zonesJson)
{
    m_overlayService->updateShaderPreview(x, y, width, height, shaderParamsJson, zonesJson);
}

void OverlayAdaptor::hideShaderPreview()
{
    m_overlayService->hideShaderPreview();
}

bool OverlayAdaptor::showSnapAssist(const QString& screenId, const EmptyZoneList& emptyZones,
                                    const SnapAssistCandidateList& candidates)
{
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
    if (!VirtualScreenId::isVirtual(screenId)) {
        auto* mgr = ScreenManager::instance();
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
    m_overlayService->hideSnapAssist();
}

bool OverlayAdaptor::isSnapAssistVisible()
{
    return m_overlayService->isSnapAssistVisible();
}

void OverlayAdaptor::setSnapAssistThumbnail(const QString& compositorHandle, const QString& dataUrl)
{
    m_overlayService->setSnapAssistThumbnail(compositorHandle, dataUrl);
}

} // namespace PlasmaZones
