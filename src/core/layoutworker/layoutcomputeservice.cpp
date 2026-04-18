// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "layoutcomputeservice.h"
#include "layoutworker.h"
#include <PhosphorZones/Layout.h>
#include "../layoutmanager.h"
#include <PhosphorZones/Zone.h>
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

void LayoutComputeService::setLayoutManager(LayoutManager* manager)
{
    m_layoutManager = manager;
    if (!manager) {
        return;
    }
    // Evict tracked entries when layouts disappear, so the hash doesn't
    // accumulate dangling QPointers over long-running sessions.
    connect(manager, &LayoutManager::layoutRemoved, this, [this](PhosphorZones::Layout* layout) {
        if (layout) {
            onLayoutRemoved(layout->id());
        }
    });
}

bool LayoutComputeService::requestRecalculate(PhosphorZones::Layout* layout, const QString& screenId,
                                              const QRectF& screenGeometry)
{
    if (!layout || !screenGeometry.isValid()) {
        return false;
    }

    // Cache hit: geometry already current. Emit the completion signal
    // synchronously (next event-loop tick via QueuedConnection to self)
    // so completion barriers armed by the caller still fire exactly
    // once per requestRecalculate call. Emitting inline would re-enter
    // the caller on the same stack, which is surprising; defer it.
    if (screenGeometry == layout->lastRecalcGeometry()) {
        const QString sid = screenId;
        const QUuid lid = layout->id();
        QPointer<PhosphorZones::Layout> lp(layout);
        QMetaObject::invokeMethod(
            this,
            [this, sid, lid, lp]() {
                // Always fire so barriers keyed on (screenId, layoutId)
                // drain even if the PhosphorZones::Layout was destroyed on this tick.
                Q_EMIT geometriesComputed(sid, lid, lp.data());
            },
            Qt::QueuedConnection);
        return true;
    }

    // Track layout for result application (QPointer guards against
    // destruction while the worker is computing).
    m_trackedLayouts[layout->id()] = layout;

    // Increment per-screen generation for coalescing
    uint64_t gen = ++m_screenGeneration[screenId];

    LayoutSnapshot snapshot = buildSnapshot(layout, screenId, screenGeometry);
    Q_EMIT requestCompute(snapshot, gen);
    return true;
}

void LayoutComputeService::recalculateSync(PhosphorZones::Layout* layout, const QRectF& screenGeometry)
{
    if (!layout) {
        return;
    }
    layout->recalculateZoneGeometries(screenGeometry);
}

LayoutSnapshot LayoutComputeService::buildSnapshot(PhosphorZones::Layout* layout, const QString& screenId,
                                                   const QRectF& screenGeometry)
{
    LayoutSnapshot snapshot;
    snapshot.layoutId = layout->id();
    snapshot.screenId = screenId;
    snapshot.screenGeometry = screenGeometry;
    snapshot.zones.reserve(layout->zoneCount());

    for (const auto* zone : layout->zones()) {
        ZoneSnapshot zs;
        zs.id = zone->id();
        zs.geometryMode = zone->geometryMode();
        zs.relativeGeometry = zone->relativeGeometry();
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

    // Find the live layout. QPointer catches destruction between request
    // and result (layout removed, session swap, etc.) — a dangling raw
    // PhosphorZones::Layout* here would be a use-after-free. Even in the destroyed case
    // we still emit geometriesComputed(…, nullptr) so completion barriers
    // keyed on (screenId, layoutId) drain exactly once per request.
    auto layoutIt = m_trackedLayouts.constFind(result.layoutId);
    if (layoutIt == m_trackedLayouts.constEnd() || layoutIt->isNull()) {
        qCDebug(lcCore) << "LayoutComputeService: dropping result for destroyed layout" << result.layoutId.toString();
        Q_EMIT geometriesComputed(result.screenId, result.layoutId, nullptr);
        return;
    }
    PhosphorZones::Layout* layout = layoutIt->data();

    // Race guard: if a sync caller (recalculateSync) already computed for
    // this exact screen geometry while the async result was in flight, skip
    // the redundant apply. Both paths produce identical results for the
    // same input; applying the async one would just re-emit signals.
    if (layout->lastRecalcGeometry() == result.screenGeometry) {
        qCDebug(lcCore) << "LayoutComputeService: sync path already applied for" << result.screenId;
        Q_EMIT geometriesComputed(result.screenId, result.layoutId, layout);
        return;
    }

    // Batch-apply: update all zone geometries with a single layoutModified signal at end
    layout->beginBatchModify();
    for (const auto& computed : result.zones) {
        PhosphorZones::Zone* zone = layout->zoneById(computed.zoneId);
        if (zone) {
            zone->setGeometry(computed.absoluteGeometry);
        }
    }
    layout->setLastRecalcGeometry(result.screenGeometry);
    layout->endBatchModify();

    Q_EMIT geometriesComputed(result.screenId, result.layoutId, layout);
}

void LayoutComputeService::onLayoutRemoved(const QUuid& layoutId)
{
    m_trackedLayouts.remove(layoutId);
}

} // namespace PlasmaZones
