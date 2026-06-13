// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorSnapEngine/ISnapSettings.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/Zone.h>
#include "snapenginelogging.h"

namespace PhosphorSnapEngine {

using PhosphorEngine::SnapIntent;
using PhosphorEngine::UnfloatResult;

// ═══════════════════════════════════════════════════════════════════════════════
// Float toggle / set
// ═══════════════════════════════════════════════════════════════════════════════

void SnapEngine::toggleWindowFloat(const QString& windowId, const QString& screenId)
{
    const bool currentlyFloating = m_snapState->isFloating(windowId);
    const bool currentlySnapped = m_snapState->isWindowSnapped(windowId);

    if (!currentlyFloating && !currentlySnapped) {
        return;
    }

    if (currentlyFloating) {
        if (!unfloatToZone(windowId, screenId)) {
            Q_EMIT navigationFeedback(false, QStringLiteral("float"), QStringLiteral("no_pre_float_zone"), QString(),
                                      QString(), screenId);
            return;
        }
        Q_EMIT navigationFeedback(true, QStringLiteral("float"), QStringLiteral("unfloated"), QString(), QString(),
                                  screenId);
    } else {
        m_windowTracker->unsnapForFloat(windowId);
        m_windowTracker->setWindowFloating(windowId, true);
        Q_EMIT windowFloatingChanged(windowId, true, screenId);
        applyGeometryForFloat(windowId, screenId);
        Q_EMIT navigationFeedback(true, QStringLiteral("float"), QStringLiteral("floated"), QString(), QString(),
                                  screenId);
    }
}

void SnapEngine::setWindowFloat(const QString& windowId, bool shouldFloat)
{
    // IPlacementEngine::setWindowFloat has no screenId param, so resolve it:
    // 1. Try the window's tracked screen from WTS (most accurate)
    // 2. Fall back to m_lastActiveScreenId (from last windowFocused)
    // 3. Fall back to empty (unfloatToZone/applyGeometryForFloat handle gracefully)
    QString screenId = m_snapState->screenAssignments().value(windowId);
    if (screenId.isEmpty()) {
        screenId = m_lastActiveScreenId;
    }
    if (screenId.isEmpty()) {
        qCDebug(PhosphorSnapEngine::lcSnapEngine)
            << "setWindowFloat: no screen context for" << windowId << "- using empty screenId";
    }

    if (shouldFloat) {
        m_windowTracker->unsnapForFloat(windowId);
        m_windowTracker->setWindowFloating(windowId, true);
        Q_EMIT windowFloatingChanged(windowId, true, screenId);
        applyGeometryForFloat(windowId, screenId);
    } else {
        if (!unfloatToZone(windowId, screenId)) {
            // No pre-float zone to restore to — keep the window floating rather than
            // leaving it in a limbo state (not floating, not snapped to any zone).
            qCDebug(PhosphorSnapEngine::lcSnapEngine)
                << "setWindowFloat: cannot unfloat" << windowId << "- no pre-float zone, keeping floating";
            return;
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Private helpers
// ═══════════════════════════════════════════════════════════════════════════════

bool SnapEngine::unfloatToZone(const QString& windowId, const QString& screenId)
{
    UnfloatResult unfloat = resolveUnfloatGeometry(windowId, screenId);
    if (!unfloat.found) {
        // No pre-float zone (a never-snapped window that defaulted to floating).
        // With the unfloatFallbackToZone setting on, snap it to a fallback zone
        // instead of refusing; otherwise return false so the caller keeps it
        // floating with feedback.
        unfloat = resolveFallbackUnfloatGeometry(windowId, screenId);
        if (!unfloat.found) {
            return false;
        }
    }

    // No saved-float entry to consume — the snap commit below re-captures the
    // window's snap slot as "snapped" in the unified record, so a future mode
    // transition restores it snapped, not floating (single source of truth).

    // Commit the snap via the unified orchestration. User-initiated because
    // the user just toggled float off — they want this snap to update the
    // last-used-zone tracking. commitSnap handles clearing floating state
    // (and emits windowFloatingClearedForSnap which WTA relays as
    // windowFloatingChanged), plus the zone assignment.
    if (unfloat.zoneIds.size() > 1) {
        commitMultiZoneSnap(windowId, unfloat.zoneIds, unfloat.screenId, SnapIntent::UserInitiated);
    } else {
        commitSnap(windowId, unfloat.zoneIds.first(), unfloat.screenId, SnapIntent::UserInitiated);
    }

    // Carry the (representative) zone id, NOT an empty string. The KWin effect's
    // applyGeometryRequested handler uses an empty zoneId as the "float-restore"
    // discriminator (→ clearWindowSnapped, which strips the snap title-bar /
    // border chrome) and a non-empty zoneId as the "snap commit" discriminator
    // (→ markWindowSnapped, which re-applies it). Unfloat-to-zone IS a snap
    // commit, so an empty zoneId here would leave the re-snapped window wearing
    // its floating chrome (no hidden title bar, no snap border).
    Q_EMIT applyGeometryRequested(windowId, unfloat.geometry.x(), unfloat.geometry.y(), unfloat.geometry.width(),
                                  unfloat.geometry.height(), unfloat.zoneIds.first(), unfloat.screenId, false);
    return true;
}

bool SnapEngine::applyGeometryForFloat(const QString& windowId, const QString& screenId)
{
    // Prefer the unified placement record's float-back geometry. It is the single
    // source of truth: appId-keyed (survives the uuid change on logout/login),
    // one-record-per-window, and — unlike the legacy m_unmanagedGeometries store —
    // not silently dropped on load by the disabled-context gate when the user has
    // toggled snapping off/on. The legacy store is consulted only as a fallback
    // for windows with no record yet (pre-migration / first float of the session).
    if (m_windowTracker) {
        const QString appId = m_windowTracker->currentAppIdFor(windowId);
        auto rec = m_windowTracker->placementStore().peek(windowId, appId);
        if (rec) {
            // The shared free/float geometry, per screen — never a zone/tile rect by
            // construction. Prefer this screen's remembered spot, else any captured
            // free spot.
            QRect g = rec->freeGeometryFor(screenId);
            if (!g.isValid()) {
                g = rec->anyFreeGeometry();
            }
            if (g.isValid()) {
                qCInfo(PhosphorSnapEngine::lcSnapEngine)
                    << "applyGeometryForFloat:" << windowId << "restoring to" << g << "(placement record)";
                Q_EMIT applyGeometryRequested(windowId, g.x(), g.y(), g.width(), g.height(), QString(), screenId,
                                              false);
                return true;
            }
            // A record exists but the window has no genuine free geometry yet (it has
            // only ever been snapped/tiled). Do NOT fall back to the legacy unmanaged
            // store — that may still hold a stale zone rect, which is exactly the
            // geometry leak this model removes. Leave the window where it is; the next
            // move while floating captures a real free position into the record.
            qCInfo(PhosphorSnapEngine::lcSnapEngine)
                << "applyGeometryForFloat:" << windowId << "no free geometry on record — leaving in place";
            return false;
        }
    }

    auto geo = m_windowTracker->validatedUnmanagedGeometry(windowId, screenId);
    if (geo) {
        qCInfo(PhosphorSnapEngine::lcSnapEngine)
            << "applyGeometryForFloat:" << windowId << "restoring to" << *geo << "(legacy unmanaged store)";
        Q_EMIT applyGeometryRequested(windowId, geo->x(), geo->y(), geo->width(), geo->height(), QString(), screenId,
                                      false);
        return true;
    }
    qCWarning(PhosphorSnapEngine::lcSnapEngine) << "applyGeometryForFloat:" << windowId << "no pre-tile geometry found";
    return false;
}

// SnapEngine::clearFloatingStateForSnap was removed — its two callers
// (windowOpened in lifecycle.cpp, unfloatToZone above) now go through
// SnapEngine::commitSnap which handles clearing floating
// state as step 1 of its orchestration. The D-Bus-visible behaviour is
// identical: commitSnap emits windowFloatingClearedForSnap, WTA relays
// it as windowFloatingChanged on the same D-Bus interface.

UnfloatResult SnapEngine::resolveUnfloatGeometry(const QString& windowId, const QString& fallbackScreen) const
{
    UnfloatResult result;

    QStringList zoneIds = m_windowTracker->preFloatZones(windowId);
    if (zoneIds.isEmpty()) {
        return result;
    }

    QString restoreScreen = m_windowTracker->preFloatScreen(windowId);
    if (!restoreScreen.isEmpty()) {
        restoreScreen = m_windowTracker->resolveEffectiveScreenId(restoreScreen);
        auto* mgr = m_windowTracker->screenManager();
        const bool screenExists = mgr ? mgr->physicalScreenFor(restoreScreen).isValid()
                                      : (PhosphorScreens::ScreenIdentity::findByIdOrName(restoreScreen) != nullptr);
        if (!screenExists) {
            restoreScreen.clear();
        }
    }
    if (restoreScreen.isEmpty() && !fallbackScreen.isEmpty()) {
        restoreScreen = m_windowTracker->resolveEffectiveScreenId(fallbackScreen);
    }

    QRect geo = m_windowTracker->resolveZoneGeometry(zoneIds, restoreScreen);
    if (!geo.isValid()) {
        return result;
    }

    result.found = true;
    result.zoneIds = zoneIds;
    result.geometry = geo;
    result.screenId = restoreScreen;
    return result;
}

UnfloatResult SnapEngine::resolveFallbackUnfloatGeometry(const QString& windowId, const QString& fallbackScreen) const
{
    UnfloatResult result;

    // Opt-in only: when the setting is off, a no-pre-float-zone unfloat leaves the
    // window floating (the caller emits feedback). The engine reads the bool via the
    // settings-agnostic ISnapSettings seam, like moveNewWindowsToLastZone.
    auto* s = snapSettings();
    if (!s || !s->unfloatFallbackToZone()) {
        return result;
    }

    // Resolve the window's effective screen — its tracked float screen, else the
    // caller's fallback. Zone geometry is resolved on THIS screen so the fallback
    // lands where the window currently is.
    QString screen = m_snapState->screenAssignments().value(windowId);
    if (screen.isEmpty()) {
        screen = fallbackScreen;
    }
    screen = m_windowTracker->resolveEffectiveScreenId(screen);
    if (screen.isEmpty() || !m_layoutManager) {
        return result;
    }
    PhosphorZones::Layout* layout = m_layoutManager->resolveLayoutForScreen(screen);
    if (!layout) {
        return result;
    }

    // Target resolution order: last-used zone (if it exists in this screen's layout)
    // → first empty zone → first zone in the layout. The last two reuse the same
    // accessors as the auto-snap chain (findEmptyZoneInLayout / zoneGeometry).
    QString zoneId;
    const QString lastUsed = m_snapState->lastUsedZoneId();
    if (!lastUsed.isEmpty() && m_windowTracker->zoneGeometry(lastUsed, screen).isValid()) {
        zoneId = lastUsed;
    }
    if (zoneId.isEmpty()) {
        const int desktopFilter = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;
        zoneId = m_windowTracker->findEmptyZoneInLayout(layout, screen, desktopFilter);
    }
    if (zoneId.isEmpty()) {
        // Final fallback: the first zone in the layout. May already be occupied —
        // snapping supports multiple windows per zone (stacking), so that is fine.
        const QVector<PhosphorZones::Zone*> zones = layout->zones();
        if (!zones.isEmpty() && zones.first()) {
            zoneId = zones.first()->id().toString();
        }
    }
    if (zoneId.isEmpty()) {
        return result;
    }

    const QRect geo = m_windowTracker->zoneGeometry(zoneId, screen);
    if (!geo.isValid()) {
        return result;
    }

    result.found = true;
    result.zoneIds = QStringList{zoneId};
    result.geometry = geo;
    result.screenId = screen;
    qCInfo(PhosphorSnapEngine::lcSnapEngine)
        << "resolveFallbackUnfloatGeometry:" << windowId << "→ zone" << zoneId << "on" << screen;
    return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Cross-engine handoff (see IPlacementEngine.h for contract)
// ═══════════════════════════════════════════════════════════════════════════════

void SnapEngine::handoffReceive(const HandoffContext& ctx)
{
    if (ctx.windowId.isEmpty() || ctx.toScreenId.isEmpty()) {
        return;
    }
    qCInfo(PhosphorSnapEngine::lcSnapEngine) << "SnapEngine::handoffReceive:" << ctx.windowId << "to" << ctx.toScreenId
                                             << "from" << ctx.fromEngineId << "wasFloating=" << ctx.wasFloating;

    if (!ctx.sourceZoneIds.isEmpty()) {
        QRect zoneGeo = m_windowTracker->resolveZoneGeometry(ctx.sourceZoneIds, ctx.toScreenId);
        if (zoneGeo.isValid()) {
            if (ctx.sourceZoneIds.size() > 1) {
                commitMultiZoneSnap(ctx.windowId, ctx.sourceZoneIds, ctx.toScreenId, SnapIntent::UserInitiated);
            } else {
                commitSnap(ctx.windowId, ctx.sourceZoneIds.first(), ctx.toScreenId, SnapIntent::UserInitiated);
            }
            // Non-empty zoneId so the effect routes this cross-engine snap to
            // markWindowSnapped (snap chrome), not clearWindowSnapped — see the
            // matching note in unfloatToZone().
            Q_EMIT applyGeometryRequested(ctx.windowId, zoneGeo.x(), zoneGeo.y(), zoneGeo.width(), zoneGeo.height(),
                                          ctx.sourceZoneIds.first(), ctx.toScreenId, false);
            return;
        }
    }

    const int currentDesktop = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;
    m_snapState->setFloatingOnScreen(ctx.windowId, ctx.toScreenId, currentDesktop);
    m_windowTracker->setWindowFloating(ctx.windowId, true);
    Q_EMIT windowFloatingChanged(ctx.windowId, true, ctx.toScreenId);
}

void SnapEngine::handoffRelease(const QString& windowId)
{
    if (windowId.isEmpty()) {
        return;
    }
    qCInfo(PhosphorSnapEngine::lcSnapEngine) << "SnapEngine::handoffRelease:" << windowId;

    if (m_snapState->isWindowSnapped(windowId)) {
        m_snapState->unassignWindow(windowId);
    }
    if (m_snapState->isFloating(windowId)) {
        m_snapState->setFloating(windowId, false);
    }
}

QString SnapEngine::screenForTrackedWindow(const QString& windowId) const
{
    return m_snapState->screenAssignments().value(windowId);
}

bool SnapEngine::isWindowTracked(const QString& windowId) const
{
    return m_snapState->isWindowSnapped(windowId) || m_snapState->isFloating(windowId)
        || m_snapState->screenAssignments().contains(windowId);
}

} // namespace PhosphorSnapEngine
