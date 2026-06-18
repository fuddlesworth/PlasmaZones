// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../windowtrackingadaptor.h"
#include "internal.h"
#include "persistenceworker.h"
#include <PhosphorSnapEngine/SnapEngine.h>
#include "../../config/configbackends.h"
#include "../../core/interfaces.h"
#include "../../core/screenmoderouter.h"
#include <PhosphorScreens/VirtualScreen.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/Layout.h>
#include "../../core/logging.h"
#include "../../core/utils.h"
#include <PhosphorWorkspaces/VirtualDesktopManager.h>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include "../../config/configkeys.h"
#include <QTimer>
#include <PhosphorScreens/ScreenIdentity.h>
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorEngine/EngineTypes.h>

namespace PlasmaZones {
using namespace WindowTrackingInternal;

static QHash<QString, QStringList> parseZoneListMap(const QString& json)
{
    QHash<QString, QStringList> result;
    if (json.isEmpty()) {
        return result;
    }
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isObject()) {
        return result;
    }
    QJsonObject obj = doc.object();
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        if (it.value().isArray()) {
            QStringList zones;
            for (const QJsonValue& v : it.value().toArray()) {
                if (v.isString() && !v.toString().isEmpty()) {
                    zones.append(v.toString());
                }
            }
            if (!zones.isEmpty()) {
                result[it.key()] = zones;
            }
        }
    }
    return result;
}

bool WindowTrackingAdaptor::isPersistedContextDisabled(const QString& screenId, int virtualDesktop,
                                                       const QString& activity) const
{
    // Empty screen means the entry never carried a usable context tag (legacy
    // snap entries pre-multi-monitor, sticky windows, etc.). Loading and
    // keeping them is the historical default — the alternative drops snap
    // state on every windowless desktop session.
    //
    // m_screenModeRouter is wired post-construction by the daemon; the very
    // first call site is loadState() (invoked from this adaptor's own ctor
    // before the daemon calls setScreenModeRouter), where it is still null.
    // Snap-side consumers (WindowZoneAssignmentsFull, PendingRestoreQueues,
    // PreFloat* maps) and snap's ShouldTrackPredicate route through here and
    // get the Snapping fallback. Autotile's ShouldPersistRestorePredicate also
    // routes through here and supplies its own activity tag — `modeFor` picks
    // up the screen's current Autotile mode whenever the router is wired.
    if (screenId.isEmpty()) {
        return false;
    }
    const PhosphorZones::AssignmentEntry::Mode mode =
        m_screenModeRouter ? m_screenModeRouter->modeFor(screenId) : PhosphorZones::AssignmentEntry::Snapping;
    return isContextDisabled(m_settings, mode, screenId, virtualDesktop, activity);
}

