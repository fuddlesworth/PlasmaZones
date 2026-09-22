// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorEngine/PlacementEngineBase.h>

#include <PhosphorEngine/IWindowTrackingService.h>
#include <PhosphorEngine/LayerFocusSwitch.h>
#include <PhosphorEngine/PerScreenStates.h>
#include <PhosphorEngine/WindowPlacementStore.h>

#include <QLoggingCategory>

#include <algorithm>

// Filterable like every other diagnostic in this library (the bare qWarning
// it replaces could not be silenced per-category).
Q_LOGGING_CATEGORY(lcPlacementEngineBase, "org.phosphor.engine.placementbase")

namespace PhosphorEngine {

// Defined here rather than in a file of its own: PerScreenStates is a
// header-only template with no TU to host it. INSIDE the namespace, matching
// the exported declaration in that header — a definition at global scope
// declares a different function, and the template's use of it would resolve to
// the namespaced declaration nothing then defines.
Q_LOGGING_CATEGORY(lcPerScreenStates, "org.phosphor.engine.perscreenstates")

PlacementEngineBase::PlacementEngineBase(QObject* parent)
    : QObject(parent)
{
}

PlacementEngineBase::~PlacementEngineBase() = default;

void PlacementEngineBase::restoreFreeSizeWhereItStands(IWindowTrackingService* tracker, const QString& windowId,
                                                       const QString& screenId, RestoreReason reason,
                                                       const QList<QSize>& managedSizes, const QSize& availableSize)
{
    if (!tracker || windowId.isEmpty() || screenId.isEmpty()) {
        return;
    }
    if (reason == RestoreReason::Unminimize || reason == RestoreReason::DaemonRestartSweep) {
        qCDebug(lcPlacementEngineBase) << "restoreFreeSizeWhereItStands:" << windowId
                                       << "re-resolve of a visible window, size left alone";
        return;
    }
    // Within a couple of pixels, not exact: a fractional-scale round-trip or
    // a size-increment client (terminal cells) lands the frame a pixel or two
    // off the requested rect. A false match only skips the resize, which is
    // the harmless direction.
    const auto isManagedSize = [&](const QSize& size) {
        return std::any_of(managedSizes.cbegin(), managedSizes.cend(), [&](const QSize& s) {
            return qAbs(s.width() - size.width()) <= 2 && qAbs(s.height() - size.height()) <= 2;
        });
    };
    const auto usableOn = [&](const QRect& rect) {
        return rect.isValid() && tracker->geometryBelongsToScreen(rect, screenId) && !isManagedSize(rect.size());
    };
    const WindowPlacementStore& store = tracker->placementStore();
    QRect freeGeo;
    QString sourceWindowId;
    const auto own = store.peekExact(windowId);
    const QRect ownRect = own ? own->freeGeometryFor(screenId) : QRect();
    if (usableOn(ownRect)) {
        freeGeo = ownRect;
        sourceWindowId = own->windowId;
    } else {
        const QString appId = tracker->currentAppIdFor(windowId);
        const auto sibling = store.peekLiveSibling(windowId, appId, [&](const WindowPlacement& p) {
            return usableOn(p.freeGeometryFor(screenId));
        });
        if (sibling) {
            freeGeo = sibling->freeGeometryFor(screenId);
            sourceWindowId = sibling->windowId;
        }
    }
    if (!freeGeo.isValid()) {
        qCDebug(lcPlacementEngineBase) << "restoreFreeSizeWhereItStands:" << windowId << "no free size on record for"
                                       << screenId;
        return;
    }
    QSize size = freeGeo.size();
    if (availableSize.isValid()) {
        size = size.boundedTo(availableSize);
    }
    qCInfo(lcPlacementEngineBase) << "restoreFreeSizeWhereItStands:" << windowId << "->" << size << "from"
                                  << sourceWindowId;
    Q_EMIT sizeRestoreRequested(windowId, size, screenId);
}

int PlacementEngineBase::pruneStaleWindows(const QSet<QString>& aliveWindowIds)
{
    // The base keeps no per-window state since the unmanaged-geometry store was
    // collapsed into the unified WindowPlacementStore. Engines override this and
    // prune their own state (window→state maps, overflow, float markers).
    Q_UNUSED(aliveWindowIds)
    return 0;
}

void PlacementEngineBase::announceLayerSwitch(const LayerSwitchResult& result, const QString& action,
                                              const QString& screenId)
{
    if (!result.success) {
        Q_EMIT navigationFeedback(false, action, result.reason, result.source, QString(), screenId);
        return;
    }
    Q_EMIT activateWindowRequested(result.target);
    Q_EMIT navigationFeedback(true, action, result.reason, result.source, result.target, screenId);
}

void PlacementEngineBase::setEngineSettings(QObject* settings)
{
    if (Q_UNLIKELY(!settings)) {
        qCWarning(lcPlacementEngineBase, "PlacementEngineBase::setEngineSettings called with nullptr");
        return;
    }
    m_engineSettings = settings;
}

} // namespace PhosphorEngine
