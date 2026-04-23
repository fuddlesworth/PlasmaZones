// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windowtrackingservice.h"
#include "constants.h"
#include "interfaces.h"
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/SnapState.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/VirtualScreen.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorZones/LayoutRegistry.h>
#include "virtualdesktopmanager.h"
#include "utils.h"
#include "logging.h"
#include "windowregistry.h"
#include <QScreen>
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <PhosphorScreens/ScreenIdentity.h>

namespace PlasmaZones {

WindowTrackingService::WindowTrackingService(PhosphorZones::LayoutRegistry* layoutManager,
                                             PhosphorZones::IZoneDetector* zoneDetector,
                                             Phosphor::Screens::ScreenManager* screenManager, ISettings* settings,
                                             VirtualDesktopManager* vdm, QObject* parent)
    : QObject(parent)
    , m_layoutManager(layoutManager)
    , m_zoneDetector(zoneDetector)
    , m_settings(settings)
    , m_virtualDesktopManager(vdm)
    , m_screenManager(screenManager)
{
    Q_ASSERT(layoutManager);
    Q_ASSERT(zoneDetector);
    Q_ASSERT(settings);

    // Note: No save timer needed - persistence handled by WindowTrackingAdaptor via KConfig
    // Service just emits stateChanged() signal when state changes
    //
    // PhosphorZones::Layout change handling: WindowTrackingAdaptor connects to activeLayoutChanged and calls
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
        const QString instanceId = PhosphorIdentity::WindowId::extractInstanceId(anyWindowId);
        const QString fromRegistry = m_windowRegistry->appIdFor(instanceId);
        if (!fromRegistry.isEmpty()) {
            return fromRegistry;
        }
    }
    return PhosphorIdentity::WindowId::extractAppId(anyWindowId);
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
// PhosphorZones::Zone Assignment Management
// ═══════════════════════════════════════════════════════════════════════════════

void WindowTrackingService::assignWindowToZone(const QString& windowId, const QString& zoneId, const QString& screenId,
                                               int virtualDesktop)
{
    assignWindowToZones(windowId, QStringList{zoneId}, screenId, virtualDesktop);
}

void WindowTrackingService::assignWindowToZones(const QString& windowId, const QStringList& zoneIds,
                                                const QString& screenId, int virtualDesktop)
{
    Q_ASSERT(m_snapState);
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
    QStringList previousZones = m_snapState->zonesForWindow(windowId);
    bool zoneChanged = (previousZones != validZoneIds);

    m_snapState->assignWindowToZones(windowId, validZoneIds, screenId, virtualDesktop);

    if (zoneChanged) {
        Q_EMIT windowZoneChanged(windowId, validZoneIds.first());
    }
    // Only the zone/screen/desktop maps changed. Narrower than DirtyAll so
    // the next save rewrites exactly one JSON field instead of all ten.
    markDirty(DirtyZoneAssignments);
}

void WindowTrackingService::unassignWindow(const QString& windowId)
{
    Q_ASSERT(m_snapState);
    auto result = m_snapState->unassignWindow(windowId);
    if (!result.wasAssigned) {
        return;
    }

    Q_EMIT windowZoneChanged(windowId, QString());
    markDirty(DirtyZoneAssignments | (result.lastUsedZoneCleared ? DirtyLastUsedZone : DirtyNone));
}

QString WindowTrackingService::zoneForWindow(const QString& windowId) const
{
    return m_snapState->zoneForWindow(windowId);
}

QStringList WindowTrackingService::zonesForWindow(const QString& windowId) const
{
    return m_snapState->zonesForWindow(windowId);
}

QStringList WindowTrackingService::windowsInZone(const QString& zoneId) const
{
    return m_snapState->windowsInZone(zoneId);
}

QStringList WindowTrackingService::snappedWindows() const
{
    return m_snapState->snappedWindows();
}

int WindowTrackingService::pruneStaleAssignments(const QSet<QString>& aliveWindowIds)
{
    int pruned = m_snapState->pruneStaleAssignments(aliveWindowIds);

    int wtsCleaned = 0;
    auto removeHash = [&](auto& hash) {
        for (auto it = hash.begin(); it != hash.end();) {
            if (!aliveWindowIds.contains(it.key())) {
                it = hash.erase(it);
                ++wtsCleaned;
            } else {
                ++it;
            }
        }
    };
    auto removeSet = [&](auto& set) {
        for (auto it = set.begin(); it != set.end();) {
            if (!aliveWindowIds.contains(*it)) {
                it = set.erase(it);
                ++wtsCleaned;
            } else {
                ++it;
            }
        }
    };

    removeHash(m_preTileGeometries);
    removeHash(m_preFloatZoneAssignments);
    removeHash(m_preFloatScreenAssignments);
    removeHash(m_windowStickyStates);
    removeSet(m_floatingWindows);
    removeSet(m_savedSnapFloatingWindows);

    if (pruned > 0 || wtsCleaned > 0) {
        markDirty(DirtyZoneAssignments | DirtyPreTileGeometries | DirtyPreFloatZones | DirtyPreFloatScreens);
    }

    return pruned + wtsCleaned;
}

bool WindowTrackingService::isWindowSnapped(const QString& windowId) const
{
    return m_snapState->isWindowSnapped(windowId);
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

    if (m_snapState) {
        m_snapState->storePreTileGeometry(windowId, geometry, connectorName, overwrite);
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
    if (m_snapState) {
        m_snapState->clearPreTileGeometry(windowId);
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
        && !Phosphor::Screens::ScreenIdentity::screensMatch(storedScreen, currentScreenName)) {
        auto* mgr = m_screenManager;
        QScreen* target = mgr ? mgr->physicalQScreenFor(currentScreenName)
                              : Phosphor::Screens::ScreenIdentity::findByIdOrName(currentScreenName);
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
    }

    if (m_snapState) {
        m_snapState->setFloating(windowId, floating);
    }

    scheduleSaveState();
}

// markAutotileFloated, clearAutotileFloated, isAutotileFloated moved to AutotileEngine.

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
    Q_ASSERT(m_snapState);
    if (!m_snapState->isWindowSnapped(windowId)) {
        return;
    }

    // Read zone/screen BEFORE unsnapForFloat clears them in SnapState.
    // WTS keeps its own pre-float maps keyed by both windowId and appId
    // for session-restore fallback (SnapState only keys by windowId).
    QStringList zoneIds = m_snapState->zonesForWindow(windowId);
    QString screenId = m_snapState->screenForWindow(windowId);

    // SnapState::unsnapForFloat saves pre-float state internally and unassigns.
    auto unassignResult = m_snapState->unsnapForFloat(windowId);

    // Write WTS pre-float maps (appId-fallback for session restore).
    m_preFloatZoneAssignments[windowId] = zoneIds;
    if (!screenId.isEmpty()) {
        m_preFloatScreenAssignments[windowId] = screenId;
    }
    qCInfo(lcCore) << "Saved pre-float zones for" << windowId << "->" << zoneIds << "screen:" << screenId;

    markDirty(DirtyPreFloatZones | DirtyPreFloatScreens);

    Q_EMIT windowZoneChanged(windowId, QString());
    markDirty(DirtyZoneAssignments | (unassignResult.lastUsedZoneCleared ? DirtyLastUsedZone : DirtyNone));

    consumePendingAssignment(windowId);
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
    if (m_snapState) {
        m_snapState->clearPreFloatZone(windowId);
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

void WindowTrackingService::sortZonesByNumber(QVector<PhosphorZones::Zone*>& zones)
{
    std::stable_sort(zones.begin(), zones.end(), [](PhosphorZones::Zone* a, PhosphorZones::Zone* b) {
        if (a->zoneNumber() != b->zoneNumber())
            return a->zoneNumber() < b->zoneNumber();
        return a->id() < b->id();
    });
}

QHash<QString, int> WindowTrackingService::buildZonePositionMap(PhosphorZones::Layout* layout)
{
    QHash<QString, int> map;
    if (!layout) {
        return map;
    }
    QVector<PhosphorZones::Zone*> zones = layout->zones();
    sortZonesByNumber(zones);
    for (int i = 0; i < zones.size(); ++i) {
        map[zones[i]->id().toString()] = i + 1;
    }
    return map;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Out-of-line accessors delegating to SnapState
// ═══════════════════════════════════════════════════════════════════════════════

const QHash<QString, QStringList>& WindowTrackingService::zoneAssignments() const
{
    Q_ASSERT(m_snapState);
    return m_snapState->zoneAssignments();
}

const QHash<QString, QString>& WindowTrackingService::screenAssignments() const
{
    Q_ASSERT(m_snapState);
    return m_snapState->screenAssignments();
}

const QHash<QString, int>& WindowTrackingService::desktopAssignments() const
{
    Q_ASSERT(m_snapState);
    return m_snapState->desktopAssignments();
}

QString WindowTrackingService::lastUsedZoneId() const
{
    Q_ASSERT(m_snapState);
    return m_snapState->lastUsedZoneId();
}

QString WindowTrackingService::lastUsedZoneClass() const
{
    Q_ASSERT(m_snapState);
    return m_snapState->lastUsedZoneClass();
}

void WindowTrackingService::retagLastUsedZoneClass(const QString& newClass)
{
    Q_ASSERT(m_snapState);
    m_snapState->retagLastUsedZoneClass(newClass);
}

const QSet<QString>& WindowTrackingService::userSnappedClasses() const
{
    Q_ASSERT(m_snapState);
    return m_snapState->userSnappedClasses();
}

void WindowTrackingService::setUserSnappedClasses(const QSet<QString>& classes)
{
    if (!m_snapState) {
        qCWarning(lcCore) << "setUserSnappedClasses: no SnapState — dropping" << classes.size() << "classes";
        return;
    }
    m_snapState->setUserSnappedClasses(classes);
}

void WindowTrackingService::setActiveAssignments(const QHash<QString, QStringList>& zones,
                                                 const QHash<QString, QString>& screens,
                                                 const QHash<QString, int>& desktops)
{
    if (!m_snapState) {
        qCWarning(lcCore) << "setActiveAssignments: no SnapState — dropping" << zones.size() << "assignments";
        return;
    }
    m_snapState->setZoneAssignments(zones);
    m_snapState->setScreenAssignments(screens);
    m_snapState->setDesktopAssignments(desktops);
}

QRect WindowTrackingService::resolveZoneGeometry(const QStringList& zoneIds, const QString& screenId) const
{
    if (zoneIds.isEmpty()) {
        return QRect();
    }
    return (zoneIds.size() > 1) ? multiZoneGeometry(zoneIds, screenId) : zoneGeometry(zoneIds.first(), screenId);
}

} // namespace PlasmaZones
