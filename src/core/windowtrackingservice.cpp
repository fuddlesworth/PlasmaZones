// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windowtrackingservice.h"
#include "constants.h"
#include "interfaces.h"
#include <PhosphorZones/Layout.h>
#include <PhosphorSnapEngine/SnapState.h>
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

    removeHash(m_windowStickyStates);
    removeSet(m_floatingWindows);

    if (m_snapEngine) {
        wtsCleaned += m_snapEngine->pruneStaleWindows(aliveWindowIds);
    }

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
// Geometry Validation Utility
// ═══════════════════════════════════════════════════════════════════════════════

std::optional<QRect> WindowTrackingService::validateGeometryForScreen(const QRect& geo, const QString& savedScreen,
                                                                      const QString& currentScreenName) const
{
    if (!geo.isValid() || geo.width() <= 0 || geo.height() <= 0) {
        return std::nullopt;
    }

    // Cross-screen check: if the geometry was captured on a different screen than where
    // the window currently is, the absolute coordinates are wrong. Preserve the size
    // but center on the current screen. This triggers for:
    // 1. Different physical monitors (e.g. DP-1 vs HDMI-1)
    // 2. Different virtual screens on the same physical monitor (e.g. DP-1/vs:0 vs DP-1/vs:1)
    //    — the virtual screens have different geometry bounds, so coordinates are wrong.
    if (!savedScreen.isEmpty() && !currentScreenName.isEmpty()
        && !Phosphor::Screens::ScreenIdentity::screensMatch(savedScreen, currentScreenName)) {
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
            int w = qMin(geo.width(), available.width());
            int h = qMin(geo.height(), available.height());
            int x = available.x() + (available.width() - w) / 2;
            int y = available.y() + (available.height() - h) / 2;
            QRect adjusted(x, y, w, h);
            qCDebug(lcCore) << "validateGeometryForScreen: cross-screen adjustment from" << savedScreen << "to"
                            << currentScreenName << ":" << geo << "->" << adjusted;
            return adjusted;
        }
    }

    if (isGeometryOnScreen(geo)) {
        return geo;
    }
    return adjustGeometryToScreen(geo);
}

