// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "snapadaptor.h"
#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"
#include "dbus/windowtrackingadaptor/internal.h"
#include "core/platform/logging.h"
#include <PhosphorEngine/WindowPlacement.h>
#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorZones/LayoutRegistry.h>

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════════
// Snap confirmations from the effect and external callers: each is checked by
// validBusSnapTarget (busgate.cpp), then committed through the engine, which
// owns the orchestration (clear floating and the auto-snapped flag, assign,
// last-used tracking, the state-change signal). They confirm placements already
// applied, so no policy gate.
// ═══════════════════════════════════════════════════════════════════════════════

void SnapAdaptor::windowSnapped(const QString& windowId, const QString& zoneId, const QString& screenId)
{
    if (!validateWindowId(windowId, QStringLiteral("track window snap"))) {
        return;
    }
    // A live window, a known screen and a zone its layout holds (F459).
    const std::optional<BusSnapTarget> target = validBusSnapTarget(windowId, {zoneId}, screenId);
    if (!target) {
        return;
    }
    m_engine->commitSnap(windowId, zoneId, target->screenId, PhosphorEngine::SnapIntent::UserInitiated,
                         target->desktop);
}

void SnapAdaptor::windowSnappedMultiZone(const QString& windowId, const QStringList& zoneIds, const QString& screenId)
{
    if (!validateWindowId(windowId, QStringLiteral("track multi-zone window snap"))) {
        return;
    }
    // Every member checked, not only the first (F459).
    const std::optional<BusSnapTarget> target = validBusSnapTarget(windowId, zoneIds, screenId);
    if (!target) {
        return;
    }
    m_engine->commitMultiZoneSnap(windowId, zoneIds, target->screenId, PhosphorEngine::SnapIntent::UserInitiated,
                                  target->desktop);
}

void SnapAdaptor::windowUnsnapped(const QString& windowId)
{
    if (!validateWindowId(windowId, QStringLiteral("untrack window"))) {
        return;
    }
    if (!m_engine) {
        return;
    }
    m_engine->uncommitSnap(windowId);
    releaseSnapSlotIfFree(windowId);
}

void SnapAdaptor::releaseSnapSlotIfFree(const QString& windowId)
{
    // With no store holding a zone or the float bit for the window, the
    // record's snap slot goes too, or a reopen would restore the zone just left.
    if (m_adaptor && m_adaptor->service() && !m_engine->isFloating(windowId)
        && WindowTrackingInternal::snapZoneScreen(m_engine, windowId, QString()).isEmpty()) {
        m_adaptor->service()->releaseEngineSlot(windowId, PhosphorEngine::WindowPlacement::snapEngineId());
    }
}

void SnapAdaptor::windowsSnappedBatch(const PhosphorProtocol::SnapConfirmationList& entries)
{
    qCInfo(lcDbusWindow) << "windowsSnappedBatch: processing" << entries.size() << "entries";

    for (const auto& entry : entries) {
        if (entry.windowId.isEmpty()) {
            continue;
        }

        if (entry.isRestore) {
            // Unsnap a live window and drop its float-back on that screen, as
            // the engine's RestoreSentinel arm does; every screen's only when
            // the screen cannot be resolved (F366).
            if (!m_engine || !m_adaptor || !m_adaptor->service() || !m_adaptor->isRegistryTracked(entry.windowId)) {
                continue;
            }
            m_engine->uncommitSnap(entry.windowId);
            const QString screen = m_adaptor->resolveBusScreen(entry.screenId, entry.windowId);
            if (screen.isEmpty()) {
                m_adaptor->service()->clearFreeGeometry(entry.windowId);
            } else {
                m_adaptor->service()->clearFreeGeometry(entry.windowId, screen);
            }
            releaseSnapSlotIfFree(entry.windowId);
        } else {
            windowSnapped(entry.windowId, entry.zoneId, entry.screenId);
            // The snap-all confirmation, a user snap like any shortcut's.
            recordSnapIntent(entry.windowId, true);
        }
    }
}

