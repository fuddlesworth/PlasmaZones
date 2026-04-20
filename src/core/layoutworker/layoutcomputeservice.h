// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "types.h"
#include "plasmazones_export.h"
#include <QHash>
#include <QObject>
#include <QPointer>

class QThread;

namespace PhosphorZones {
class Layout;
}

namespace PhosphorZones {
class LayoutRegistry;
}

namespace PlasmaZones {
class LayoutWorker;

/**
 * @brief Main-thread coordinator for async layout geometry computation.
 *
 * Owns a dedicated QThread + LayoutWorker. Callers on the main thread
 * call requestRecalculate() which snapshots the PhosphorZones::Layout and sends the
 * work to the worker thread. Results arrive back on the main thread
 * via geometriesComputed().
 *
 * For init-time paths that cannot be async, recalculateSync() runs
 * the computation inline on the calling thread.
 *
 * Per-screen generation counters enable coalescing: if a newer request
 * for the same screen arrives before the worker finishes, the stale
 * result is discarded.
 */
class PLASMAZONES_EXPORT LayoutComputeService : public QObject
{
    Q_OBJECT

public:
    explicit LayoutComputeService(QObject* parent = nullptr);
    ~LayoutComputeService() override;

    /**
     * Optional: wire in the PhosphorZones::LayoutRegistry so tracked layouts are evicted
     * when layouts are removed/destroyed. Without this m_trackedLayouts
     * would grow unbounded over a long process lifetime.
     */
    void setLayoutManager(PhosphorZones::LayoutRegistry* manager);

    /**
     * Request async geometry computation for a layout on a given screen.
     * Snapshots the layout, sends to worker. geometriesComputed() fires
     * when results are ready OR when the request was a no-op (cache hit).
     * Callers that track a completion barrier can rely on the signal
     * firing exactly once per successful requestRecalculate() call.
     *
     * @return true if the signal will fire, false if the request was
     *         rejected (null layout or invalid geometry). A rejected
     *         request never fires geometriesComputed.
     */
    bool requestRecalculate(PhosphorZones::Layout* layout, const QString& screenId, const QRectF& screenGeometry);

    /**
     * Synchronous fallback — runs computation inline on the main thread.
     * Use for init-time code and D-Bus queries that must return immediately.
     */
    static void recalculateSync(PhosphorZones::Layout* layout, const QRectF& screenGeometry);

Q_SIGNALS:
    /**
     * Emitted on the main thread after the worker completes geometry
     * computation for a layout-screen pair. The live PhosphorZones::Layout/PhosphorZones::Zone objects
     * have already been updated when this signal fires. Also emitted
     * from requestRecalculate() when the request hit the geometry cache
     * (no work needed), and from applyResult() when the tracked PhosphorZones::Layout
     * was destroyed mid-compute — so completion barriers always drain
     * exactly once per accepted requestRecalculate() call.
     *
     * @param layout may be nullptr if the PhosphorZones::Layout was destroyed while
     *               the worker was running; @p layoutId is always the
     *               id of the originally-requested PhosphorZones::Layout and is the
     *               key completion barriers should match on.
     */
    void geometriesComputed(const QString& screenId, const QUuid& layoutId, PhosphorZones::Layout* layout);

    // Internal: forward snapshot to worker thread via QueuedConnection.
    // Publicly visible because Qt signals cannot be private, but must
    // only be emitted by LayoutComputeService itself.
    void requestCompute(const PlasmaZones::LayoutSnapshot& snapshot, uint64_t generation);

private:
    static LayoutSnapshot buildSnapshot(PhosphorZones::Layout* layout, const QString& screenId,
                                        const QRectF& screenGeometry);
    void applyResult(const LayoutComputeResult& result);
    void onLayoutRemoved(const QUuid& layoutId);

    /// Map layout ID → live PhosphorZones::Layout* (QPointer guards against destruction
    /// while a compute result is in flight on the worker thread).
    QHash<QUuid, QPointer<PhosphorZones::Layout>> m_trackedLayouts;

    /// Per-screen generation counters for coalescing
    QHash<QString, uint64_t> m_screenGeneration;

    QPointer<PhosphorZones::LayoutRegistry> m_layoutManager;

    QThread* m_thread = nullptr;
    LayoutWorker* m_worker = nullptr;
};

} // namespace PlasmaZones
