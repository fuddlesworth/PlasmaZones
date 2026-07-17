// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../windowtrackingadaptor.h"
#include "internal.h"
#include "persistenceworker.h"
#include <PhosphorSnapEngine/SnapEngine.h>
#include "../../config/configbackends.h"
#include "../../core/interfaces.h"
#include "../../core/screenmoderouter.h"
#include <PhosphorContext/ContextResolver.h>
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

bool WindowTrackingAdaptor::isPersistedContextDisabled(const QString& screenId, int virtualDesktop,
                                                       const QString& activity) const
{
    // Empty screen means the entry never carried a usable context tag (legacy
    // snap entries pre-multi-monitor, sticky windows, etc.). Loading and
    // keeping them is the historical default — the alternative drops snap
    // state on every windowless desktop session.
    if (screenId.isEmpty()) {
        return false;
    }
    if (!m_contextResolver) {
        // Pre-`setContextResolver` path — the first call lands from
        // this adaptor's ctor (loadState()) before Daemon hands the
        // resolver over. Returns "nothing is disabled" so the
        // historical "load everything when no settings backend is
        // wired" behaviour is preserved.
        return false;
    }
    // Routes through `handleForPersisted` which resolves the screen's
    // mode via the bound IModeProvider — every consumer (snap-side
    // ShouldTrackPredicate, the WindowPlacementStore serialize keep-predicate,
    // and the save/load filters) ends up applying the cascade keyed on
    // the mode the screen is actually running.
    return m_contextResolver->isDisabled(m_contextResolver->handleForPersisted(screenId, virtualDesktop, activity));
}

