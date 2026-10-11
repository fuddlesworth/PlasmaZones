// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "snapadaptor.h"
#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"
#include "core/platform/logging.h"
#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorEngine/EngineTypes.h>
#include <PhosphorEngine/GeometryUtils.h>
#include <PhosphorIdentity/VirtualScreenId.h>

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════════
// Snap-mode navigation D-Bus slots: the focused-window verbs, gated as the
// keyboard twin gates them (focusedVerbPermitted), then forwarded to SnapEngine.
// ═══════════════════════════════════════════════════════════════════════════════

void SnapAdaptor::moveWindowToAdjacentZone(const QString& direction)
{
    if (focusedVerbPermitted(QString(), true)) {
        m_engine->moveFocusedInDirection(direction, NavigationContext{});
    }
}

void SnapAdaptor::focusAdjacentZone(const QString& direction)
{
    if (focusedVerbPermitted(QString(), false)) {
        m_engine->focusInDirection(direction, NavigationContext{});
    }
}

void SnapAdaptor::pushToEmptyZone(const QString& screenId)
{
    if (screenId.isEmpty()) {
        if (focusedVerbPermitted(QString(), true)) {
            m_engine->pushFocusedToEmptyZone(NavigationContext{});
        }
        return;
    }
    // A named screen is the target: its first empty zone, through the bus
    // snap gate (F362).
    if (!m_adaptor || !m_adaptor->service()) {
        return;
    }
    const QString windowId = m_adaptor->lastActiveWindowId();
    const QString screen = m_adaptor->resolveBusScreen(screenId, windowId);
    if (windowId.isEmpty() || screen.isEmpty()) {
        Q_EMIT m_adaptor->navigationFeedback(false, QStringLiteral("push"),
                                             windowId.isEmpty() ? QStringLiteral("no_window")
                                                                : QStringLiteral("unknown_screen"),
                                             QString(), QString(), screenId);
        return;
    }
    const QString emptyZone = m_adaptor->service()->findEmptyZone(screen);
    if (emptyZone.isEmpty()) {
        Q_EMIT m_adaptor->navigationFeedback(false, QStringLiteral("push"), QStringLiteral("no_empty_zone"), QString(),
                                             QString(), screen);
        return;
    }
    moveWindowToZoneOnScreen(windowId, emptyZone, screen);
}

void SnapAdaptor::restoreWindowSize()
{
    if (focusedVerbPermitted(QString(), true)) {
        m_engine->restoreFocusedWindow(NavigationContext{});
    }
}

void SnapAdaptor::swapWindowWithAdjacentZone(const QString& direction)
{
    if (focusedVerbPermitted(QString(), true)) {
        m_engine->swapFocusedInDirection(direction, NavigationContext{});
    }
}

void SnapAdaptor::snapToZoneByNumber(int zoneNumber, const QString& screenId)
{
    if (screenId.isEmpty()) {
        // Like the shortcut: the focused window's own screen when it is
        // snapped, else the cursor's, else the last active one.
        if (focusedVerbPermitted(QString(), true)) {
            m_engine->moveFocusedToPosition(zoneNumber, NavigationContext{});
        }
        return;
    }
    // A named screen is the target, which the shell's placement map names
    // (F362), and its layout may have more than nine zones (F85).
    if (!m_adaptor) {
        return;
    }
    const QString windowId = m_adaptor->lastActiveWindowId();
    if (windowId.isEmpty()) {
        Q_EMIT m_adaptor->navigationFeedback(false, QStringLiteral("snap"), QStringLiteral("no_window"), QString(),
                                             QString(), screenId);
        return;
    }
    moveWindowToZoneNumberOnScreen(windowId, zoneNumber, screenId);
}

void SnapAdaptor::rotateWindowsInLayout(bool clockwise, const QString& screenId)
{
    if (!m_engine || !m_adaptor) {
        return;
    }
    // A named screen must be one the daemon knows; the engine refuses one
    // snapping does not run (F367).
    const QString screen = screenId.isEmpty() ? QString() : m_adaptor->resolveBusScreen(screenId);
    if (!screenId.isEmpty() && screen.isEmpty()) {
        qCInfo(lcDbusWindow) << "Bus rotate refused: unknown screen" << screenId;
        return;
    }
    m_engine->rotateWindowsInLayout(clockwise, screen);
}

