// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windowtrackingadaptor.h"
#include "zonedetectionadaptor.h"
#include "../autotile/AutotileEngine.h"
#include "../snap/SnapEngine.h"
#include "../core/geometryutils.h"
#include "../core/interfaces.h"
#include "../core/layoutmanager.h"
#include "../core/layout.h"
#include "../core/screenmanager.h"
#include "../core/virtualdesktopmanager.h"
#include "../core/logging.h"
#include "../core/utils.h"
#include "../core/virtualscreen.h"
#include "../core/types.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

namespace PlasmaZones {

WindowTrackingAdaptor::WindowTrackingAdaptor(LayoutManager* layoutManager, IZoneDetector* zoneDetector,
                                             ISettings* settings, VirtualDesktopManager* virtualDesktopManager,
                                             IConfigBackend* configBackend, QObject* parent)
    : QDBusAbstractAdaptor(parent)
    , m_layoutManager(layoutManager)
    , m_settings(settings)
    , m_virtualDesktopManager(virtualDesktopManager)
    , m_configBackend(configBackend)
{
    Q_ASSERT(layoutManager);
    Q_ASSERT(zoneDetector);
    Q_ASSERT(settings);

    // Create business logic service
    m_service = new WindowTrackingService(layoutManager, zoneDetector, settings, virtualDesktopManager, this);

    // Forward service signals to D-Bus
    connect(m_service, &WindowTrackingService::windowZoneChanged, this, &WindowTrackingAdaptor::windowZoneChanged);

    // Connect service state changes to persistence
    connect(m_service, &WindowTrackingService::stateChanged, this, &WindowTrackingAdaptor::scheduleSaveState);

    // Setup debounced save timer (500ms delay to batch rapid state changes)
    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(500);
    connect(m_saveTimer, &QTimer::timeout, this, &WindowTrackingAdaptor::saveState);

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
        QString windowClass = Utils::extractWindowClass(windowId);
        m_service->updateLastUsedZone(zoneId, resolvedScreen, windowClass, currentDesktop);
    }

    qCInfo(lcDbusWindow) << "Window" << windowId << "snapped to zone" << zoneId << "on screen" << resolvedScreen;

    // Emit unified state change
    QJsonObject stateObj =
        buildStateObject(windowId, zoneId, QJsonArray{zoneId}, resolvedScreen, false, QStringLiteral("snapped"));
    Q_EMIT windowStateChanged(windowId, QString::fromUtf8(QJsonDocument(stateObj).toJson(QJsonDocument::Compact)));
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
        QString windowClass = Utils::extractWindowClass(windowId);
        m_service->updateLastUsedZone(primaryZoneId, resolvedScreen, windowClass, currentDesktop);
    }

    qCInfo(lcDbusWindow) << "Window" << windowId << "snapped to multi-zone:" << zoneIds << "on screen"
                         << resolvedScreen;

    QJsonArray zoneIdsArr;
    for (const QString& zid : zoneIds)
        zoneIdsArr.append(zid);
    QJsonObject stateObj =
        buildStateObject(windowId, primaryZoneId, zoneIdsArr, resolvedScreen, false, QStringLiteral("snapped"));
    Q_EMIT windowStateChanged(windowId, QString::fromUtf8(QJsonDocument(stateObj).toJson(QJsonDocument::Compact)));
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
    QJsonObject stateObj =
        buildStateObject(windowId, QString(), QJsonArray(), QString(), false, QStringLiteral("unsnapped"));
    Q_EMIT windowStateChanged(windowId, QString::fromUtf8(QJsonDocument(stateObj).toJson(QJsonDocument::Compact)));
}

void WindowTrackingAdaptor::windowsSnappedBatch(const QString& batchJson)
{
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(batchJson.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        qCWarning(lcDbusWindow) << "windowsSnappedBatch: invalid JSON:" << parseError.errorString();
        return;
    }

    const QJsonArray entries = doc.array();
    qCInfo(lcDbusWindow) << "windowsSnappedBatch: processing" << entries.size() << "entries";

    for (const QJsonValue& val : entries) {
        QJsonObject obj = val.toObject();
        QString windowId = obj.value(QLatin1String("windowId")).toString();
        bool isRestore = obj.value(QLatin1String("isRestore")).toBool(false);

        if (windowId.isEmpty()) {
            continue;
        }

        if (isRestore) {
            // Window's zone exceeded the new layout — unsnap and clear pre-tile geometry
            windowUnsnapped(windowId);
            clearPreTileGeometry(windowId);
        } else {
            QString zoneId = obj.value(QLatin1String("zoneId")).toString();
            QString screenId = obj.value(QLatin1String("screenId")).toString();
            windowSnapped(windowId, zoneId, screenId);
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
            // Different physical screen — resolve via zone geometry as fallback
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

    qCInfo(lcDbusWindow) << "windowScreenChanged:" << windowId << "moved from" << storedScreen << "to" << newScreenId
                         << "- unsnapping";
    m_service->clearStalePendingAssignment(windowId);
    m_service->unassignWindow(windowId);

    // Emit unified state change for screen-change-triggered unsnap
    QJsonObject stateObj =
        buildStateObject(windowId, QString(), QJsonArray(), newScreenId, false, QStringLiteral("screen_changed"));
    Q_EMIT windowStateChanged(windowId, QString::fromUtf8(QJsonDocument(stateObj).toJson(QJsonDocument::Compact)));
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

    m_service->windowClosed(windowId);
    qCDebug(lcDbusWindow) << "Cleaned up tracking data for closed window" << windowId;
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
    if (!screenId.isEmpty()) {
        QString resolvedScreen = screenId;
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
        QString windowClass = Utils::extractWindowClass(windowId);
        int currentDesktop = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;
        m_service->updateLastUsedZone(zoneId, screenId, windowClass, currentDesktop);
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

QString WindowTrackingAdaptor::getEmptyZonesJson(const QString& screenId)
{
    return m_service->getEmptyZonesJson(screenId);
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

QString WindowTrackingAdaptor::getZoneGeometry(const QString& zoneId)
{
    return getZoneGeometryForScreen(zoneId, QString());
}

QString WindowTrackingAdaptor::getZoneGeometryForScreen(const QString& zoneId, const QString& screenId)
{
    if (zoneId.isEmpty()) {
        qCDebug(lcDbusWindow) << "getZoneGeometryForScreen: empty zone ID";
        return QString();
    }

    // Delegate to service
    QRect geo = m_service->zoneGeometry(zoneId, screenId);
    if (!geo.isValid()) {
        qCDebug(lcDbusWindow) << "getZoneGeometryForScreen: invalid geometry for zone:" << zoneId;
        return QString();
    }

    return GeometryUtils::rectToJson(geo);
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
    obj[QLatin1String("changeType")] = changeType;
    return obj;
}

} // namespace PlasmaZones