void SnapAdaptor::recordSnapIntent(const QString& windowId, bool wasUserInitiated)
{
    if (windowId.isEmpty()) {
        return;
    }
    if (!m_adaptor || !m_adaptor->service()) {
        return;
    }
    m_adaptor->service()->recordSnapIntent(windowId, wasUserInitiated);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Snap-mode convenience D-Bus slots
//
// They gate through admitBusSnap (busgate.cpp), record the free frame and the
// intent, then commit and ask the effect for the geometry (applyGeometryRequested).
// ═══════════════════════════════════════════════════════════════════════════════

void SnapAdaptor::moveWindowToZone(const QString& windowId, const QString& zoneId)
{
    moveWindowToZoneOnScreen(windowId, zoneId, QString());
}

void SnapAdaptor::moveWindowToZoneOnScreen(const QString& windowId, const QString& zoneId, const QString& screenHint)
{
    if (!validateWindowId(windowId, QStringLiteral("moveWindowToZone"))) {
        return;
    }

    if (zoneId.isEmpty()) {
        qCWarning(lcDbusWindow) << "moveWindowToZone: empty zone ID";
        return;
    }

    // Lands only where a keyboard snap could: a live window, a screen the
    // daemon knows whose layout holds the zone, on the window's desktop, with
    // snapping running there and switched on (admitBusSnap).
    const std::optional<BusSnapTarget> target = admitBusSnap(windowId, {zoneId}, screenHint);
    if (!target) {
        return;
    }
    const QString screenId = target->screenId;
    const QRect geo = m_adaptor->service()->zoneGeometry(zoneId, screenId);
    if (!geo.isValid()) {
        qCWarning(lcDbusWindow) << "moveWindowToZone: invalid geometry for zone" << zoneId << "on" << screenId;
        return;
    }

    // The float-back of a window that is not in a zone, recorded before the
    // commit that would make the effect's own pre-snap capture refused. The
    // helper files it under the screen the frame is on.
    m_engine->recordFreeFrameBeforeUserSnap(windowId, screenId);

    // Committed on the desktop the window is on, which is not the one in view
    // for a window on a hidden desktop (F179).
    m_engine->commitSnap(windowId, zoneId, screenId, PhosphorEngine::SnapIntent::UserInitiated, target->desktop);
    m_adaptor->service()->recordSnapIntent(windowId, true);

    // Request compositor to apply geometry
    Q_EMIT m_adaptor->applyGeometryRequested(windowId, geo.x(), geo.y(), geo.width(), geo.height(), zoneId, screenId,
                                             false, static_cast<int>(PhosphorProtocol::PlacementPurpose::UserVerb));

    qCInfo(lcDbusWindow) << "moveWindowToZone:" << windowId << "-> zone" << zoneId << "on screen" << screenId;
}

void SnapAdaptor::swapWindowsById(const QString& windowId1, const QString& windowId2)
{
    if (!validateWindowId(windowId1, QStringLiteral("swapWindowsById (window1)"))) {
        return;
    }
    if (!validateWindowId(windowId2, QStringLiteral("swapWindowsById (window2)"))) {
        return;
    }
    if (windowId1 == windowId2) {
        qCWarning(lcDbusWindow) << "swapWindowsById: cannot swap window with itself:" << windowId1;
        return;
    }

    if (!m_adaptor || !m_adaptor->service() || !m_engine) {
        return;
    }

    auto* svc = m_adaptor->service();

    // Each window takes the other's whole span, in the context that span is
    // held in, through the gate a single bus snap passes (F79, F179). A span in
    // a store with no desktop identity lands on the window's own desktop.
    const QStringList zones1 = svc->zonesForWindow(windowId1);
    const QStringList zones2 = svc->zonesForWindow(windowId2);
    if (zones1.isEmpty() || zones2.isEmpty()) {
        qCWarning(lcDbusWindow) << "swapWindowsById: one or both windows not snapped"
                                << "w1:" << windowId1 << "zones:" << zones1 << "w2:" << windowId2 << "zones:" << zones2;
        return;
    }
    PhosphorZones::LayoutRegistry* const layouts = m_adaptor->layoutRegistry();
    const QString activity = layouts ? layouts->currentActivity() : QString();
    const auto heldContext = [&](const QString& windowId) -> std::optional<PhosphorEngine::PlacementStateKey> {
        const std::optional<PhosphorEngine::PlacementStateKey> key = m_engine->heldKeyForWindow(windowId);
        if (!key) {
            return PhosphorEngine::PlacementStateKey{svc->screenForWindow(windowId), 0, QString()};
        }
        // A commit pins the current activity's store, so a span held under
        // another activity cannot be handed over.
        if (!activity.isEmpty() && !key->activity.isEmpty() && key->activity != activity) {
            qCInfo(lcDbusWindow) << "swapWindowsById refused:" << windowId << "is held on activity" << key->activity;
            return std::nullopt;
        }
        return key;
    };
    const std::optional<PhosphorEngine::PlacementStateKey> held1 = heldContext(windowId1);
    const std::optional<PhosphorEngine::PlacementStateKey> held2 = heldContext(windowId2);
    if (!held1 || !held2 || held1->screenId.isEmpty() || held2->screenId.isEmpty()) {
        return;
    }
    const std::optional<BusSnapTarget> target1 = admitBusSnap(windowId1, zones2, held2->screenId, held2->desktop);
    const std::optional<BusSnapTarget> target2 = admitBusSnap(windowId2, zones1, held1->screenId, held1->desktop);
    if (!target1 || !target2) {
        return;
    }

    const QRect geo1 = svc->resolveZoneGeometry(zones2, target1->screenId);
    const QRect geo2 = svc->resolveZoneGeometry(zones1, target2->screenId);
    if (!geo1.isValid() || !geo2.isValid()) {
        qCWarning(lcDbusWindow) << "swapWindowsById: invalid geometry:" << windowId1 << zones2 << "on"
                                << target1->screenId << geo1 << "and" << windowId2 << zones1 << "on"
                                << target2->screenId << geo2;
        return;
    }

    const auto commit = [this](const QString& windowId, const QStringList& zones, const BusSnapTarget& target) {
        if (zones.size() > 1) {
            m_engine->commitMultiZoneSnap(windowId, zones, target.screenId, PhosphorEngine::SnapIntent::UserInitiated,
                                          target.desktop);
        } else {
            m_engine->commitSnap(windowId, zones.first(), target.screenId, PhosphorEngine::SnapIntent::UserInitiated,
                                 target.desktop);
        }
    };
    commit(windowId1, zones2, *target1);
    commit(windowId2, zones1, *target2);
    recordSnapIntent(windowId1, true);
    recordSnapIntent(windowId2, true);

    // Emit geometry requests for both
    Q_EMIT m_adaptor->applyGeometryRequested(windowId1, geo1.x(), geo1.y(), geo1.width(), geo1.height(), zones2.first(),
                                             target1->screenId, false,
                                             static_cast<int>(PhosphorProtocol::PlacementPurpose::UserVerb));
    Q_EMIT m_adaptor->applyGeometryRequested(windowId2, geo2.x(), geo2.y(), geo2.width(), geo2.height(), zones1.first(),
                                             target2->screenId, false,
                                             static_cast<int>(PhosphorProtocol::PlacementPurpose::Restatement));

    qCInfo(lcDbusWindow) << "swapWindowsById:" << windowId1 << "<->" << windowId2 << "zones:" << zones1 << "<->"
                         << zones2;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Snap-mode float D-Bus slots
//
// Snap-mode only, gated through admitBusFloat (busgate.cpp). The effect floats
// through WindowTrackingAdaptor::setWindowFloatingForScreen, which routes to
// whichever engine owns the screen.
// ═══════════════════════════════════════════════════════════════════════════════

void SnapAdaptor::toggleFloatForWindow(const QString& windowId, const QString& screenId)
{
    qCInfo(lcDbusWindow) << "toggleFloatForWindow: windowId=" << windowId << "screen=" << screenId;

    if (!validateWindowId(windowId, QStringLiteral("toggle float"))) {
        if (m_adaptor) {
            Q_EMIT m_adaptor->navigationFeedback(false, QStringLiteral("float"), QStringLiteral("invalid_window"),
                                                 QString(), QString(), screenId);
        }
        return;
    }

    const QString screen = admitBusFloat(windowId, screenId);
    if (!screen.isEmpty()) {
        m_engine->toggleWindowFloat(windowId, screen);
    }
}

void SnapAdaptor::setWindowFloat(const QString& windowId, bool floating)
{
    if (!validateWindowId(windowId, QStringLiteral("set snap float"))) {
        return;
    }

    // The window's live screen, not the last focused one (F28).
    const QString screen = admitBusFloat(windowId, QString());
    if (!screen.isEmpty()) {
        m_engine->setWindowFloat(windowId, floating, screen);
    }
}

PhosphorProtocol::UnfloatRestoreResult SnapAdaptor::calculateUnfloatRestore(const QString& windowId,
                                                                            const QString& screenId)
{
    if (windowId.isEmpty()) {
        return PhosphorProtocol::UnfloatRestoreResult{};
    }

    if (!m_engine) {
        return PhosphorProtocol::UnfloatRestoreResult{};
    }

    UnfloatResult unfloat = m_engine->resolveUnfloatGeometry(windowId, screenId);
    if (!unfloat.found) {
        // The unfloatFallbackToZone setting, as SnapEngine::unfloatToZone
        // honours it. Unlike the live toggle this skips the SnapToZone rule
        // tier, which that path consults first.
        unfloat = m_engine->resolveFallbackUnfloatGeometry(windowId, screenId);
    }
    if (!unfloat.found) {
        qCDebug(lcDbusWindow) << "calculateUnfloatRestore: no restore target for" << windowId;
        return PhosphorProtocol::UnfloatRestoreResult{};
    }

    qCDebug(lcDbusWindow) << "calculateUnfloatRestore for" << windowId << "-> zones:" << unfloat.zoneIds
                          << "geo:" << unfloat.geometry;
    return PhosphorProtocol::UnfloatRestoreResult{
        true,
        unfloat.zoneIds,
        unfloat.screenId,
        unfloat.geometry.x(),
        unfloat.geometry.y(),
        unfloat.geometry.width(),
        unfloat.geometry.height(),
    };
}

void SnapAdaptor::windowUnsnappedForFloat(const QString& windowId)
{
    if (!validateWindowId(windowId, QStringLiteral("prepare float"))) {
        return;
    }

    if (!m_adaptor || !m_adaptor->service()) {
        return;
    }

    QString previousZoneId = m_adaptor->service()->zoneForWindow(windowId);
    if (previousZoneId.isEmpty()) {
        // Window was not snapped - no-op
        qCDebug(lcDbusWindow) << "windowUnsnappedForFloat: window not in any zone:" << windowId;
        return;
    }

    // Delegate to service
    m_adaptor->service()->unsnapForFloat(windowId);

    qCInfo(lcDbusWindow) << "Window" << windowId << "unsnapped for float from zone" << previousZoneId;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Private helpers
// ═══════════════════════════════════════════════════════════════════════════════

bool SnapAdaptor::validateWindowId(const QString& windowId, const QString& operation) const
{
    if (windowId.isEmpty()) {
        qCWarning(lcDbusWindow) << "Cannot" << operation << "- empty window ID";
        return false;
    }
    return true;
}

} // namespace PlasmaZones