void WindowTrackingAdaptor::saveState()
{
    using D = PhosphorPlacement::WindowTrackingService;
    // Snapshot the dirty mask — after this call the service is clean
    // from our perspective. If the write fails, the persistence worker's
    // writeCompleted(success=false) handler re-marks the same bits on
    // the service so the next tick retries. The committed mask is
    // pushed onto m_pendingWriteMasks only at the actual hand-off point,
    // so a no-op wake (dirty == DirtyNone) never clobbers an in-flight
    // write's committed snapshot.
    const D::DirtyMask dirty = m_service->takeDirty();

    // Fast path: no bits dirty means no disk work to do. Returning here
    // skips both the (unnecessary) group walk and the per-field writes.
    // Nothing has been pushed onto m_pendingWriteMasks for this call,
    // so in-flight writes are unaffected.
    if (dirty == D::DirtyNone) {
        qCDebug(lcDbusWindow) << "Saved state: no dirty fields, skipping write";
        return;
    }

    auto tracking = m_sessionBackend->group(ConfigKeys::windowTrackingGroup());

    // Save active layout ID so we can restore it after daemon restart.
    // Gated on DirtyActiveLayoutId — only rewrites when the layout manager
    // signal fired since the last save.
    if ((dirty & D::DirtyActiveLayoutId) && m_layoutManager && m_layoutManager->activeLayout()) {
        tracking->writeString(ConfigKeys::activeLayoutIdKey(), m_layoutManager->activeLayout()->id().toString());
    }

    // Save zone assignments with full windowIds (appId|uuid) to preserve multi-instance
    // distinction. On daemon-only restart (KWin still running), UUIDs are stable so
    // exact matching prevents restoring the wrong instance of a multi-instance app.
    QJsonArray fullAssignments;
    if (dirty & D::DirtyZoneAssignments) {
        for (auto it = m_service->zoneAssignments().constBegin(); it != m_service->zoneAssignments().constEnd(); ++it) {
            const QString assignedScreen = m_service->screenAssignments().value(it.key());
            const int desktop = m_service->desktopAssignments().value(it.key(), 0);
            // Disabled-context filter (discussion #461 item 2). A window that
            // crossed onto a now-disabled monitor (e.g. user disabled snapping
            // on their secondary) should not have its zone assignment
            // re-persisted — that's the entry that resurfaces and yanks the
            // window back on the next session. The runtime in-memory map keeps
            // the assignment; only persistence is suppressed.
            if (isPersistedContextDisabled(assignedScreen, desktop)) {
                continue;
            }
            QJsonObject entry;
            entry[QLatin1String("windowId")] = it.key();
            entry[QLatin1String("zoneIds")] = toJsonArray(it.value());
            entry[QLatin1String("screen")] = PhosphorIdentity::VirtualScreenId::isVirtual(assignedScreen)
                ? assignedScreen
                : Phosphor::Screens::ScreenIdentity::idForName(assignedScreen);
            entry[QLatin1String("desktop")] = desktop;
            fullAssignments.append(entry);
        }
        tracking->writeString(ConfigKeys::windowZoneAssignmentsFullKey(),
                              QString::fromUtf8(QJsonDocument(fullAssignments).toJson(QJsonDocument::Compact)));
    }

    // Save pending restore queues as JSON: appId -> array of entry objects
    // Each entry: {zoneIds: [...], screen: "...", desktop: N, layout: "...", zoneNumbers: [...]}
    QJsonObject pendingQueuesObj;
    if (dirty & D::DirtyPendingRestores) {
        for (auto it = m_service->pendingRestoreQueues().constBegin();
             it != m_service->pendingRestoreQueues().constEnd(); ++it) {
            QJsonArray entryArray;
            for (const auto& entry : it.value()) {
                // Disabled-context filter (discussion #461 item 2). The Tier-A
                // write gate in lifecycle.cpp covers new closes, but the
                // in-memory queue may still hold pre-fix entries from before
                // the user disabled the screen. Filtering here keeps them out
                // of the on-disk session.
                if (isPersistedContextDisabled(entry.screenId, entry.virtualDesktop)) {
                    continue;
                }
                QJsonObject entryObj;
                entryObj[QLatin1String("zoneIds")] = toJsonArray(entry.zoneIds);
                if (!entry.screenId.isEmpty()) {
                    entryObj[QLatin1String("screen")] = PhosphorIdentity::VirtualScreenId::isVirtual(entry.screenId)
                        ? entry.screenId
                        : Phosphor::Screens::ScreenIdentity::idForName(entry.screenId);
                }
                if (entry.virtualDesktop > 0) {
                    entryObj[QLatin1String("desktop")] = entry.virtualDesktop;
                }
                if (!entry.layoutId.isEmpty()) {
                    entryObj[QLatin1String("layout")] = entry.layoutId;
                }
                if (!entry.zoneNumbers.isEmpty()) {
                    QJsonArray numArray;
                    for (int num : entry.zoneNumbers) {
                        numArray.append(num);
                    }
                    entryObj[QLatin1String("zoneNumbers")] = numArray;
                }
                // Persist windowKind only when concrete. `Unknown` is the
                // wire default and re-loads identically from a missing key,
                // so omitting it keeps pre-fix on-disk sessions byte-stable
                // for the common case (no kind plumbed yet).
                if (entry.windowKind != PhosphorEngine::WindowKind::Unknown) {
                    entryObj[QLatin1String("windowKind")] = static_cast<int>(entry.windowKind);
                }
                entryArray.append(entryObj);
            }
            if (!entryArray.isEmpty()) {
                pendingQueuesObj[it.key()] = entryArray;
            }
        }
        tracking->writeString(ConfigKeys::pendingRestoreQueuesKey(),
                              QString::fromUtf8(QJsonDocument(pendingQueuesObj).toJson(QJsonDocument::Compact)));
    }

    // Clean up obsolete keys from old formats. Reached only when at
    // least one dirty bit is set (the DirtyNone early-return above
    // skips this path), so we always flush the deletions along with
    // the rest of the write. On subsequent saves the keys are already
    // gone so deleteKey is a cheap no-op.
    tracking->deleteKey(ConfigKeys::obsoletePendingWindowScreenAssignmentsKey());
    tracking->deleteKey(ConfigKeys::obsoletePendingWindowDesktopAssignmentsKey());
    tracking->deleteKey(ConfigKeys::obsoletePendingWindowLayoutAssignmentsKey());
    tracking->deleteKey(ConfigKeys::obsoletePendingWindowZoneNumbersKey());
    tracking->deleteKey(ConfigKeys::obsoleteWindowZoneAssignmentsKey());
    tracking->deleteKey(ConfigKeys::obsoleteWindowScreenAssignmentsKey());
    tracking->deleteKey(ConfigKeys::obsoleteWindowDesktopAssignmentsKey());

    // Save pre-tile geometries so float-toggle restores to the correct position
    // even after daemon restart (windows stay at their zone positions across restarts).
    // Save full windowId format for daemon-only restarts (UUIDs stable, multi-instance distinction).
    // Save appId format as fallback for KWin restarts (UUIDs change).
    //
    // Disabled-context filter (discussion #461 item 2): drop entries whose
    // screenId is on the disabled-monitor list. Without this, float-restore
    // on the next session would place the window at coordinates the user
    // disabled snapping on (and after a screen-disconnect they may even be
    // off-screen). Mirrors the WindowZoneAssignmentsFull filter above.
    if ((dirty & D::DirtyPreTileGeometries) && m_snapEngine) {
        QHash<QString, PhosphorEngine::PlacementEngineBase::UnmanagedEntry> filteredGeoms;
        const auto& allGeoms = m_snapEngine->unmanagedGeometries();
        filteredGeoms.reserve(allGeoms.size());
        for (auto it = allGeoms.constBegin(); it != allGeoms.constEnd(); ++it) {
            if (isPersistedContextDisabled(it.value().screenId, 0)) {
                continue;
            }
            filteredGeoms.insert(it.key(), it.value());
        }
        tracking->writeString(ConfigKeys::preTileGeometriesFullKey(), serializeGeometryMapFull(filteredGeoms));
        tracking->writeString(ConfigKeys::preTileGeometriesKey(), serializeGeometryMap(filteredGeoms, m_service));
    }

    // Save last used zone info (from service)
    if (dirty & D::DirtyLastUsedZone) {
        tracking->writeString(ConfigKeys::lastUsedZoneIdKey(), m_service->lastUsedZoneId());
        // Note: Other last-used fields would need accessors in service
    }

    // Float state is ephemeral (session-only) — do NOT persist across restarts.
    // Clear any stale entry from older versions so restored sessions start clean.
    tracking->deleteKey(ConfigKeys::obsoleteFloatingWindowsKey());

    // Disabled-context filter (discussion #461 item 2) for the pre-float maps:
    // a window floated on a since-disabled monitor would otherwise restore
    // there on unfloat. PreFloat has no desktop dimension (it's appId-keyed),
    // so we pass desktop=0 — the helper short-circuits the desktop gate on
    // 0 and the monitor-disabled list is the load-bearing check anyway.
    // Build the set of dropped appIds from the screen map so the zone map
    // can drop its paired entries without re-doing the screen lookup.
    QSet<QString> droppedPreFloatAppIds;
    for (auto it = m_service->preFloatScreenAssignments().constBegin();
         it != m_service->preFloatScreenAssignments().constEnd(); ++it) {
        if (isPersistedContextDisabled(it.value(), 0)) {
            const QString key = m_service->currentAppIdFor(it.key());
            if (!key.isEmpty()) {
                droppedPreFloatAppIds.insert(key);
            }
        }
    }

    // Save pre-float zone assignments (for unfloating after session restore).
    // Runtime keys are full window IDs; convert to the CURRENT app class so
    // that a window which renamed mid-session persists under the live class.
    QJsonObject preFloatZonesObj;
    if (dirty & D::DirtyPreFloatZones) {
        for (auto it = m_service->preFloatZoneAssignments().constBegin();
             it != m_service->preFloatZoneAssignments().constEnd(); ++it) {
            QString key = m_service->currentAppIdFor(it.key());
            if (key.isEmpty() || droppedPreFloatAppIds.contains(key)) {
                continue;
            }
            preFloatZonesObj[key] = toJsonArray(it.value());
        }
        tracking->writeString(ConfigKeys::preFloatZoneAssignmentsKey(),
                              QString::fromUtf8(QJsonDocument(preFloatZonesObj).toJson(QJsonDocument::Compact)));
    }

    // Save pre-float screen assignments (for unfloating to correct monitor).
    // Same current-class conversion as above, plus translate to screen IDs.
    // Disabled-context drop is funnelled through droppedPreFloatAppIds
    // (built above) so the screen and zone loops use a single decision.
    if (dirty & D::DirtyPreFloatScreens) {
        QJsonObject preFloatScreensObj;
        for (auto it = m_service->preFloatScreenAssignments().constBegin();
             it != m_service->preFloatScreenAssignments().constEnd(); ++it) {
            QString key = m_service->currentAppIdFor(it.key());
            if (key.isEmpty() || droppedPreFloatAppIds.contains(key)) {
                continue;
            }
            preFloatScreensObj[key] = PhosphorIdentity::VirtualScreenId::isVirtual(it.value())
                ? it.value()
                : Phosphor::Screens::ScreenIdentity::idForName(it.value());
        }
        tracking->writeString(ConfigKeys::preFloatScreenAssignmentsKey(),
                              QString::fromUtf8(QJsonDocument(preFloatScreensObj).toJson(QJsonDocument::Compact)));
    }

    // Save user-snapped classes
    QJsonArray userSnappedArray;
    if (dirty & D::DirtyUserSnapped) {
        for (const QString& windowClass : m_service->userSnappedClasses()) {
            userSnappedArray.append(windowClass);
        }
        tracking->writeString(ConfigKeys::userSnappedClassesKey(),
                              QString::fromUtf8(QJsonDocument(userSnappedArray).toJson(QJsonDocument::Compact)));
    }

    // Save autotile per-context window orders (analogous to WindowZoneAssignmentsFull
    // for snap mode). masterCount/splitRatio are NOT saved here — Settings owns those
    // via AutotileScreen:<id> per-screen overrides.
    if (dirty & D::DirtyAutotileOrders) {
        if (m_serializeTilingStatesFn) {
            const QJsonArray autotileOrders = m_serializeTilingStatesFn();
            if (!autotileOrders.isEmpty()) {
                tracking->writeString(ConfigKeys::autotileWindowOrdersKey(),
                                      QString::fromUtf8(QJsonDocument(autotileOrders).toJson(QJsonDocument::Compact)));
            } else {
                tracking->deleteKey(ConfigKeys::autotileWindowOrdersKey());
            }
        } else {
            // No serialize delegate — clean up any stale key from a prior session
            // where autotile was enabled. Prevents restoring orphaned window orders.
            tracking->deleteKey(ConfigKeys::autotileWindowOrdersKey());
        }
    }

    // Save autotile pending restore queues (close/reopen window preservation).
    // Separate key from window orders to keep the orders array homogeneous.
    if (dirty & D::DirtyAutotilePending) {
        if (m_serializePendingRestoresFn) {
            const QJsonObject pendingRestores = m_serializePendingRestoresFn();
            if (!pendingRestores.isEmpty()) {
                tracking->writeString(ConfigKeys::autotilePendingRestoresKey(),
                                      QString::fromUtf8(QJsonDocument(pendingRestores).toJson(QJsonDocument::Compact)));
            } else {
                tracking->deleteKey(ConfigKeys::autotilePendingRestoresKey());
            }
        } else {
            tracking->deleteKey(ConfigKeys::autotilePendingRestoresKey());
        }
    }

    tracking.reset(); // release group before write

    // Async I/O: snapshot the in-memory JSON root (COW copy) and hand off
    // to the persistence worker thread. The main thread returns immediately.
    // The dirty flag is cleared asynchronously when the worker's
    // writeCompleted(success=true) signal lands (see ctor wiring) — so a
    // failed write is retried on the next timer tick instead of silently
    // losing state.
    //
    // Push the committed mask onto the pending-writes FIFO at the exact
    // hand-off point: the worker processes requestWrite signals in queued
    // order, so dequeueing from the head in writeCompleted correctly
    // matches masks to completions even with multiple writes in flight.
    auto* jsonBackend = dynamic_cast<PhosphorConfig::JsonBackend*>(m_sessionBackend.get());
    if (jsonBackend && m_persistenceWorker) {
        m_pendingWriteMasks.enqueue(dirty);
        m_persistenceWorker->enqueueWrite(jsonBackend->filePath(), jsonBackend->jsonRootSnapshot());
    } else {
        // Fallback synchronous path.
        //
        // This branch is only reachable when m_sessionBackend is not a
        // PhosphorConfig::JsonBackend — i.e. tests that wire a memory-only backend.
        // The production daemon always uses PhosphorConfig::JsonBackend, so the
        // async/retry path above is the only one exercised outside of
        // the test harness.
        //
        // sync() returns void, so we have no way to detect a failed
        // write and re-mark the committed bits for retry. The mask has
        // already been taken (above), which means a silent failure here
        // silently loses the committed bits — same behavior as
        // pre-Phase-3 code, and acceptable only because this path is
        // test-only. Log a one-time warning on first hit so any future
        // production regression is obvious in logs.
        if (!m_syncFallbackWarned) {
            qCWarning(lcDbusWindow) << "saveState: using synchronous fallback backend; failed writes cannot be retried "
                                       "(expected only in unit tests)";
            m_syncFallbackWarned = true;
        }
        m_sessionBackend->sync();
    }
    qCInfo(lcDbusWindow) << "Saved state: dirty=" << Qt::hex << dirty << Qt::dec << "zones=" << fullAssignments.size()
                         << "pending=" << pendingQueuesObj.size()
                         << "preTile=" << (m_snapEngine ? m_snapEngine->unmanagedGeometries().size() : 0)
                         << "preFloat=" << preFloatZonesObj.size() << "userSnapped=" << userSnappedArray.size();
}

