// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windowtrackingadaptor.h"
#include "core/resolve/daemongeometryresolver.h"
#include <PhosphorPlacement/PlacementConfig.h>
#include <PhosphorSnapEngine/snapnavigationtargets.h>
#include "persistenceworker.h"
#include "dbus/zonedetectionadaptor.h"
#include <PhosphorEngine/IPlacementEngine.h>
#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorSnapEngine/SnapEngine.h>
#include "config/configbackends.h"
#include "core/interfaces/interfaces.h"
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorWorkspaces/ActivityManager.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>
#include "core/platform/logging.h"
#include "core/resolve/screenmoderouter.h"
#include "core/utils/utils.h"
#include <PhosphorScreens/VirtualScreen.h>
#include "core/types/types.h"
#include <PhosphorEngine/WindowRegistry.h>
// Complete type required where ~WindowTrackingAdaptor destroys the
// unique_ptr<RuleEvaluator> member (m_ruleEvaluator).
#include <PhosphorRules/RuleEvaluator.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <PhosphorScreens/ScreenIdentity.h>

namespace PlasmaZones {

WindowTrackingAdaptor::WindowTrackingAdaptor(PhosphorZones::LayoutRegistry* layoutManager,
                                             PhosphorZones::IZoneDetector* zoneDetector,
                                             PhosphorScreens::ScreenManager* screenManager, ISettings* settings,
                                             PhosphorWorkspaces::VirtualDesktopManager* virtualDesktopManager,
                                             PhosphorWorkspaces::ActivityManager* activityManager, QObject* parent)
    : QDBusAbstractAdaptor(parent)
    , m_layoutManager(layoutManager)
    , m_settings(settings)
    , m_virtualDesktopManager(virtualDesktopManager)
    , m_activityManager(activityManager)
    , m_sessionBackend(createSessionBackend())
{
    // Null dependencies are a daemon-wiring bug, not a recoverable runtime
    // condition: the earlier "refuse to wire" early-return left m_service,
    // m_persistenceWorker, m_saveTimer, and m_sessionBackend null while
    // many public slots (setWindowSticky, windowClosed, cursorScreenChanged,
    // windowActivated, pruneStaleWindows, getEmptyZones, getLastUsedZoneId,
    // findEmptyZone, zoneGeometryRect) plus the saveStateOnShutdown path
    // dereference m_service unguarded — so the early-return just deferred
    // the crash to the first D-Bus call. qFatal aborts unambiguously in both
    // debug and release builds, supersedes Q_ASSERT (debug-only) entirely,
    // and prints which dependency was null so a wiring regression is loud
    // and immediate, not concealed in an "it works until first slot fires"
    // failure mode.
    if (!layoutManager || !zoneDetector || !settings) {
        qFatal(
            "WindowTrackingAdaptor: null dependency at construction "
            "(layoutManager=%p, zoneDetector=%p, settings=%p) — daemon-wiring bug",
            static_cast<void*>(layoutManager), static_cast<void*>(zoneDetector), static_cast<void*>(settings));
    }

    // Create geometry resolver (bridges ISettings + window-rule context gap
    // resolution to the library's IGeometryResolver). Owned via unique_ptr —
    // WindowTrackingService takes a non-owning raw pointer to it. The current
    // desktop/activity callbacks let it resolve per-context gap rules for the
    // context a snap happens in (current desktop/activity), mirroring the
    // current-desktop occupancy filter used in buildEmptyZoneList.
    m_geometryResolver = std::make_unique<PlasmaZones::DaemonGeometryResolver>(
        settings, layoutManager,
        [vdm = m_virtualDesktopManager](const QString& screenId) -> int {
            return vdm ? vdm->currentDesktopForScreen(screenId) : 0;
        },
        [am = m_activityManager]() -> QString {
            return PhosphorWorkspaces::ActivityManager::currentActivityOrEmpty(am);
        });

    // Create business logic service
    m_service = new PhosphorPlacement::WindowTrackingService(
        layoutManager, zoneDetector, screenManager, virtualDesktopManager, m_geometryResolver.get(),
        PhosphorPlacement::PlacementConfig{settings->keepWindowsInZonesOnResolutionChange()}, this);

    // Wire the disabled-context gate consulted before recording a snap-side
    // PendingRestore on windowClosed. The placement library has no settings
    // dependency, so the gate is injected from here — single funnel via
    // isPersistedContextDisabled() so the predicate and the load/save filters
    // share one decision implementation. See discussion #461.
    m_service->setShouldTrackPredicate([this](const QString& screenId, int virtualDesktop) -> bool {
        return !isPersistedContextDisabled(screenId, virtualDesktop);
    });

    // Snap-mode navigation target resolver moved to SnapEngine in Phase 5E.
    // SnapEngine::ensureTargetResolver() lazy-constructs the resolver on
    // first navigation call; setZoneDetectionAdaptor is forwarded to
    // SnapEngine alongside WTA's own copy, so the late-wired zone detector
    // still reaches the resolver.

    // Forward service signals to D-Bus
    connect(m_service, &PhosphorPlacement::WindowTrackingService::windowZoneChanged, this,
            &WindowTrackingAdaptor::windowZoneChanged);

    // Snap-commit signal wiring is deferred to setEngines() where m_snapEngine
    // is available — commitSnap/commitMultiZoneSnap/uncommitSnap live on
    // SnapEngine, not WTS.

    // Connect service state changes to persistence
    connect(m_service, &PhosphorPlacement::WindowTrackingService::stateChanged, this,
            &WindowTrackingAdaptor::scheduleSaveState);

    // Setup debounced save timer (500ms delay to batch rapid state changes)
    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(500);
    connect(m_saveTimer, &QTimer::timeout, this, &WindowTrackingAdaptor::saveState);

    // Persistence I/O worker — disk writes happen off the main thread.
    // PersistenceIO::processWrite runs on the worker thread via a queued
    // requestWrite signal, so writes are handled strictly FIFO. The
    // writeCompleted handler dequeues the head of m_pendingWriteMasks in
    // the same order: on success we discard the mask (bits made it to
    // disk); on failure we OR the mask back into the service's dirty
    // state so the retry picks up exactly the fields the failed write
    // was supposed to cover, without disturbing any bits set by
    // mutations that landed after takeDirty() was called for that write.
    m_persistenceWorker = std::make_unique<PersistenceWorker>(nullptr);
    connect(m_persistenceWorker.get(), &PersistenceWorker::writeCompleted, this,
            [this](const QString& filePath, bool success) {
                // writeCompleted only fires from writes WE enqueued (the
                // sync-fallback path calls m_sessionBackend->sync() directly
                // without routing through the worker), so the queue head
                // must exist. Debug builds halt; release builds log and
                // bail (silent return without the warning would mask the
                // enqueue/dequeue-pairing bug in production).
                if (m_pendingWriteMasks.isEmpty()) {
                    qCWarning(lcDbusWindow)
                        << "writeCompleted callback with empty m_pendingWriteMasks for" << filePath
                        << "— enqueue/dequeue pairing bug; ignoring this callback to keep the queue consistent";
                    Q_ASSERT(false);
                    return;
                }
                const PhosphorPlacement::WindowTrackingService::DirtyMask committed = m_pendingWriteMasks.dequeue();
                if (!success) {
                    qCWarning(lcDbusWindow) << "session state write failed for" << filePath
                                            << "— restoring dirty mask and retrying on next tick";
                    if (m_service && committed != PhosphorPlacement::WindowTrackingService::DirtyNone) {
                        // markDirty emits stateChanged, which is wired to
                        // scheduleSaveState() above — the retry lands on
                        // the next debounce tick automatically.
                        m_service->markDirty(committed);
                    }
                    return;
                }
                // Success: intentionally NOT calling PhosphorConfig::JsonBackend::clearDirty()
                // here. The backend's own m_dirty flag tracks in-memory mutations
                // since the last inline sync(); a successful async write of an
                // earlier snapshot does not mean the current in-memory state
                // matches disk, because the main thread may have mutated m_root
                // between snapshot and completion. Clearing m_dirty in that
                // window would cause the shutdown inline sync() to skip a real
                // pending write. The WTS dirty mask is the authoritative
                // gate for async writes anyway.
            });

    // Active-layout changes: single handler. onLayoutChanged() covers the
    // pending-restores notification side; we prepend a dirty-mark for the
    // DirtyActiveLayoutId field so the next save captures the new value
    // without needing an unrelated DirtyAll path to drag it along.
    connect(m_layoutManager, &PhosphorZones::LayoutRegistry::activeLayoutChanged, this, [this]() {
        if (m_service) {
            m_service->markDirty(PhosphorPlacement::WindowTrackingService::DirtyActiveLayoutId);
        }
        onLayoutChanged();
    });

    // Deferred: PhosphorScreens::ScreenManager may not be initialized yet during adaptor construction.
    // If PhosphorScreens::ScreenManager is still unavailable after the first event loop iteration,
    // virtual screen signals won't be connected until the next daemon restart or
    // screen change. Window restoration will still work via onLayoutChanged() ->
    // tryEmitPendingRestoresAvailable(), which emits immediately when
    // (m_service->screenManager() && m_service->screenManager()->isPanelGeometryReady()) returns false (no
    // PhosphorScreens::ScreenManager instance).
    QTimer::singleShot(0, this, [this]() {
        auto* screenMgr = m_service->screenManager();
        if (!screenMgr) {
            qCWarning(lcDbusWindow) << "PhosphorScreens::ScreenManager instance not available - window restoration "
                                       "may use incorrect geometry";
            return;
        }
        connect(screenMgr, &PhosphorScreens::ScreenManager::panelGeometryReady, this,
                &WindowTrackingAdaptor::onPanelGeometryReady);
        // If panel geometry is already ready, trigger the check now
        if ((m_service->screenManager() && m_service->screenManager()->isPanelGeometryReady())) {
            onPanelGeometryReady();
        }
    });

    // Load persisted window tracking state from previous session
    loadState();

    // Exclude-pattern pruning is now driven from `Daemon::init` (and again
    // from `Daemon::finalizeStartup` once AutotileEngine::loadState has
    // populated the autotile queue) — see WTA::pruneExcludedPendingRestores.

    // If we have placement records but missed activeLayoutChanged (layout was set before we
    // connected), set the flag so tryEmitPendingRestoresAvailable will emit when panel
    // geometry is ready. Fixes daemon restart: windows that were snapped before stop
    // are not re-registered because pendingRestoresAvailable was never emitted. The
    // unified WindowPlacementStore is the source of truth (the legacy pending-restore
    // queue is in-session-only and empty right after loadState).
    if (m_service->placementStore().size() > 0 && m_layoutManager->activeLayout()) {
        m_hasPendingRestores = true;
        qCDebug(lcDbusWindow) << "Pending restores: loaded at init, will emit when panel geometry ready";
    }
}

// Out-of-line destructor. The unique_ptr<SnapNavigationTargetResolver>
// that originally required this has moved to SnapEngine (Phase 5E), but
// the destructor is kept out-of-line to avoid churning every translation
// unit that includes this header.
//
// Symmetric clear of the should-track predicate to mirror the
// `setShouldRestorePredicate({})` clear in enginewiring.cpp's `setEngines()`
// (predicate handoff path).
// `m_service` is parented to WTA and dies with it, so the captured
// `this` never outlives the predicate today — the clear keeps the
// "every late-bound predicate is cleared symmetrically before its
// captured `this` becomes unsafe" contract defensible if a future
// refactor moves service ownership or re-parents the m_service member.
WindowTrackingAdaptor::~WindowTrackingAdaptor()
{
    if (m_service) {
        m_service->setShouldTrackPredicate({});
    }
}

PhosphorSnapEngine::SnapEngine* WindowTrackingAdaptor::snapEngine() const
{
    return m_cachedSnapEngine;
}

// Current virtual desktop, with a safe fallback of 0 when no
// VirtualDesktopManager is wired (guiless tests, minimal sessions).
// Centralises the null-guarded read shared by the disabled-context gates and
// last-used-zone tracking. setEngines() lives in enginewiring.cpp.
int WindowTrackingAdaptor::currentDesktop() const
{
    return m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;
}

int WindowTrackingAdaptor::currentDesktopForScreen(const QString& screenId) const
{
    // Per-output virtual desktops (#648): this screen's current desktop, falling
    // back to the global current when no per-output value is on record.
    return m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktopForScreen(screenId) : 0;
}

void WindowTrackingAdaptor::setScreenModeRouter(ScreenModeRouter* router)
{
    m_screenModeRouter = router;
}

void WindowTrackingAdaptor::setZoneDetectionAdaptor(ZoneDetectionAdaptor* adaptor)
{
    m_zoneDetectionAdaptor = adaptor;
    // Target resolver ownership moved to SnapEngine (Phase 5E). SnapEngine's
    // own setZoneDetectionAdaptor wiring pushes the adaptor into its
    // resolver when late-wired; this setter no longer has anything to do
    // except record the pointer for any WTA-side consumers.
}

// ═══════════════════════════════════════════════════════════════════════════════
// Window Snapping
//
// Snap-commit D-Bus slots (windowSnapped, windowSnappedMultiZone,
// windowUnsnapped, windowsSnappedBatch, recordSnapIntent) moved to
// SnapAdaptor (org.plasmazones.Snap D-Bus interface).
// ═══════════════════════════════════════════════════════════════════════════════

bool WindowTrackingAdaptor::validateWindowId(const QString& windowId, const QString& operation) const
{
    if (windowId.isEmpty()) {
        qCWarning(lcDbusWindow) << "Cannot" << operation << "- empty window ID";
        return false;
    }
    return true;
}

QString WindowTrackingAdaptor::resolveScreenForSnap(const QString& callerScreen, const QString& zoneId) const
{
    if (!callerScreen.isEmpty()) {
        return callerScreen;
    }
    QString detected = detectScreenForZone(zoneId);
    if (!detected.isEmpty()) {
        return detected;
    }
    // Tertiary: use cursor or active window screen
    if (!m_lastCursorScreenId.isEmpty()) {
        return m_lastCursorScreenId;
    }
    return m_lastActiveScreenId;
}

void WindowTrackingAdaptor::setWindowRegistry(PhosphorEngine::WindowRegistry* registry)
{
    m_windowRegistry = registry;
    if (m_service) {
        m_service->setWindowRegistry(registry);
    }
    if (!registry) {
        return;
    }
    // Reactive metadata updates. Per feedback_class_change_exclusion.md we do
    // NOT retroactively enforce rules — a committed snap/autotile/float state
    // stays put even if the new class would have behaved differently at open.
    // The only safe reactive update is refreshing tracking fields that mirror
    // the app class, so future lookups don't compare against a stale string.
    QObject::connect(registry, &PhosphorEngine::WindowRegistry::metadataChanged, this,
                     [this](const QString& instanceId, const PhosphorEngine::WindowMetadata& oldMeta,
                            const PhosphorEngine::WindowMetadata& newMeta) {
                         // QPointer auto-nulls m_windowRegistry if the registered
                         // object outlives this adaptor — guard against that
                         // alongside the m_service belt-and-braces guard so the
                         // `m_windowRegistry->instancesWithAppId(...)` deref
                         // below can't fault.
                         if (oldMeta.appId == newMeta.appId || !m_service || !m_windowRegistry) {
                             return;
                         }
                         // If the last-used-zone tracking is stamped with the old class and
                         // the renamed window was the only instance of that class, update
                         // the tag so the next auto-snap-by-class check sees the live name.
                         // Any other tracking that's keyed internally by windowId (zone
                         // assignments, pre-float zones, etc.) already uses the canonical
                         // key from WindowRegistry::canonicalizeWindowId, so it doesn't
                         // need migration — only human-readable class tags do.
                         if (m_service->lastUsedZoneClass() == oldMeta.appId
                             && m_windowRegistry->instancesWithAppId(oldMeta.appId).isEmpty()) {
                             m_service->retagLastUsedZoneClass(newMeta.appId);
                             qCDebug(lcDbusWindow) << "Retagged last-used-zone class after rename:" << oldMeta.appId
                                                   << "→" << newMeta.appId << "(instance" << instanceId << ")";
                         }
                     });
}

void WindowTrackingAdaptor::reapplyWindowAppearance()
{
    // Fan out to both engines through the common IPlacementEngine contract.
    // Each engine re-emits its placement geometry for the windows it manages,
    // which the compositor turns back into per-window chrome (borders / hidden
    // title bars). No window moves — see the interface doc comment.
    if (m_snapEngine) {
        m_snapEngine->reapplyManagedWindowAppearance();
    }
    if (m_autotileEngine) {
        m_autotileEngine->reapplyManagedWindowAppearance();
    }
}

} // namespace PlasmaZones
