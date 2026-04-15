// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windowtrackingadaptor.h"
#include "snapnavigationtargets.h"
#include "windowtrackingadaptor/persistenceworker.h"
#include "zonedetectionadaptor.h"
#include "../autotile/AutotileEngine.h"
#include "../snap/SnapEngine.h"
#include "../config/iconfigbackend.h"
#include "../config/configbackend_json.h"
#include "../core/interfaces.h"
#include "../core/layoutmanager.h"
#include "../core/layout.h"
#include "../core/screenmanager.h"
#include "../core/virtualdesktopmanager.h"
#include "../core/logging.h"
#include "../core/screenmoderouter.h"
#include "../core/utils.h"
#include "../core/virtualscreen.h"
#include "../core/types.h"
#include "../core/windowregistry.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

namespace PlasmaZones {

WindowTrackingAdaptor::WindowTrackingAdaptor(LayoutManager* layoutManager, IZoneDetector* zoneDetector,
                                             ISettings* settings, VirtualDesktopManager* virtualDesktopManager,
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

    // Create business logic service
    m_service = new WindowTrackingService(layoutManager, zoneDetector, settings, virtualDesktopManager, this);

    // Snap-mode navigation target resolver — pure compute, owned here, fed
    // via a lambda that forwards into this adaptor's navigationFeedback
    // signal. The ZoneDetectionAdaptor is wired later via setZoneDetectionAdaptor.
    m_targetResolver = std::make_unique<SnapNavigationTargetResolver>(
        m_service, m_layoutManager, /*zoneDetector=*/nullptr,
        [this](bool success, const QString& action, const QString& reason, const QString& sourceZoneId,
               const QString& targetZoneId, const QString& screenId) {
            Q_EMIT navigationFeedback(success, action, reason, sourceZoneId, targetZoneId, screenId);
        });

    // Forward service signals to D-Bus
    connect(m_service, &WindowTrackingService::windowZoneChanged, this, &WindowTrackingAdaptor::windowZoneChanged);

    // Connect service state changes to persistence
    connect(m_service, &WindowTrackingService::stateChanged, this, &WindowTrackingAdaptor::scheduleSaveState);

    // Setup debounced save timer (500ms delay to batch rapid state changes)
    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(500);
    connect(m_saveTimer, &QTimer::timeout, this, &WindowTrackingAdaptor::saveState);

    // Persistence I/O worker — disk writes happen off the main thread.
    // On a verified-successful write, clear the backend's dirty flag so
    // the next saveState() can be a no-op. On failure we leave the dirty
    // flag alone so the next timer tick retries the write.
    m_persistenceWorker = std::make_unique<PersistenceWorker>(nullptr);
    connect(m_persistenceWorker.get(), &PersistenceWorker::writeCompleted, this,
            [this](const QString& filePath, bool success) {
                if (!success) {
                    qCWarning(lcDbusWindow) << "session state write failed for" << filePath << "— will retry";
                    return;
                }
                if (auto* json = dynamic_cast<JsonConfigBackend*>(m_sessionBackend.get())) {
                    if (json->filePath() == filePath) {
                        json->clearDirty();
                    }
                }
            });

    // Connect to layout changes for pending restores notification
    connect(m_layoutManager, &LayoutManager::activeLayoutChanged, this, &WindowTrackingAdaptor::onLayoutChanged);

    // Deferred: ScreenManager may not be initialized yet during adaptor construction.
    // If ScreenManager is still unavailable after the first event loop iteration,
    // virtual screen signals won't be connected until the next daemon restart or
    // screen change. Window restoration will still work via onLayoutChanged() ->
    // tryEmitPendingRestoresAvailable(), which emits immediately when
    // isPanelGeometryReady() returns false (no ScreenManager instance).
    QTimer::singleShot(0, this, [this]() {
        auto* screenMgr = ScreenManager::instance();
        if (!screenMgr) {
            qCWarning(lcDbusWindow)
                << "ScreenManager instance not available - window restoration may use incorrect geometry";
            return;
        }
        connect(screenMgr, &ScreenManager::panelGeometryReady, this, &WindowTrackingAdaptor::onPanelGeometryReady);
        // If panel geometry is already ready, trigger the check now
        if (ScreenManager::isPanelGeometryReady()) {
            onPanelGeometryReady();
        }
    });

    // Load persisted window tracking state from previous session
    loadState();

    // If we have pending restores but missed activeLayoutChanged (layout was set before we
    // connected), set the flag so tryEmitPendingRestoresAvailable will emit when panel
    // geometry is ready. Fixes daemon restart: windows that were snapped before stop
    // are not re-registered because pendingRestoresAvailable was never emitted.
    if (!m_service->pendingRestoreQueues().isEmpty() && m_layoutManager->activeLayout()) {
        m_hasPendingRestores = true;
        qCDebug(lcDbusWindow) << "Pending restores: loaded at init, will emit when panel geometry ready";
    }
}

// Out-of-line so unique_ptr<SnapNavigationTargetResolver> can destroy the
// resolver here where snapnavigationtargets.h is included and the type is
// complete. Declaring `= default` in the header would require the full type
// at every include site.
WindowTrackingAdaptor::~WindowTrackingAdaptor() = default;

// ═══════════════════════════════════════════════════════════════════════════════
// Engine wiring — cross-references and shared navigation feedback
//
// Signal relay is handled by dedicated adaptors (SnapAdaptor, AutotileAdaptor).
// This method only wires cross-engine references and the shared OSD path.
// ═══════════════════════════════════════════════════════════════════════════════

void WindowTrackingAdaptor::setEngines(SnapEngine* snapEngine, AutotileEngine* autotileEngine)
{
    // Disconnect previous autotile engine nav feedback (the only signal connected here)
    if (m_autotileEngine) {
        disconnect(m_autotileEngine, &AutotileEngine::navigationFeedbackRequested, this, nullptr);
    }

    m_snapEngine = snapEngine;
    m_autotileEngine = autotileEngine;

    // ═══════════════════════════════════════════════════════════════════════════
    // Cross-engine references — SnapEngine needs AutotileEngine for
    // isActiveOnScreen() routing and ZoneDetectionAdaptor for adjacency queries.
    // When clearing (nullptr, nullptr), we also clear stale cross-references
    // to prevent dangling pointer access.
    // ═══════════════════════════════════════════════════════════════════════════
    if (m_snapEngine) {
        m_snapEngine->setZoneDetectionAdaptor(m_zoneDetectionAdaptor);
        m_snapEngine->setAutotileEngine(m_autotileEngine);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // AutotileEngine navigation feedback — shared OSD path
    //
    // Both engines' navigation feedback routes through this adaptor's
    // navigationFeedback signal because the KWin effect listens for OSD
    // data on org.plasmazones.WindowTracking regardless of engine mode.
    //
    // SnapEngine's navigation feedback is connected by SnapAdaptor (mirrors
    // AutotileAdaptor's constructor pattern). This single connection is the
    // only autotile signal that routes through WTA — all other autotile
    // signals go through AutotileAdaptor on org.plasmazones.Autotile.
    //
    // NOTE: AutotileEngine::windowFloatingChanged is NOT relayed here.
    // The daemon intercepts it with a lambda (signals.cpp) for cross-mode
    // state management (autotile-float markers, snap-float preservation,
    // geometry application). See ADR docs/adr-snapengine-migration.md.
    // ═══════════════════════════════════════════════════════════════════════════
    if (m_autotileEngine) {
        connect(m_autotileEngine, &AutotileEngine::navigationFeedbackRequested, this,
                &WindowTrackingAdaptor::navigationFeedback);
    }
}

void WindowTrackingAdaptor::setScreenModeRouter(ScreenModeRouter* router)
{
    m_screenModeRouter = router;
}

void WindowTrackingAdaptor::setZoneDetectionAdaptor(ZoneDetectionAdaptor* adaptor)
{
    m_zoneDetectionAdaptor = adaptor;
    if (m_targetResolver) {
        m_targetResolver->setZoneDetector(adaptor);
    }
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

// ═══════════════════════════════════════════════════════════════════════════════
// Window Snapping - Delegate to Service
// ═══════════════════════════════════════════════════════════════════════════════

void WindowTrackingAdaptor::windowSnapped(const QString& windowId, const QString& zoneId, const QString& screenId)
{
    if (!validateWindowId(windowId, QStringLiteral("track window snap"))) {
        return;
    }

    if (zoneId.isEmpty()) {
        qCWarning(lcDbusWindow) << "Window snap: cannot track, empty zone ID";
        return;
    }

    clearFloatingStateForSnap(windowId, screenId);

    // Check if this was an auto-snap (restore from session or snap to last zone)
    // and clear the flag. Auto-snapped windows don't update last-used zone tracking.
    bool wasAutoSnapped = m_service->clearAutoSnapped(windowId);

    // If NOT auto-snapped (user explicitly snapped), clear any stale pending
    // assignment from a previous session. This prevents the window from restoring
    // to the wrong zone if it's closed and reopened.
    if (!wasAutoSnapped) {
        m_service->clearStalePendingAssignment(windowId);
    }

    // Use caller-provided screen name if available, otherwise auto-detect,
    // then fall back to cursor/active screen as tertiary fallback
    QString resolvedScreen = resolveScreenForSnap(screenId, zoneId);

    int currentDesktop = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;

    // Delegate to service
    m_service->assignWindowToZone(windowId, zoneId, resolvedScreen, currentDesktop);

    // Update last used zone (skip zone selector special IDs and auto-snapped windows)
    if (!zoneId.startsWith(QStringLiteral("zoneselector-")) && !wasAutoSnapped) {
        QString windowClass = m_service->currentAppIdFor(windowId);
        m_service->updateLastUsedZone(zoneId, resolvedScreen, windowClass, currentDesktop);
    }

    qCInfo(lcDbusWindow) << "Window" << windowId << "snapped to zone" << zoneId << "on screen" << resolvedScreen;

    // Emit unified state change
    Q_EMIT windowStateChanged(
        windowId,
        WindowStateEntry{windowId, zoneId, resolvedScreen, false, QStringLiteral("snapped"), QStringList{}, false});
}

void WindowTrackingAdaptor::windowSnappedMultiZone(const QString& windowId, const QStringList& zoneIds,
                                                   const QString& screenId)
{
    if (!validateWindowId(windowId, QStringLiteral("track multi-zone window snap"))) {
        return;
    }

    if (zoneIds.isEmpty() || zoneIds.first().isEmpty()) {
        qCWarning(lcDbusWindow) << "Multi-zone window snap: cannot track, empty zone IDs";
        return;
    }

    clearFloatingStateForSnap(windowId, screenId);

    bool wasAutoSnapped = m_service->clearAutoSnapped(windowId);

    if (!wasAutoSnapped) {
        m_service->clearStalePendingAssignment(windowId);
    }

    // Use caller-provided screen name if available, otherwise auto-detect,
    // then fall back to cursor/active screen as tertiary fallback
    QString primaryZoneId = zoneIds.first();
    QString resolvedScreen = resolveScreenForSnap(screenId, primaryZoneId);

    int currentDesktop = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;

    // Delegate to service with all zone IDs
    m_service->assignWindowToZones(windowId, zoneIds, resolvedScreen, currentDesktop);

    // Update last used zone with primary (skip zone selector special IDs and auto-snapped)
    if (!primaryZoneId.startsWith(QStringLiteral("zoneselector-")) && !wasAutoSnapped) {
        QString windowClass = m_service->currentAppIdFor(windowId);
        m_service->updateLastUsedZone(primaryZoneId, resolvedScreen, windowClass, currentDesktop);
    }

    qCInfo(lcDbusWindow) << "Window" << windowId << "snapped to multi-zone:" << zoneIds << "on screen"
                         << resolvedScreen;

    Q_EMIT windowStateChanged(
        windowId,
        WindowStateEntry{windowId, primaryZoneId, resolvedScreen, false, QStringLiteral("snapped"), zoneIds, false});
}

void WindowTrackingAdaptor::windowUnsnapped(const QString& windowId)
{
    if (!validateWindowId(windowId, QStringLiteral("untrack window"))) {
        return;
    }

    QString previousZoneId = m_service->zoneForWindow(windowId);
    if (previousZoneId.isEmpty()) {
        qCWarning(lcDbusWindow) << "Window not found for unsnap:" << windowId;
        return;
    }

    // Clear pending assignment so window won't be auto-restored on next focus/reopen
    m_service->clearStalePendingAssignment(windowId);

    // Delegate to service
    m_service->unassignWindow(windowId);

    qCInfo(lcDbusWindow) << "Window" << windowId << "unsnapped from zone" << previousZoneId;

    // Emit unified state change
    Q_EMIT windowStateChanged(
        windowId,
        WindowStateEntry{windowId, QString(), QString(), false, QStringLiteral("unsnapped"), QStringList{}, false});
}

void WindowTrackingAdaptor::windowsSnappedBatch(const SnapConfirmationList& entries)
{
    qCInfo(lcDbusWindow) << "windowsSnappedBatch: processing" << entries.size() << "entries";

    // Note: entry.isRestore == true means the window should be unsnapped (its zone no
    // longer exists in the new layout), NOT restored to a zone. The name is misleading
    // but preserved for wire compatibility.
    for (const auto& entry : entries) {
        if (entry.windowId.isEmpty()) {
            continue;
        }

        if (entry.isRestore) {
            // Window's zone exceeded the new layout — unsnap and clear pre-tile geometry
            windowUnsnapped(entry.windowId);
            clearPreTileGeometry(entry.windowId);
        } else {
            windowSnapped(entry.windowId, entry.zoneId, entry.screenId);
        }
    }
}

void WindowTrackingAdaptor::windowScreenChanged(const QString& windowId, const QString& newScreenId)
{
    if (!validateWindowId(windowId, QStringLiteral("screen changed"))) {
        return;
    }

    // Check if the window is snapped — if not, nothing to do
    QString currentZoneId = m_service->zoneForWindow(windowId);
    if (currentZoneId.isEmpty()) {
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
    if (!VirtualScreenId::isVirtual(newScreenId) && VirtualScreenId::isVirtual(storedScreen)) {
        QString storedPhysical = VirtualScreenId::extractPhysicalId(storedScreen);
        if (Utils::screensMatch(storedPhysical, newScreenId)) {
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
                QString vsId = Utils::effectiveScreenIdAt(zoneGeo.center());
                if (!vsId.isEmpty()) {
                    resolvedNewScreen = vsId;
                }
            }
        }
    }

    if (Utils::screensMatch(storedScreen, resolvedNewScreen)) {
        qCDebug(lcDbusWindow) << "windowScreenChanged:" << windowId << "moved to assigned screen, keeping snap";
        return;
    }

    qCInfo(lcDbusWindow) << "windowScreenChanged:" << windowId << "moved from" << storedScreen << "to"
                         << resolvedNewScreen << "- unsnapping";
    m_service->clearStalePendingAssignment(windowId);
    m_service->unassignWindow(windowId);

    // Emit unified state change for screen-change-triggered unsnap
    Q_EMIT windowStateChanged(windowId,
                              WindowStateEntry{windowId, QString(), newScreenId, false,
                                               QStringLiteral("screen_changed"), QStringList{}, false});
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

void WindowTrackingAdaptor::windowClosed(const QString& windowId)
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

    m_service->windowClosed(windowId);

    // Drop registry state last: consumers subscribed to windowDisappeared may
    // rely on other WTS state still being present during their cleanup. The
    // canonical release MUST happen after remove() because WindowRegistry's
    // disappear signal fires synchronously from remove() and subscribers may
    // still call canonicalizeForLookup on their way out.
    if (m_windowRegistry) {
        const QString instanceId = Utils::extractInstanceId(windowId);
        m_windowRegistry->remove(instanceId);
        m_windowRegistry->releaseCanonical(instanceId);
    }

    qCDebug(lcDbusWindow) << "Cleaned up tracking data for closed window" << windowId;
}

void WindowTrackingAdaptor::setWindowRegistry(WindowRegistry* registry)
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
    QObject::connect(registry, &WindowRegistry::metadataChanged, this,
                     [this](const QString& instanceId, const WindowMetadata& oldMeta, const WindowMetadata& newMeta) {
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
                                              const QString& desktopFile, const QString& title)
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

    WindowMetadata meta;
    meta.appId = appId;
    meta.desktopFile = desktopFile;
    meta.title = title;
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
    if (!VirtualScreenId::isVirtual(screenId)) {
        auto* mgr = ScreenManager::instance();
        if (mgr && mgr->hasVirtualScreens(screenId)) {
            // Use focused window's tracked screen as hint
            if (m_service && !m_lastActiveWindowId.isEmpty()) {
                const QString trackedScreen = m_service->screenAssignments().value(m_lastActiveWindowId);
                if (VirtualScreenId::isVirtual(trackedScreen)
                    && VirtualScreenId::extractPhysicalId(trackedScreen) == screenId) {
                    resolvedId = trackedScreen;
                }
            }
            // If no window hint, fall back to first virtual screen
            if (!VirtualScreenId::isVirtual(resolvedId)) {
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
        if (!VirtualScreenId::isVirtual(screenId) && m_service) {
            const QString trackedScreen = m_service->screenAssignments().value(windowId);
            if (VirtualScreenId::isVirtual(trackedScreen)
                && VirtualScreenId::extractPhysicalId(trackedScreen) == VirtualScreenId::extractPhysicalId(screenId)) {
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
        int currentDesktop = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;
        m_service->updateLastUsedZone(zoneId, resolvedScreen, windowClass, currentDesktop);
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
    if (pruned > 0) {
        qCInfo(lcDbusWindow) << "Pruned" << pruned << "stale window assignments (not in KWin)";
        scheduleSaveState();
    }
}

EmptyZoneList WindowTrackingAdaptor::getEmptyZones(const QString& screenId)
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
// Zone Geometry Queries - Delegate to Service
// ═══════════════════════════════════════════════════════════════════════════════

QString WindowTrackingAdaptor::findEmptyZone()
{
    // Use cursor screen for per-screen layout resolution
    return m_service->findEmptyZone(m_lastCursorScreenId);
}

ZoneGeometryRect WindowTrackingAdaptor::getZoneGeometry(const QString& zoneId)
{
    return getZoneGeometryForScreen(zoneId, QString());
}

ZoneGeometryRect WindowTrackingAdaptor::getZoneGeometryForScreen(const QString& zoneId, const QString& screenId)
{
    QRect geo = zoneGeometryRect(zoneId, screenId);
    if (!geo.isValid()) {
        return ZoneGeometryRect{};
    }
    return ZoneGeometryRect::fromRect(geo);
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