void SnapAdaptor::cycleWindowsInZone(bool forward)
{
    if (focusedVerbPermitted(QString(), false)) {
        m_engine->cycleFocus(forward, NavigationContext{});
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Batch helper used by methods that operate on pre-built
// ZoneAssignmentEntry vectors (resnapForVirtualScreenReconfigure,
// handleBatchedResnap). Thin wrapper over
// SnapEngine::applyBatchAssignments.
// ═══════════════════════════════════════════════════════════════════════════════
static bool processBatchEntries(WindowTrackingAdaptor* wta, PhosphorSnapEngine::SnapEngine* engine,
                                const QVector<ZoneAssignmentEntry>& entries, const QString& action)
{
    if (!engine || !wta) {
        return false;
    }

    PhosphorProtocol::WindowGeometryList geometries =
        engine->applyBatchAssignments(entries, SnapIntent::UserInitiated, [wta]() -> QString {
            const QString cursor = wta->lastCursorScreenName();
            return cursor.isEmpty() ? wta->lastActiveScreenName() : cursor;
        });

    if (geometries.isEmpty()) {
        return false;
    }
    Q_EMIT wta->applyGeometriesBatch(geometries, action);
    return true;
}

/// A batch whose every entry re-states a placement (the desktop carry, the
/// re-apply on a switch) is relayed as "restate": no Snap Assist (F461).
static void processResnapEntries(WindowTrackingAdaptor* wta, PhosphorSnapEngine::SnapEngine* engine,
                                 const QVector<ZoneAssignmentEntry>& entries)
{
    const bool restatement = !entries.isEmpty() && std::all_of(entries.cbegin(), entries.cend(), [](const auto& e) {
        return e.restatement;
    });
    processBatchEntries(wta, engine, entries, restatement ? QStringLiteral("restate") : QStringLiteral("resnap"));
}

QStringList SnapAdaptor::resolveSnapModeScreensForResnap(const QString& screenFilter) const
{
    if (!m_adaptor) {
        return {};
    }
    return m_adaptor->resolveSnapModeScreensForResnap(screenFilter);
}

void SnapAdaptor::resnapToNewLayout()
{
    if (m_engine) {
        m_engine->resnapToNewLayout();
    }
}

void SnapAdaptor::resnapCurrentAssignments(const QString& screenFilter)
{
    if (!m_engine || !m_adaptor) {
        return;
    }
    // Only screens snapping runs, switched on and not disabled in the context
    // in view (resolveSnapModeScreensForResnap). A physical filter keeps its
    // whole monitor.
    QString scope;
    if (!screenFilter.isEmpty()) {
        const QString resolved = m_adaptor->resolveBusScreen(screenFilter);
        if (resolved.isEmpty()) {
            qCInfo(lcDbusWindow) << "Bus resnap refused: unknown screen" << screenFilter;
            return;
        }
        scope = PhosphorIdentity::VirtualScreenId::isVirtual(screenFilter)
            ? resolved
            : PhosphorIdentity::VirtualScreenId::extractPhysicalId(resolved);
    }
    QVector<ZoneAssignmentEntry> entries;
    for (const QString& screenId : resolveSnapModeScreensForResnap(scope)) {
        if (m_engine->isActiveOnScreen(screenId)) {
            entries.append(m_engine->calculateResnapFromCurrentAssignments(screenId));
        }
    }
    if (entries.isEmpty()) {
        Q_EMIT m_adaptor->navigationFeedback(false, QStringLiteral("resnap"), QStringLiteral("no_windows_to_resnap"),
                                             QString(), QString(),
                                             scope.isEmpty() ? m_adaptor->lastActiveScreenName() : scope);
        return;
    }
    processBatchEntries(m_adaptor, m_engine, entries, QStringLiteral("resnap"));
}

void SnapAdaptor::reapplySnapZones(const QStringList& screens, bool restatement)
{
    if (!m_engine) {
        return;
    }
    QVector<ZoneAssignmentEntry> entries;
    for (const QString& screenId : resolveSnapModeScreensForResnap(QString())) {
        if (screens.isEmpty() || screens.contains(screenId)) {
            entries.append(m_engine->calculateResnapFromCurrentAssignments(screenId));
        }
    }
    for (ZoneAssignmentEntry& entry : entries) {
        entry.restatement = restatement;
    }
    processBatchEntries(m_adaptor, m_engine, entries,
                        restatement ? QStringLiteral("restate") : QStringLiteral("resnap"));
}

void SnapAdaptor::resnapFromAutotileOrder(const QStringList& autotileWindowOrder, const QString& screenId)
{
    qCDebug(lcDbusWindow) << "resnapFromAutotileOrder: count=" << autotileWindowOrder.size() << "screen=" << screenId;

    if (!m_engine || !m_adaptor) {
        return;
    }
    // Only on a known screen snapping runs and the user has not disabled, and
    // only live windows snapping does not leave alone there.
    const QString screen = m_adaptor->resolveBusScreen(screenId);
    if (screen.isEmpty() || !m_engine->isActiveOnScreen(screen) || !snapPermittedForContext(QString(), screen, 0)) {
        qCInfo(lcDbusWindow) << "Bus resnap from autotile order refused on" << screenId;
        return;
    }
    QStringList order;
    for (const QString& windowId : autotileWindowOrder) {
        if (m_adaptor->isRegistryTracked(windowId) && !m_engine->isWindowExcluded(windowId, screen)) {
            order.append(windowId);
        }
    }
    const QVector<ZoneAssignmentEntry> entries = m_engine->calculateResnapEntriesFromAutotileOrder(order, screen);
    if (!entries.isEmpty()) {
        processBatchEntries(m_adaptor, m_engine, entries, QStringLiteral("resnap"));
    }
}

void SnapAdaptor::resnapForVirtualScreenReconfigure(const QString& physicalScreenId)
{
    qCDebug(lcDbusWindow) << "resnapForVirtualScreenReconfigure: physId=" << physicalScreenId;

    if (!m_engine) {
        return;
    }

    const QStringList snapScreens = resolveSnapModeScreensForResnap(physicalScreenId);
    QVector<ZoneAssignmentEntry> entries;
    for (const QString& sid : snapScreens) {
        entries.append(m_engine->calculateResnapFromCurrentAssignments(sid));
    }

    if (entries.isEmpty()) {
        return;
    }

    // Tagged "vs_reconfigure" so the kwin-effect does NOT fire snap-assist
    // continuation — no user-initiated snap happened here, windows are just
    // following their VS's new geometry after a swap/rotate/split edit.
    processBatchEntries(m_adaptor, m_engine, entries, QStringLiteral("vs_reconfigure"));
}

PhosphorProtocol::SnapAllResultList SnapAdaptor::calculateSnapAllWindows(const QStringList& windowIds,
                                                                         const QString& screenId)
{
    qCDebug(lcDbusWindow) << "calculateSnapAllWindows: count=" << windowIds.size() << "screen=" << screenId;
    if (!m_engine) {
        return {};
    }
    const PhosphorProtocol::SnapAllResultList results = m_engine->calculateSnapAllWindows(windowIds, screenId);
    // Each candidate's free frame, while the shadow still holds it: the effect
    // applies the zone geometry only after this reply, and its own pre-snap
    // capture then meets a window already in a zone.
    for (const PhosphorProtocol::SnapAllResultEntry& r : results) {
        m_engine->recordFreeFrameBeforeUserSnap(r.windowId, screenId);
    }
    return results;
}

void SnapAdaptor::snapAllWindows(const QString& screenId)
{
    qCDebug(lcDbusWindow) << "snapAllWindows: screen=" << screenId;
    if (m_engine) {
        m_engine->snapAllWindows(screenId);
    }
}

void SnapAdaptor::applyEngineResnap(const QString& resnapData)
{
    // Deserialize through the lib-side pair of serializeZoneAssignments — one
    // serializer, one deserializer, shared JsonKeys, so the two sides cannot
    // drift and the parse is unit-testable next to the serializer.
    QString parseError;
    const QVector<ZoneAssignmentEntry> entries =
        PhosphorEngine::GeometryUtils::deserializeZoneAssignments(resnapData, &parseError);
    if (!parseError.isEmpty()) {
        qCWarning(lcDbusWindow) << "applyEngineResnap: invalid JSON:" << parseError;
        return;
    }
    processResnapEntries(m_adaptor, m_engine, entries);
}

void SnapAdaptor::handleBatchedResnap(const QString& resnapData)
{
    if (!m_engine || !m_adaptor) {
        return;
    }
    QString parseError;
    const QVector<ZoneAssignmentEntry> entries =
        PhosphorEngine::GeometryUtils::deserializeZoneAssignments(resnapData, &parseError);
    if (!parseError.isEmpty()) {
        qCWarning(lcDbusWindow) << "handleBatchedResnap: invalid JSON:" << parseError;
        return;
    }
    // An external batch commits only what a bus snap could: a live window, a
    // known screen, a desktop that exists, and a landing the snap gate admits
    // (an unsnap entry only on a screen snapping runs).
    const int desktops = m_adaptor->desktopCount();
    QVector<ZoneAssignmentEntry> admitted;
    for (ZoneAssignmentEntry entry : entries) {
        if (!m_adaptor->isRegistryTracked(entry.windowId) || (desktops > 0 && entry.virtualDesktop > desktops)) {
            continue;
        }
        const QString screen = m_adaptor->resolveBusScreen(entry.targetScreenId, entry.windowId);
        if (screen.isEmpty()) {
            continue;
        }
        if (entry.targetZoneId == PhosphorEngine::RestoreSentinel) {
            if (!m_engine->isActiveOnScreen(screen)) {
                continue;
            }
        } else {
            const QStringList zones =
                entry.targetZoneIds.isEmpty() ? QStringList{entry.targetZoneId} : entry.targetZoneIds;
            const std::optional<BusSnapTarget> target =
                admitBusSnap(entry.windowId, zones, screen, entry.virtualDesktop);
            if (!target) {
                continue;
            }
            entry.virtualDesktop = target->desktop;
        }
        entry.targetScreenId = screen;
        admitted.append(entry);
    }
    if (admitted.size() < entries.size()) {
        qCInfo(lcDbusWindow) << "handleBatchedResnap: dropped" << entries.size() - admitted.size() << "of"
                             << entries.size() << "entries";
    }
    processResnapEntries(m_adaptor, m_engine, admitted);
}

} // namespace PlasmaZones
