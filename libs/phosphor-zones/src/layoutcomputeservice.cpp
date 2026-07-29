// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorZones/LayoutComputeService.h>
#include <PhosphorZones/LayoutWorker.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/Zone.h>
#include "zoneslogging.h"

#include <QThread>

namespace PhosphorZones {

LayoutComputeService::LayoutComputeService(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<LayoutSnapshot>("PhosphorZones::LayoutSnapshot");
    qRegisterMetaType<LayoutComputeResult>("PhosphorZones::LayoutComputeResult");

    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("LayoutWorker"));
    m_worker = new LayoutWorker();
    m_worker->moveToThread(m_thread);

    connect(this, &LayoutComputeService::requestCompute, m_worker, &LayoutWorker::computeGeometries);
    connect(m_worker, &LayoutWorker::geometriesReady, this, &LayoutComputeService::applyResult);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

    m_thread->start();
    qCDebug(lcLayoutLib) << "LayoutComputeService: worker thread started";
}

LayoutComputeService::~LayoutComputeService()
{
    m_thread->quit();
    m_thread->wait(5000);
}

void LayoutComputeService::setLayoutManager(LayoutRegistry* manager)
{
    m_layoutManager = manager;
    if (!manager) {
        return;
    }
    connect(manager, &LayoutRegistry::layoutRemoved, this, [this](Layout* layout) {
        if (layout) {
            onLayoutRemoved(layout->id());
        }
    });
}

bool LayoutComputeService::requestRecalculate(Layout* layout, const QString& screenId, const QRectF& screenGeometry)
{
    if (!layout || !screenGeometry.isValid()) {
        return false;
    }

    // needsRecalc, not a bare rect comparison: a zone edit at unchanged
    // screen geometry (m_zoneGeometryDirty) must take the worker path or the
    // edited zones keep stale/absent absolute geometry.
    if (!layout->needsRecalc(screenGeometry)) {
        // NO generation bump for a no-op: bumping here would supersede an
        // in-flight worker compute carrying a REAL geometry change — its
        // result would be discarded as stale while this cached callback
        // publishes a non-null layout for a generation whose geometry it
        // never computed, satisfying the caller's barrier with nothing
        // applied. Publishing under the current generation keeps both the
        // barrier and any in-flight compute honest.
        const uint64_t gen = m_screenGeneration.value(screenId);
        const QString sid = screenId;
        const QUuid lid = layout->id();
        QPointer<Layout> lp(layout);
        QMetaObject::invokeMethod(
            this,
            [this, sid, lid, lp, gen]() {
                const bool superseded = gen < m_screenGeneration.value(sid);
                publishResult(sid, lid, superseded ? nullptr : lp.data(), gen);
            },
            Qt::QueuedConnection);
        return true;
    }

    const uint64_t gen = ++m_screenGeneration[screenId];
    m_trackedLayouts[layout->id()] = layout;

    LayoutSnapshot snapshot = buildSnapshot(layout, screenId, screenGeometry);
    Q_EMIT requestCompute(snapshot, gen);
    return true;
}

uint64_t LayoutComputeService::currentGeneration(const QString& screenId) const
{
    return m_screenGeneration.value(screenId);
}

void LayoutComputeService::recalculateSync(Layout* layout, const QRectF& screenGeometry)
{
    if (!layout) {
        return;
    }
    layout->recalculateZoneGeometries(screenGeometry);
}

LayoutSnapshot LayoutComputeService::buildSnapshot(Layout* layout, const QString& screenId,
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
    auto genIt = m_screenGeneration.constFind(result.screenId);
    if (genIt != m_screenGeneration.constEnd() && result.generation < *genIt) {
        qCDebug(lcLayoutLib) << "LayoutComputeService: discarding stale result for" << result.screenId
                             << "gen=" << result.generation << "current=" << *genIt;
        // Emit a null layout so screen-scoped async barriers know this result
        // was superseded and must wait for the newest generation. The current
        // result may belong to a different layout after an assignment change.
        publishResult(result.screenId, result.layoutId, nullptr, result.generation);
        return;
    }

    auto layoutIt = m_trackedLayouts.constFind(result.layoutId);
    if (layoutIt == m_trackedLayouts.constEnd() || layoutIt->isNull()) {
        qCDebug(lcLayoutLib) << "LayoutComputeService: dropping result for destroyed layout"
                             << result.layoutId.toString();
        publishResult(result.screenId, result.layoutId, nullptr, result.generation);
        return;
    }
    Layout* layout = layoutIt->data();

    // needsRecalc, not a bare rect comparison: if zones were edited after the
    // sync recalc stamped this rect, the worker's freshly computed geometries
    // are exactly what the edited zones are missing — discarding them here
    // would publish a non-null layout whose new zones have no absolute
    // geometry.
    if (!layout->needsRecalc(result.screenGeometry)) {
        qCDebug(lcLayoutLib) << "LayoutComputeService: sync path already applied for" << result.screenId;
        publishResult(result.screenId, result.layoutId, layout, result.generation);
        return;
    }

    layout->beginBatchModify();
    for (const auto& computed : result.zones) {
        Zone* zone = layout->zoneById(computed.zoneId);
        if (zone) {
            zone->setGeometry(computed.absoluteGeometry);
        }
    }
    // Clear the zone-set dirty bit only when this result covered the layout's
    // CURRENT zones. A zone edited mid-flight (snapshot taken before the
    // edit) leaves the layout dirty so the next request recomputes it; the
    // rect is still stamped either way, preserving the pre-existing
    // rect-comparison behavior for that partial case.
    bool coversCurrentZones = result.zones.size() == layout->zoneCount();
    if (coversCurrentZones) {
        for (const auto& computed : result.zones) {
            if (!layout->zoneById(computed.zoneId)) {
                coversCurrentZones = false;
                break;
            }
        }
    }
    if (coversCurrentZones) {
        layout->markRecalculated(result.screenGeometry);
    } else {
        layout->setLastRecalcGeometry(result.screenGeometry);
    }
    layout->endBatchModify();

    publishResult(result.screenId, result.layoutId, layout, result.generation);
}

void LayoutComputeService::publishResult(const QString& screenId, const QUuid& layoutId, Layout* layout,
                                         uint64_t generation)
{
    QPointer<LayoutComputeService> guard(this);
    // QPointer the layout across the first emit too: a subscriber that
    // destroys the layout (layout removal) must not leave the second emit
    // publishing a dangling pointer.
    QPointer<Layout> layoutGuard(layout);
    Q_EMIT geometriesComputed(screenId, layoutId, layout);
    if (!guard) {
        return;
    }
    Q_EMIT geometriesComputedForGeneration(screenId, layoutId, layoutGuard.data(), generation);
}

void LayoutComputeService::onLayoutRemoved(const QUuid& layoutId)
{
    m_trackedLayouts.remove(layoutId);
}

} // namespace PhosphorZones
