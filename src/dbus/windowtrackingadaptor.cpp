// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windowtrackingadaptor.h"
#include "../core/daemongeometryresolver.h"
#include <PhosphorPlacement/PlacementConfig.h>
#include <PhosphorSnapEngine/snapnavigationtargets.h>
#include "windowtrackingadaptor/persistenceworker.h"
#include "zonedetectionadaptor.h"
#include <PhosphorEngine/IPlacementEngine.h>
#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorSnapEngine/SnapEngine.h>
#include "../config/configbackends.h"
#include "../core/interfaces.h"
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>
#include "../core/logging.h"
#include "../core/screenmoderouter.h"
#include "../core/utils.h"
#include <PhosphorScreens/VirtualScreen.h>
#include "../core/types.h"
#include <PhosphorEngine/WindowRegistry.h>
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
                                             QObject* parent)
    : QDBusAbstractAdaptor(parent)
    , m_layoutManager(layoutManager)
    , m_settings(settings)
    , m_virtualDesktopManager(virtualDesktopManager)
    , m_sessionBackend(createSessionBackend())
{
    Q_ASSERT(layoutManager);
    Q_ASSERT(zoneDetector);
    Q_ASSERT(settings);

    // Create geometry resolver (bridges ISettings to the library's IGeometryResolver)
    m_geometryResolver = new PlasmaZones::DaemonGeometryResolver(settings);

    // Create business logic service
    m_service = new PhosphorPlacement::WindowTrackingService(
        layoutManager, zoneDetector, screenManager, virtualDesktopManager, m_geometryResolver,
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
                // must exist. Assert instead of hedging — a disappearing
                // head would indicate a bug in enqueue/dequeue pairing.
                Q_ASSERT(!m_pendingWriteMasks.isEmpty());
                if (m_pendingWriteMasks.isEmpty()) {
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
                Q_UNUSED(filePath);
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

    // Prune any pending-restore queues for appIds that are now on the user's
    // exclusion list. Snap-engine's resolveWindowRestore already refuses to
    // honor them at runtime, but they live on disk until pruned and spam one
    // "pending snap:" log line per entry at every startup. One-shot at boot
    // catches entries authored before the user added the class to exclusions.
    // The signal hookups cover live additions for the running session. The
    // autotile-side prune happens again from the daemon's finalizeStartup
    // after AutotileEngine::loadState populates m_pendingAutotileRestores.
    pruneExcludedPendingRestoresFromSettings();
    connect(m_settings, &ISettings::excludedApplicationsChanged, this,
            &WindowTrackingAdaptor::pruneExcludedPendingRestoresFromSettings);
    connect(m_settings, &ISettings::excludedWindowClassesChanged, this,
            &WindowTrackingAdaptor::pruneExcludedPendingRestoresFromSettings);

    // If we have pending restores but missed activeLayoutChanged (layout was set before we
    // connected), set the flag so tryEmitPendingRestoresAvailable will emit when panel
    // geometry is ready. Fixes daemon restart: windows that were snapped before stop
    // are not re-registered because pendingRestoresAvailable was never emitted.
    if (!m_service->pendingRestoreQueues().isEmpty() && m_layoutManager->activeLayout()) {
        m_hasPendingRestores = true;
        qCDebug(lcDbusWindow) << "Pending restores: loaded at init, will emit when panel geometry ready";
    }
}

// Out-of-line destructor. The unique_ptr<SnapNavigationTargetResolver>
// that originally required this has moved to SnapEngine (Phase 5E), but
// the destructor is kept out-of-line to avoid churning every translation
// unit that includes this header.
WindowTrackingAdaptor::~WindowTrackingAdaptor() = default;

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

void WindowTrackingAdaptor::setTilingStateDelegates(std::function<QJsonArray()> serializeFn,
                                                    std::function<void(const QJsonArray&)> deserializeFn)
{
    m_serializeTilingStatesFn = std::move(serializeFn);
    m_deserializeTilingStatesFn = std::move(deserializeFn);
}

void WindowTrackingAdaptor::setTilingPendingRestoreDelegates(std::function<QJsonObject()> serializeFn,
                                                             std::function<void(const QJsonObject&)> deserializeFn)
{
    m_serializePendingRestoresFn = std::move(serializeFn);
    m_deserializePendingRestoresFn = std::move(deserializeFn);
}

void WindowTrackingAdaptor::pruneExcludedPendingRestoresFromSettings()
{
    if (!m_settings) {
        return;
    }
    QStringList patterns = m_settings->excludedApplications();
    patterns += m_settings->excludedWindowClasses();
    if (patterns.isEmpty()) {
        return;
    }

    int snapRemoved = 0;
    if (m_service) {
        snapRemoved = m_service->pruneExcludedPendingRestores(patterns);
    }

    int autotileRemoved = 0;
    if (auto* autotile = qobject_cast<PhosphorTileEngine::AutotileEngine*>(m_autotileEngine.data())) {
        autotileRemoved = autotile->pruneExcludedPendingRestores(patterns);
        // The engine is settings-agnostic and does NOT mark dirty itself.
        // Flip the autotile-pending bit on WTS so the next debounced save
        // rewrites AutotilePendingRestores. The snap-side
        // pruneExcludedPendingRestores method does this internally.
        if (autotileRemoved > 0 && m_service) {
            m_service->markDirty(PhosphorPlacement::WindowTrackingService::DirtyAutotilePending);
        }
    }

    if (snapRemoved > 0 || autotileRemoved > 0) {
        qCInfo(lcDbusWindow) << "Pruned pending-restore queues for excluded apps. snap" << snapRemoved << "autotile"
                             << autotileRemoved;
    }
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

void WindowTrackingAdaptor::windowScreenChanged(const QString& windowId, const QString& newScreenId)
{
    if (!m_service)
        return;
    if (!validateWindowId(windowId, QStringLiteral("screen changed"))) {
        return;
    }

    // Floating window with a tracked screen: refresh the engine's
    // screen-tracking via the cross-engine handoff contract so subsequent
    // shortcut routing (lastActiveScreenName) finds the new screen instead of
    // the stale one. Without this, a snap-floated window dragged to another
    // screen leaves screenAssignments pointing at the source forever — the
    // float toggle then unfloats it back to the source-screen zone.
    QString currentZoneId = m_service->zoneForWindow(windowId);
    if (currentZoneId.isEmpty()) {
        if (!m_service->isWindowFloating(windowId)) {
            return;
        }
        const QString trackedSnap = m_snapEngine ? m_snapEngine->screenForTrackedWindow(windowId) : QString();
        const QString trackedAutotile =
            m_autotileEngine ? m_autotileEngine->screenForTrackedWindow(windowId) : QString();
        const QString trackedScreen = !trackedSnap.isEmpty() ? trackedSnap : trackedAutotile;
        if (trackedScreen.isEmpty() || PhosphorScreens::ScreenIdentity::screensMatch(trackedScreen, newScreenId)) {
            return;
        }
        PhosphorEngine::PlacementEngineBase* source =
            !trackedSnap.isEmpty() ? m_snapEngine.data() : m_autotileEngine.data();
        PhosphorEngine::PlacementEngineBase* dest = nullptr;
        if (m_autotileEngine && m_autotileEngine->isActiveOnScreen(newScreenId)) {
            dest = m_autotileEngine.data();
        } else if (m_snapEngine) {
            dest = m_snapEngine.data();
        }
        if (!dest) {
            return;
        }
        PhosphorEngine::IPlacementEngine::HandoffContext ctx;
        ctx.windowId = windowId;
        ctx.toScreenId = newScreenId;
        ctx.fromEngineId = source ? source->engineId() : QString();
        ctx.wasFloating = true;
        ctx.sourceGeometry = m_frameGeometry.value(windowId);
        if (source && source != dest) {
            source->handoffRelease(windowId);
        }
        dest->handoffReceive(ctx);
        qCInfo(lcDbusWindow) << "windowScreenChanged: floating window" << windowId << "moved from" << trackedScreen
                             << "to" << newScreenId << "- handoff complete";
        return;
    }

    // Compare the stored screen assignment with the new screen.
    // If they match (format-agnostic), the window was moved programmatically
    // to its assigned zone's screen (restore, resnap, snap assist) — keep snapped.
    // If they differ, the user moved the window away — unsnap it.
    QString storedScreen = m_service->screenAssignments().value(windowId);

    // KWin reports physical screen names in outputChanged. When the stored screen
    // is a virtual screen (e.g. "HDMI-1/vs:0"), comparing against the physical name
    // ("HDMI-1") via screensMatch returns false → spurious unsnap.
    //
    // If the stored screen is virtual and its physical parent matches the reported
    // physical screen, keep the stored virtual screen ID — it's already correct.
    // Only re-resolve via geometry when the physical screens actually differ,
    // which avoids the zone-center ambiguity when a zone straddles a VS boundary.
    QString resolvedNewScreen = newScreenId;
    if (!PhosphorIdentity::VirtualScreenId::isVirtual(newScreenId)
        && PhosphorIdentity::VirtualScreenId::isVirtual(storedScreen)) {
        QString storedPhysical = PhosphorIdentity::VirtualScreenId::extractPhysicalId(storedScreen);
        if (PhosphorScreens::ScreenIdentity::screensMatch(storedPhysical, newScreenId)) {
            // Physical parent matches — the window is still on the same monitor,
            // so the stored virtual screen ID is still valid.
            resolvedNewScreen = storedScreen;
        } else {
            // Different physical screen — resolve via zone geometry as fallback.
            // NOTE: Ideally we'd use the window's actual position, but window
            // geometry is not available in this context (KWin only reports the
            // physical screen name). Using the zone center is imprecise when the
            // zone straddles a virtual screen boundary; however, the resolved ID
            // will still differ from storedScreen (different physical parent), so
            // the window correctly unsnaps regardless.
            QRect zoneGeo = m_service->zoneGeometry(currentZoneId, storedScreen);
            if (zoneGeo.isValid()) {
                QString vsId = Utils::effectiveScreenIdAt(m_service->screenManager(), zoneGeo.center());
                if (!vsId.isEmpty()) {
                    resolvedNewScreen = vsId;
                }
            }
        }
    }

    if (PhosphorScreens::ScreenIdentity::screensMatch(storedScreen, resolvedNewScreen)) {
        qCDebug(lcDbusWindow) << "windowScreenChanged:" << windowId << "moved to assigned screen, keeping snap";
        return;
    }

    qCInfo(lcDbusWindow) << "windowScreenChanged:" << windowId << "moved from" << storedScreen << "to"
                         << resolvedNewScreen << "- unsnapping";
    m_service->consumePendingAssignment(windowId);
    m_service->unassignWindow(windowId);

    // Emit unified state change for screen-change-triggered unsnap
    Q_EMIT windowStateChanged(windowId,
                              PhosphorProtocol::WindowStateEntry{windowId, QString(), newScreenId, false,
                                                                 QStringLiteral("screen_changed"), QStringList{},
                                                                 false});
}

void WindowTrackingAdaptor::setWindowSticky(const QString& windowId, bool sticky)
{
    if (windowId.isEmpty()) {
        return;
    }
    // Delegate to service
    m_service->setWindowSticky(windowId, sticky);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Window Lifecycle - Delegate to Service
// ═══════════════════════════════════════════════════════════════════════════════

void WindowTrackingAdaptor::windowClosed(const QString& windowId, int windowKind)
{
    if (!validateWindowId(windowId, QStringLiteral("clean up closed window"))) {
        return;
    }

    // Clear active window tracking if the closed window was the active one.
    // Without this, navigation shortcuts after closing the active window would
    // operate on a stale ID, producing confusing OSD failure messages.
    if (m_lastActiveWindowId == windowId) {
        m_lastActiveWindowId.clear();
    }

    // Drop frame-geometry shadow entry for this window.
    m_frameGeometry.remove(windowId);

    const PhosphorEngine::WindowKind kind = PhosphorEngine::clampWindowKindFromWire(windowKind);
    m_service->windowClosed(windowId, kind);

    // Drop registry state last: consumers subscribed to windowDisappeared may
    // rely on other WTS state still being present during their cleanup. The
    // canonical release MUST happen after remove() because WindowRegistry's
    // disappear signal fires synchronously from remove() and subscribers may
    // still call canonicalizeForLookup on their way out.
    if (m_windowRegistry) {
        const QString instanceId = PhosphorIdentity::WindowId::extractInstanceId(windowId);
        m_windowRegistry->remove(instanceId);
        m_windowRegistry->releaseCanonical(instanceId);
    }

    qCDebug(lcDbusWindow) << "Cleaned up tracking data for closed window" << windowId;
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
                         if (oldMeta.appId == newMeta.appId || !m_service) {
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

void WindowTrackingAdaptor::setWindowMetadata(const QString& instanceId, const QString& appId,
                                              const QString& desktopFile, const QString& title,
                                              const QString& windowRole, int pid, int virtualDesktop,
                                              const QString& activity, int windowType)
{
    if (!m_windowRegistry) {
        // Registry not wired yet — during daemon startup the kwin-effect may
        // fire before setWindowRegistry runs. Drop silently; the effect re-emits
        // on every class change so state converges.
        return;
    }
    if (instanceId.isEmpty()) {
        qCWarning(lcDbusWindow) << "setWindowMetadata: rejecting empty instance id";
        return;
    }

    PhosphorEngine::WindowMetadata meta;
    meta.appId = appId;
    meta.desktopFile = desktopFile;
    meta.title = title;
    meta.windowRole = windowRole;
    // pid / virtualDesktop crossed D-Bus as plain ints. The contract is
    // "0 = unknown" — negative values are malformed input (version skew,
    // a buggy caller) that would otherwise propagate into WindowMetadata
    // and WindowQuery. Clamp them to 0 at the boundary.
    if (pid < 0) {
        // Negative pid is now clamped to 0 at the effect-side source
        // (see window_identity.cpp pushWindowMetadata) so this path is the
        // defensive belt-and-braces for a malformed external caller — not a
        // KWin -1 leaking through. Logged at debug so the routine
        // session-restore "-1" no longer spams the warning log.
        qCDebug(lcDbusWindow) << "setWindowMetadata: negative pid" << pid << "for instance" << instanceId
                              << "— treating as 0 (unknown)";
    }
    if (virtualDesktop < 0) {
        qCWarning(lcDbusWindow) << "setWindowMetadata: negative virtualDesktop" << virtualDesktop << "for instance"
                                << instanceId << "— treating as 0 (unknown)";
    }
    meta.pid = pid < 0 ? 0 : pid;
    meta.virtualDesktop = virtualDesktop < 0 ? 0 : virtualDesktop;
    meta.activity = activity;
    // windowType crossed D-Bus as a plain int — clamp out-of-range values
    // (version skew, a malformed caller) to Unknown rather than casting blind.
    if (!PhosphorProtocol::isValidWindowType(windowType)) {
        qCWarning(lcDbusWindow) << "setWindowMetadata: out-of-range windowType" << windowType << "for instance"
                                << instanceId << "— treating as Unknown";
    }
    meta.windowType = PhosphorProtocol::windowTypeFromInt(windowType);
    m_windowRegistry->upsert(instanceId, meta);
}

void WindowTrackingAdaptor::cursorScreenChanged(const QString& screenId)
{
    if (screenId.isEmpty()) {
        return;
    }

    // The KWin effect may send a physical screen ID when virtual screen configs
    // haven't loaded yet.  Resolve to the correct virtual screen using the
    // focused window's daemon-tracked screen assignment as the best hint.
    QString resolvedId = screenId;
    if (!PhosphorIdentity::VirtualScreenId::isVirtual(screenId)) {
        auto* mgr = m_service->screenManager();
        if (mgr && mgr->hasVirtualScreens(screenId)) {
            // Use focused window's tracked screen as hint
            if (m_service && !m_lastActiveWindowId.isEmpty()) {
                const QString trackedScreen = m_service->screenAssignments().value(m_lastActiveWindowId);
                if (PhosphorIdentity::VirtualScreenId::isVirtual(trackedScreen)
                    && PhosphorIdentity::VirtualScreenId::extractPhysicalId(trackedScreen) == screenId) {
                    resolvedId = trackedScreen;
                }
            }
            // If no window hint, fall back to first virtual screen
            if (!PhosphorIdentity::VirtualScreenId::isVirtual(resolvedId)) {
                QStringList vsIds = mgr->virtualScreenIdsFor(screenId);
                if (!vsIds.isEmpty()) {
                    resolvedId = vsIds.first();
                }
            }
        }
    }

    m_lastCursorScreenId = resolvedId;
    qCDebug(lcDbusWindow) << "Cursor screen changed to" << resolvedId;
}

void WindowTrackingAdaptor::setFrameGeometry(const QString& windowId, int x, int y, int width, int height)
{
    if (windowId.isEmpty() || width <= 0 || height <= 0) {
        return;
    }
    m_frameGeometry[windowId] = QRect(x, y, width, height);
}

QRect WindowTrackingAdaptor::frameGeometry(const QString& windowId) const
{
    return m_frameGeometry.value(windowId);
}

QString WindowTrackingAdaptor::lastActiveScreenName() const
{
    // Prefer the active window's live screen tracking from either engine
    // over the cached m_lastActiveScreenId. KWin only fires windowActivated
    // on focus changes, so a window dragged/snapped/tiled to a different
    // VS without losing focus leaves the cache pointing at the source
    // screen — and the shortcut router would then dispatch (float,
    // navigate, etc.) to the wrong engine.
    //
    // Lookup order:
    //   1. snap-side screenForTrackedWindow (covers snap-mode windows
    //      including snap-floated ones whose screen we now preserve through
    //      float, and floating windows received via cross-engine handoff)
    //   2. autotile-side screenForTrackedWindow (covers windows that
    //      crossed engines via drag-insert handoff — snap released its
    //      tracking, autotile took ownership)
    //   3. cached m_lastActiveScreenId (windows neither engine tracks —
    //      brand-new windows pre-tile, dialogs, etc.)
    if (!m_lastActiveWindowId.isEmpty()) {
        if (m_snapEngine) {
            const QString tracked = m_snapEngine->screenForTrackedWindow(m_lastActiveWindowId);
            if (!tracked.isEmpty()) {
                return tracked;
            }
        }
        if (m_autotileEngine) {
            const QString tracked = m_autotileEngine->screenForTrackedWindow(m_lastActiveWindowId);
            if (!tracked.isEmpty()) {
                return tracked;
            }
        }
    }
    return m_lastActiveScreenId;
}

void WindowTrackingAdaptor::windowActivated(const QString& windowId, const QString& screenId)
{
    if (!validateWindowId(windowId, QStringLiteral("process windowActivated"))) {
        return;
    }

    // Track the active window for daemon-driven navigation (move/focus/swap/etc.)
    m_lastActiveWindowId = windowId;

    // Track the active window's screen as fallback for shortcut screen detection.
    // The primary source is now cursorScreenChanged (from KWin effect's mouseChanged).
    // Prefer the daemon-tracked screen assignment (set at snap time) over what the
    // effect reports, since the effect may send a physical ID before VS configs load.
    QString resolvedScreen = screenId;
    if (!screenId.isEmpty()) {
        if (!PhosphorIdentity::VirtualScreenId::isVirtual(screenId) && m_service) {
            const QString trackedScreen = m_service->screenAssignments().value(windowId);
            if (PhosphorIdentity::VirtualScreenId::isVirtual(trackedScreen)
                && PhosphorIdentity::VirtualScreenId::extractPhysicalId(trackedScreen)
                    == PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenId)) {
                resolvedScreen = trackedScreen;
            }
        }
        m_lastActiveScreenId = resolvedScreen;
    }

    qCDebug(lcDbusWindow) << "Window activated:" << windowId << "on screen" << screenId;

    // Update last-used zone when focusing a snapped window
    // Skip auto-snapped windows - only user-focused windows should update the tracking
    QString zoneId = m_service->zoneForWindow(windowId);
    if (!zoneId.isEmpty() && m_settings && m_settings->moveNewWindowsToLastZone()
        && !m_service->isAutoSnapped(windowId)) {
        QString windowClass = m_service->currentAppIdFor(windowId);
        m_service->updateLastUsedZone(zoneId, resolvedScreen, windowClass, currentDesktop());
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Window Tracking Queries - Delegate to Service
// ═══════════════════════════════════════════════════════════════════════════════

QString WindowTrackingAdaptor::getZoneForWindow(const QString& windowId)
{
    if (!validateWindowId(windowId, QStringLiteral("get zone for window"))) {
        return QString();
    }
    // Delegate to service
    return m_service->zoneForWindow(windowId);
}

QStringList WindowTrackingAdaptor::getWindowsInZone(const QString& zoneId)
{
    if (zoneId.isEmpty()) {
        qCWarning(lcDbusWindow) << "getWindowsInZone: empty zone ID";
        return QStringList();
    }
    // Delegate to service
    return m_service->windowsInZone(zoneId);
}

QStringList WindowTrackingAdaptor::getSnappedWindows()
{
    // Delegate to service
    return m_service->snappedWindows();
}

void WindowTrackingAdaptor::pruneStaleWindows(const QStringList& aliveWindowIds)
{
    const QSet<QString> alive(aliveWindowIds.begin(), aliveWindowIds.end());
    int pruned = m_service->pruneStaleAssignments(alive);
    if (m_autotileEngine) {
        pruned += m_autotileEngine->pruneStaleWindows(alive);
    }
    if (pruned > 0) {
        qCInfo(lcDbusWindow) << "Pruned" << pruned << "stale window assignments (not in KWin)";
        scheduleSaveState();
    }
}

PhosphorProtocol::EmptyZoneList WindowTrackingAdaptor::getEmptyZones(const QString& screenId)
{
    return m_service->getEmptyZones(screenId);
}

QStringList WindowTrackingAdaptor::getMultiZoneForWindow(const QString& windowId)
{
    if (!validateWindowId(windowId, QStringLiteral("get multi-zone for window"))) {
        return QStringList();
    }

    // Return stored zone IDs directly (multi-zone support)
    return m_service->zonesForWindow(windowId);
}

QString WindowTrackingAdaptor::getLastUsedZoneId()
{
    // Delegate to service
    return m_service->lastUsedZoneId();
}

// ═══════════════════════════════════════════════════════════════════════════════
// PhosphorZones::Zone Geometry Queries - Delegate to Service
// ═══════════════════════════════════════════════════════════════════════════════

QString WindowTrackingAdaptor::findEmptyZone()
{
    // Use cursor screen for per-screen layout resolution
    return m_service->findEmptyZone(m_lastCursorScreenId);
}

PhosphorProtocol::ZoneGeometryRect WindowTrackingAdaptor::getZoneGeometry(const QString& zoneId)
{
    return getZoneGeometryForScreen(zoneId, QString());
}

PhosphorProtocol::ZoneGeometryRect WindowTrackingAdaptor::getZoneGeometryForScreen(const QString& zoneId,
                                                                                   const QString& screenId)
{
    QRect geo = zoneGeometryRect(zoneId, screenId);
    if (!geo.isValid()) {
        return PhosphorProtocol::ZoneGeometryRect{};
    }
    return PhosphorProtocol::ZoneGeometryRect::fromRect(geo);
}

QRect WindowTrackingAdaptor::zoneGeometryRect(const QString& zoneId, const QString& screenId)
{
    if (zoneId.isEmpty()) {
        qCDebug(lcDbusWindow) << "zoneGeometryRect: empty zone ID";
        return QRect();
    }
    QRect geo = m_service->zoneGeometry(zoneId, screenId);
    if (!geo.isValid()) {
        qCDebug(lcDbusWindow) << "zoneGeometryRect: invalid geometry for zone:" << zoneId;
    }
    return geo;
}

QJsonObject WindowTrackingAdaptor::buildStateObject(const QString& windowId, const QString& zoneId,
                                                    const QJsonArray& zoneIds, const QString& screenId, bool isFloating,
                                                    const QString& changeType) const
{
    QJsonObject obj;
    obj[QLatin1String("windowId")] = windowId;
    obj[QLatin1String("zoneId")] = zoneId;
    obj[QLatin1String("zoneIds")] = zoneIds;
    obj[QLatin1String("screenId")] = screenId;
    obj[QLatin1String("isFloating")] = isFloating;
    obj[QLatin1String("isSticky")] = m_service ? m_service->isWindowSticky(windowId) : false;
    obj[QLatin1String("changeType")] = changeType;
    return obj;
}

} // namespace PlasmaZones