void WindowTrackingAdaptor::saveState()
{
    using D = PhosphorPlacement::WindowTrackingService;
    // Refresh open floating windows' live geometry into the unified store BEFORE
    // snapshotting the dirty mask, so a dragged floated window persists its
    // current position (no per-move capture hook fires). This may set
    // DirtyWindowPlacements, which takeDirty() then picks up below.
    refreshOpenWindowPlacements();
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

    // Snap zone assignments and pending-restore queues are no longer persisted
    // as separate keys — a snapped window's restore state is one WindowPlacement
    // record (state="snapped", engineData.zoneIds). The runtime SnapState maps are
    // re-populated by resolveWindowRestore re-committing from the store on open.
    tracking->deleteKey(ConfigKeys::windowZoneAssignmentsFullKey());
    tracking->deleteKey(ConfigKeys::pendingRestoreQueuesKey());

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

    // Pre-tile (float-back) geometry is no longer persisted separately — it lives
    // in the unified WindowPlacement record's shared freeGeometryByScreen (the single
    // float-back store). Drop the obsolete keys.
    tracking->deleteKey(ConfigKeys::preTileGeometriesFullKey());
    tracking->deleteKey(ConfigKeys::preTileGeometriesKey());

    // Save last used zone info (from service)
    if (dirty & D::DirtyLastUsedZone) {
        tracking->writeString(ConfigKeys::lastUsedZoneIdKey(), m_service->lastUsedZoneId());
        // lastUsedZoneClass, lastUsedScreenName, and lastUsedDesktop are
        // exposed on PhosphorPlacement::WindowTrackingService but are
        // intentionally NOT persisted here: each is a transient hint used
        // during the current session's snap-restore lookup and is
        // recomputed from the live zone topology on the next snap event.
        // Persisting them would lock-in stale state across config changes
        // (zone added / removed / renamed). The matching loadState read (the
        // lastUsedZoneId block below) only restores the id and threads the
        // live companions through unchanged to avoid blanking them.
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
    // Pre-float zone/screen assignments are no longer persisted separately — a
    // floated window's pre-float zones live in its WindowPlacement record
    // (engineData.preFloatZones), restored into SnapState on reopen.
    tracking->deleteKey(ConfigKeys::preFloatZoneAssignmentsKey());
    tracking->deleteKey(ConfigKeys::preFloatScreenAssignmentsKey());

    // Save user-snapped classes
    QJsonArray userSnappedArray;
    if (dirty & D::DirtyUserSnapped) {
        for (const QString& windowClass : m_service->userSnappedClasses()) {
            userSnappedArray.append(windowClass);
        }
        tracking->writeJson(ConfigKeys::userSnappedClassesKey(), userSnappedArray);
    }

    // Autotile window orders and pending restores are no longer persisted as
    // separate keys — an autotiled window's position is one WindowPlacement record
    // (state="autotiled", engineData.position), captured by the common save-time
    // snapshot and close hook, exactly like snap. Drop the obsolete keys.
    tracking->deleteKey(ConfigKeys::autotileWindowOrdersKey());
    tracking->deleteKey(ConfigKeys::autotilePendingRestoresKey());

    // Obsolete float-restore queue — replaced by the unified WindowPlacementStore.
    // Drop any stale key left by an older build.
    tracking->deleteKey(ConfigKeys::floatRestoreQueuesKey());

    // Unified WindowPlacementStore — the single source of truth for per-window
    // restore state (snapped / floated). The keep predicate reuses the single
    // disabled-context gate, so a record on a disabled monitor/desktop never
    // persists. refreshOpenWindowPlacements() (top of saveState) has already
    // re-captured open floating windows' live geometry into the store.
    if (dirty & D::DirtyWindowPlacements) {
        const QJsonObject placements =
            m_service->placementStore().serialize([this](const PhosphorEngine::WindowPlacement& p) {
                // Persist only records with something to restore. A contentless
                // {floating, no geometry, no zones} residue carries nothing yet and,
                // at MaxPerApp per app, would crowd out a real placement in the next
                // session's FIFO — so it must never reach disk. (refreshOpenWindowPlacements
                // above re-captures live geometry, so an actually-floating window is
                // content-bearing here.)
                return p.hasRestorableContent()
                    && !isPersistedContextDisabled(p.screenId, p.virtualDesktop, p.activity);
            });
        if (!placements.isEmpty()) {
            tracking->writeJson(ConfigKeys::windowPlacementsKey(), placements);
        } else {
            tracking->deleteKey(ConfigKeys::windowPlacementsKey());
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
        // Reachable in two cases:
        //   1. m_sessionBackend is not a PhosphorConfig::JsonBackend —
        //      tests that wire a memory-only backend.
        //   2. The production shutdown path: `saveStateOnShutdown` resets
        //      `m_persistenceWorker` before calling `saveState()`, so the
        //      async branch is unavailable on the very last save.
        //
        // sync() returns void, so we have no way to detect a failed
        // write and re-mark the committed bits for retry. The mask has
        // already been taken (above), which means a silent failure here
        // silently loses the committed bits — same behavior as
        // pre-Phase-3 code. Log a one-time warning on first hit so any
        // unexpected production regression (steady-state saves landing
        // here instead of the shutdown one-shot) is obvious in logs.
        if (!m_syncFallbackWarned) {
            qCWarning(lcDbusWindow) << "saveState: using synchronous fallback backend; failed writes cannot be retried "
                                       "(expected for the one shutdown save and for unit tests with a memory backend)";
            m_syncFallbackWarned = true;
        }
        m_sessionBackend->sync();
    }
    qCInfo(lcDbusWindow) << "Saved state: dirty=" << Qt::hex << dirty << Qt::dec
                         << "placements=" << m_service->placementStore().size()
                         << "userSnapped=" << userSnappedArray.size();
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
    // (UserSnappedClasses, WindowPlacements) — the group's readString()
    // serializes them back to compact JSON strings, so the same fromJson parse
    // below also reads a pre-native session.json where these were escaped strings.
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

    // Snap zone assignments and pending-restore queues are no longer loaded from
    // disk — the unified WindowPlacementStore (loaded below) is the sole source of
    // per-window restore state. resolveWindowRestore re-commits snapped records into
    // SnapState as each window reopens.

    // Load last used zone info. Only the id is persisted by saveState
    // (lastUsedZoneClass/Screen/Desktop are intentionally session-local —
    // see the DirtyLastUsedZone save block above). Skip the setLastUsedZone call entirely
    // when the id key is absent — calling it with empty id + empty
    // companions would destructively overwrite any live values the
    // service holds. The service's own write paths populate the
    // companions at runtime as the user snaps windows; restoring the
    // id alone is sufficient for the cross-session "last used zone"
    // intent.
    const QString lastZoneId = readVal(ConfigKeys::lastUsedZoneIdKey(), QString());
    if (!lastZoneId.isEmpty()) {
        // Pass through the current service state for the companions so
        // a same-session reload doesn't blank them.
        m_service->setLastUsedZone(lastZoneId, m_service->lastUsedScreenName(), m_service->lastUsedZoneClass(),
                                   m_service->lastUsedDesktop());
    }

    // Float state is ephemeral (session-only) — skip loading.
    // Any stale FloatingWindows entries from older versions are cleaned up in saveState().

    // Pre-float zone/screen assignments are no longer loaded — a floated window's
    // pre-float zones are restored from its WindowPlacement record on reopen.

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

    // Autotile window orders / pending restores are no longer loaded — autotiled
    // windows reopen at their saved position from the unified store (insertWindow
    // consumes the record); within-session context-switch order is runtime state.

    // Restore the unified WindowPlacementStore — the single source of truth for
    // per-window restore state.
    {
        const QString placementsStr = readVal(ConfigKeys::windowPlacementsKey(), QString());
        if (!placementsStr.isEmpty()) {
            QJsonParseError parseError;
            const QJsonDocument doc = QJsonDocument::fromJson(placementsStr.toUtf8(), &parseError);
            if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
                m_service->placementStore().deserialize(doc.object());
            } else {
                qCWarning(lcDbusWindow) << "Failed to parse saved window placements:" << parseError.errorString();
            }
        }
    }

    // No engine float-back cache to re-seed: the unified record's freeGeometryByScreen
    // IS the single float-back store and is loaded directly above (deserialize), and
    // validatedUnmanagedGeometry reads it. (The per-engine m_unmanagedGeometries store
    // was removed.)

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

    const int placementCount = m_service ? m_service->placementStore().size() : 0;
    qCInfo(lcDbusWindow) << "Loaded state from KConfig: windowPlacements=" << placementCount;
    if (placementCount > 0) {
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
