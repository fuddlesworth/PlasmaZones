// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorEngine/PlacementEngineBase.h>

#include <PhosphorEngine/IWindowTrackingService.h>
#include <PhosphorEngine/LayerFocusSwitch.h>
#include <PhosphorEngine/PerScreenStates.h>
#include <PhosphorEngine/WindowPlacementStore.h>
#include <PhosphorIdentity/WindowId.h>

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

void PlacementEngineBase::releaseWindowOffScreen(const QString& windowId, const QString& keepScreenId)
{
    if (windowId.isEmpty()) {
        return;
    }
    // Compared at the virtual-screen level: a window that moved to the other
    // half of a split monitor left the half it was held on, too.
    const QString heldScreen = screenForTrackedWindow(windowId);
    if (heldScreen.isEmpty() || heldScreen == keepScreenId) {
        return;
    }
    qCInfo(lcPlacementEngineBase) << "releaseWindowOffScreen:" << engineId() << "releases" << windowId << "held on"
                                  << heldScreen << "— it was placed on" << keepScreenId;
    handoffRelease(windowId);
}

bool PlacementEngineBase::isManagedSize(const QList<QSize>& managedSizes, const QSize& size)
{
    // A false match only skips the resize, which is the harmless direction.
    return std::any_of(managedSizes.cbegin(), managedSizes.cend(), [&](const QSize& s) {
        return qAbs(s.width() - size.width()) <= 2 && qAbs(s.height() - size.height()) <= 2;
    });
}

bool PlacementEngineBase::placedByPreviousLineage(const WindowPlacementStore& store, const QString& windowId)
{
    if (windowId.isEmpty()) {
        return false;
    }
    const auto own = store.peekExact(windowId);
    return own && !own->engines.isEmpty();
}

void PlacementEngineBase::restoreFreeSizeWhereItStands(IWindowTrackingService* tracker, const QString& windowId,
                                                       const QString& screenId, RestoreReason reason, bool placedBefore,
                                                       const QList<QSize>& managedSizes)
{
    if (!tracker || windowId.isEmpty() || screenId.isEmpty()) {
        return;
    }
    if (reason == RestoreReason::Unminimize) {
        qCDebug(lcPlacementEngineBase) << "restoreFreeSizeWhereItStands:" << windowId
                                       << "unminimize re-drive of a visible window, size left alone";
        return;
    }
    if (placedBefore) {
        qCDebug(lcPlacementEngineBase) << "restoreFreeSizeWhereItStands:" << windowId
                                       << "already placed by a previous daemon lineage, size left alone";
        return;
    }
    const WindowPlacementStore& store = tracker->placementStore();
    const auto own = store.peekExact(windowId);
    const QString appId = tracker->currentAppIdFor(windowId);
    const bool stableAppId = hasStableAppIdFor(appId, windowId);
    QRect freeGeo;
    QString sourceWindowId;
    // The source order, run with one rect picker per pass. Returns whether a
    // record supplied a rect.
    const auto search = [&](const auto& rectOf) {
        // 1. The window's own record, only when an engine has ever captured
        //    it: the slot-less record under the live id is this open's
        //    pre-tile capture and its rect is the spawn frame this exists to
        //    undo.
        if (own && !own->engines.isEmpty()) {
            if (const QRect rect = rectOf(*own); rect.isValid()) {
                freeGeo = rect;
                sourceWindowId = own->windowId;
                return true;
            }
        }
        if (!stableAppId) {
            return false;
        }
        // 2. The earliest live sibling with a usable rect.
        const auto sibling = store.peekLiveSibling(windowId, appId, [&](const WindowPlacement& p) {
            return rectOf(p).isValid();
        });
        // 3. A closed same-app record, read but never consumed, so a tiled
        //    record stays as the exact-final evidence the tiling engines'
        //    reopen accept relies on. The asker's own records are excluded
        //    explicitly: without a probe the store cannot call them live.
        const auto source = sibling ? sibling : store.peek(windowId, appId, [&](const WindowPlacement& p) {
            return !PhosphorIdentity::WindowId::sameWindowInstance(p.windowId, windowId)
                && !store.isLiveInstance(p.windowId) && rectOf(p).isValid();
        });
        if (!source) {
            return false;
        }
        freeGeo = rectOf(*source);
        sourceWindowId = source->windowId;
        return true;
    };
    // First pass: a rect on this screen.
    const auto onThisScreen = [&](const WindowPlacement& p) {
        const QRect rect = p.freeGeometryFor(screenId);
        return rect.isValid() && tracker->geometryBelongsToScreen(rect, screenId)
                && !isManagedSize(managedSizes, rect.size())
            ? rect
            : QRect();
    };
    // Second pass, when no record holds one: the rect a record keeps for
    // another screen, of which only the size is used. Still refused when it is
    // a size managed on THIS screen.
    const auto onAnyScreen = [&](const WindowPlacement& p) {
        const QRect rect = p.anyFreeGeometry();
        return rect.isValid() && !isManagedSize(managedSizes, rect.size()) ? rect : QRect();
    };
    const bool local = search(onThisScreen);
    if (!local && !search(onAnyScreen)) {
        qCDebug(lcPlacementEngineBase) << "restoreFreeSizeWhereItStands:" << windowId << "no free size on record for"
                                       << screenId;
        return;
    }
    QSize size = freeGeo.size();
    // Clamped to the output's available area when the tracker can resolve
    // one. An unresolvable screen answers an invalid rect, whose 0x0 size
    // QSize::isValid would accept as a bound, hence the emptiness test.
    const QRect available = tracker->screenAvailableGeometry(screenId);
    if (available.isValid() && !available.isEmpty()) {
        size = size.boundedTo(available.size());
    }
    qCInfo(lcPlacementEngineBase) << "restoreFreeSizeWhereItStands:" << windowId << "->" << size << "from"
                                  << sourceWindowId << (local ? "" : "(from another screen)");
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
