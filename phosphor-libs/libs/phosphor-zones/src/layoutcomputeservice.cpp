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
    m_worker = std::make_unique<LayoutWorker>();
    m_worker->moveToThread(m_thread);

    connect(this, &LayoutComputeService::requestCompute, m_worker.get(), &LayoutWorker::computeGeometries);
    connect(m_worker.get(), &LayoutWorker::geometriesReady, this, &LayoutComputeService::applyResult);

    m_thread->start();
    qCDebug(lcLayoutLib) << "LayoutComputeService: worker thread started";
}

LayoutComputeService::~LayoutComputeService()
{
    m_thread->quit();
    if (!m_thread->wait(5000)) {
        // A wedged worker (runaway compute) must not let the parented QThread
        // be destroyed while still running — Qt aborts the whole process on
        // that. The worker only writes its own local result state, so a
        // forced stop at teardown cannot corrupt anything the surviving
        // process still reads.
        qCWarning(lcLayoutLib) << "LayoutComputeService: worker thread did not quit within 5s — terminating";
        m_thread->terminate();
        m_thread->wait();
    }
    // The thread has stopped either way, so its event loop will never run
    // again and nothing races this. Destroying the worker here rather than
    // through a deleteLater posted to that loop is what makes the terminated
    // case and the clean one the same path.
    m_worker.reset();
}

