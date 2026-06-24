// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include "snapenginelogging.h"

#include <QGuiApplication>
#include <QScreen>

namespace PhosphorSnapEngine {

using PhosphorEngine::SnapIntent;
using PhosphorEngine::ZoneAssignmentEntry;

void SnapEngine::commitSnapImpl(const QString& windowId, const QStringList& zoneIds, const QString& screenId,
                                SnapIntent intent, int virtualDesktop)
{
    Q_ASSERT(m_snapState);
    if (!m_snapState) {
        return;
    }
    Q_ASSERT(!zoneIds.isEmpty());
    if (Q_UNLIKELY(zoneIds.isEmpty())) {
        qCWarning(PhosphorSnapEngine::lcSnapEngine) << "commitSnapImpl: empty zoneIds for" << windowId;
        return;
    }
    const QString& primaryZoneId = zoneIds.first();

    if (m_windowTracker->clearFloatingForSnap(windowId)) {
        Q_EMIT windowFloatingClearedForSnap(windowId, screenId);
    }

    bool wasAutoSnapped = false;
    if (intent == SnapIntent::UserInitiated) {
        wasAutoSnapped = m_windowTracker->clearAutoSnapped(windowId);
        if (!wasAutoSnapped) {
            m_windowTracker->consumePendingAssignment(windowId);
        }
    }

    // A RouteToDesktop placement pins the assignment to its destination desktop
    // (virtualDesktop >= 1); every other commit path records on the window's
    // current desktop. Tracking the right desktop keeps zone occupancy, snap-assist,
    // and empty-zone detection correct on both the source and destination desktops.
    const int currentDesktop = virtualDesktop >= 1 ? virtualDesktop : currentVirtualDesktopForScreen(screenId);
    if (zoneIds.size() > 1) {
        m_windowTracker->assignWindowToZones(windowId, zoneIds, screenId, currentDesktop);
    } else {
        m_windowTracker->assignWindowToZone(windowId, primaryZoneId, screenId, currentDesktop);
    }

    if (intent == SnapIntent::UserInitiated && !wasAutoSnapped
        && !primaryZoneId.startsWith(QStringLiteral("zoneselector-"))) {
        const QString windowClass = m_windowTracker->currentAppIdFor(windowId);
        m_windowTracker->updateLastUsedZone(primaryZoneId, screenId, windowClass, currentDesktop);
    }

    qCInfo(PhosphorSnapEngine::lcSnapEngine)
        << "commitSnap:" << windowId << "zones=" << zoneIds << "screen=" << screenId
        << "intent=" << (intent == SnapIntent::UserInitiated ? "user" : "auto");

    Q_EMIT windowSnapStateChanged(windowId,
                                  PhosphorProtocol::WindowStateEntry{windowId, primaryZoneId, screenId, false,
                                                                     QStringLiteral("snapped"), zoneIds, false});

    // Focus-new-windows: activate a window that was just auto-placed into a zone on
    // open. AutoRestored is used only by the auto-snap-on-open paths (windowOpened and
    // the D-Bus resolveWindowRestore facade); manual drag, keyboard snap, snap-all,
    // unfloat, and navigation all use UserInitiated, so they keep KWin's normal focus.
    // This matches AutotileEngine's focusNewWindows intent-gating, but emits
    // immediately rather than deferring like autotile does. Autotile defers focus to
    // after its windowsTiled reflow because its post-commit raise-in-tiling-order loop
    // would otherwise bury an early-focused window. Snap has no such batch raise — each
    // window's geometry is applied independently — so activating now is safe and lets
    // this stay the single chokepoint for both single- and multi-zone auto-restored
    // commits.
    if (intent == SnapIntent::AutoRestored) {
        if (auto* settings = snapSettings(); settings && settings->focusNewWindows()) {
            Q_EMIT activateWindowRequested(windowId);
        }
    }
}

void SnapEngine::commitSnap(const QString& windowId, const QString& zoneId, const QString& screenId, SnapIntent intent,
                            int virtualDesktop)
{
    if (windowId.isEmpty() || zoneId.isEmpty()) {
        qCWarning(PhosphorSnapEngine::lcSnapEngine) << "commitSnap: empty windowId or zoneId";
        return;
    }
    commitSnapImpl(windowId, QStringList{zoneId}, screenId, intent, virtualDesktop);
}

void SnapEngine::commitMultiZoneSnap(const QString& windowId, const QStringList& zoneIds, const QString& screenId,
                                     SnapIntent intent, int virtualDesktop)
{
    if (windowId.isEmpty() || zoneIds.isEmpty() || zoneIds.first().isEmpty()) {
        qCWarning(PhosphorSnapEngine::lcSnapEngine) << "commitMultiZoneSnap: empty windowId or zoneIds";
        return;
    }
    commitSnapImpl(windowId, zoneIds, screenId, intent, virtualDesktop);
}

void SnapEngine::uncommitSnap(const QString& windowId)
{
    Q_ASSERT(m_snapState);
    if (!m_snapState) {
        return;
    }
    if (windowId.isEmpty()) {
        return;
    }

    const QString previousZoneId = m_snapState->zoneForWindow(windowId);
    if (previousZoneId.isEmpty()) {
        qCDebug(PhosphorSnapEngine::lcSnapEngine) << "uncommitSnap: window not in any zone:" << windowId;
        return;
    }

    m_windowTracker->consumePendingAssignment(windowId);
    m_windowTracker->unassignWindow(windowId);

    qCInfo(PhosphorSnapEngine::lcSnapEngine) << "uncommitSnap:" << windowId << "from zone" << previousZoneId;

    Q_EMIT windowSnapStateChanged(windowId,
                                  PhosphorProtocol::WindowStateEntry{windowId, QString(), QString(), false,
                                                                     QStringLiteral("unsnapped"), QStringList{},
                                                                     false});
}

PhosphorProtocol::WindowGeometryList SnapEngine::applyBatchAssignments(const QVector<ZoneAssignmentEntry>& entries,
                                                                       SnapIntent intent,
                                                                       std::function<QString()> fallbackScreenResolver)
{
    PhosphorProtocol::WindowGeometryList geometries;
    if (entries.isEmpty()) {
        return geometries;
    }

    // Every non-restore entry below routes through commitSnap/commitMultiZoneSnap,
    // which require a live m_snapState. Guard the whole batch symmetrically with
    // commitSnapImpl/uncommitSnap rather than doing the full per-entry resolution
    // pass against a half-dead engine.
    Q_ASSERT(m_snapState);
    if (!m_snapState) {
        return geometries;
    }

    auto* mgr = m_windowTracker->screenManager();

    // Resolve and remember per-entry screenId in a single pass so the geometry
    // payload below can carry it to the compositor. Without this, the wire
    // entry is built from entry.targetGeometry alone and the compositor must
    // re-derive the screen via geometry.center() against its (possibly stale)
    // m_virtualScreenDefs — which races with VS swap/rotate and produces
    // spurious cross-VS unsnap events.
    QVector<QString> resolvedScreens;
    resolvedScreens.reserve(entries.size());

    for (const auto& entry : entries) {
        if (entry.targetZoneId == PhosphorEngine::RestoreSentinel) {
            // uncommitSnap already dereferences m_windowTracker unconditionally
            // (production deps are non-null; tests never reach this path with a
            // null tracker), so no separate guard is needed here.
            uncommitSnap(entry.windowId);
            m_windowTracker->clearFreeGeometry(entry.windowId);
            resolvedScreens.append(QString());
            continue;
        }

        QString screenId = entry.targetScreenId;
        const QPoint center = entry.targetGeometry.center();
        if (screenId.isEmpty() && mgr) {
            screenId = mgr->effectiveScreenAt(center);
        }
        if (screenId.isEmpty()) {
            for (QScreen* screen : QGuiApplication::screens()) {
                if (screen->geometry().contains(center)) {
                    screenId = PhosphorScreens::ScreenIdentity::identifierFor(screen);
                    break;
                }
            }
        }
        if (screenId.isEmpty() && fallbackScreenResolver) {
            screenId = fallbackScreenResolver();
        }
        if (screenId.isEmpty()) {
            // Last resort: a real (non-restore) snap commit MUST carry a
            // non-empty screenId. The compositor treats an empty screenId on a
            // batch entry as the float/restore marker (only the RestoreSentinel
            // branch above legitimately emits empty), so a real commit that
            // resolved to nothing here would be misclassified as a float and
            // lose its snap border/title-bar tracking. Pick the screen whose
            // geometry sits nearest the target center (the window's intended
            // position) — a better heuristic than an arbitrary primary screen
            // when the center lands on no known screen (off-screen, pre-attach)
            // and no fallbackScreenResolver was supplied. Falls back to the
            // primary screen only if there are no screens to measure against.
            QScreen* nearest = nullptr;
            qint64 bestDistSq = -1;
            for (QScreen* screen : QGuiApplication::screens()) {
                const QRect g = screen->geometry();
                // Use the half-open far edges (x + width / y + height) rather than
                // QRect::right()/bottom() (which return x + width - 1): the latter's
                // off-by-one scores a point sitting exactly on a screen's far edge as
                // 1px outside it.
                const qint64 dx = qMax(0, qMax(g.left() - center.x(), center.x() - (g.x() + g.width())));
                const qint64 dy = qMax(0, qMax(g.top() - center.y(), center.y() - (g.y() + g.height())));
                const qint64 distSq = dx * dx + dy * dy;
                if (bestDistSq < 0 || distSq < bestDistSq) {
                    bestDistSq = distSq;
                    nearest = screen;
                }
            }
            if (!nearest) {
                nearest = QGuiApplication::primaryScreen();
            }
            if (nearest) {
                screenId = PhosphorScreens::ScreenIdentity::identifierFor(nearest);
            }
            qCWarning(PhosphorSnapEngine::lcSnapEngine)
                << "applyBatchAssignments: last-resort nearest-screen heuristic fired for" << entry.windowId
                << "center=" << center << "resolved screen=" << screenId;
        }

        if (entry.targetZoneIds.size() > 1) {
            commitMultiZoneSnap(entry.windowId, entry.targetZoneIds, screenId, intent);
        } else {
            commitSnap(entry.windowId, entry.targetZoneId, screenId, intent);
        }
        resolvedScreens.append(screenId);
    }

    geometries.reserve(entries.size());
    for (int i = 0; i < entries.size(); ++i) {
        const auto& entry = entries[i];
        // Restore sentinels carry their pre-tile geometry and get applied like
        // any other entry — but with empty screenId, since the window is being
        // returned to free-floating state and no tracked-screen seeding should
        // override the compositor's geometry-based resolution for it.
        geometries.append(PhosphorProtocol::WindowGeometryEntry::fromRect(entry.windowId, entry.targetGeometry,
                                                                          resolvedScreens.value(i)));
    }
    return geometries;
}

} // namespace PhosphorSnapEngine