void WindowTrackingAdaptor::requestReapplyWindowGeometries()
{
    Q_EMIT reapplyWindowGeometriesRequested();
}

void WindowTrackingAdaptor::saveStateOnShutdown()
{
    if (m_saveTimer && m_saveTimer->isActive()) {
        m_saveTimer->stop();
    }

    // Destroy the persistence worker first. Its destructor posts a quit to
    // the I/O thread and waits, which drains any pending async writes. If we
    // did the sync write before draining, a previously-queued async snapshot
    // could land after and silently clobber the fresh shutdown save.
    m_persistenceWorker.reset();

    // With the worker gone, saveState() falls through to the synchronous
    // m_sessionBackend->sync() path — guaranteed to be the last write.
    saveState();

    // Block all subsequent saves. During Qt object destruction after stop(),
    // PhosphorZones::LayoutRegistry destruction can trigger activeLayoutChanged → onLayoutChanged()
    // which purges zone assignments and calls scheduleSaveState(). Without this
    // guard, the debounced save fires with empty state and overwrites the correct
    // shutdown save above — causing window snap state to be lost across restarts.
    m_shutdownSaveGuard = true;
}

void WindowTrackingAdaptor::loadState()
{
    // Read config via the PhosphorConfig::IBackend group API (readString/readInt).
    // This correctly handles values stored as native JSON objects/arrays
    // (e.g. PendingRestoreQueues, PreTileGeometries) — the group's readString()
    // serializes them back to compact JSON strings.
    //
    // The previous approach used readJsonConfigFromDisk() which flattened the
    // entire config into a QMap. Its flattener recursed into native JSON objects
    // as if they were config groups, making object-type values like
    // PendingRestoreQueues invisible to key lookups — the root cause of the
    // window restore bug after v2 config migration.
    //
    // Sync the shared backend first so any in-memory writes (e.g., from a
    // concurrent saveState()) are flushed to disk before we read.
    m_sessionBackend->sync();
    auto tracking = m_sessionBackend->group(ConfigKeys::windowTrackingGroup());
    auto readVal = [&](const QString& key, const QString& def = QString()) -> QString {
        return tracking->readString(key, def);
    };
    auto readIntVal = [&](const QString& key, int def = 0) -> int {
        return tracking->readInt(key, def);
    };

    // Build pending restore queues from:
    // 1. Active zone assignments — windows still open at daemon shutdown
    // 2. Pending restore queues (PendingRestoreQueues) — windows closed before shutdown
    //
    // If full windowId format is available (WindowZoneAssignmentsFull), load exact
    // assignments into m_windowZoneAssignments for precise instance matching after
    // daemon-only restart (KWin still running, UUIDs stable). Also build appId-keyed
    // pending queues as fallback for KWin restarts (UUIDs change).

    QHash<QString, QList<PhosphorEngine::PendingRestore>> pendingQueues;

    // Pending entries derived from active assignments (fallback for KWin restarts where
    // UUIDs change). Collected separately from persisted pending queues so we can do
    // count-based merging — prevents exponential growth while preserving multi-instance entries.
    QHash<QString, QList<PhosphorEngine::PendingRestore>> activePending;

    // Try full-windowId format first (preserves multi-instance distinction)
    QHash<QString, QStringList> fullZones;
    QHash<QString, QString> fullScreens;
    QHash<QString, int> fullDesktops;

    QString fullJson = readVal(ConfigKeys::windowZoneAssignmentsFullKey(), QString());
    if (!fullJson.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(fullJson.toUtf8());
        if (doc.isArray()) {
            for (const QJsonValue& val : doc.array()) {
                if (!val.isObject()) {
                    continue;
                }
                QJsonObject entry = val.toObject();
                QString windowId = entry[QLatin1String("windowId")].toString();
                if (windowId.isEmpty()) {
                    continue;
                }
                QStringList zoneIds;
                for (const QJsonValue& v : entry[QLatin1String("zoneIds")].toArray()) {
                    if (v.isString() && !v.toString().isEmpty()) {
                        zoneIds.append(v.toString());
                    }
                }
                if (zoneIds.isEmpty()) {
                    continue;
                }
                QString screen = entry[QLatin1String("screen")].toString();
                int desktop = entry[QLatin1String("desktop")].toInt(0);

                // Disabled-context filter (discussion #461 item 2). A session
                // saved before the user disabled this monitor/desktop carries
                // assignments that would otherwise resurface and yank the
                // window back into a zone the user told us to leave alone.
                // Drop them on load — the matching autotile cleanup in
                // pruneStaleRestores fires automatically when active screens
                // are recomputed; this is the snap-side equivalent.
                if (isPersistedContextDisabled(screen, desktop)) {
                    qCInfo(lcDbusWindow) << "loadState: dropping zone assignment for" << windowId
                                         << "on disabled context — screen:" << screen << "desktop:" << desktop;
                    continue;
                }

                fullZones[windowId] = zoneIds;
                fullScreens[windowId] = screen;
                fullDesktops[windowId] = desktop;

                // Also build appId-keyed pending entries as fallback for KWin restarts
                // (UUIDs change, so exact matches won't work). For daemon-only restarts,
                // calculateRestoreFromSession() guards against wrong-instance consumption.
                // Collected separately — merged after loading persisted queues to prevent duplication.
                // Registry is typically empty during load, so currentAppIdFor
                // falls back to string parsing — same behavior as the old code
                // but consistent with the rest of the codebase.
                QString appId = m_service->currentAppIdFor(windowId);
                // Skip a corrupt appId for the same reason the persisted-queue
                // loop below does: currentAppIdFor falls back to string parsing
                // here (the registry is empty during load), so a pre-3.0
                // windowId yields a whitespace-bearing key no live window
                // matches. Without this gate the merge would reintroduce the
                // corrupt key the persisted-queue gate set out to drop.
                if (!PhosphorIdentity::WindowId::isValidAppId(appId)) {
                    continue;
                }
                PhosphorEngine::PendingRestore pending;
                pending.zoneIds = zoneIds;
                pending.screenId = screen;
                pending.virtualDesktop = desktop;
                activePending[appId].append(pending);
            }
        }
    }

    // Load exact assignments so isWindowSnapped() returns true for daemon-only restarts.
    // calculateRestoreFromSession() checks for exact-match siblings before allowing
    // FIFO consumption, preventing wrong-instance restore for multi-instance apps.
    m_service->setActiveAssignments(fullZones, fullScreens, fullDesktops);

    // Load persisted pending restore queues (appId -> array of entry objects).
    // These go directly into pendingQueues. Persisted entries are richer than
    // active-derived ones (they include layoutId and zoneNumbers).
    QString pendingQueuesJson = readVal(ConfigKeys::pendingRestoreQueuesKey(), QString());
    if (!pendingQueuesJson.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(pendingQueuesJson.toUtf8());
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
                if (!it.value().isArray()) {
                    continue;
                }
                // Drop entries under a corrupt key — blank " " keys and the
                // pre-3.0 "resourceName resourceClass" composites that survive
                // an upgrade (session state outlives a ~/.config wipe).
                // Loading them verbatim lets them be consumed by unrelated
                // windows on reopen. See WindowId::isValidAppId.
                if (!PhosphorIdentity::WindowId::isValidAppId(it.key())) {
                    qCInfo(lcDbusWindow) << "Dropping pending-restore entries under corrupt appId key:" << it.key();
                    continue;
                }
                for (const QJsonValue& entryVal : it.value().toArray()) {
                    if (!entryVal.isObject()) {
                        continue;
                    }
                    QJsonObject entryObj = entryVal.toObject();
                    PhosphorEngine::PendingRestore entry;
                    // Parse zoneIds
                    for (const QJsonValue& v : entryObj[QLatin1String("zoneIds")].toArray()) {
                        if (v.isString() && !v.toString().isEmpty()) {
                            entry.zoneIds.append(v.toString());
                        }
                    }
                    if (entry.zoneIds.isEmpty()) {
                        continue;
                    }
                    entry.screenId = entryObj[QLatin1String("screen")].toString();
                    entry.virtualDesktop = entryObj[QLatin1String("desktop")].toInt(0);
                    entry.layoutId = entryObj[QLatin1String("layout")].toString();
                    for (const QJsonValue& v : entryObj[QLatin1String("zoneNumbers")].toArray()) {
                        if (v.isDouble()) {
                            entry.zoneNumbers.append(v.toInt());
                        }
                    }
                    // windowKind defaults to Unknown when missing (legacy on-disk
                    // session predates the kind gate). Out-of-range integers
                    // are clamped to Unknown for the same reason adaptor-side
                    // wire ints are clamped: never reinterpret a corrupt value
                    // as a concrete enum.
                    const int kindInt = entryObj[QLatin1String("windowKind")].toInt(0);
                    entry.windowKind = (kindInt == static_cast<int>(PhosphorEngine::WindowKind::Normal))
                        ? PhosphorEngine::WindowKind::Normal
                        : (kindInt == static_cast<int>(PhosphorEngine::WindowKind::Transient))
                        ? PhosphorEngine::WindowKind::Transient
                        : PhosphorEngine::WindowKind::Unknown;
                    // Disabled-context filter (discussion #461 item 2).
                    // Pre-fix sessions may contain restore entries for a
                    // monitor / desktop the user has since disabled snapping
                    // on; drop those on load so they never get replayed.
                    if (isPersistedContextDisabled(entry.screenId, entry.virtualDesktop)) {
                        qCInfo(lcDbusWindow)
                            << "loadState: dropping pending restore for" << it.key()
                            << "on disabled context — screen:" << entry.screenId << "desktop:" << entry.virtualDesktop;
                        continue;
                    }
                    pendingQueues[it.key()].append(entry);
                }
            }
        }
    }

    // Merge active-derived entries with persisted pending queues.
    //
    // Active entries represent windows that were alive and snapped at the time of
    // daemon shutdown — they are the MOST RECENT state. Persisted pending entries
    // may include older entries from windows that were closed in previous sessions.
    //
    // FIFO ordering invariant: active entries must come FIRST so that when N windows
    // reopen, each consumes the entry matching its last-known state. Without this,
    // stale persisted entries from a previous session (e.g., a second Firefox window
    // on VS2 that no longer exists) could be consumed before the current VS1 entry,
    // causing the window to restore to the wrong virtual screen.
    //
    // When an active entry matches a persisted entry (same fingerprint), the persisted
    // version is used because it contains richer data (layoutId, zoneNumbers).
    // Unmatched persisted entries (from previously-closed windows) are appended AFTER
    // all active entries. This correctly handles multi-instance apps and prevents
    // entry doubling on every daemon restart.
    for (auto activeIt = activePending.constBegin(); activeIt != activePending.constEnd(); ++activeIt) {
        const QString& appId = activeIt.key();
        QList<PhosphorEngine::PendingRestore> persistedList = pendingQueues.value(appId);
        QVector<bool> persistedUsed(persistedList.size(), false);

        // Phase 1: For each active entry, find a matching persisted entry to enrich it.
        // Active entries go to the front of the queue (most recent state first).
        QList<PhosphorEngine::PendingRestore> activeEnriched;
        for (const auto& activeEntry : activeIt.value()) {
            bool matched = false;
            for (int i = 0; i < persistedList.size(); ++i) {
                if (!persistedUsed[i] && persistedList[i].zoneIds == activeEntry.zoneIds
                    && persistedList[i].screenId == activeEntry.screenId
                    && persistedList[i].virtualDesktop == activeEntry.virtualDesktop) {
                    // Use persisted entry (has layoutId and zoneNumbers)
                    activeEnriched.append(persistedList[i]);
                    persistedUsed[i] = true;
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                // New window snapped since last save — no persisted match
                activeEnriched.append(activeEntry);
            }
        }

        // Phase 2: Collect unmatched persisted entries (from previously-closed windows).
        QList<PhosphorEngine::PendingRestore> unmatchedPersisted;
        for (int i = 0; i < persistedList.size(); ++i) {
            if (!persistedUsed[i]) {
                unmatchedPersisted.append(persistedList[i]);
            }
        }

        // Rebuild queue: active entries first, then stale entries from older sessions.
        QList<PhosphorEngine::PendingRestore> merged;
        merged.reserve(activeEnriched.size() + unmatchedPersisted.size());
        merged.append(activeEnriched);
        merged.append(unmatchedPersisted);

        if (merged.isEmpty()) {
            pendingQueues.remove(appId);
        } else {
            pendingQueues[appId] = merged;
        }
    }

    m_service->setPendingRestoreQueues(pendingQueues);

    // Load pre-tile geometries into the engine's unmanaged-geometry store.
    // Disabled-context filter mirrors saveState: drop entries whose stored
    // screen is on the user's disabled-monitor list, so a session saved
    // before the fix doesn't replay leaked entries on the next start.
    using UnmanagedEntry = PhosphorEngine::PlacementEngineBase::UnmanagedEntry;
    QHash<QString, UnmanagedEntry> unmanagedGeometries;
    auto loadGeometries = [this](const QString& json, QHash<QString, UnmanagedEntry>& out) {
        if (json.isEmpty()) {
            return;
        }
        QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
        if (!doc.isObject()) {
            return;
        }
        QJsonObject obj = doc.object();
        for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
            if (it.value().isObject()) {
                QJsonObject geomObj = it.value().toObject();
                QRect geom(geomObj[QLatin1String("x")].toInt(), geomObj[QLatin1String("y")].toInt(),
                           geomObj[QLatin1String("width")].toInt(), geomObj[QLatin1String("height")].toInt());
                if (geom.width() > 0 && geom.height() > 0) {
                    QString screen = geomObj[QLatin1String("screen")].toString();
                    if (isPersistedContextDisabled(screen, 0)) {
                        continue;
                    }
                    out[it.key()] = UnmanagedEntry{geom, screen};
                }
            }
        }
    };

    // Load full windowId format (per-runtime-instance data from a daemon-only
    // restart where KWin UUIDs are still valid). These are instance-scoped —
    // deliberately NOT mirrored to the appId key on load.
    QString fullTileJson = readVal(ConfigKeys::preTileGeometriesFullKey(), QString());
    if (!fullTileJson.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(fullTileJson.toUtf8());
        if (doc.isArray()) {
            for (const QJsonValue& val : doc.array()) {
                if (!val.isObject()) {
                    continue;
                }
                QJsonObject entry = val.toObject();
                QString windowId = entry[QLatin1String("windowId")].toString();
                if (windowId.isEmpty()) {
                    continue;
                }
                QRect geom(entry[QLatin1String("x")].toInt(), entry[QLatin1String("y")].toInt(),
                           entry[QLatin1String("width")].toInt(), entry[QLatin1String("height")].toInt());
                if (geom.width() > 0 && geom.height() > 0) {
                    QString screen = entry[QLatin1String("screen")].toString();
                    if (isPersistedContextDisabled(screen, 0)) {
                        continue;
                    }
                    unmanagedGeometries[windowId] = UnmanagedEntry{geom, screen};
                }
            }
        }
    }

    // Load appId-keyed format (cross-session baseline).
    QString tileJson = readVal(ConfigKeys::preTileGeometriesKey(), QString());
    if (!tileJson.isEmpty()) {
        QHash<QString, UnmanagedEntry> appIdGeometries;
        loadGeometries(tileJson, appIdGeometries);
        for (auto it = appIdGeometries.constBegin(); it != appIdGeometries.constEnd(); ++it) {
            if (!unmanagedGeometries.contains(it.key())) {
                unmanagedGeometries[it.key()] = it.value();
            }
        }
    }
    if (m_snapEngine) {
        m_snapEngine->setUnmanagedGeometries(unmanagedGeometries);
    }

    // Load last used zone info
    QString lastZoneId = readVal(ConfigKeys::lastUsedZoneIdKey(), QString());
    QString lastScreenId = readVal(ConfigKeys::lastUsedScreenNameKey(), QString());
    QString lastZoneClass = readVal(ConfigKeys::lastUsedZoneClassKey(), QString());
    int lastDesktop = readIntVal(ConfigKeys::lastUsedDesktopKey(), 0);
    m_service->setLastUsedZone(lastZoneId, lastScreenId, lastZoneClass, lastDesktop);

    // Float state is ephemeral (session-only) — skip loading.
    // Any stale FloatingWindows entries from older versions are cleaned up in saveState().

    // Load pre-float zone assignments (for unfloating after session restore).
    // Supports both old format (string) and new format (JSON array) for
    // backward compat. The disabled-context filter for these entries needs
    // the paired screen, so the zone-keyed drop-set is built below from the
    // screen-load loop and then applied to the parsed zones map.
    QHash<QString, QStringList> preFloatZones =
        parseZoneListMap(readVal(ConfigKeys::preFloatZoneAssignmentsKey(), QString()));

    // Load pre-float screen assignments (for unfloating to correct monitor).
    // Values may be screen IDs (new) or connector names (legacy) — resolve to
    // current connector name. Disabled-context filter (discussion #461 item 2):
    // drop entries whose stored screen is on the user's disabled-monitor list
    // before they reach the in-memory map. PreFloat has no desktop dimension,
    // so we pass desktop=0 — the helper short-circuits the desktop gate and
    // monitor-disabled is the load-bearing check.
    QHash<QString, QString> preFloatScreens;
    QSet<QString> droppedPreFloatAppIds;
    QString preFloatScreensJson = readVal(ConfigKeys::preFloatScreenAssignmentsKey(), QString());
    if (!preFloatScreensJson.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(preFloatScreensJson.toUtf8());
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
                if (it.value().isString()) {
                    QString storedScreen = it.value().toString();
                    if (isPersistedContextDisabled(storedScreen, 0)) {
                        qCInfo(lcDbusWindow) << "loadState: dropping pre-float screen entry for" << it.key()
                                             << "on disabled context — screen:" << storedScreen;
                        droppedPreFloatAppIds.insert(it.key());
                        continue;
                    }
                    // Virtual screen IDs are stored as-is — no connector/ID translation
                    if (!PhosphorIdentity::VirtualScreenId::isVirtual(storedScreen)) {
                        if (!Phosphor::Screens::ScreenIdentity::isConnectorName(storedScreen)) {
                            QString connectorName = Phosphor::Screens::ScreenIdentity::nameForId(storedScreen);
                            if (!connectorName.isEmpty()) {
                                storedScreen = connectorName;
                            }
                        }
                    }
                    preFloatScreens[it.key()] = storedScreen;
                }
            }
        }
    }

    // Strip paired zone entries for any appId whose screen entry was dropped
    // above — keeping a zone-only entry would unfloat to the wrong screen.
    for (const QString& appId : std::as_const(droppedPreFloatAppIds)) {
        preFloatZones.remove(appId);
    }

    m_service->setPreFloatZoneAssignments(preFloatZones);
    m_service->setPreFloatScreenAssignments(preFloatScreens);

    // Load user-snapped classes
    QSet<QString> userSnappedClasses;
    QString userSnappedJson = readVal(ConfigKeys::userSnappedClassesKey(), QString());
    if (!userSnappedJson.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(userSnappedJson.toUtf8());
        if (doc.isArray()) {
            QJsonArray arr = doc.array();
            for (const QJsonValue& val : arr) {
                if (val.isString()) {
                    userSnappedClasses.insert(val.toString());
                }
            }
        }
    }
    m_service->setUserSnappedClasses(userSnappedClasses);

    // Restore autotile per-context window orders (analogous to WindowZoneAssignmentsFull)
    if (m_deserializeTilingStatesFn) {
        const QString autotileOrdersStr = readVal(ConfigKeys::autotileWindowOrdersKey(), QString());
        if (!autotileOrdersStr.isEmpty()) {
            QJsonParseError parseError;
            const QJsonDocument doc = QJsonDocument::fromJson(autotileOrdersStr.toUtf8(), &parseError);
            if (parseError.error == QJsonParseError::NoError && doc.isArray()) {
                m_deserializeTilingStatesFn(doc.array());
            } else {
                qCWarning(lcDbusWindow) << "Failed to parse saved autotile window orders:" << parseError.errorString();
            }
        }
    }

    // Restore autotile pending restore queues (close/reopen window preservation)
    if (m_deserializePendingRestoresFn) {
        const QString pendingRestoresStr = readVal(ConfigKeys::autotilePendingRestoresKey(), QString());
        if (!pendingRestoresStr.isEmpty()) {
            QJsonParseError parseError;
            const QJsonDocument doc = QJsonDocument::fromJson(pendingRestoresStr.toUtf8(), &parseError);
            if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
                m_deserializePendingRestoresFn(doc.object());
            } else {
                qCWarning(lcDbusWindow) << "Failed to parse saved autotile pending restores:"
                                        << parseError.errorString();
            }
        }
    }

    // Restore active layout from previous session so that previousLayout() is correct
    // on the next layout switch. Without this, the daemon starts with defaultLayout()
    // which may differ from the layout the user was on, causing resnap to build its
    // zone-position map from the wrong layout's zones (all entries get pos=0 → empty buffer).
    //
    // CRITICAL: Use QSignalBlocker to suppress activeLayoutChanged during restore.
    // At this point in init, m_layoutManager->currentVirtualDesktop() is still the
    // default (1) — the real desktop hasn't been set yet (done in Daemon::start()).
    // If activeLayoutChanged fires, onLayoutChanged() runs stale-assignment removal
    // using resolveLayoutForScreen() with the wrong desktop, which falls back to
    // defaultLayout() instead of the restored layout. PhosphorZones::Zone assignments from the
    // saved layout get purged because those zones don't exist in defaultLayout().
    // This is a state restoration, not a real layout switch — no signal needed.
    QString savedActiveLayoutId = readVal(ConfigKeys::activeLayoutIdKey(), QString());
    if (!savedActiveLayoutId.isEmpty() && m_layoutManager) {
        auto savedUuid = Utils::parseUuid(savedActiveLayoutId);
        if (savedUuid) {
            PhosphorZones::Layout* savedLayout = m_layoutManager->layoutById(*savedUuid);
            if (savedLayout && savedLayout != m_layoutManager->activeLayout()) {
                qCInfo(lcDbusWindow) << "Restoring active layout from previous session:" << savedLayout->name();
                QSignalBlocker blocker(m_layoutManager);
                m_layoutManager->setActiveLayoutById(*savedUuid);
            }
        }
    }

    int totalPendingEntries = 0;
    for (auto it = pendingQueues.constBegin(); it != pendingQueues.constEnd(); ++it) {
        totalPendingEntries += it.value().size();
        for (const auto& entry : it.value()) {
            qCInfo(lcDbusWindow) << "  pending snap: app=" << it.key() << "zone=" << entry.zoneIds;
        }
    }
    qCInfo(lcDbusWindow) << "Loaded state from KConfig: pendingApps=" << pendingQueues.size()
                         << "totalEntries=" << totalPendingEntries;
    if (!pendingQueues.isEmpty()) {
        m_hasPendingRestores = true;
        tryEmitPendingRestoresAvailable();
    }

    // In-memory state now mirrors the disk file — nothing is dirty until
    // the next mutation lands. Without this clear, the first saveState()
    // after startup would re-serialize every field we just loaded.
    if (m_service) {
        m_service->clearDirty();
    }
}

void WindowTrackingAdaptor::scheduleSaveState()
{
    // After saveStateOnShutdown(), block all saves. During Qt object destruction,
    // PhosphorZones::LayoutRegistry teardown can trigger onLayoutChanged() → scheduleSaveState()
    // with purged (empty) state, overwriting the correct shutdown save.
    if (m_shutdownSaveGuard) {
        return;
    }
    if (m_saveTimer) {
        m_saveTimer->start();
    } else {
        saveState();
    }
}

} // namespace PlasmaZones