void LayoutComputeService::setLayoutManager(LayoutRegistry* manager)
{
    // Sever the previous manager's connection first: re-wiring would
    // otherwise stack a second layoutRemoved handler, and setting null would
    // leave the dead manager's connection live.
    if (m_layoutManager) {
        disconnect(m_layoutManager, nullptr, this, nullptr);
    }
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

bool LayoutComputeService::requestRecalculate(Layout* layout, const QString& screenId, const QRectF& screenGeometry,
                                              qreal screenScale)
{
    if (!layout || !screenGeometry.isValid()) {
        return false;
    }

    // needsRecalc, not a bare rect comparison: a zone edit at unchanged
    // screen geometry (m_zoneGeometryDirty) must take the worker path or the
    // edited zones keep stale/absent absolute geometry.
    //
    // The cached no-op answers only when nothing is in flight for the screen.
    // With a compute in flight the layout is about to move to THAT request's
    // frame, so "already there" is not true for long: a request that flips
    // back to the current frame (or names another layout) must supersede the
    // in-flight one, and only a real dispatch does that. Its result then finds
    // the layout already at its frame and publishes without recomputing.
    if (!layout->needsRecalc(screenGeometry, screenScale) && m_inFlight.value(screenId) == 0) {
        // NO generation bump for a no-op. Nothing is in flight for this
        // screen, so the current generation's result has already been
        // published, and this queued publish repeats it for the new caller.
        // A newer request made before the callback runs bumps the generation
        // and turns this into a superseded (null) publish.
        if (screenScale > 0.0) {
            // Record the asked-for scale the way a recalculation would, so a
            // layout the scale cannot move still answers the current one.
            layout->setLastRecalcGeometry(layout->lastRecalcGeometry(), screenScale);
        }
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
    ++m_inFlight[screenId];

    LayoutSnapshot snapshot = buildSnapshot(layout, screenId, screenGeometry, screenScale);
    Q_EMIT requestCompute(snapshot, gen);
    return true;
}

uint64_t LayoutComputeService::currentGeneration(const QString& screenId) const
{
    return m_screenGeneration.value(screenId);
}

void LayoutComputeService::recalculateSync(Layout* layout, const QRectF& screenGeometry, qreal screenScale)
{
    if (!layout || !screenGeometry.isValid()) {
        return;
    }
    layout->recalculateZoneGeometries(screenGeometry, screenScale);
}

LayoutSnapshot LayoutComputeService::buildSnapshot(Layout* layout, const QString& screenId,
                                                   const QRectF& screenGeometry, qreal screenScale)
{
    LayoutSnapshot snapshot;
    snapshot.layoutId = layout->id();
    snapshot.screenId = screenId;
    snapshot.screenGeometry = screenGeometry;
    // Zero keeps the layout's last scale, as recalculateZoneGeometries reads it.
    snapshot.screenScale = screenScale > 0.0 ? screenScale : layout->lastRecalcScale();
    snapshot.fixedDivisor = layout->fixedGeometryDivisor(snapshot.screenScale);
    snapshot.zoneGeometryEpoch = layout->zoneGeometryEpoch();
    snapshot.syncRecalcEpoch = layout->syncRecalcEpoch();
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
    // Every result, stale or not, retires one dispatched compute.
    auto inFlightIt = m_inFlight.find(result.screenId);
    if (inFlightIt != m_inFlight.end() && --inFlightIt.value() <= 0) {
        m_inFlight.erase(inFlightIt);
    }

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
        if (layoutIt != m_trackedLayouts.constEnd()) {
            // Reap the nulled QPointer so destroyed-without-layoutRemoved
            // layouts don't accumulate tombstones for the session.
            m_trackedLayouts.remove(result.layoutId);
        }
        publishResult(result.screenId, result.layoutId, nullptr, result.generation);
        return;
    }
    Layout* layout = layoutIt->data();

    // A synchronous recalculation ran after this snapshot was taken. It
    // recorded the frame its caller wants NOW, which is newer than the one
    // this result was asked for, so applying the result would put the layout
    // back on an older frame (an older scale, or a rect the caller has since
    // flipped away from). The epoch is per layout, not per screen, so the
    // layout now holds the newer state for whichever screen that sync named,
    // which need not be this result's screen. The result publishes the layout
    // as it stands as the outcome of this generation. The generation itself
    // is still current, so a null publish would strand the barrier.
    if (layout->syncRecalcEpoch() != result.syncRecalcEpoch) {
        qCDebug(lcLayoutLib) << "LayoutComputeService: a later sync recalculation superseded the result for"
                             << result.screenId;
        publishResult(result.screenId, result.layoutId, layout, result.generation);
        return;
    }

    // needsRecalc, not a bare rect comparison: if zones were edited after the
    // last pass stamped this rect, the geometries computed below are exactly
    // what the edited zones are missing — skipping them here would publish a
    // non-null layout whose new zones have no absolute geometry.
    if (!layout->needsRecalc(result.screenGeometry, result.screenScale)) {
        qCDebug(lcLayoutLib) << "LayoutComputeService: sync path already applied for" << result.screenId;
        // Same rect and nothing to recompute, but record the scale the way a
        // recalculation would, so lastRecalcScale() stays the current one.
        layout->setLastRecalcGeometry(result.screenGeometry, result.screenScale);
        publishResult(result.screenId, result.layoutId, layout, result.generation);
        return;
    }

    // The worker computed from the zones and unit the layout held at snapshot
    // time. If either changed since (a zone added or removed, a zone replaced
    // by one with the same id, the fixed-geometry unit flipped), those
    // geometries describe inputs the layout no longer has, and stamping them
    // as a full recalculation would clear the dirty bit over stale zones. The
    // frame is still the current request's, so run the same computation again
    // in place from the layout as it is now.
    const LayoutComputeResult current = layout->zoneGeometryEpoch() == result.zoneGeometryEpoch
        ? result
        : LayoutWorker::compute(buildSnapshot(layout, result.screenId, result.screenGeometry, result.screenScale),
                                result.generation);

    layout->beginBatchModify();
    for (const auto& computed : current.zones) {
        Zone* zone = layout->zoneById(computed.zoneId);
        if (zone) {
            zone->setGeometry(computed.absoluteGeometry);
        }
    }
    layout->markRecalculated(current.screenGeometry, current.screenScale);
    layout->endBatchModify();

    publishResult(result.screenId, result.layoutId, layout, result.generation);
}

void LayoutComputeService::publishResult(const QString& screenId, const QUuid& layoutId, Layout* layout,
                                         uint64_t generation)
{
    Q_EMIT geometriesComputedForGeneration(screenId, layoutId, layout, generation);
}

void LayoutComputeService::onLayoutRemoved(const QUuid& layoutId)
{
    m_trackedLayouts.remove(layoutId);
}

} // namespace PhosphorZones
