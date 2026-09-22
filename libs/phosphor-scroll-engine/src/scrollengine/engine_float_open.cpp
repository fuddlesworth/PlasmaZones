// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// The float-at-open helpers of the scroll engine, split out of
// engine_lifecycle.cpp by concern: the float-restore seed, the gated
// remembered-position restore a floated open performs, and the scroll arm of
// the shared free-size restore (#1106) for a float that moved nowhere.

#include <PhosphorScrollEngine/ScrollEngine.h>

#include <PhosphorEngine/IWindowTrackingService.h>
#include <PhosphorEngine/WindowPlacementStore.h>
#include <PhosphorScreens/ScreenIdentity.h>

#include "enginelimits.h"
#include "scrollenginelogging.h"

#include <QTimer>
#include <QVariant>

#include <algorithm>
#include <utility>

namespace PhosphorScrollEngine {

void ScrollEngine::seedFloatRestoreForOpen(const QString& windowId, int minWidth, int minHeight)
{
    // windowMinimumSize reads the clamp out of this entry for every window
    // that is floated rather than tiled, and the cross-engine handoff queries
    // it whatever state the window is in: with no entry the answer is the
    // "unknown" one, and the receiving engine gets an unclamped window. That
    // bites hardest on exactly the windows these paths float — the oversized
    // ones, whose clamp is why they could not take a column in the first
    // place.
    const auto existing = m_floatRestore.find(windowId);
    if (existing != m_floatRestore.end()) {
        // A real remembered slot is worth more than a slotless seed; only
        // the clamp is refreshed.
        existing->minWidth = qMax(0, minWidth);
        existing->minHeight = qMax(0, minHeight);
        return;
    }
    FloatRestore restore;
    restore.column = -1; // no slot to go back to; unfloat opens a fresh column
    restore.minWidth = qMax(0, minWidth);
    restore.minHeight = qMax(0, minHeight);
    m_floatRestore.insert(windowId, restore);
}

bool ScrollEngine::emitGatedFloatGeometryRestore(const QString& windowId, const PhosphorEngine::WindowPlacement& record,
                                                 const QString& screenId)
{
    // SCREEN-LOCAL recorded position only, for autotile's documented reason: a
    // rect captured on a different screen would teleport the window while the
    // float tracking points elsewhere. The move itself is gated (daemon-wired
    // scrollingRestoreFloatedWindowsOnLogin setting + per-window
    // RestorePosition rule) while the floating MARK is not — the callers mark
    // unconditionally and only the geometry comes through here.
    //
    // Shared by the two restore paths (insertOpenedWindow's record-float branch
    // and restoreFloatRecordForOpen) because they are one rule with two entry
    // points, and a change to the gate that reached only one of them would
    // restore the position on one open path and not the other.
    const QString restoreScreen = record.screenId.isEmpty() ? screenId : record.screenId;
    const QRect freeGeo = record.freeGeometryFor(restoreScreen);
    const bool restorePosition = !m_restorePositionPredicate || m_restorePositionPredicate(windowId);
    // Do not trust the KEY. This reads the shared free-geometry map directly
    // rather than through validatedUnmanagedGeometry, so it gets none of that
    // resolver's validation — and the rect goes straight out as an absolute
    // geometry to apply. A record filed under one screen whose coordinates
    // describe another would teleport the window to that monitor, which is
    // exactly what the comment above says this restore must never do.
    if (freeGeo.isValid() && restorePosition
        && (!m_windowTracker || m_windowTracker->geometryBelongsToScreen(freeGeo, restoreScreen))) {
        Q_EMIT geometryRestoreRequested(windowId, freeGeo, restoreScreen);
        return true;
    }
    return false;
}

bool ScrollEngine::restoreFloatRecordForOpen(const QString& windowId, const QString& screenId)
{
    // Registry answer, not a parse: a canonical id frozen before KWin resolved
    // the class has no appId to parse, and this gate would then silently skip
    // the float restore for the window's whole life.
    const QString appId = currentAppIdFor(windowId);
    if (!m_windowTracker || !PhosphorEngine::hasStableAppIdFor(appId, windowId)) {
        return false;
    }
    // The window floats regardless (the caller already decided that), so only
    // a FLOATING record is consumed — takeForReopen's contract. The accept
    // itself is the store's shared predicate.
    const PhosphorEngine::PlacementStateKey key = currentKeyForScreen(screenId);
    const auto record = m_windowTracker->placementStore().takeForReopen(engineId(), windowId, appId, key.screenId);
    if (!record) {
        return false;
    }
    return emitGatedFloatGeometryRestore(windowId, *record, screenId);
}

void ScrollEngine::restoreFreeSizeForFloatedOpen(const QString& windowId, const QString& screenId,
                                                 const QSize& workAreaSize)
{
    // A window this engine leaves floating at open, and did not move to its
    // remembered free spot, stays where the compositor put it at the size the
    // client asked for: for a KDE app whose sibling is a column that is the
    // column's size (#1106). The live column rects on this screen are the
    // sizes such a spawn frame can have. Runs ahead of the float-state sync
    // the callers emit, so the size-only apply precedes the sync on the wire.
    QList<QSize> columnSizes;
    for (const QString& member : managedWindowOrder(screenId)) {
        const QRect rect = lastManagedRect(member);
        if (rect.isValid()) {
            columnSizes.append(rect.size());
        }
    }
    restoreFreeSizeWhereItStands(m_windowTracker, windowId, screenId, PhosphorEngine::RestoreReason::Open, columnSizes,
                                 workAreaSize.isValid() ? workAreaSize : QSize());
}

} // namespace PhosphorScrollEngine
