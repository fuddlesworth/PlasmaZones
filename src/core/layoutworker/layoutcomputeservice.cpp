// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "layoutcomputeservice.h"
#include "layoutworker.h"
#include "../layout.h"
#include "../zone.h"
#include "../logging.h"

#include <QThread>

namespace PlasmaZones {

LayoutComputeService::LayoutComputeService(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<LayoutSnapshot>("PlasmaZones::LayoutSnapshot");
    qRegisterMetaType<LayoutComputeResult>("PlasmaZones::LayoutComputeResult");

    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("LayoutWorker"));
    m_worker = new LayoutWorker(); // no parent — will be moved to thread
    m_worker->moveToThread(m_thread);

    // Forward requests to worker (main → worker, QueuedConnection automatic)
    connect(this, &LayoutComputeService::requestCompute, m_worker, &LayoutWorker::computeGeometries);

    // Receive results back (worker → main, QueuedConnection automatic)
    connect(m_worker, &LayoutWorker::geometriesReady, this, &LayoutComputeService::applyResult);

    // Clean up worker when thread finishes
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

    m_thread->start();
    qCDebug(lcCore) << "LayoutComputeService: worker thread started";
}

LayoutComputeService::~LayoutComputeService()
{
    m_thread->quit();
    m_thread->wait(5000);
}

void LayoutComputeService::requestRecalculate(Layout* layout, const QString& screenId, const QRectF& screenGeometry)
{
    if (!layout || !screenGeometry.isValid()) {
        return;
    }

    // Skip if geometry hasn't changed (same cache logic as Layout::recalculateZoneGeometries)
    if (screenGeometry == layout->lastRecalcGeometry()) {
        return;
    }

    // Track layout for result application
    m_trackedLayouts[layout->id()] = layout;

    // Increment per-screen generation for coalescing
    uint64_t gen = ++m_screenGeneration[screenId];

    LayoutSnapshot snapshot = buildSnapshot(layout, screenId, screenGeometry);
    Q_EMIT requestCompute(snapshot, gen);
}

void LayoutComputeService::recalculateSync(Layout* layout, const QRectF& screenGeometry)
{
    layout->recalculateZoneGeometries(screenGeometry);
}

LayoutSnapshot LayoutComputeService::buildSnapshot(Layout* layout, const QString& screenId,
                                                   const QRectF& screenGeometry)
{
    LayoutSnapshot snapshot;
    snapshot.layoutId = layout->id();
    snapshot.screenId = screenId;
    snapshot.screenGeometry = screenGeometry;
    snapshot.useFullScreenGeometry = layout->useFullScreenGeometry();
    snapshot.zones.reserve(layout->zoneCount());

    for (const auto* zone : layout->zones()) {
        ZoneSnapshot zs;
        zs.id = zone->id();
        zs.relativeGeometry = zone->relativeGeometry();
        zs.fixedMode = zone->isFixedGeometry();
        zs.fixedGeometry = zone->fixedGeometry();
        snapshot.zones.append(zs);
    }

    return snapshot;
}

void LayoutComputeService::applyResult(const LayoutComputeResult& result)
{
    // Coalescing: discard if a newer request for this screen superseded
    auto genIt = m_screenGeneration.constFind(result.screenId);
    if (genIt != m_screenGeneration.constEnd() && result.generation < *genIt) {
        qCDebug(lcCore) << "LayoutComputeService: discarding stale result for" << result.screenId
                        << "gen=" << result.generation << "current=" << *genIt;
        return;
    }

    // Find the live layout
    auto layoutIt = m_trackedLayouts.constFind(result.layoutId);
    if (layoutIt == m_trackedLayouts.constEnd() || !*layoutIt) {
        return;
    }
    Layout* layout = *layoutIt;

    // Batch-apply: update all zone geometries with a single layoutModified signal at end
    layout->beginBatchModify();
    for (const auto& computed : result.zones) {
        Zone* zone = layout->zoneById(computed.zoneId);
        if (zone) {
            zone->setGeometry(computed.absoluteGeometry);
        }
    }
    layout->setLastRecalcGeometry(result.screenGeometry);
    layout->endBatchModify();

    Q_EMIT geometriesComputed(result.screenId, layout);
}

} // namespace PlasmaZones
