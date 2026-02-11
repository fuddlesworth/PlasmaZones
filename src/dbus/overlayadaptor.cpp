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
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

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

void OverlayAdaptor::switchToLayout(const QString& layoutId)
{
    auto uuidOpt = DbusHelpers::parseAndValidateUuid(layoutId, QStringLiteral("switch layout"));
    if (!uuidOpt) {
        return;
    }

    if (!m_layoutManager) {
        qCWarning(lcDbus) << "Cannot switch layout - no layout manager";
        return;
    }

    auto* layout = m_layoutManager->layoutById(*uuidOpt);
    if (!layout) {
        qCWarning(lcDbus) << "Layout not found for switching:" << layoutId;
        return;
    }

    m_layoutManager->setActiveLayout(layout);
    m_overlayService->updateLayout(layout);
    Q_EMIT layoutSwitched(layoutId);
    qCInfo(lcDbus) << "Switched to layout:" << layout->name();
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

} // namespace PlasmaZones
