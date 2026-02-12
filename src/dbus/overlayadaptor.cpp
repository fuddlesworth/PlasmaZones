// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "overlayadaptor.h"
#include "dbushelpers.h"
#include "../core/interfaces.h"
#include "../core/layout.h"
#include "../core/zone.h"
#include "../core/constants.h"
#include "../core/logging.h"
#include "../core/utils.h"
#include <QTimer>

namespace PlasmaZones {

OverlayAdaptor::OverlayAdaptor(IOverlayService* overlay, IZoneDetector* detector, ILayoutManager* layoutManager,
                               ISettings* settings, QObject* parent)
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
    connect(m_zoneDetector, &IZoneDetector::zoneHighlighted, this, [this](Zone* zone) {
        Q_EMIT zoneHighlightChanged(zone ? zone->id().toString() : QString());
    });

    connect(m_zoneDetector, &IZoneDetector::highlightsCleared, this, [this]() {
        Q_EMIT zoneHighlightChanged(QString());
    });

    connect(m_overlayService, &IOverlayService::snapAssistShown, this, [this](const QString& screenName,
                                                                              const QString& emptyZonesJson,
                                                                              const QString& candidatesJson) {
        Q_EMIT snapAssistShown(screenName, emptyZonesJson, candidatesJson);
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
        qCWarning(lcDbus) << "Cannot highlight zones - empty zone ID list";
        return;
    }

    if (!m_layoutManager || !m_layoutManager->activeLayout()) {
        qCWarning(lcDbus) << "Cannot highlight zones - no active layout";
        return;
    }

    QVector<Zone*> zones;
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

void OverlayAdaptor::showShaderPreview(int x, int y, int width, int height, const QString& screenName,
                                       const QString& shaderId, const QString& shaderParamsJson,
                                       const QString& zonesJson)
{
    m_overlayService->showShaderPreview(x, y, width, height, screenName, shaderId, shaderParamsJson, zonesJson);
}

void OverlayAdaptor::updateShaderPreview(int x, int y, int width, int height,
                                        const QString& shaderParamsJson, const QString& zonesJson)
{
    m_overlayService->updateShaderPreview(x, y, width, height, shaderParamsJson, zonesJson);
}

void OverlayAdaptor::hideShaderPreview()
{
    m_overlayService->hideShaderPreview();
}

bool OverlayAdaptor::showSnapAssist(const QString& screenName, const QString& emptyZonesJson,
                                    const QString& candidatesJson)
{
    // Return false when we know overlay won't be shown (avoids misleading "success")
    if (emptyZonesJson.isEmpty() || emptyZonesJson == QLatin1String("[]")
        || candidatesJson.isEmpty() || candidatesJson == QLatin1String("[]")) {
        return false;
    }
    // Defer actual work so we return immediately — the KWin effect blocks on this D-Bus
    // call; returning quickly prevents compositor freeze during overlay creation.
    // Note: Return value means "request accepted for deferred processing", not "overlay shown".
    QTimer::singleShot(0, m_overlayService, [this, screenName, emptyZonesJson, candidatesJson]() {
        m_overlayService->showSnapAssist(screenName, emptyZonesJson, candidatesJson);
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

void OverlayAdaptor::setSnapAssistThumbnail(const QString& kwinHandle, const QString& dataUrl)
{
    m_overlayService->setSnapAssistThumbnail(kwinHandle, dataUrl);
}

} // namespace PlasmaZones
