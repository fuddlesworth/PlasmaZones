// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windowtrackingservice.h"
#include "constants.h"
#include "interfaces.h"
#include <PhosphorZones/Layout.h>
#include "screenmanager.h"
#include "virtualscreen.h"
#include <PhosphorZones/Zone.h>
#include "layoutmanager.h"
#include "virtualdesktopmanager.h"
#include "utils.h"
#include "logging.h"
#include "windowregistry.h"
#include <QScreen>
#include <QSet>
#include <QUuid>
#include <algorithm>

namespace PlasmaZones {

WindowTrackingService::WindowTrackingService(LayoutManager* layoutManager, IZoneDetector* zoneDetector,
                                             ISettings* settings, VirtualDesktopManager* vdm, QObject* parent)
    : QObject(parent)
    , m_layoutManager(layoutManager)
    , m_zoneDetector(zoneDetector)
    , m_settings(settings)
    , m_virtualDesktopManager(vdm)
{
    Q_ASSERT(layoutManager);
    Q_ASSERT(zoneDetector);
    Q_ASSERT(settings);

    // Note: No save timer needed - persistence handled by WindowTrackingAdaptor via KConfig
    // Service just emits stateChanged() signal when state changes
    //
    // Layout change handling: WindowTrackingAdaptor connects to activeLayoutChanged and calls
    // onLayoutChanged(). Do NOT connect here - duplicate invocation would clear m_resnapBuffer
    // on the second run (after assignments were already removed), causing no_windows_to_resnap.

    // Note: Persistence is handled by WindowTrackingAdaptor via KConfig.
    // The service is a pure in-memory state manager - adaptor calls
    // populateState() after loading from KConfig.
}

WindowTrackingService::~WindowTrackingService()
{
    // Note: Persistence is handled by WindowTrackingAdaptor via KConfig
    // Service is purely in-memory state management
}

QString WindowTrackingService::currentAppIdFor(const QString& anyWindowId) const
{
    if (anyWindowId.isEmpty()) {
        return QString();
    }
    if (m_windowRegistry) {
        const QString instanceId = Utils::extractInstanceId(anyWindowId);
        const QString fromRegistry = m_windowRegistry->appIdFor(instanceId);
        if (!fromRegistry.isEmpty()) {
            return fromRegistry;
        }
    }
    return Utils::extractAppId(anyWindowId);
}

QString WindowTrackingService::canonicalizeForLookup(const QString& rawWindowId) const
{
    if (rawWindowId.isEmpty()) {
        return rawWindowId;
    }
    if (m_windowRegistry) {
        return m_windowRegistry->canonicalizeForLookup(rawWindowId);
    }
    return rawWindowId;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Zone Assignment Management
// ═══════════════════════════════════════════════════════════════════════════════

void WindowTrackingService::assignWindowToZone(const QString& windowId, const QString& zoneId, const QString& screenId,
                                               int virtualDesktop)
{
    assignWindowToZones(windowId, QStringList{zoneId}, screenId, virtualDesktop);
}

void WindowTrackingService::assignWindowToZones(const QString& windowId, const QStringList& zoneIds,
                                                const QString& screenId, int virtualDesktop)
{
    if (windowId.isEmpty() || zoneIds.isEmpty()) {
        return;
    }

    // Filter out empty/null zone IDs — callers may pass partially-valid lists
    QStringList validZoneIds;
    validZoneIds.reserve(zoneIds.size());
    for (const auto& id : zoneIds) {
        if (!id.isEmpty()) {
            validZoneIds.append(id);
        }
    }
    if (validZoneIds.isEmpty()) {
        return;
    }

    // Only emit signal if value actually changed
    QStringList previousZones = m_windowZoneAssignments.value(windowId);
    bool zoneChanged = (previousZones != validZoneIds);

    m_windowZoneAssignments[windowId] = validZoneIds;
    m_windowScreenAssignments[windowId] = screenId;
    m_windowDesktopAssignments[windowId] = virtualDesktop;

    // Clear stale autotile-floated flag when a window is zone-assigned in snap mode.
    // A window that crossed from an autotile VS to a snap VS via drag keeps its
    // autotileFloated marker (only windowsReleasedFromTiling clears it). Without
    // this, a subsequent mode change on the autotile VS incorrectly processes the
    // window (already snapped on the snap VS) as if it were still autotile-managed.
    m_autotileFloatedWindows.remove(windowId);

    // NOTE: Do NOT store to m_pendingRestoreQueues here!
    // Pending assignments are for session persistence and should only be populated
    // when a window closes (in windowClosed()). Storing here causes ALL previously-snapped
    // windows to auto-restore on open, even when they shouldn't.

    if (zoneChanged) {
        Q_EMIT windowZoneChanged(windowId, validZoneIds.first());
    }
    // Only the zone/screen/desktop maps changed. Narrower than DirtyAll so
    // the next save rewrites exactly one JSON field instead of all ten.
    markDirty(DirtyZoneAssignments);
}

void WindowTrackingService::unassignWindow(const QString& windowId)
{
    // Get the zones before removing (needed for last-used zone check)
    QStringList previousZoneIds = m_windowZoneAssignments.take(windowId);
    if (previousZoneIds.isEmpty()) {
        return; // Window wasn't assigned, nothing to do
    }

    m_windowScreenAssignments.remove(windowId);
    m_windowDesktopAssignments.remove(windowId);

    // Clear last-used zone if we're unsnapping from it. Track whether this
    // branch ran so the dirty mask accurately reflects what changed.
    bool lastUsedCleared = false;
    if (!m_lastUsedZoneId.isEmpty() && previousZoneIds.contains(m_lastUsedZoneId)) {
        m_lastUsedZoneId.clear();
        m_lastUsedScreenId.clear();
        m_lastUsedZoneClass.clear();
        m_lastUsedDesktop = 0;
        lastUsedCleared = true;
    }

    // Don't remove from pending - keep for session restore
    // (pending is keyed by app ID anyway)

    Q_EMIT windowZoneChanged(windowId, QString());
    markDirty(DirtyZoneAssignments | (lastUsedCleared ? DirtyLastUsedZone : DirtyNone));
}

QString WindowTrackingService::zoneForWindow(const QString& windowId) const
{
    const QStringList zones = m_windowZoneAssignments.value(windowId);
    return zones.isEmpty() ? QString() : zones.first();
}

QStringList WindowTrackingService::zonesForWindow(const QString& windowId) const
{
    return m_windowZoneAssignments.value(windowId);
}

QStringList WindowTrackingService::windowsInZone(const QString& zoneId) const
{
    QStringList result;
    for (auto it = m_windowZoneAssignments.constBegin(); it != m_windowZoneAssignments.constEnd(); ++it) {
        if (it.value().contains(zoneId)) {
            result.append(it.key());
        }
    }
    return result;
}

QStringList WindowTrackingService::snappedWindows() const
{
    return m_windowZoneAssignments.keys();
}

int WindowTrackingService::pruneStaleAssignments(const QSet<QString>& aliveWindowIds)
{
    int pruned = 0;
    auto it = m_windowZoneAssignments.begin();
    while (it != m_windowZoneAssignments.end()) {
        if (!aliveWindowIds.contains(it.key())) {
            const QString& wid = it.key();
            m_windowScreenAssignments.remove(wid);
            m_windowDesktopAssignments.remove(wid);
            m_preTileGeometries.remove(wid);
            m_preFloatZoneAssignments.remove(wid);
            m_preFloatScreenAssignments.remove(wid);
            m_floatingWindows.remove(wid);
            m_autotileFloatedWindows.remove(wid);
            it = m_windowZoneAssignments.erase(it);
            ++pruned;
        } else {
            ++it;
        }
    }

    // Sweep tracking hashes/sets that can have entries without a zone assignment.
    // windowClosed() cleans all of these per-window; prune must do the same
    // for windows that disappeared without a close event.
    //
    // The first loop removed stale entries from maps keyed by zone-assigned windows.
    // This second sweep catches entries in maps that can have entries WITHOUT a
    // corresponding m_windowZoneAssignments entry (e.g. floating windows with
    // pre-float state but no current zone assignment).
    auto removeIfNotAlive = [&](auto& hash) {
        for (auto hashIt = hash.begin(); hashIt != hash.end();) {
            if (!aliveWindowIds.contains(hashIt.key())) {
                hashIt = hash.erase(hashIt);
                ++pruned;
            } else {
                ++hashIt;
            }
        }
    };

    removeIfNotAlive(m_windowStickyStates);
    removeIfNotAlive(m_preFloatZoneAssignments);
    removeIfNotAlive(m_preFloatScreenAssignments);

    // Sweep QSet members (different iterator API)
    auto removeSetIfNotAlive = [&](auto& set) {
        for (auto setIt = set.begin(); setIt != set.end();) {
            if (!aliveWindowIds.contains(*setIt)) {
                setIt = set.erase(setIt);
                ++pruned;
            } else {
                ++setIt;
            }
        }
    };

    removeSetIfNotAlive(m_savedSnapFloatingWindows);
    removeSetIfNotAlive(m_autoSnappedWindows);
    removeSetIfNotAlive(m_effectReportedWindows);

    // Persist the prune. Without this, delta-persistence short-circuits in
    // saveState() (takeDirty() == DirtyNone) and stale entries come back as
    // ghost windows on the next daemon restart — exactly the bug that the
    // initial pruneStaleWindows fix was written to prevent.
    if (pruned > 0) {
        markDirty(DirtyZoneAssignments | DirtyPreTileGeometries | DirtyPreFloatZones | DirtyPreFloatScreens);
    }

    return pruned;
}

bool WindowTrackingService::isWindowSnapped(const QString& windowId) const
{
    auto it = m_windowZoneAssignments.constFind(windowId);
    return it != m_windowZoneAssignments.constEnd() && !it->isEmpty();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Pre-Tile Geometry Storage (unified snap + autotile)
// ═══════════════════════════════════════════════════════════════════════════════

void WindowTrackingService::storePreTileGeometry(const QString& windowId, const QRect& geometry,
                                                 const QString& connectorName, bool overwrite)
{
    if (windowId.isEmpty()) {
        qCWarning(lcCore) << "Cannot store pre-tile geometry: empty windowId";
        return;
    }
    if (!geometry.isValid()) {
        return;
    }

    QString appId = currentAppIdFor(windowId);

    if (!overwrite) {
        // First-only mode (snap): don't overwrite this runtime instance's already-
        // captured pre-snap entry when moving A→B. Match on the EXACT windowId only.
        //
        // Do NOT skip on a matching appId entry: appId-keyed entries are persisted
        // across daemon restarts and window close/reopen (for cross-session restore).
        // A stale appId entry from a prior session must never block the fresh per-
        // instance capture — otherwise float-restore/mode-change teleports the new
        // window back to ancient coordinates. The fresh write below replaces the
        // appId entry with current-session data.
        if (m_preTileGeometries.contains(windowId)) {
            qCDebug(lcCore) << "storePreTileGeometry: skipping (windowId exists)" << windowId
                            << "existing:" << m_preTileGeometries.value(windowId).geometry << "proposed:" << geometry;
            return;
        }
    }

    PreTileGeometry entry{geometry, connectorName};
    qCDebug(lcCore) << "storePreTileGeometry:" << windowId << "=" << geometry << "screen:" << connectorName
                    << "overwrite:" << overwrite;
    m_preTileGeometries[windowId] = entry;
    if (appId != windowId) {
        m_preTileGeometries[appId] = entry;
    }

    // Memory cleanup: limit cache to prevent unbounded growth.
    // Each window stores up to 2 keys (windowId + appId), so evict until we're
    // back at the cap. Skip just-inserted keys.
    // Note: The inner loop is O(N) per eviction, but N is bounded by
    // MaxPreTileGeometries (100), so the total cost is acceptable.
    static constexpr int MaxPreTileGeometries = 100;
    while (m_preTileGeometries.size() > MaxPreTileGeometries) {
        bool evicted = false;
        for (auto it = m_preTileGeometries.begin(); it != m_preTileGeometries.end(); ++it) {
            if (it.key() != windowId && it.key() != appId) {
                m_preTileGeometries.erase(it);
                evicted = true;
                break;
            }
        }
        if (!evicted) {
            break; // Only our own keys remain
        }
    }

    markDirty(DirtyPreTileGeometries);
}

std::optional<QRect> WindowTrackingService::preTileGeometry(const QString& windowId) const
{
    if (windowId.isEmpty()) {
        return std::nullopt;
    }
    if (m_preTileGeometries.contains(windowId)) {
        const auto& entry = m_preTileGeometries.value(windowId);
        qCDebug(lcCore) << "preTileGeometry: found by windowId" << windowId << "=" << entry.geometry
                        << "screen:" << entry.connectorName;
        return entry.geometry;
    }
    QString appId = currentAppIdFor(windowId);
    if (appId != windowId && m_preTileGeometries.contains(appId)) {
        const auto& entry = m_preTileGeometries.value(appId);
        qCDebug(lcCore) << "preTileGeometry: found by appId" << appId << "=" << entry.geometry
                        << "screen:" << entry.connectorName;
        return entry.geometry;
    }
    qCDebug(lcCore) << "preTileGeometry: not found for" << windowId;
    return std::nullopt;
}

bool WindowTrackingService::hasPreTileGeometry(const QString& windowId) const
{
    if (windowId.isEmpty()) {
        return false;
    }
    if (m_preTileGeometries.contains(windowId)) {
        return true;
    }
    QString appId = currentAppIdFor(windowId);
    return (appId != windowId && m_preTileGeometries.contains(appId));
}

void WindowTrackingService::clearPreTileGeometry(const QString& windowId)
{
    if (windowId.isEmpty()) {
        return;
    }
    bool removed = m_preTileGeometries.remove(windowId) > 0;
    QString appId = currentAppIdFor(windowId);
    if (appId != windowId) {
        removed |= (m_preTileGeometries.remove(appId) > 0);
    }
    if (removed) {
        qCDebug(lcCore) << "clearPreTileGeometry:" << windowId;
        markDirty(DirtyPreTileGeometries);
    }
}

std::optional<QRect> WindowTrackingService::validatedPreTileGeometry(const QString& windowId,
                                                                     const QString& currentScreenName) const
{
    return validatePreTileEntry(windowId, currentScreenName, /*exactOnly=*/false);
}

std::optional<QRect> WindowTrackingService::validatedPreTileGeometryExact(const QString& windowId,
                                                                          const QString& currentScreenName) const
{
    return validatePreTileEntry(windowId, currentScreenName, /*exactOnly=*/true);
}

std::optional<QRect> WindowTrackingService::validatePreTileEntry(const QString& windowId,
                                                                 const QString& currentScreenName, bool exactOnly) const
{
    if (windowId.isEmpty()) {
        return std::nullopt;
    }

    // Look up the full entry (with screen context)
    QString storedScreen;
    QRect rect;
    auto lookupEntry = [&](const QString& key) -> bool {
        auto it = m_preTileGeometries.constFind(key);
        if (it == m_preTileGeometries.constEnd()) {
            return false;
        }
        rect = it->geometry;
        storedScreen = it->connectorName;
        return true;
    };
    if (!lookupEntry(windowId)) {
        if (exactOnly) {
            return std::nullopt;
        }
        QString appId = currentAppIdFor(windowId);
        if (appId == windowId || !lookupEntry(appId)) {
            return std::nullopt;
        }
    }

    if (!rect.isValid() || rect.width() <= 0 || rect.height() <= 0) {
        return std::nullopt;
    }

    // Cross-screen check: if the geometry was captured on a different screen than where
    // the window currently is, the absolute coordinates are wrong. Preserve the size
    // but center on the current screen. This triggers for:
    // 1. Different physical monitors (e.g. DP-1 vs HDMI-1)
    // 2. Different virtual screens on the same physical monitor (e.g. DP-1/vs:0 vs DP-1/vs:1)
    //    — the virtual screens have different geometry bounds, so coordinates are wrong.
    if (!storedScreen.isEmpty() && !currentScreenName.isEmpty()
        && !Utils::screensMatch(storedScreen, currentScreenName)) {
        auto* mgr = ScreenManager::instance();
        QScreen* target =
            mgr ? mgr->physicalQScreenFor(currentScreenName) : Utils::findScreenByIdOrName(currentScreenName);
        if (target) {
            // For virtual screens, prefer virtual screen bounds over full physical screen
            QRect available = (mgr && mgr->screenGeometry(currentScreenName).isValid())
                ? mgr->screenAvailableGeometry(currentScreenName)
                : target->availableGeometry();
            // Clamp size to fit within the target screen (the window may have been
            // larger than the target VS when captured on a wider screen/physical monitor).
            int w = qMin(rect.width(), available.width());
            int h = qMin(rect.height(), available.height());
            int x = available.x() + (available.width() - w) / 2;
            int y = available.y() + (available.height() - h) / 2;
            QRect adjusted(x, y, w, h);
            qCDebug(lcCore) << "validatedPreTileGeometry: cross-screen adjustment for" << windowId << "from"
                            << storedScreen << "to" << currentScreenName << ":" << rect << "->" << adjusted;
            return adjusted;
        }
    }

    if (isGeometryOnScreen(rect)) {
        return rect;
    }
    return adjustGeometryToScreen(rect);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Floating Window State
// ═══════════════════════════════════════════════════════════════════════════════

bool WindowTrackingService::isWindowFloating(const QString& windowId) const
{
    // Try full window ID first (runtime - distinguishes multiple instances)
    if (m_floatingWindows.contains(windowId)) {
        return true;
    }
    // Fall back to app ID (session restore - pointer addresses change across restarts)
    QString appId = currentAppIdFor(windowId);
    return (appId != windowId && m_floatingWindows.contains(appId));
}

void WindowTrackingService::setWindowFloating(const QString& windowId, bool floating)
{
    // Use full windowId so each window instance has independent floating state
    // (appId would collide for multiple instances of the same app)
    if (floating) {
        m_floatingWindows.insert(windowId);
    } else {
        m_floatingWindows.remove(windowId);
        // Also remove app ID entry (session-restored entries)
        QString appId = currentAppIdFor(windowId);
        if (appId != windowId) {
            m_floatingWindows.remove(appId);
        }
        // Clear autotile-floated origin tracking for this specific instance only.
        // No appId removal — autotile-floated is per-instance, never shared.
        m_autotileFloatedWindows.remove(windowId);
    }
    scheduleSaveState();
}

void WindowTrackingService::markAutotileFloated(const QString& windowId)
{
    m_autotileFloatedWindows.insert(windowId);
}

void WindowTrackingService::clearAutotileFloated(const QString& windowId)
{
    m_autotileFloatedWindows.remove(windowId);
}

bool WindowTrackingService::isAutotileFloated(const QString& windowId) const
{
    // Exact match only — NO appId fallback.
    // m_autotileFloatedWindows is ephemeral runtime state (not persisted).
    // A appId fallback would cross-contaminate multiple instances of the
    // same app (e.g., 3 Dolphin windows share appId "dolphin:dolphin",
    // so floating one would incorrectly mark all three).
    return m_autotileFloatedWindows.contains(windowId);
}

void WindowTrackingService::saveSnapFloating(const QString& windowId)
{
    m_savedSnapFloatingWindows.insert(windowId);
}

bool WindowTrackingService::restoreSnapFloating(const QString& windowId)
{
    return m_savedSnapFloatingWindows.remove(windowId);
}

void WindowTrackingService::clearSavedSnapFloating()
{
    m_savedSnapFloatingWindows.clear();
}

QStringList WindowTrackingService::floatingWindows() const
{
    return m_floatingWindows.values();
}

void WindowTrackingService::unsnapForFloat(const QString& windowId)
{
    // Save zone(s) and screen for restore on unfloat.
    // Key by full windowId (not appId) so multiple instances of the same
    // application each remember their own zone independently.
    if (m_windowZoneAssignments.contains(windowId)) {
        QStringList zoneIds = m_windowZoneAssignments.value(windowId);
        m_preFloatZoneAssignments[windowId] = zoneIds;
        // Save the screen where the window was snapped so unfloat restores to the correct monitor
        QString screenId = m_windowScreenAssignments.value(windowId);
        if (!screenId.isEmpty()) {
            m_preFloatScreenAssignments[windowId] = screenId;
        }
        qCInfo(lcCore) << "Saved pre-float zones for" << windowId << "->" << zoneIds << "screen:" << screenId;

        // Mark the pre-float mutations dirty in their own right. Historically
        // every caller immediately follows up with setWindowFloating(true),
        // which uses DirtyAll and masks the hole — but that's an implicit
        // contract between unrelated methods. Marking here makes
        // unsnapForFloat self-sufficient so a future refactor that separates
        // the two calls cannot silently lose pre-float restore state.
        markDirty(DirtyPreFloatZones | DirtyPreFloatScreens);

        unassignWindow(windowId);

        // Pop one pending-restore entry (FIFO) so this window doesn't get
        // re-snapped to the old zone when closed and reopened. The queue is
        // per-appId, so popping one entry leaves any remaining entries for
        // other instances of the same app intact — unsnapping one Konsole
        // doesn't disturb restore entries for the other two.
        consumePendingAssignment(windowId);
    }
    // Note: If window not in assignments, it's already unsnapped - no action needed
}

QString WindowTrackingService::preFloatZone(const QString& windowId) const
{
    // Try full window ID first (runtime - distinguishes multiple instances)
    QStringList zones = m_preFloatZoneAssignments.value(windowId);
    if (zones.isEmpty()) {
        // Fall back to app ID (session restore - pointer addresses change across restarts)
        QString appId = currentAppIdFor(windowId);
        zones = m_preFloatZoneAssignments.value(appId);
    }
    return zones.isEmpty() ? QString() : zones.first();
}

QStringList WindowTrackingService::preFloatZones(const QString& windowId) const
{
    // Try full window ID first, fall back to app ID for session restore
    QStringList zones = m_preFloatZoneAssignments.value(windowId);
    if (zones.isEmpty()) {
        QString appId = currentAppIdFor(windowId);
        zones = m_preFloatZoneAssignments.value(appId);
    }
    return zones;
}

QString WindowTrackingService::preFloatScreen(const QString& windowId) const
{
    // Try full window ID first, fall back to app ID for session restore
    QString screen = m_preFloatScreenAssignments.value(windowId);
    if (screen.isEmpty()) {
        QString appId = currentAppIdFor(windowId);
        screen = m_preFloatScreenAssignments.value(appId);
    }
    return screen;
}

void WindowTrackingService::clearPreFloatZoneForWindow(const QString& windowId)
{
    if (windowId.isEmpty()) {
        return;
    }
    m_preFloatZoneAssignments.remove(windowId);
    m_preFloatScreenAssignments.remove(windowId);
}

void WindowTrackingService::clearPreFloatZone(const QString& windowId)
{
    // Remove by full window ID (runtime entries)
    m_preFloatZoneAssignments.remove(windowId);
    m_preFloatScreenAssignments.remove(windowId);
    // Also remove by app ID (session-restored entries)
    QString appId = currentAppIdFor(windowId);
    if (appId != windowId) {
        m_preFloatZoneAssignments.remove(appId);
        m_preFloatScreenAssignments.remove(appId);
    }
}

bool WindowTrackingService::clearFloatingForSnap(const QString& windowId)
{
    if (!isWindowFloating(windowId)) {
        return false;
    }
    setWindowFloating(windowId, false);
    clearPreFloatZone(windowId);
    return true;
}

UnfloatResult WindowTrackingService::resolveUnfloatGeometry(const QString& windowId,
                                                            const QString& fallbackScreen) const
{
    UnfloatResult result;

    QStringList zoneIds = preFloatZones(windowId);
    if (zoneIds.isEmpty()) {
        return result;
    }

    // Validate saved screen — fall back to caller's screen if monitor is gone
    QString restoreScreen = preFloatScreen(windowId);
    if (!restoreScreen.isEmpty()) {
        // Validate virtual screen still exists — configuration may have changed since float
        restoreScreen = resolveEffectiveScreenId(restoreScreen);
        // Check if the physical screen still exists
        QScreen* physScreen = ScreenManager::resolvePhysicalScreen(restoreScreen);
        if (!physScreen) {
            restoreScreen.clear();
        }
    }
    if (restoreScreen.isEmpty() && !fallbackScreen.isEmpty()) {
        restoreScreen = resolveEffectiveScreenId(fallbackScreen);
    }

    // Compute geometry (combined for multi-zone)
    QRect geo = resolveZoneGeometry(zoneIds, restoreScreen);

    if (!geo.isValid()) {
        return result;
    }

    result.found = true;
    result.zoneIds = zoneIds;
    result.geometry = geo;
    result.screenId = restoreScreen;
    return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Sticky Window Handling
// ═══════════════════════════════════════════════════════════════════════════════

void WindowTrackingService::setWindowSticky(const QString& windowId, bool sticky)
{
    m_windowStickyStates[windowId] = sticky;
}

bool WindowTrackingService::isWindowSticky(const QString& windowId) const
{
    return m_windowStickyStates.value(windowId, false);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Shared Helpers
// ═══════════════════════════════════════════════════════════════════════════════

void WindowTrackingService::sortZonesByNumber(QVector<Zone*>& zones)
{
    std::stable_sort(zones.begin(), zones.end(), [](Zone* a, Zone* b) {
        if (a->zoneNumber() != b->zoneNumber())
            return a->zoneNumber() < b->zoneNumber();
        return a->id() < b->id();
    });
}

QHash<QString, int> WindowTrackingService::buildZonePositionMap(Layout* layout)
{
    QHash<QString, int> map;
    if (!layout) {
        return map;
    }
    QVector<Zone*> zones = layout->zones();
    sortZonesByNumber(zones);
    for (int i = 0; i < zones.size(); ++i) {
        map[zones[i]->id().toString()] = i + 1;
    }
    return map;
}

QRect WindowTrackingService::resolveZoneGeometry(const QStringList& zoneIds, const QString& screenId) const
{
    if (zoneIds.isEmpty()) {
        return QRect();
    }
    return (zoneIds.size() > 1) ? multiZoneGeometry(zoneIds, screenId) : zoneGeometry(zoneIds.first(), screenId);
}

} // namespace PlasmaZones