std::optional<QRect> WindowTrackingService::validatedUnmanagedGeometry(const QString& windowId, const QString& screenId,
                                                                       bool exactOnly) const
{
    if (windowId.isEmpty() || !m_snapEngine) {
        return std::nullopt;
    }
    if (m_snapEngine->hasUnmanagedGeometry(windowId)) {
        return validateGeometryForScreen(m_snapEngine->unmanagedGeometry(windowId),
                                         m_snapEngine->unmanagedScreen(windowId), screenId);
    }
    if (!exactOnly) {
        const QString appId = currentAppIdFor(windowId);
        if (appId != windowId && m_snapEngine->hasUnmanagedGeometry(appId)) {
            return validateGeometryForScreen(m_snapEngine->unmanagedGeometry(appId),
                                             m_snapEngine->unmanagedScreen(appId), screenId);
        }
    }
    return std::nullopt;
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

QStringList WindowTrackingService::floatingWindows() const
{
    return m_floatingWindows.values();
}

void WindowTrackingService::unsnapForFloat(const QString& windowId)
{
    if (!m_snapState || !m_snapState->isWindowSnapped(windowId)) {
        return;
    }

    // Read zone/screen for logging BEFORE unsnapForFloat clears them.
    QStringList zoneIds = m_snapState->zonesForWindow(windowId);
    QString screenId = m_snapState->screenForWindow(windowId);

    // SnapState::unsnapForFloat saves pre-float state (windowId-keyed) and unassigns.
    auto unassignResult = m_snapState->unsnapForFloat(windowId);

    // Also write an appId-keyed entry into SnapState for session-restore fallback.
    // SnapState::unsnapForFloat only writes the windowId key; the appId alias
    // lets preFloatZone()/preFloatScreen() find the entry after a window
    // close+reopen cycle where the windowId changes but the appId persists.
    QString appId = currentAppIdFor(windowId);
    if (appId != windowId && !appId.isEmpty()) {
        m_snapState->addPreFloatZone(appId, zoneIds);
        if (!screenId.isEmpty()) {
            m_snapState->addPreFloatScreen(appId, screenId);
        }
    }
    qCInfo(lcCore) << "Saved pre-float zones for" << windowId << "->" << zoneIds << "screen:" << screenId;

    markDirty(DirtyPreFloatZones | DirtyPreFloatScreens);

    Q_EMIT windowZoneChanged(windowId, QString());
    markDirty(DirtyZoneAssignments | (unassignResult.lastUsedZoneCleared ? DirtyLastUsedZone : DirtyNone));

    consumePendingAssignment(windowId);
}

template<typename Func>
auto WindowTrackingService::preFloatLookup(const QString& windowId, Func&& getter) const -> decltype(getter(windowId))
{
    if (!m_snapState) {
        return {};
    }
    auto result = getter(windowId);
    if (!result.isEmpty()) {
        return result;
    }
    QString appId = currentAppIdFor(windowId);
    if (appId != windowId) {
        result = getter(appId);
    }
    return result;
}

QString WindowTrackingService::preFloatZone(const QString& windowId) const
{
    return preFloatLookup(windowId, [this](const QString& id) {
        return m_snapState->preFloatZone(id);
    });
}

QStringList WindowTrackingService::preFloatZones(const QString& windowId) const
{
    return preFloatLookup(windowId, [this](const QString& id) {
        return m_snapState->preFloatZones(id);
    });
}

QString WindowTrackingService::preFloatScreen(const QString& windowId) const
{
    return preFloatLookup(windowId, [this](const QString& id) {
        return m_snapState->preFloatScreen(id);
    });
}

void WindowTrackingService::clearPreFloatZoneForWindow(const QString& windowId)
{
    if (windowId.isEmpty() || !m_snapState) {
        return;
    }
    m_snapState->clearPreFloatZone(windowId);
}

void WindowTrackingService::clearPreFloatZone(const QString& windowId)
{
    if (!m_snapState) {
        return;
    }
    // Remove by full window ID (runtime entries)
    m_snapState->clearPreFloatZone(windowId);
    // Also remove by app ID (session-restored entries)
    QString appId = currentAppIdFor(windowId);
    if (appId != windowId) {
        m_snapState->clearPreFloatZone(appId);
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

// sortZonesByNumber / buildZonePositionMap removed — callers should use
// PhosphorZones::LayoutUtils directly.

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

const QHash<QString, QStringList>& WindowTrackingService::preFloatZoneAssignments() const
{
    static const QHash<QString, QStringList> empty;
    if (!m_snapState) {
        return empty;
    }
    return m_snapState->preFloatZoneAssignments();
}

const QHash<QString, QString>& WindowTrackingService::preFloatScreenAssignments() const
{
    static const QHash<QString, QString> empty;
    if (!m_snapState) {
        return empty;
    }
    return m_snapState->preFloatScreenAssignments();
}

void WindowTrackingService::setPreFloatZoneAssignments(const QHash<QString, QStringList>& assignments)
{
    if (!m_snapState) {
        qCWarning(lcCore) << "setPreFloatZoneAssignments: no SnapState — dropping" << assignments.size() << "entries";
        return;
    }
    m_snapState->setPreFloatZoneAssignments(assignments);
}

void WindowTrackingService::setPreFloatScreenAssignments(const QHash<QString, QString>& assignments)
{
    if (!m_snapState) {
        qCWarning(lcCore) << "setPreFloatScreenAssignments: no SnapState — dropping" << assignments.size() << "entries";
        return;
    }
    m_snapState->setPreFloatScreenAssignments(assignments);
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
