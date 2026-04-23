// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../snapadaptor.h"
#include "../windowtrackingadaptor.h"
#include "../../core/interfaces.h"
#include "../../core/logging.h"
#include "../../core/windowtrackingservice.h"
#include <PhosphorScreens/Manager.h>
#include "../../core/isettings.h"
#include "../../snap/SnapEngine.h"

namespace PlasmaZones {

namespace {
// Non-blocking startup gate shared by all synchronous snap D-Bus methods.
//
// Rationale: these slots return zone geometry synchronously to the KWin effect.
// Before the first panel D-Bus query completes, Phosphor::Screens::ScreenManager's availability cache
// is empty and zones would be computed against the unreserved full-screen rect —
// handing the effect coordinates that place the window partially behind the panel.
//
// The effect already waits for WindowTrackingAdaptor::pendingRestoresAvailable before
// issuing initial restore calls, and that signal is gated on panelGeometryReady (see
// tryEmitPendingRestoresAvailable in persistence.cpp). This helper is belt-and-suspenders:
// if a snap slot is nevertheless invoked before panel geometry is known — a bug in the
// effect-side ordering, a programmatic D-Bus client, or a future refactor — we log once
// per session and return shouldSnap=false rather than handing back wrong coordinates.
// The effect treats shouldSnap=false as "no snap" and leaves the window where KWin placed
// it, which is the same fallback as if the slot had never been called.
bool isSnapReadyOrWarn(WindowTrackingService* service, const char* method)
{
    auto* mgr = service ? service->screenManager() : nullptr;
    if (!mgr || mgr->isPanelGeometryReady()) {
        return true;
    }
    static bool warned = false;
    if (!warned) {
        warned = true;
        qCWarning(lcDbusWindow) << method << "called before panel geometry ready — returning no-snap."
                                << "The KWin effect should gate restore calls on pendingRestoresAvailable;"
                                << "if you see this, the effect-side gate was bypassed or is racing startup.";
    } else {
        qCDebug(lcDbusWindow) << method << "called before panel geometry ready — returning no-snap";
    }
    return false;
}
} // namespace

void SnapAdaptor::snapToLastZone(const QString& windowId, const QString& windowScreenId, bool sticky, int& snapX,
                                 int& snapY, int& snapWidth, int& snapHeight, bool& shouldSnap)
{
    snapX = snapY = snapWidth = snapHeight = 0;
    shouldSnap = false;

    if (!m_adaptor || !m_adaptor->service()) {
        return;
    }

    if (!isSnapReadyOrWarn(m_adaptor->service(), "snapToLastZone")) {
        return;
    }

    if (!m_engine) {
        return;
    }

    SnapResult result = m_engine->calculateSnapToLastZone(windowId, windowScreenId, sticky);
    if (!result.shouldSnap) {
        return;
    }

    applySnapResult(result, windowId, snapX, snapY, snapWidth, snapHeight, shouldSnap);
    qCInfo(lcDbusWindow) << "Snapping new window" << windowId << "to last used zone" << result.zoneId;
}

void SnapAdaptor::snapToAppRule(const QString& windowId, const QString& windowScreenName, bool sticky, int& snapX,
                                int& snapY, int& snapWidth, int& snapHeight, bool& shouldSnap)
{
    snapX = snapY = snapWidth = snapHeight = 0;
    shouldSnap = false;

    if (windowId.isEmpty()) {
        return;
    }

    if (!m_adaptor || !m_adaptor->service()) {
        return;
    }

    if (!isSnapReadyOrWarn(m_adaptor->service(), "snapToAppRule")) {
        return;
    }

    if (!m_engine) {
        return;
    }

    SnapResult result = m_engine->calculateSnapToAppRule(windowId, windowScreenName, sticky);
    if (!result.shouldSnap) {
        return;
    }

    applySnapResult(result, windowId, snapX, snapY, snapWidth, snapHeight, shouldSnap);
    qCInfo(lcDbusWindow) << "App rule snapping window" << windowId << "to zone" << result.zoneId;
}

void SnapAdaptor::snapToEmptyZone(const QString& windowId, const QString& windowScreenId, bool sticky, int& snapX,
                                  int& snapY, int& snapWidth, int& snapHeight, bool& shouldSnap)
{
    snapX = snapY = snapWidth = snapHeight = 0;
    shouldSnap = false;

    if (windowId.isEmpty()) {
        return;
    }

    if (!m_adaptor || !m_adaptor->service()) {
        return;
    }

    if (!isSnapReadyOrWarn(m_adaptor->service(), "snapToEmptyZone")) {
        return;
    }

    if (!m_engine) {
        return;
    }

    qCDebug(lcDbusWindow) << "snapToEmptyZone: windowId=" << windowId << "screen=" << windowScreenId;
    SnapResult result = m_engine->calculateSnapToEmptyZone(windowId, windowScreenId, sticky);
    if (!result.shouldSnap) {
        qCDebug(lcDbusWindow) << "snapToEmptyZone: no snap";
        return;
    }

    applySnapResult(result, windowId, snapX, snapY, snapWidth, snapHeight, shouldSnap);
    qCInfo(lcDbusWindow) << "Auto-assign snapping window" << windowId << "to empty zone" << result.zoneId;
}

void SnapAdaptor::restoreToPersistedZone(const QString& windowId, const QString& screenId, bool sticky, int& snapX,
                                         int& snapY, int& snapWidth, int& snapHeight, bool& shouldRestore)
{
    snapX = snapY = snapWidth = snapHeight = 0;
    shouldRestore = false;

    if (!m_adaptor || !m_adaptor->service()) {
        return;
    }

    if (m_settings && !m_settings->restoreWindowsToZonesOnLogin()) {
        qCDebug(lcDbusWindow) << "Session zone restoration disabled by setting";
        return;
    }

    if (windowId.isEmpty()) {
        return;
    }

    if (!isSnapReadyOrWarn(m_adaptor->service(), "restoreToPersistedZone")) {
        return;
    }

    if (!m_engine) {
        return;
    }

    SnapResult result = m_engine->calculateRestoreFromSession(windowId, screenId, sticky);
    if (!result.shouldSnap) {
        return;
    }

    applySnapResult(result, windowId, snapX, snapY, snapWidth, snapHeight, shouldRestore);
    // Consume the pending assignment so other windows of the same class won't restore to this zone
    m_adaptor->service()->consumePendingAssignment(windowId);
    qCInfo(lcDbusWindow) << "Restoring window" << windowId << "to zone(s)" << result.zoneIds;
}

void SnapAdaptor::resolveWindowRestore(const QString& windowId, const QString& screenId, bool sticky, int& snapX,
                                       int& snapY, int& snapWidth, int& snapHeight, bool& shouldSnap)
{
    snapX = snapY = snapWidth = snapHeight = 0;
    shouldSnap = false;

    if (windowId.isEmpty() || screenId.isEmpty()) {
        return;
    }

    if (!m_engine) {
        qCWarning(lcDbusWindow) << "resolveWindowRestore: no SnapEngine available";
        return;
    }

    if (!m_adaptor || !m_adaptor->service()) {
        return;
    }

    if (!isSnapReadyOrWarn(m_adaptor->service(), "resolveWindowRestore")) {
        return;
    }

    SnapResult result = m_engine->resolveWindowRestore(windowId, screenId, sticky);
    if (!result.shouldSnap) {
        return;
    }

    applySnapResult(result, windowId, snapX, snapY, snapWidth, snapHeight, shouldSnap);
}

void SnapAdaptor::applySnapResult(const SnapResult& result, const QString& windowId, int& snapX, int& snapY,
                                  int& snapWidth, int& snapHeight, bool& shouldSnap)
{
    snapX = result.geometry.x();
    snapY = result.geometry.y();
    snapWidth = result.geometry.width();
    snapHeight = result.geometry.height();
    shouldSnap = true;

    if (!m_adaptor || !m_adaptor->service() || !m_engine) {
        return;
    }

    // Mark auto-snapped first so the flag persists through commitSnap
    // (AutoRestored leaves it alone). commitSnap runs the full
    // orchestration — clears any pre-existing floating state (emits
    // windowFloatingClearedForSnap which the adaptor relays as
    // windowFloatingChanged), assigns to zone(s), emits state change.
    m_adaptor->service()->markAsAutoSnapped(windowId);
    const QStringList zoneIds = result.zoneIds.isEmpty() ? QStringList{result.zoneId} : result.zoneIds;
    if (zoneIds.size() > 1) {
        m_engine->commitMultiZoneSnap(windowId, zoneIds, result.screenId, SnapIntent::AutoRestored);
    } else {
        m_engine->commitSnap(windowId, zoneIds.first(), result.screenId, SnapIntent::AutoRestored);
    }
}

} // namespace PlasmaZones
