// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Qt headers
#include <algorithm>
#include <cmath>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QScopeGuard>
#include <QScreen>
#include <QTimer>
#include <QVarLengthArray>

// Project headers
#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/ITileAlgorithmRegistry.h>
#include <PhosphorGeometry/GeometryUtils.h>
#include <PhosphorTileEngine/AutotileConfig.h>
#include <PhosphorTileEngine/NavigationController.h>
#include <PhosphorTileEngine/PerScreenConfigResolver.h>
#include <PhosphorTiles/AlgorithmPreviewParams.h>
#include <PhosphorTiles/TilingAlgorithm.h>
// DwindleMemoryAlgorithm.h no longer needed — prepareTilingState() is virtual on PhosphorTiles::TilingAlgorithm
#include <PhosphorTiles/TilingState.h>
#include <PhosphorTiles/SplitTree.h>
#include <PhosphorEngine/PerScreenKeys.h>
#include <PhosphorTiles/AutotileConstants.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include "tileenginelogging.h"
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/VirtualScreen.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorScreens/ScreenIdentity.h>

namespace PhosphorTileEngine {

using NavigationContext = PhosphorEngine::NavigationContext;
using TilingStateKey = PhosphorEngine::TilingStateKey;
namespace PerScreenKeys = PhosphorEngine::PerScreenKeys;

namespace {
// Safety timeout for pending initial window orders that never arrive via D-Bus.
// If windows fail to open (e.g., app crash during startup), this prevents
// m_pendingInitialOrders from leaking state indefinitely.
constexpr int PendingOrderTimeoutMs = 10000;

template<typename T>
T* checkedCast(QObject* obj, const char* context)
{
    if (!obj)
        return nullptr;
    auto* concrete = qobject_cast<T*>(obj);
    if (!concrete) {
        qCWarning(PhosphorTileEngine::lcTileEngine) << context << ": QObject is not the expected type — skipping";
    }
    return concrete;
}

// Union the rendered zones of every leaf under @p node into @p bbox. Leaves are
// matched to zones by looking their window id up in @p tiled (parallel to
// @p zones), so this makes no assumption about leaf/zone ordering. Depth-guarded
// like the rest of the SplitTree recursion.
void unionSubtreeZones(const PhosphorTiles::SplitNode* node, const QStringList& tiled, const QVector<QRect>& zones,
                       QRect& bbox, int depth = 0)
{
    if (!node || depth > PhosphorTiles::AutotileDefaults::MaxRuntimeTreeDepth) {
        return;
    }
    if (node->isLeaf()) {
        const int idx = tiled.indexOf(node->windowId);
        if (idx >= 0 && idx < zones.size()) {
            bbox = bbox.united(zones[idx]);
        }
        return;
    }
    unionSubtreeZones(node->first.get(), tiled, zones, bbox, depth + 1);
    unionSubtreeZones(node->second.get(), tiled, zones, bbox, depth + 1);
}

// The rendered extent of a split = bounding box of its subtree's zones. Read
// from the currently rendered zones (not recomputed from the tree) so the
// interactive-resize edge→ratio math stays in the same coordinate space as the
// compositor-reported window frame.
QRect subtreeBoundingRect(const PhosphorTiles::SplitNode* split, const QStringList& tiled, const QVector<QRect>& zones)
{
    QRect bbox;
    unionSubtreeZones(split, tiled, zones, bbox);
    return bbox;
}

} // namespace

AutotileEngine::AutotileEngine(PhosphorZones::LayoutRegistry* layoutManager,
                               PhosphorEngine::IWindowTrackingService* windowTracker,
                               PhosphorScreens::ScreenManager* screenManager,
                               PhosphorTiles::ITileAlgorithmRegistry* algorithmRegistry, QObject* parent)
    : PlacementEngineBase(parent)
    , m_layoutManager(layoutManager)
    , m_windowTracker(windowTracker)
    , m_screenManager(screenManager)
    , m_algorithmRegistry(algorithmRegistry)
    , m_config(std::make_unique<AutotileConfig>())
    , m_configResolver(std::make_unique<PerScreenConfigResolver>(this))
    , m_navigation(std::make_unique<NavigationController>(this))
    , m_algorithmId(PhosphorTiles::AlgorithmRegistry::staticDefaultAlgorithmId())
{
    // In production (Daemon::start) all three dependencies are non-null.
    // Headless unit tests deliberately pass nullptr to construct an engine
    // with minimal parents for testing peripheral classes (adaptors, bridges,
    // sub-controllers) — every method that dereferences a dependency guards
    // it locally. Do not Q_ASSERT here.

    // Guard timer: while active, refreshConfigFromSettings() skips overwriting
    // splitRatio/masterCount with Settings values. Mirrors the old SettingsBridge
    // m_shortcutSaveTimer — restarts on each write-back so rapid shortcut presses
    // keep the guard alive until the burst settles.
    m_writeBackGuardTimer.setSingleShot(true);
    m_writeBackGuardTimer.setInterval(500);
    connect(&m_writeBackGuardTimer, &QTimer::timeout, this, &AutotileEngine::settingsPersistRequested);

    m_settingsRetileTimer.setSingleShot(true);
    m_settingsRetileTimer.setInterval(100);
    connect(&m_settingsRetileTimer, &QTimer::timeout, this, [this]() {
        if (isEnabled()) {
            m_pendingRetileScreens.clear();
            retile();
        }
    });

    // Bounded retry timer for transient screen geometry failures.
    // When QScreen is unavailable during desktop switch, retileScreen defers
    // to this timer rather than silently dropping the retile.
    m_retileRetryTimer.setSingleShot(true);
    m_retileRetryTimer.setInterval(RetileRetryIntervalMs);
    connect(&m_retileRetryTimer, &QTimer::timeout, this, &AutotileEngine::processRetileRetries);

    connectSignals();
}

AutotileEngine::~AutotileEngine() = default;

// Canonicalize on every access so set/clear/read key on the same id regardless of
// the raw alias a caller passes — symmetric with isWindowFloatingInAutotile(). The
// daemon already passes canonical ids today, but relying on every caller's discipline
// is fragile: a raw mark + canonical clear would leak the marker across a mode flip.
void AutotileEngine::markAutotileFloated(const QString& rawWindowId)
{
    m_autotileFloatedWindows.insert(canonicalizeForLookup(rawWindowId));
}

void AutotileEngine::clearAutotileFloated(const QString& rawWindowId)
{
    m_autotileFloatedWindows.remove(canonicalizeForLookup(rawWindowId));
}

bool AutotileEngine::isAutotileFloated(const QString& rawWindowId) const
{
    return m_autotileFloatedWindows.contains(canonicalizeForLookup(rawWindowId));
}

int AutotileEngine::pruneStaleWindows(const QSet<QString>& aliveWindowIds)
{
    int pruned = PlacementEngineBase::pruneStaleWindows(aliveWindowIds);
    for (auto it = m_autotileFloatedWindows.begin(); it != m_autotileFloatedWindows.end();) {
        if (!aliveWindowIds.contains(*it)) {
            it = m_autotileFloatedWindows.erase(it);
            ++pruned;
        } else {
            ++it;
        }
    }
    // Engine tracking sweep — the contract this override exists for ("window
    // died without a windowClosed signal"): a dead window's TilingState
    // membership is otherwise permanent, since windowOpened's ghost-removal
    // only fires when the same window re-announces and a dead window never
    // does — the layout retiles around a ghost tile forever. This is a BATCH
    // path (N dead windows reported at once), so do the lifecycle hook + state
    // removal per id but retile each affected screen ONCE afterward, rather
    // than N immediate retiles of the same screen via onWindowRemoved.
    QStringList staleTracked;
    for (auto it = m_windowToStateKey.constBegin(); it != m_windowToStateKey.constEnd(); ++it) {
        if (!aliveWindowIds.contains(it.key())) {
            staleTracked.append(it.key());
        }
    }
    QSet<QString> affectedScreens;
    for (const QString& windowId : std::as_const(staleTracked)) {
        qCInfo(PhosphorTileEngine::lcTileEngine) << "pruneStaleWindows: removing dead tracked window" << windowId;
        // Drop a dead window from an active drag-insert preview before tearing
        // down its state, mirroring windowClosed — else commit/cancel would
        // later re-add or float a dead id.
        dropClosedWindowFromDragPreview(windowId);
        const QString screenId = removeTrackedWindowNoRetile(windowId);
        if (!screenId.isEmpty()) {
            affectedScreens.insert(screenId);
        }
        ++pruned;
    }
    for (const QString& screenId : std::as_const(affectedScreens)) {
        retileAfterOperation(screenId, true);
    }
    // Min-size entries are keyed independently of tracking (windowOpened
    // stores them before any state insert), so sweep them directly.
    for (auto it = m_windowMinSizes.begin(); it != m_windowMinSizes.end();) {
        if (!aliveWindowIds.contains(it.key())) {
            it = m_windowMinSizes.erase(it);
        } else {
            ++it;
        }
    }
    return pruned;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Signal connections
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileEngine::onWindowZoneChanged(const QString& windowId, const QString& zoneId)
{
    if (m_retiling)
        return;
    if (zoneId.isEmpty()) {
        for (auto it = m_screenStates.constBegin(); it != m_screenStates.constEnd(); ++it) {
            if (it.key().desktop != currentKeyForScreen(it.key().screenId).desktop
                || it.key().activity != m_currentActivity) {
                continue;
            }
            if (it.value() && it.value()->isFloating(windowId)) {
                return;
            }
        }
        onWindowRemoved(windowId);
    }
}

void AutotileEngine::connectSignals()
{
    // Window tracking signals
    // Primary window events (open/close/focus) are received via public methods:
    // windowOpened(), windowClosed(), windowFocused() - connected by Daemon to
    // WindowTrackingAdaptor signals. This connection also handles zone changes:
    if (m_windowTracker) {
        // String-based connect is required: m_windowTracker is IWindowTrackingService*
        // (non-QObject ABC), so PMF syntax is unavailable. The asQObject() escape
        // hatch is the standard Qt pattern for interface-based signal routing.
        // Verify the connection succeeds so signal/slot renames are caught at startup.
        bool ok = connect(m_windowTracker->asQObject(), SIGNAL(windowZoneChanged(QString, QString)), this,
                          SLOT(onWindowZoneChanged(QString, QString)));
        Q_ASSERT(ok);
        if (Q_UNLIKELY(!ok)) {
            qCCritical(PhosphorTileEngine::lcTileEngine)
                << "Failed to connect windowZoneChanged — autotile will not react to zone changes";
        }
    }

    // Screen geometry changes
    if (m_screenManager) {
        connect(m_screenManager, &PhosphorScreens::ScreenManager::availableGeometryChanged, this,
                [this](const PhosphorScreens::PhysicalScreen& screen, const QRect&) {
                    if (!screen.isValid()) {
                        return;
                    }
                    const QString physId = screen.identifier;
                    // When virtual screens are configured, autotile state is keyed by
                    // virtual screen IDs — retile each one instead of the physical ID.
                    if (m_screenManager->hasVirtualScreens(physId)) {
                        for (const QString& vsId : m_screenManager->virtualScreenIdsFor(physId)) {
                            onScreenGeometryChanged(vsId);
                        }
                    } else {
                        onScreenGeometryChanged(physId);
                    }
                });

        // Virtual screen reconfiguration — retile new virtual screens and
        // clean up orphaned PhosphorTiles::TilingState entries for virtual screens that no longer exist.
        connect(m_screenManager, &PhosphorScreens::ScreenManager::virtualScreensChanged, this,
                [this](const QString& physicalScreenId) {
                    const QStringList newVsIds = m_screenManager->virtualScreenIdsFor(physicalScreenId);
                    const QSet<QString> newVsSet(newVsIds.begin(), newVsIds.end());

                    // Cancel an active drag-insert preview whose target or
                    // prior screen is about to be orphaned — mirrors the
                    // setAutotileScreens guard. Without this the preview
                    // survives the state's deleteLater with dangling keys
                    // (null-guarded, but commit/cancel would then silently
                    // restore nothing or re-key a released window).
                    if (m_dragInsertPreview) {
                        const auto orphaned = [&](const QString& sid) {
                            return PhosphorIdentity::VirtualScreenId::isVirtual(sid)
                                && PhosphorIdentity::VirtualScreenId::extractPhysicalId(sid) == physicalScreenId
                                && !newVsSet.contains(sid);
                        };
                        const QString targetScreen = m_dragInsertPreview->targetScreenId;
                        const QString priorScreen =
                            m_dragInsertPreview->hadPriorState ? m_dragInsertPreview->priorKey.screenId : QString();
                        if (orphaned(targetScreen) || (!priorScreen.isEmpty() && orphaned(priorScreen))) {
                            cancelDragInsertPreview();
                        }
                    }

                    // Find and release orphaned virtual screen states for this physical screen
                    QStringList releasedWindows;
                    QSet<QString> orphanedVsIds;
                    QMutableHashIterator<TilingStateKey, PhosphorTiles::TilingState*> it(m_screenStates);
                    while (it.hasNext()) {
                        it.next();
                        const QString& sid = it.key().screenId;
                        if (!PhosphorIdentity::VirtualScreenId::isVirtual(sid)) {
                            continue;
                        }
                        if (PhosphorIdentity::VirtualScreenId::extractPhysicalId(sid) != physicalScreenId) {
                            continue;
                        }
                        if (newVsSet.contains(sid)) {
                            continue;
                        }
                        // This virtual screen no longer exists — release its
                        // windows via the shared teardown body. Unlike the
                        // toggle-off path, an orphaned VS id is never reused,
                        // so BOTH override layers go: the resolver's
                        // in-memory map here, the persisted settings below
                        // (clearPerScreenAutotileSettings). drainOverflow is
                        // deferred to the per-screen loop below: this loop
                        // visits EVERY desktop/activity context of the same
                        // VS id, and an in-helper drain on the first context
                        // would blind capturePlacement's overflow
                        // discriminator for the remaining contexts.
                        orphanedVsIds.insert(sid);
                        releaseScreenStateForTeardown(sid, it.value(), releasedWindows,
                                                      /*drainOverflow=*/false);
                        m_configResolver->removeOverridesForScreen(sid);
                        it.remove();
                    }
                    for (const QString& sid : std::as_const(orphanedVsIds)) {
                        m_overflow.takeForScreen(sid);
                    }
                    for (const QString& windowId : std::as_const(releasedWindows)) {
                        m_windowToStateKey.remove(windowId);
                    }
                    if (!releasedWindows.isEmpty()) {
                        Q_EMIT windowsReleased(releasedWindows, orphanedVsIds);
                    }

                    // Clean up per-screen autotile settings for removed virtual screens.
                    // Orphaned AutotileScreen: groups would otherwise accumulate indefinitely
                    // as virtual screen IDs are never reused after reconfiguration.
                    if (!orphanedVsIds.isEmpty()) {
                        const QSignalBlocker blocker(engineSettings());
                        if (auto* s = autotileSettings()) {
                            for (const QString& orphanId : orphanedVsIds)
                                s->clearPerScreenAutotileSettings(orphanId);
                        }
                        Q_EMIT settingsPersistRequested();
                    }

                    // Clean up per-screen desktop maps for removed virtual screens on this
                    // physical screen — BOTH the sticky-pin override and the per-output-VD
                    // map (#648). Use newVsSet (freshly-computed from
                    // PhosphorScreens::ScreenManager) rather than m_autotileScreens which
                    // reflects mode assignments and may not yet be updated for the new config.
                    const auto isOrphanedVsOfThisPhysical = [&](const QString& key) {
                        return PhosphorIdentity::VirtualScreenId::isVirtual(key)
                            && PhosphorIdentity::VirtualScreenId::extractPhysicalId(key) == physicalScreenId
                            && !newVsSet.contains(key);
                    };
                    auto overrideIt = m_screenDesktopOverride.begin();
                    while (overrideIt != m_screenDesktopOverride.end()) {
                        if (isOrphanedVsOfThisPhysical(overrideIt.key()))
                            overrideIt = m_screenDesktopOverride.erase(overrideIt);
                        else
                            ++overrideIt;
                    }
                    auto perOutputIt = m_screenCurrentDesktop.begin();
                    while (perOutputIt != m_screenCurrentDesktop.end()) {
                        if (isOrphanedVsOfThisPhysical(perOutputIt.key()))
                            perOutputIt = m_screenCurrentDesktop.erase(perOutputIt);
                        else
                            ++perOutputIt;
                    }

                    // Retile the new virtual screens
                    for (const QString& vsId : newVsIds) {
                        onScreenGeometryChanged(vsId);
                    }
                });

        // Regions-only changes (VS swap/rotate/boundary resize) — skip the
        // orphan cleanup (no VSs removed/added) and just retile each VS with
        // its new geometry. This is the single authoritative retile for the
        // change; the Daemon's regions-only handler deliberately does NOT
        // call updateAutotileScreens so there is no second retile pass.
        connect(m_screenManager, &PhosphorScreens::ScreenManager::virtualScreenRegionsChanged, this,
                [this](const QString& physicalScreenId) {
                    const QStringList vsIds = m_screenManager->virtualScreenIdsFor(physicalScreenId);
                    for (const QString& vsId : vsIds) {
                        onScreenGeometryChanged(vsId);
                    }
                });
    }

    // PhosphorZones::Layout changes — intentionally NOT connected.
    // Autotile screens are managed by per-screen assignments, not the global
    // active layout. Retile is triggered by setAutotileScreens() and
    // onScreenGeometryChanged() instead.
    // if (m_layoutManager) {
    //     connect(m_layoutManager, &PhosphorZones::LayoutRegistry::activeLayoutChanged,
    //             this, &AutotileEngine::onLayoutChanged);
    // }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Enable/disable
// ═══════════════════════════════════════════════════════════════════════════════

bool AutotileEngine::isEnabled() const noexcept
{
    return !m_autotileScreens.isEmpty();
}

bool AutotileEngine::isAutotileScreen(const QString& screenId) const
{
    return m_autotileScreens.contains(screenId);
}

bool AutotileEngine::isActiveOnScreen(const QString& screenId) const
{
    return isAutotileScreen(screenId);
}

bool AutotileEngine::isWindowTiled(const QString& rawWindowId) const
{
    // Canonicalize for the lookup, symmetric with isWindowFloatingInAutotile() — both
    // are consulted from the same daemon mode-resolution path with the same id.
    const QString windowId = canonicalizeForLookup(rawWindowId);
    auto it = m_windowToStateKey.constFind(windowId);
    if (it == m_windowToStateKey.constEnd()) {
        return false;
    }
    const PhosphorTiles::TilingState* state = m_screenStates.value(it.value());
    return state && !state->isFloating(windowId);
}

bool AutotileEngine::isWindowFloatingInAutotile(const QString& rawWindowId) const
{
    const QString windowId = canonicalizeForLookup(rawWindowId);
    auto it = m_windowToStateKey.constFind(windowId);
    if (it == m_windowToStateKey.constEnd()) {
        return false;
    }
    const PhosphorTiles::TilingState* state = m_screenStates.value(it.value());
    return state && state->isFloating(windowId);
}

QStringList AutotileEngine::allFloatingWindows() const
{
    QStringList result;
    for (auto it = m_screenStates.constBegin(); it != m_screenStates.constEnd(); ++it) {
        if (it.value()) {
            result += it.value()->floatingWindows();
        }
    }
    return result;
}

void AutotileEngine::swapInDirection(const QString& direction, const QString& action)
{
    swapFocusedInDirection(direction, action);
}

void AutotileEngine::rotateWindows(bool clockwise, const QString& /*screenId*/)
{
    // AutotileEngine operates on the active screen internally
    rotateWindowOrder(clockwise);
}

void AutotileEngine::moveToPosition(const QString& windowId, int position, const QString& /*screenId*/)
{
    if (windowId.isEmpty()) {
        // Fall back to focused window when no windowId is provided
        moveFocusedToPosition(position);
        return;
    }

    QString resolvedScreenId;
    PhosphorTiles::TilingState* state = stateForWindow(windowId, &resolvedScreenId);
    if (!state) {
        qCWarning(PhosphorTileEngine::lcTileEngine)
            << "moveToPosition: window" << windowId << "not found in any tiling state";
        return;
    }

    // position is 1-based (from snap-to-zone-N shortcuts), convert to 0-based
    const int targetIndex = qBound(0, position - 1, qMax(0, state->tiledWindowCount() - 1));
    const bool moved = state->moveToTiledPosition(windowId, targetIndex);
    retileAfterOperation(resolvedScreenId, moved);
}

void AutotileEngine::setCurrentDesktop(int desktop)
{
    if (desktop == m_currentDesktop) {
        // A same-desktop push still ESTABLISHES the desktop context: the
        // daemon's startup push lands here whenever the session begins on
        // the engine's default desktop. Without recording it, the next
        // genuine change would read as initialization and skip arming.
        m_desktopContextEverSet = true;
        return;
    }
    qCInfo(PhosphorTileEngine::lcTileEngine)
        << "Switching autotile context: desktop" << m_currentDesktop << "->" << desktop;
    // Only flag as desktop switch when a desktop context was already
    // established by a prior call. The daemon pushes the initial desktop in
    // start() BEFORE the first updateAutotileScreens(); that first push must
    // NOT read as a switch — regardless of which desktop the session starts
    // on — or login with autotile enabled suppresses enabledChanged and the
    // effect treats the first autotileScreensChanged as a "desktop return",
    // skipping window notification to the daemon entirely. m_currentDesktop
    // has no reserved "unset" value (it defaults to 1, and KWin desktops are
    // always >= 1), so a separate established-flag — not a sentinel
    // comparison against the current value — carries "context exists";
    // mirrors m_activityContextEverSet on the activity side.
    // Use |= so that a prior setCurrentActivity() flag is not lost when both
    // desktop AND activity change simultaneously (e.g., activity-per-desktop).
    m_isDesktopContextSwitch |= m_desktopContextEverSet;
    m_desktopContextEverSet = true;
    m_currentDesktop = desktop;
}

void AutotileEngine::setCurrentDesktopForScreen(const QString& screenId, int desktop)
{
    if (screenId.isEmpty() || desktop < 1) {
        return;
    }
    const int previous = m_screenCurrentDesktop.value(screenId, m_currentDesktop);
    if (previous == desktop) {
        // Same per-screen desktop still establishes the context (mirrors the
        // same-desktop branch of setCurrentDesktop for the startup push).
        m_desktopContextEverSet = true;
        return;
    }
    qCInfo(PhosphorTileEngine::lcTileEngine)
        << "Switching autotile context for screen" << screenId << "desktop" << previous << "->" << desktop;
    // PURE context swap — no state migration. The other desktop's TilingState for
    // this screen stays put and reappears when the screen returns to it; migrating
    // would destroy the per-desktop isolation that the (screen, desktop) keying
    // exists to provide. Arm the (global) desktop-switch flag exactly like
    // setCurrentDesktop so the effect's desktop-switch pass runs — over-broad
    // across screens but idempotent (the catch-scan re-adds).
    m_isDesktopContextSwitch |= m_desktopContextEverSet;
    m_desktopContextEverSet = true;
    m_screenCurrentDesktop.insert(screenId, desktop);
}

void AutotileEngine::clearCurrentDesktopForScreen(const QString& screenId)
{
    m_screenCurrentDesktop.remove(screenId);
}

void AutotileEngine::setCurrentActivity(const QString& activity)
{
    if (activity == m_currentActivity) {
        // A same-activity push still establishes context — but only a
        // NON-EMPTY one ("" == "" is the daemon pushing "activities
        // unavailable", which is no context at all).
        m_activityContextEverSet = m_activityContextEverSet || !activity.isEmpty();
        return;
    }
    qCInfo(PhosphorTileEngine::lcTileEngine)
        << "Switching autotile context: activity" << m_currentActivity << "->" << activity;
    // Only flag as desktop/activity switch when an activity context was
    // already established. The established-flag (not a bare empty-string
    // sentinel on the previous value) keeps the "a" → "" → "b" sequence —
    // an activities-service restart hiccup — armed on the "" → "b" leg:
    // with the sentinel alone that leg read as initialization, and a
    // changed-set setAutotileScreens would then run the genuine-toggle
    // restore path, leaking geometry restores into the new activity's
    // session. Mirrors m_desktopContextEverSet on the desktop side.
    // Use |= so that a prior setCurrentDesktop() flag is not lost when both
    // desktop AND activity change simultaneously.
    m_isDesktopContextSwitch |= m_activityContextEverSet;
    m_activityContextEverSet = true;
    m_currentActivity = activity;
}

void AutotileEngine::updateStickyScreenPins(const std::function<bool(const QString&)>& isWindowSticky)
{
    for (const QString& screenId : std::as_const(m_autotileScreens)) {
        const auto key = currentKeyForScreen(screenId);
        auto stateIt = m_screenStates.constFind(key);
        if (stateIt == m_screenStates.constEnd()) {
            continue;
        }

        const PhosphorTiles::TilingState* state = stateIt.value();
        const QStringList tiled = state->tiledWindows();
        const QStringList floating = state->floatingWindows();

        if (tiled.isEmpty() && floating.isEmpty()) {
            continue;
        }

        bool allSticky = true;
        for (const QString& wid : tiled) {
            if (!isWindowSticky(wid)) {
                allSticky = false;
                break;
            }
        }
        if (allSticky) {
            for (const QString& wid : floating) {
                if (!isWindowSticky(wid)) {
                    allSticky = false;
                    break;
                }
            }
        }

        if (allSticky) {
            if (!m_screenDesktopOverride.contains(screenId)) {
                // Pin to current effective desktop (which is the desktop where
                // the PhosphorTiles::TilingState actually lives).
                m_screenDesktopOverride[screenId] = key.desktop;
                qCInfo(PhosphorTileEngine::lcTileEngine)
                    << "Pinning screen" << screenId << "to desktop" << key.desktop << "(all"
                    << (tiled.size() + floating.size()) << "windows sticky)";
            }
        } else {
            if (m_screenDesktopOverride.contains(screenId)) {
                int pinnedDesktop = m_screenDesktopOverride.take(screenId);
                qCInfo(PhosphorTileEngine::lcTileEngine)
                    << "Unpinning screen" << screenId << "from desktop" << pinnedDesktop;

                // Migrate PhosphorTiles::TilingState from the pinned key to this
                // screen's CURRENT desktop key. The sticky-pin override was just
                // removed above, so currentKeyForScreen now resolves the screen's
                // effective desktop — its per-output virtual desktop under Plasma
                // 6.7 (#648), else the global m_currentDesktop. Identical to
                // m_currentDesktop when per-output desktops aren't in use.
                const int targetDesktop = currentKeyForScreen(screenId).desktop;
                if (pinnedDesktop != targetDesktop) {
                    TilingStateKey oldKey{screenId, pinnedDesktop, m_currentActivity};
                    TilingStateKey newKey{screenId, targetDesktop, m_currentActivity};

                    auto oldIt = m_screenStates.find(oldKey);
                    if (oldIt != m_screenStates.end()) {
                        // If a state already exists at the target key (e.g., created
                        // by tilingStateForScreen() during a transient lookup), delete it —
                        // the pinned state has the actual windows.
                        auto existingIt = m_screenStates.find(newKey);
                        if (existingIt != m_screenStates.end()) {
                            existingIt.value()->deleteLater();
                            m_screenStates.erase(existingIt);
                        }
                        PhosphorTiles::TilingState* migratedState = oldIt.value();
                        m_screenStates.erase(oldIt);
                        m_screenStates.insert(newKey, migratedState);

                        // Update window-to-key mapping
                        for (auto wit = m_windowToStateKey.begin(); wit != m_windowToStateKey.end(); ++wit) {
                            if (wit.value() == oldKey) {
                                wit.value() = newKey;
                            }
                        }

                        qCInfo(PhosphorTileEngine::lcTileEngine)
                            << "Migrated screen" << screenId << "state from desktop" << pinnedDesktop << "to"
                            << targetDesktop;
                    }
                }
            }
        }
    }
}

void AutotileEngine::setAutotileScreens(const QSet<QString>& screens)
{
    if (m_autotileScreens == screens) {
        // Must consume the desktop-context-switch flag even on early return.
        // Without this, a desktop switch between two desktops with the same
        // autotile screen set leaves the flag set. The NEXT setAutotileScreens
        // call (e.g. from a toggle OFF) then incorrectly reports isDesktopSwitch=true,
        // causing the effect to skip geometry/border restore on toggle OFF.
        const bool wasDesktopSwitch = m_isDesktopContextSwitch;
        m_isDesktopContextSwitch = false;
        // Discussion #219: a desktop/activity switch between two contexts with
        // an IDENTICAL autotile set still needs the compositor effect's
        // desktop-switch pass — its catch-scan re-adds windows that were moved
        // to this desktop while the user was away (the move untracked them on
        // the source desktop). Re-emit the unchanged set flagged as a desktop
        // switch. An empty set means no screen autotiles anywhere — nothing to
        // catch, skip the wakeup.
        //
        // Deliberately NO retile here, unlike the changed-set path's
        // returning-screen retile: the early return exists to keep
        // identical-set switches cheap, and re-entrant receivers rely on the
        // second call terminating without side effects. The cost is that
        // screen-geometry drift that happened while the user was on the other
        // desktop (panel added/removed) is not reconciled until the next
        // retile trigger on this desktop — availableGeometryChanged only
        // retiles the CURRENT desktop's state at change time. Accepted: the
        // drift window is panel changes made on another desktop, and the
        // first insert/close/float on this desktop heals it.
        if (wasDesktopSwitch && !m_autotileScreens.isEmpty()) {
            Q_EMIT autotileScreensChanged(QStringList(m_autotileScreens.begin(), m_autotileScreens.end()), true);
        }
        return;
    }

    const bool wasEnabled = !m_autotileScreens.isEmpty();
    const QSet<QString> added = screens - m_autotileScreens;
    const QSet<QString> removed = m_autotileScreens - screens;

    // If an active drag-insert preview touches any screen being removed (or
    // its prior screen), cancel it before states get torn down below. The
    // cancel path restores the window to its prior location; otherwise the
    // dangling preview would reference a PhosphorTiles::TilingState about to be deleted.
    if (m_dragInsertPreview) {
        const QString targetScreen = m_dragInsertPreview->targetScreenId;
        const QString priorScreen =
            m_dragInsertPreview->hadPriorState ? m_dragInsertPreview->priorKey.screenId : QString();
        if (removed.contains(targetScreen) || (!priorScreen.isEmpty() && removed.contains(priorScreen))) {
            cancelDragInsertPreview();
        }
    }

    m_autotileScreens = screens;

    // R1 fix: Retile newly-added screens without requiring pre-existing state.
    // tilingStateForScreen() creates the PhosphorTiles::TilingState lazily, so windows that arrive
    // shortly after (via KWin effect re-notification) have a state ready.
    for (const QString& screenId : added) {
        tilingStateForScreen(screenId);
        // Skip retile if windows are expected to arrive shortly (pending initial
        // order from seedAutotileOrderForScreen). The KWin effect sends windowOpened
        // D-Bus calls after receiving autotileScreensChanged, and each insertWindow
        // schedules its own retile. Retiling an empty screen here produces a wasted
        // empty windowsTiled signal + stagger generation increment, which can interfere
        // with the first real retile's animation timing.
        // For screen hotplug (no pending order), windows are already in the PhosphorTiles::TilingState
        // and the retile is needed to reflow them on the new screen.
        //
        // Skip retile when pending initial order exists (windows arriving shortly
        // via D-Bus). For desktop return with existing tiled windows, still retile
        // to ensure geometry is up-to-date (screen geometry may have changed while
        // on another desktop, e.g., panel added/removed). The effect-side borderless
        // re-application handles the visual state; the retile ensures positions match
        // the current screen geometry.
        // Only consume the pending order eagerly for STRICT entries (mode
        // transition seeded by setInitialWindowOrder — windows are already
        // open in KWin and need to be added to the autotile state with the
        // computed order BEFORE the effect's windowOpened re-announce lands,
        // so the first retile uses the seeded order; the later windowOpened
        // for an already-present window is a tracked no-op insert).
        // Advisory entries describe historical positions for windows that aren't open yet —
        // pre-seeding the state would create ghost entries the user can't
        // close, and would also override the user's insertPosition preference
        // when the windows actually do open. Leave the advisory order in
        // pendingInitialOrders for insertWindow() to consult on arrival.
        if (m_pendingInitialOrders.contains(screenId) && m_strictInitialOrderScreens.contains(screenId)) {
            const QStringList order = m_pendingInitialOrders.take(screenId);
            m_pendingOrderGeneration.remove(screenId);
            m_strictInitialOrderScreens.remove(screenId);
            PhosphorTiles::TilingState* ts = tilingStateForScreen(screenId);
            if (ts) {
                const TilingStateKey stateKey = currentKeyForScreen(screenId);
                for (const QString& windowId : order) {
                    if (!ts->containsWindow(windowId)) {
                        ts->addWindow(windowId);
                        // Register engine tracking immediately — without the
                        // key entry, a window closing before the effect's
                        // windowOpened round-trip hits onWindowRemoved's
                        // empty-stored-key early return and stays a permanent
                        // ghost the layout retiles around.
                        m_windowToStateKey[windowId] = stateKey;
                        // Restore floating state from the unified record (single source
                        // of truth). Without this, windows added from pending orders lose
                        // their floating state because windowOpened's floating restore is
                        // skipped when the window already exists in the PhosphorTiles::TilingState.
                        if (m_windowTracker) {
                            const auto rec = m_windowTracker->placementStore().peek(
                                windowId, m_windowTracker->currentAppIdFor(windowId));
                            if (rec
                                && rec->slotFor(engineId()).state == PhosphorEngine::WindowPlacement::stateFloating()) {
                                ts->setFloating(windowId, true);
                            }
                        }
                    }
                }
            }
        }
        scheduleRetileForScreen(screenId);
    }

    // Only prune states for the CURRENT desktop/activity. States belonging to
    // other desktops are preserved so desktop switching is a fast state swap
    // (no window release/re-add). windowsReleasedFromTiling MUST NOT fire
    // for desktop/activity transitions — only for true autotile disable.
    QStringList releasedWindows;
    QMutableHashIterator<TilingStateKey, PhosphorTiles::TilingState*> it(m_screenStates);
    while (it.hasNext()) {
        it.next();
        const TilingStateKey& key = it.key();
        // Only prune states that match the current desktop/activity AND whose
        // screen is no longer in the autotile set. States for other contexts
        // are left untouched here — by the time their desktop becomes current
        // the screen is already absent from m_autotileScreens, so this loop
        // never sees them again; they are healed per-window (windowFocused /
        // windowOpened migration) and reaped wholesale by
        // pruneStatesForDesktop / pruneStatesForActivities when their
        // desktop or activity is destroyed.
        if (key.desktop != currentKeyForScreen(key.screenId).desktop || key.activity != m_currentActivity) {
            continue;
        }
        if (!removed.contains(key.screenId)) {
            continue;
        }
        releaseScreenStateForTeardown(key.screenId, it.value(), releasedWindows);
        // Toggle-off drops only the resolver's IN-MEMORY overrides (they are
        // re-derived from settings on re-enable); the persisted per-screen
        // settings deliberately survive — a user toggling autotile off must
        // not lose their per-monitor configuration. Contrast with the
        // orphaned-virtual-screen teardown, which purges both layers because
        // a dead VS id is never reused.
        m_configResolver->removeOverridesForScreen(key.screenId);
        it.remove();
    }
    // Clean up m_windowToStateKey entries for released windows BEFORE emitting
    // the signal. Signal handlers (signals.cpp windowsReleasedFromTiling) check
    // zone assignments and floating state — stale mappings would cause them to
    // see phantom candidates.
    for (const QString& windowId : std::as_const(releasedWindows)) {
        m_windowToStateKey.remove(windowId);
    }

    if (!releasedWindows.isEmpty()) {
        Q_EMIT windowsReleased(releasedWindows, removed);
    }

    // Clean up any remaining overflow entries for removed screens. KNOWN
    // LIMITATION: the overflow bucket is keyed per-screenId only, while the
    // prune loop above (by design) tears down current-context states only —
    // a preserved other-desktop/activity state on a removed screen loses its
    // overflow markers here, so its save-time capturePlacement records
    // overflow-floated windows as user floats (they re-float instead of
    // re-tiling on re-enable). Accepted: fixing it requires re-keying
    // OverflowManager per (screen, context), and the window is narrow —
    // toggle-off while another context holds overflow on the same screen.
    m_overflow.clearForRemovedScreens(m_autotileScreens);

    // Clear per-screen desktop maps for removed screens — both the sticky-pin
    // override and the per-output-VD map (#648).
    for (const QString& screenId : removed) {
        m_screenDesktopOverride.remove(screenId);
        m_screenCurrentDesktop.remove(screenId);
    }

    // Clear any pending deferred retiles and retry state for removed screens
    for (auto pit = m_pendingRetileScreens.begin(); pit != m_pendingRetileScreens.end();) {
        if (!m_autotileScreens.contains(*pit)) {
            pit = m_pendingRetileScreens.erase(pit);
        } else {
            ++pit;
        }
    }
    for (const QString& screenId : removed) {
        m_retileRetryScreens.remove(screenId);
        m_retileRetryCount.remove(screenId);
    }

    const bool nowEnabled = !m_autotileScreens.isEmpty();
    // Capture before clearing — the emit below needs the original value.
    const bool wasDesktopSwitch = m_isDesktopContextSwitch;
    m_isDesktopContextSwitch = false;

    if (wasEnabled != nowEnabled && !wasDesktopSwitch) {
        // Only emit enabledChanged for actual mode toggles, not desktop/activity
        // switch. On desktop switch the effect must NOT clear borderless/monocle/
        // stacking tracking (enabledChanged false) or re-process windows (true).
        Q_EMIT enabledChanged(nowEnabled);
    }

    Q_EMIT autotileScreensChanged(QStringList(m_autotileScreens.begin(), m_autotileScreens.end()), wasDesktopSwitch);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Algorithm selection
// ═══════════════════════════════════════════════════════════════════════════════

QString AutotileEngine::algorithm() const noexcept
{
    return m_algorithmId;
}

void AutotileEngine::setAlgorithm(const QString& algorithmId)
{
    // Validate algorithm exists. Headless unit tests deliberately pass
    // nullptr for the registry (per the constructor contract), so guard
    // here rather than crashing — the engine simply records the requested
    // id without validation, mirroring the no-op return in
    // currentAlgorithm()/setWindowRegistry below.
    auto* registry = m_algorithmRegistry;
    QString newId = algorithmId;
    if (!registry) {
        if (m_algorithmId == newId) {
            return;
        }
        m_algorithmEverSet = true;
        m_algorithmId = newId;
        m_config->algorithmId = newId;
        Q_EMIT algorithmChanged(m_algorithmId);
        return;
    }

    if (!registry->hasAlgorithm(newId)) {
        qCWarning(PhosphorTileEngine::lcTileEngine)
            << "AutotileEngine: unknown algorithm" << newId << "- using default";
        newId = PhosphorTiles::AlgorithmRegistry::staticDefaultAlgorithmId();
    }

    if (m_algorithmId == newId) {
        return;
    }

    // Switching algorithms resets ratios/counts to the new algorithm's saved or
    // default values for every desktop, so per-desktop user tunings no longer
    // apply — drop them and let the propagate calls below re-seed each state.
    m_userTunedSplitRatio.clear();
    m_userTunedMasterCount.clear();

    PhosphorTiles::TilingAlgorithm* oldAlgo = registry->algorithm(m_algorithmId);
    PhosphorTiles::TilingAlgorithm* newAlgo = registry->algorithm(newId);
    const int oldMaxWindows = m_config->maxWindows;

    // Save current algorithm's ratio + master count before switching.
    // Only save after the first setAlgorithm() call has completed, to avoid
    // persisting uninitialised struct defaults from the constructor.
    if (m_algorithmEverSet && oldAlgo) {
        auto& entry = m_config->savedAlgorithmSettings[m_algorithmId];
        entry.splitRatio = m_config->splitRatio;
        entry.masterCount = m_config->masterCount;
        // customParams are not touched here — only splitRatio/masterCount are engine-managed
    }

    // Look up saved settings AFTER the save above — insertion may rehash the
    // QHash, invalidating any iterator obtained before the insert.
    auto savedIt = m_config->savedAlgorithmSettings.constFind(newId);

    // Restore per-algorithm split ratio and master count from saved settings,
    // falling back to the algorithm's defaults when no saved entry exists.
    auto restorePerAlgoSettings = [this](PhosphorTiles::TilingAlgorithm* algo,
                                         QHash<QString, AlgorithmSettings>::const_iterator it) {
        if (it != m_config->savedAlgorithmSettings.constEnd()) {
            m_config->splitRatio = it->splitRatio;
            m_config->masterCount = it->masterCount;
        } else {
            m_config->splitRatio = algo->defaultSplitRatio();
            m_config->masterCount = PhosphorTiles::AutotileDefaults::DefaultMasterCount;
        }
    };

    if (oldAlgo && newAlgo) {
        restorePerAlgoSettings(newAlgo, savedIt);
        propagateGlobalSplitRatio();
        propagateGlobalMasterCount();

        // Same pattern for maxWindows: if the user hasn't customized it away
        // from the old algorithm's default, reset to the new algorithm's default.
        // Without this, switching from MasterStack (4) to BSP (5) keeps maxWindows=4.
        resetMaxWindowsForAlgorithmSwitch(oldAlgo, newAlgo);
    } else if (newAlgo) {
        // oldAlgo is nullptr (first-ever call or corrupted m_algorithmId).
        // Initialize config from the new algorithm's defaults or saved settings.
        restorePerAlgoSettings(newAlgo, savedIt);
        m_config->maxWindows = newAlgo->defaultMaxWindows();
        propagateGlobalSplitRatio();
        propagateGlobalMasterCount();
    }

    // Commit the new algorithm id BEFORE the write-back block so that any
    // observer that reads m_algorithmId during write-back (e.g. a slot
    // that survives the QSignalBlocker via a Qt::DirectConnection from
    // outside engineSettings()) sees the new value, not the stale one.
    // The guard timer + signal blocker still prevent the normal
    // syncFromSettings re-entry path; this reorder just removes a latent
    // observable window where m_algorithmId disagreed with the value
    // being persisted.
    m_algorithmEverSet = true;
    m_algorithmId = newId;
    m_config->algorithmId = newId;

    // Persist the per-algorithm tuning (split ratio, master count, saved
    // per-algorithm settings, and maxWindows when it changed) so the next
    // session restores the user's tuning for whatever algorithm they end
    // up on. Signal-blocked write prevents recursive corruption (daemon
    // settingsChanged → syncFromSettings → setAlgorithm with stale KCM
    // algo).
    //
    // NOTE: we deliberately do NOT call `setDefaultAutotileAlgorithm(newId)`
    // here. The global default algorithm is a user-owned setting modified
    // ONLY through the Layouts page (or its sub-pages / context menus).
    // Per-screen / per-context applies that route through this method —
    // e.g. UnifiedLayoutController applying an autotile entry on the
    // current screen, or AutotileAdaptor::setAlgorithm from a script —
    // must not silently overwrite that global preference. Per-screen
    // assignments already carry the algorithm in the (screen, desktop,
    // activity) entry; the engine's m_algorithmId tracks the runtime
    // ambient algorithm and resyncs from defaultAutotileAlgorithm on the
    // next session start, which is the intended behaviour.
    {
        m_writeBackGuardTimer.start();
        const QSignalBlocker blocker(engineSettings());
        writeBackTuning();
        if (auto* s = autotileSettings()) {
            if (m_config->maxWindows != oldMaxWindows)
                s->setAutotileMaxWindows(m_config->maxWindows);
        }
    }

    // Clear stale split trees when switching away from a memory algorithm.
    // Without this, deserialized trees from a previous DwindleMemory session
    // persist after algorithm switch, wasting memory and risking confusion.
    // Must happen BEFORE emitting algorithmChanged so that listeners see
    // consistent state (no stale trees from the old algorithm).
    if (newAlgo && !newAlgo->supportsMemory()) {
        for (auto* state : m_screenStates) {
            state->clearSplitTree();
        }
    }

    // Clear the per-algorithm script-state bag on every switch. It is opaque
    // state private to the previous algorithm (e.g. an aligned grid's column
    // fractions) with no meaning to the next — a different scripted algorithm
    // that also opts into supportsScriptState must not inherit it. Unlike the
    // split tree above (which two memory algorithms can meaningfully share),
    // script state has no cross-algorithm validity, so this is unconditional.
    // Safe because this point is reached only when the algorithm id changed
    // (early return above).
    for (auto* state : m_screenStates) {
        state->setScriptState({});
    }

    Q_EMIT algorithmChanged(m_algorithmId);

    // Backfill windows when the new algorithm's maxWindows is higher.
    // Guard with maxWindows-increased check to avoid wasted iteration when the
    // new algorithm has a lower or equal limit.
    if (isEnabled()) {
        if (m_config->maxWindows > oldMaxWindows) {
            backfillWindows();
        }
        // Defer retile instead of running immediately. When setAlgorithm is called
        // from applyEntry() or connectToSettings(), the per-screen overrides haven't
        // been updated yet (updateAutotileScreens runs after). An immediate retile
        // would use effectiveAlgorithm() with the stale per-screen override (OLD algo),
        // producing wrong geometries and emitting a bad windowsTiled signal to KWin.
        // Deferring to the next event loop pass ensures per-screen overrides are current.
        //
        // Only retile screens that actually use the global algorithm (no per-screen
        // override). Screens with per-screen algorithm overrides are unaffected by
        // this global change and are handled by updateAutotileScreens() when the
        // layoutAssigned signal fires from applyEntry().
        for (const QString& screen : m_autotileScreens) {
            if (effectiveAlgorithmId(screen) == newId) {
                scheduleRetileForScreen(screen);
            }
        }
    }
}

PhosphorTiles::TilingAlgorithm* AutotileEngine::currentAlgorithm() const
{
    // Null-tolerant per the ctor contract — headless unit tests construct
    // an engine without a registry. Returning nullptr is the documented
    // signal for "no algorithm available"; every caller already guards.
    return m_algorithmRegistry ? m_algorithmRegistry->algorithm(m_algorithmId) : nullptr;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Tiling state access
// ═══════════════════════════════════════════════════════════════════════════════

PhosphorTiles::TilingState* AutotileEngine::tilingStateForScreen(const QString& screenId)
{
    // Validate screenId - don't create state for empty name
    if (screenId.isEmpty()) {
        qCWarning(PhosphorTileEngine::lcTileEngine) << "AutotileEngine::tilingStateForScreen: empty screen name";
        return nullptr;
    }

    const TilingStateKey key = currentKeyForScreen(screenId);

    // Check for existing state before validating screen existence — existing
    // states are valid even if the screen is temporarily disconnected (e.g.,
    // monitor power-off during a desktop switch). Only gate NEW state creation.
    auto it = m_screenStates.find(key);
    if (it != m_screenStates.end()) {
        return it.value();
    }

    // Reject unknown screens to prevent unbounded state creation from bogus
    // D-Bus callers. Session bus only (same user), but still good hygiene.
    if (!isKnownScreen(screenId)) {
        qCWarning(PhosphorTileEngine::lcTileEngine)
            << "AutotileEngine::tilingStateForScreen: unknown screen" << screenId;
        return nullptr;
    }

    // Create new state for this screen+desktop+activity with parent ownership
    auto* state = new PhosphorTiles::TilingState(screenId, this);

    // Initialize with config defaults
    state->setMasterCount(m_config->masterCount);
    state->setSplitRatio(m_config->splitRatio);

    m_screenStates.insert(key, state);
    return state;
}

PhosphorTiles::TilingState* AutotileEngine::stateForKey(const TilingStateKey& key)
{
    if (key.screenId.isEmpty()) {
        return nullptr;
    }

    auto it = m_screenStates.find(key);
    if (it != m_screenStates.end()) {
        return it.value();
    }

    // Reject unknown screens (same validation as tilingStateForScreen)
    if (!isKnownScreen(key.screenId)) {
        qCWarning(PhosphorTileEngine::lcTileEngine) << "AutotileEngine::stateForKey: unknown screen" << key.screenId;
        return nullptr;
    }

    auto* state = new PhosphorTiles::TilingState(key.screenId, this);
    state->setMasterCount(m_config->masterCount);
    state->setSplitRatio(m_config->splitRatio);
    m_screenStates.insert(key, state);
    return state;
}

QSet<int> AutotileEngine::desktopsWithActiveState() const
{
    QSet<int> out;
    out.reserve(m_screenStates.size());
    for (auto it = m_screenStates.constBegin(); it != m_screenStates.constEnd(); ++it) {
        out.insert(it.key().desktop);
    }
    return out;
}

void AutotileEngine::pruneStatesForDesktop(int removedDesktop)
{
    QMutableHashIterator<TilingStateKey, PhosphorTiles::TilingState*> it(m_screenStates);
    int pruned = 0;
    while (it.hasNext()) {
        it.next();
        if (it.key().desktop == removedDesktop) {
            it.value()->deleteLater();
            it.remove();
            ++pruned;
        }
    }
    // Clean up window-to-state-key entries that reference the pruned desktop.
    // Stale entries would pollute backfillWindows() and could incorrectly match
    // if desktop numbers are reused.
    QMutableHashIterator<QString, TilingStateKey> wit(m_windowToStateKey);
    while (wit.hasNext()) {
        wit.next();
        if (wit.value().desktop == removedDesktop) {
            wit.remove();
        }
    }
    // Clear desktop overrides referencing the removed desktop
    QMutableHashIterator<QString, int> oit(m_screenDesktopOverride);
    while (oit.hasNext()) {
        oit.next();
        if (oit.value() == removedDesktop) {
            oit.remove();
        }
    }
    // Same for the per-output virtual-desktop map (#648) — a screen pinned to a
    // now-deleted desktop number must drop the entry; the effect re-reports the
    // screen's true (renumbered) desktop shortly after.
    QMutableHashIterator<QString, int> sit(m_screenCurrentDesktop);
    while (sit.hasNext()) {
        sit.next();
        if (sit.value() == removedDesktop) {
            sit.remove();
        }
    }
    if (pruned > 0) {
        qCInfo(PhosphorTileEngine::lcTileEngine)
            << "Pruned" << pruned << "TilingStates for removed desktop" << removedDesktop;
    }
}

void AutotileEngine::pruneStatesForActivities(const QStringList& validActivities)
{
    const QSet<QString> valid(validActivities.begin(), validActivities.end());
    QMutableHashIterator<TilingStateKey, PhosphorTiles::TilingState*> it(m_screenStates);
    int pruned = 0;
    while (it.hasNext()) {
        it.next();
        const QString& act = it.key().activity;
        if (!act.isEmpty() && !valid.contains(act)) {
            it.value()->deleteLater();
            it.remove();
            ++pruned;
        }
    }
    // Clean up window-to-state-key entries that reference pruned activities
    QMutableHashIterator<QString, TilingStateKey> wit(m_windowToStateKey);
    while (wit.hasNext()) {
        wit.next();
        const QString& act = wit.value().activity;
        if (!act.isEmpty() && !valid.contains(act)) {
            wit.remove();
        }
    }
    if (pruned > 0) {
        qCInfo(PhosphorTileEngine::lcTileEngine) << "Pruned" << pruned << "TilingStates for removed activities";
    }
}

AutotileConfig* AutotileEngine::config() const noexcept
{
    return m_config.get();
}

// ═══════════════════════════════════════════════════════════════════════════════
// PhosphorZones::Zone-ordered window transitions
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileEngine::setInitialWindowOrder(const QString& screenId, const QStringList& rawWindowIds)
{
    if (rawWindowIds.isEmpty()) {
        return;
    }
    // Canonicalize every id up front so the pending order is consistent with
    // the keys used by PhosphorTiles::TilingState when windowOpened() arrives next.
    QStringList windowIds;
    windowIds.reserve(rawWindowIds.size());
    for (const QString& raw : rawWindowIds) {
        windowIds.append(canonicalizeWindowId(raw));
    }
    // Only take effect when the screen's PhosphorTiles::TilingState is empty (no prior windows —
    // including floating — from session restore). Uses windowCount() instead of
    // tiledWindows() to also detect floating-only states.
    PhosphorTiles::TilingState* state = tilingStateForScreen(screenId);
    if (state && state->windowCount() > 0) {
        qCDebug(PhosphorTileEngine::lcTileEngine) << "setInitialWindowOrder: screen" << screenId << "already has"
                                                  << state->windowCount() << "windows, ignoring pre-seeded order";
        return;
    }
    // Warn (but allow) if overwriting a pending order that hasn't been fully consumed
    if (m_pendingInitialOrders.contains(screenId)) {
        qCWarning(PhosphorTileEngine::lcTileEngine)
            << "setInitialWindowOrder: overwriting existing pending order for" << screenId;
    }
    m_pendingInitialOrders[screenId] = windowIds;
    // Mode-transition seeding is strict: the daemon explicitly computed an
    // order it wants preserved (zone order from the previous mode). Even if
    // windows arrive in a different sequence, the saved positions win.
    m_strictInitialOrderScreens.insert(screenId);
    uint64_t gen = ++m_pendingOrderGeneration[screenId];
    qCInfo(PhosphorTileEngine::lcTileEngine)
        << "Pre-seeded window order for screen=" << screenId << "windows=" << windowIds;

    // Safety timeout: clean up if windows never arrive (e.g., app crash during startup).
    // Use a generation counter so that stale timers from overwritten calls become no-ops.
    QTimer::singleShot(PendingOrderTimeoutMs, this, [this, screenId, gen]() {
        if (m_pendingOrderGeneration.value(screenId) != gen) {
            return; // superseded by a newer setInitialWindowOrder call
        }
        if (m_pendingInitialOrders.remove(screenId)) {
            m_pendingOrderGeneration.remove(screenId);
            m_strictInitialOrderScreens.remove(screenId);
            qCWarning(PhosphorTileEngine::lcTileEngine)
                << "Pending initial order for screen" << screenId << "timed out after" << PendingOrderTimeoutMs
                << "ms - cleaning up stale entry";
        }
    });
}

QStringList AutotileEngine::tiledWindowOrder(const QString& screenId) const
{
    const TilingStateKey key = currentKeyForScreen(screenId);
    PhosphorTiles::TilingState* state = m_screenStates.value(key);
    if (!state) {
        return {};
    }
    return state->tiledWindows();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Settings synchronization
// ═══════════════════════════════════════════════════════════════════════════════

PhosphorEngine::IAutotileSettings* AutotileEngine::autotileSettings() const
{
    return qobject_cast<PhosphorEngine::IAutotileSettings*>(engineSettings());
}

void AutotileEngine::writeBackTuning()
{
    auto* settings = engineSettings();
    if (!settings) {
        return;
    }
    const QSignalBlocker blocker(settings);
    if (auto* s = autotileSettings()) {
        s->setAutotileSplitRatio(m_config->splitRatio);
        s->setAutotileMasterCount(m_config->masterCount);
        s->setAutotilePerAlgorithmSettings(AutotileConfig::perAlgoToVariantMap(m_config->savedAlgorithmSettings));
    }
}

void AutotileEngine::refreshConfigFromSettings()
{
    auto* s = autotileSettings();
    if (!s) {
        return;
    }

    bool configChanged = false;
    const int oldMaxWindows = m_config->maxWindows;
    const auto oldOverflow = m_config->overflowBehavior;

#define SYNC_FIELD(field, getter)                                                                                      \
    do {                                                                                                               \
        auto newVal = s->getter();                                                                                     \
        if (m_config->field != newVal) {                                                                               \
            m_config->field = newVal;                                                                                  \
            configChanged = true;                                                                                      \
        }                                                                                                              \
    } while (0)

    if (!m_writeBackGuardTimer.isActive()) {
        const int newMasterCount = s->autotileMasterCount();
        if (m_config->masterCount != newMasterCount) {
            m_config->masterCount = newMasterCount;
            configChanged = true;
            // An explicit global master-count change (settings) overrides any
            // per-desktop tunings, which the propagate below then re-applies.
            m_userTunedMasterCount.clear();
        }
    }
    SYNC_FIELD(innerGap, autotileInnerGap);
    SYNC_FIELD(outerGap, autotileOuterGap);
    SYNC_FIELD(usePerSideOuterGap, autotileUsePerSideOuterGap);
    SYNC_FIELD(outerGapTop, autotileOuterGapTop);
    SYNC_FIELD(outerGapBottom, autotileOuterGapBottom);
    SYNC_FIELD(outerGapLeft, autotileOuterGapLeft);
    SYNC_FIELD(outerGapRight, autotileOuterGapRight);
    SYNC_FIELD(focusNewWindows, autotileFocusNewWindows);
    SYNC_FIELD(smartGaps, autotileSmartGaps);
    SYNC_FIELD(focusFollowsMouse, autotileFocusFollowsMouse);
    SYNC_FIELD(respectMinimumSize, autotileRespectMinimumSize);
    SYNC_FIELD(maxWindows, autotileMaxWindows);

    if (!m_writeBackGuardTimer.isActive()) {
        const qreal newRatio = s->autotileSplitRatio();
        if (!qFuzzyCompare(1.0 + m_config->splitRatio, 1.0 + newRatio)) {
            m_config->splitRatio = newRatio;
            configChanged = true;
            // An explicit global split-ratio change (settings) overrides any
            // per-desktop tunings, which the propagate below then re-applies.
            m_userTunedSplitRatio.clear();
        }
    }
    {
        const qreal newStep = s->autotileSplitRatioStep();
        if (!qFuzzyCompare(1.0 + m_config->splitRatioStep, 1.0 + newStep)) {
            m_config->splitRatioStep = newStep;
        }
    }

    {
        const auto newInsert = static_cast<AutotileConfig::InsertPosition>(s->autotileInsertPosition());
        if (m_config->insertPosition != newInsert) {
            m_config->insertPosition = newInsert;
            configChanged = true;
        }
    }

    {
        const auto newOverflow = s->autotileOverflowBehavior();
        if (m_config->overflowBehavior != newOverflow) {
            m_config->overflowBehavior = newOverflow;
            configChanged = true;
        }
    }

    {
        const auto newSaved = AutotileConfig::perAlgoFromVariantMap(s->autotilePerAlgorithmSettings());
        if (m_config->savedAlgorithmSettings != newSaved) {
            m_config->savedAlgorithmSettings = newSaved;
            configChanged = true;
        }
    }

#undef SYNC_FIELD

    const QString oldAlgorithmId = m_algorithmId;
    setAlgorithm(s->defaultAutotileAlgorithm());
    if (m_algorithmId != oldAlgorithmId) {
        configChanged = true;
    }

    if (m_algorithmId == oldAlgorithmId) {
        auto savedIt = m_config->savedAlgorithmSettings.constFind(m_algorithmId);
        if (savedIt != m_config->savedAlgorithmSettings.constEnd()) {
            m_config->splitRatio = savedIt->splitRatio;
            m_config->masterCount = savedIt->masterCount;
        }
    }

    propagateGlobalSplitRatio();
    propagateGlobalMasterCount();

    // Float→Unlimited: backfill previously-overflowed floating windows
    const bool overflowBackfilled = oldOverflow == PhosphorTiles::AutotileOverflowBehavior::Float
        && m_config->overflowBehavior == PhosphorTiles::AutotileOverflowBehavior::Unlimited;
    if (overflowBackfilled) {
        backfillWindows();
    }

    if (m_config->maxWindows > oldMaxWindows && !overflowBackfilled) {
        backfillWindows();
    }

    // Update preview params so algorithm previews in the KCM reflect current values
    PhosphorTiles::AlgorithmPreviewParams previewParams;
    previewParams.algorithmId = m_algorithmId;
    previewParams.maxWindows = m_config->maxWindows;
    previewParams.masterCount = m_config->masterCount;
    previewParams.splitRatio = m_config->splitRatio;
    for (auto it = m_config->savedAlgorithmSettings.constBegin(); it != m_config->savedAlgorithmSettings.constEnd();
         ++it) {
        QVariantMap entry{
            {PhosphorTiles::AutotileJsonKeys::MasterCount, it.value().masterCount},
            {PhosphorTiles::AutotileJsonKeys::SplitRatio, it.value().splitRatio},
        };
        if (!it.value().customParams.isEmpty()) {
            entry[PhosphorTiles::AutotileJsonKeys::CustomParams] = it.value().customParams;
        }
        previewParams.savedAlgorithmSettings[it.key()] = entry;
    }
    if (auto* reg = algorithmRegistry()) {
        reg->setPreviewParams(previewParams);
    }

    if (configChanged && isEnabled()) {
        m_settingsRetileTimer.start();
    }

    qCInfo(PhosphorTileEngine::lcTileEngine)
        << "Settings: synced, algorithm=" << m_algorithmId << "autotileScreens=" << m_autotileScreens.size();
}

void AutotileEngine::applyPerScreenConfig(const QString& screenId, const QVariantMap& overrides)
{
    m_configResolver->applyPerScreenConfig(screenId, overrides);
}

void AutotileEngine::clearPerScreenConfig(const QString& screenId)
{
    m_configResolver->clearPerScreenConfig(screenId);
}

QVariantMap AutotileEngine::perScreenOverrides(const QString& screenId) const
{
    return m_configResolver->perScreenOverrides(screenId);
}

bool AutotileEngine::hasPerScreenOverride(const QString& screenId, const QString& key) const
{
    return m_configResolver->hasPerScreenOverride(screenId, key);
}

void AutotileEngine::updatePerScreenOverride(const QString& screenId, const QString& key, const QVariant& value)
{
    m_configResolver->updatePerScreenOverride(screenId, key, value);
}

void AutotileEngine::noteSplitRatioUserTuned(const QString& screenId)
{
    m_userTunedSplitRatio.insert(currentKeyForScreen(screenId));
}

void AutotileEngine::noteMasterCountUserTuned(const QString& screenId)
{
    m_userTunedMasterCount.insert(currentKeyForScreen(screenId));
}

int AutotileEngine::effectiveInnerGap(const QString& screenId) const
{
    return m_configResolver->effectiveInnerGap(screenId);
}

int AutotileEngine::effectiveOuterGap(const QString& screenId) const
{
    return m_configResolver->effectiveOuterGap(screenId);
}

::PhosphorLayout::EdgeGaps AutotileEngine::effectiveOuterGaps(const QString& screenId) const
{
    return m_configResolver->effectiveOuterGaps(screenId);
}

bool AutotileEngine::effectiveSmartGaps(const QString& screenId) const
{
    return m_configResolver->effectiveSmartGaps(screenId);
}

bool AutotileEngine::effectiveRespectMinimumSize(const QString& screenId) const
{
    return m_configResolver->effectiveRespectMinimumSize(screenId);
}

int AutotileEngine::effectiveMaxWindows(const QString& screenId) const
{
    return m_configResolver->effectiveMaxWindows(screenId);
}

qreal AutotileEngine::effectiveSplitRatioStep(const QString& screenId) const
{
    return m_configResolver->effectiveSplitRatioStep(screenId);
}

int AutotileEngine::runtimeMaxWindows() const
{
    return m_config->maxWindows;
}

QString AutotileEngine::effectiveAlgorithmId(const QString& screenId) const
{
    return m_configResolver->effectiveAlgorithmId(screenId);
}

PhosphorTiles::TilingAlgorithm* AutotileEngine::effectiveAlgorithm(const QString& screenId) const
{
    return m_configResolver->effectiveAlgorithm(screenId);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Session Persistence
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileEngine::saveState()
{
    if (m_persistSaveFn) {
        m_persistSaveFn();
    }
}

void AutotileEngine::loadState()
{
    if (m_persistLoadFn) {
        m_persistLoadFn();
    }
}

// serializeWindowOrders / deserializeWindowOrders / serializePendingRestores /
// deserializePendingRestores / pruneExcludedPendingRestores were removed: autotile
// restore state (tiled position, floated geometry) is now persisted by the common
// WindowPlacementStore via capturePlacement(), the same path as snap. Tile order
// (current and non-current contexts alike) is reconstructed from each window's saved
// position in insertWindow() as the window re-announces in its own context — no
// separate per-context order snapshot is needed.

void AutotileEngine::scheduleRetileForScreen(const QString& screenId)
{
    // Contract: pending retiles are keyed by screenId and resolved against
    // the CURRENT desktop/activity context when processPendingRetiles runs.
    // A retile scheduled for a state living in another context (or raced by
    // a desktop switch in the one-event-loop-pass window before processing)
    // retiles the current context's state instead; the source context's
    // zones stay stale until its next operation — bounded by the same
    // accepted-drift trade-off as the identical-set desktop-switch
    // early-return in setAutotileScreens.
    m_pendingRetileScreens.insert(screenId);

    if (!m_retilePending) {
        m_retilePending = true;
        // Qt::QueuedConnection (same-thread deferral, not cross-thread — see Qt docs
        // on QMetaObject::invokeMethod) fires after all currently-pending events
        // (including D-Bus messages from the same socket read) are processed.
        // This naturally coalesces bursts: on first activation, the KWin effect
        // sends N windowOpened D-Bus calls in rapid succession — they all arrive
        // in one socket read and are dispatched before this queued call fires.
        // Single-window opens retile on the very next event loop iteration (~0ms).
        QMetaObject::invokeMethod(this, &AutotileEngine::processPendingRetiles, Qt::QueuedConnection);
    }
}

void AutotileEngine::processPendingRetiles()
{
    m_retilePending = false;

    if (m_pendingRetileScreens.isEmpty()) {
        return;
    }

    const QSet<QString> screens = m_pendingRetileScreens;
    m_pendingRetileScreens.clear();

    for (const QString& screenId : screens) {
        bool isAt = isAutotileScreen(screenId);
        bool hasState = m_screenStates.contains(currentKeyForScreen(screenId));
        if (isAt && hasState) {
            qCInfo(PhosphorTileEngine::lcTileEngine) << "processPendingRetiles: retiling screen" << screenId;
            retileAfterOperation(screenId, true);
        } else {
            qCWarning(PhosphorTileEngine::lcTileEngine) << "processPendingRetiles: skipping screen" << screenId
                                                        << "isAutotile=" << isAt << "hasState=" << hasState;
        }
    }
}

void AutotileEngine::scheduleRetileRetry(const QString& screenId)
{
    int& count = m_retileRetryCount[screenId];
    if (count >= MaxRetileRetries) {
        qCWarning(PhosphorTileEngine::lcTileEngine)
            << "scheduleRetileRetry: exhausted" << MaxRetileRetries << "retries for screen" << screenId
            << "- screen geometry may be permanently unavailable";
        m_retileRetryCount.remove(screenId);
        m_retileRetryScreens.remove(screenId);
        return;
    }
    ++count;
    m_retileRetryScreens.insert(screenId);
    qCInfo(PhosphorTileEngine::lcTileEngine) << "scheduleRetileRetry: attempt" << count << "/" << MaxRetileRetries
                                             << "for screen" << screenId << "in" << RetileRetryIntervalMs << "ms";
    // Single shared timer across all screens — a screen queued later in the
    // same interval gets a shorter effective wait, which is harmless (it just
    // retries sooner and re-schedules if geometry is still unavailable).
    if (!m_retileRetryTimer.isActive()) {
        m_retileRetryTimer.start();
    }
}

void AutotileEngine::processRetileRetries()
{
    if (m_retileRetryScreens.isEmpty()) {
        return;
    }

    const QSet<QString> screens = m_retileRetryScreens;
    m_retileRetryScreens.clear();

    for (const QString& screenId : screens) {
        if (!isAutotileScreen(screenId)) {
            m_retileRetryCount.remove(screenId);
            continue;
        }
        qCInfo(PhosphorTileEngine::lcTileEngine) << "processRetileRetries: retrying screen" << screenId;
        retileAfterOperation(screenId, true);
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Manual tiling operations
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileEngine::retile(const QString& screenId)
{
    // m_retiling serves as a re-entrancy guard for both retile() and
    // retileAfterOperation(). Both methods set it with QScopeGuard and check it
    // on entry. They are mutually exclusive: retileAfterOperation() returns early
    // if m_retiling is already true (set by retile()), so the dual QScopeGuard
    // pattern cannot leave the flag inconsistent.
    if (m_retiling) {
        return;
    }
    QScopeGuard guard([this] {
        m_retiling = false;
    });
    m_retiling = true;

    if (screenId.isEmpty()) {
        // Retile autotile screens only (current desktop/activity)
        for (const QString& screen : m_autotileScreens) {
            if (m_screenStates.contains(currentKeyForScreen(screen))) {
                retileScreen(screen);
            }
        }
    } else {
        if (!isAutotileScreen(screenId)) {
            return;
        }
        retileScreen(screenId);
    }
}

void AutotileEngine::swapWindows(const QString& rawId1, const QString& rawId2)
{
    const QString windowId1 = canonicalizeWindowId(rawId1);
    const QString windowId2 = canonicalizeWindowId(rawId2);
    // Early return if same window (no-op)
    if (windowId1 == windowId2) {
        return;
    }

    // Find screens for both windows
    const auto key1 = m_windowToStateKey.value(windowId1);
    const auto key2 = m_windowToStateKey.value(windowId2);
    const QString screen1 = key1.screenId;
    const QString screen2 = key2.screenId;

    if (screen1.isEmpty() || screen2.isEmpty()) {
        qCWarning(PhosphorTileEngine::lcTileEngine) << "AutotileEngine::swapWindows: window not found";
        return;
    }

    if (screen1 != screen2) {
        qCWarning(PhosphorTileEngine::lcTileEngine) << "AutotileEngine::swapWindows: windows on different screens";
        return;
    }

    // Use the stored key — not tilingStateForScreen(currentContext) — to avoid
    // wrong-desktop lookups from stale D-Bus calls after a context switch.
    PhosphorTiles::TilingState* state = m_screenStates.value(key1);
    if (!state) {
        return;
    }

    const bool swapped = state->swapWindowsById(windowId1, windowId2);
    retileAfterOperation(screen1, swapped);
}

void AutotileEngine::promoteToMaster(const QString& rawWindowId)
{
    const QString windowId = canonicalizeWindowId(rawWindowId);
    QString screenId;
    PhosphorTiles::TilingState* state = stateForWindow(windowId, &screenId);
    if (!state) {
        return;
    }

    const bool promoted = state->moveToTiledPosition(windowId, 0);
    retileAfterOperation(screenId, promoted);
}

void AutotileEngine::demoteFromMaster(const QString& rawWindowId)
{
    const QString windowId = canonicalizeWindowId(rawWindowId);
    QString screenId;
    PhosphorTiles::TilingState* state = stateForWindow(windowId, &screenId);
    if (!state) {
        return;
    }

    // Move to position after master area (only if currently in master area)
    const int masterCount = state->masterCount();
    const int currentPos = state->tiledWindowIndex(windowId);

    bool demoted = false;
    if (currentPos >= 0 && currentPos < masterCount) {
        demoted = state->moveToTiledPosition(windowId, masterCount);
    }

    retileAfterOperation(screenId, demoted);
}

void AutotileEngine::swapFocusedWithMaster()
{
    m_navigation->swapFocusedWithMaster();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Focus/window cycling
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileEngine::focusNext()
{
    m_navigation->focusNext();
}

void AutotileEngine::focusPrevious()
{
    m_navigation->focusPrevious();
}

void AutotileEngine::focusMaster()
{
    m_navigation->focusMaster();
}

void AutotileEngine::setFocusedWindow(const QString& rawWindowId)
{
    onWindowFocused(canonicalizeWindowId(rawWindowId));
}

void AutotileEngine::setActiveScreenHint(const QString& screenId)
{
    if (!screenId.isEmpty()) {
        m_activeScreen = screenId;
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Split ratio adjustment
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileEngine::increaseMasterRatio(qreal delta)
{
    m_navigation->increaseMasterRatio(delta);
}

void AutotileEngine::decreaseMasterRatio(qreal delta)
{
    m_navigation->decreaseMasterRatio(delta);
}

void AutotileEngine::setGlobalSplitRatio(qreal ratio)
{
    m_navigation->setGlobalSplitRatio(ratio);
}

void AutotileEngine::setGlobalMasterCount(int count)
{
    m_navigation->setGlobalMasterCount(count);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Master count adjustment
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileEngine::increaseMasterCount()
{
    m_navigation->adjustMasterCount(1);
}

void AutotileEngine::decreaseMasterCount()
{
    m_navigation->adjustMasterCount(-1);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Window rotation and floating
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileEngine::rotateWindowOrder(bool clockwise)
{
    m_navigation->rotateWindowOrder(clockwise);
}

void AutotileEngine::swapFocusedInDirection(const QString& direction, const QString& action)
{
    m_navigation->swapFocusedInDirection(direction, action);
}

void AutotileEngine::focusInDirection(const QString& direction, const QString& action)
{
    m_navigation->focusInDirection(direction, action);
}

void AutotileEngine::moveFocusedToPosition(int position)
{
    m_navigation->moveFocusedToPosition(position);
}

void AutotileEngine::toggleFocusedWindowFloat()
{
    // Resolve the focused screen — same logic as NavigationController::tiledWindowsForFocusedScreen
    // but we only need the screen name and state (not the tiled windows list).
    QString screenId;
    PhosphorTiles::TilingState* state = nullptr;

    if (!m_activeScreen.isEmpty() && m_screenStates.contains(currentKeyForScreen(m_activeScreen))) {
        PhosphorTiles::TilingState* s = m_screenStates.value(currentKeyForScreen(m_activeScreen));
        if (s && !s->focusedWindow().isEmpty()) {
            screenId = m_activeScreen;
            state = s;
        }
    }
    if (!state) {
        for (auto it = m_screenStates.constBegin(); it != m_screenStates.constEnd(); ++it) {
            if (it.value() && !it.value()->focusedWindow().isEmpty()
                && it.key().desktop == currentKeyForScreen(it.key().screenId).desktop
                && it.key().activity == m_currentActivity) {
                screenId = it.key().screenId;
                state = it.value();
                break;
            }
        }
    }

    if (!state) {
        qCWarning(PhosphorTileEngine::lcTileEngine) << "toggleFocusedWindowFloat: no state found for focused screen"
                                                    << "- m_activeScreen=" << m_activeScreen;
        Q_EMIT navigationFeedback(false, QStringLiteral("float"), QStringLiteral("no_focused_screen"), QString(),
                                  QString(), m_activeScreen);
        return;
    }

    const QString focused = state->focusedWindow();
    if (focused.isEmpty()) {
        qCWarning(PhosphorTileEngine::lcTileEngine)
            << "toggleFocusedWindowFloat: no focused window on screen" << screenId;
        Q_EMIT navigationFeedback(false, QStringLiteral("float"), QStringLiteral("no_focused_window"), QString(),
                                  QString(), screenId);
        return;
    }

    performToggleFloat(state, focused, screenId);
}

void AutotileEngine::toggleWindowFloat(const QString& rawWindowId, const QString& screenId)
{
    if (!warnIfEmptyWindowId(rawWindowId, "toggleWindowFloat")) {
        return;
    }
    // This is the path that broke for Emby (discussion #271): the incoming
    // composite has a mutated appId, so a raw lookup in m_windowToStateKey
    // missed. Canonicalize resolves it back to the first-seen form.
    const QString windowId = canonicalizeWindowId(rawWindowId);

    if (screenId.isEmpty()) {
        qCWarning(PhosphorTileEngine::lcTileEngine) << "toggleWindowFloat: empty screenId for window" << windowId;
        Q_EMIT navigationFeedback(false, QStringLiteral("float"), QStringLiteral("no_screen"), QString(), QString(),
                                  QString());
        return;
    }

    // Try the given screen first
    QString resolvedScreen = screenId;
    PhosphorTiles::TilingState* state = nullptr;

    if (isAutotileScreen(screenId)) {
        state = tilingStateForScreen(screenId);
        if (state && !state->containsWindow(windowId)) {
            state = nullptr; // Window not on this screen
        }
    }

    // Cross-screen fallback: the window may have been moved (e.g., pre-autotile
    // geometry restore put it on a different screen). Search current desktop/activity
    // states only — states for other desktops should not be considered.
    if (!state) {
        for (auto it = m_screenStates.constBegin(); it != m_screenStates.constEnd(); ++it) {
            if (it.key().desktop != currentKeyForScreen(it.key().screenId).desktop
                || it.key().activity != m_currentActivity) {
                continue;
            }
            if (it.value() && it.value()->containsWindow(windowId)) {
                state = it.value();
                resolvedScreen = it.key().screenId;
                qCInfo(PhosphorTileEngine::lcTileEngine) << "toggleWindowFloat: window" << windowId << "found on screen"
                                                         << resolvedScreen << "(caller reported" << screenId << ")";
                break;
            }
        }
    }

    if (!state) {
        // Window not tracked by autotile. The opportunistic "is this a
        // floating window I should adopt?" branch that used to live here
        // was the second-order accomplice in a class of cross-engine
        // misroute bugs: if the daemon's lastActiveScreen pointed at an
        // autotile screen while the window actually lived on a snap screen
        // (because snap had cleared its tracking on float), this branch
        // would silently grab the floating window and tile it on the wrong
        // screen.
        //
        // Cross-engine handoff now goes through the explicit
        // handoffReceive/handoffRelease contract orchestrated by the daemon
        // — this path is purely "no-op when the window isn't ours".
        qCWarning(PhosphorTileEngine::lcTileEngine)
            << "toggleWindowFloat: window" << windowId << "not found in any autotile state";
        Q_EMIT navigationFeedback(false, QStringLiteral("float"), QStringLiteral("window_not_tracked"), QString(),
                                  QString(), screenId);
        return;
    }

    performToggleFloat(state, windowId, resolvedScreen);
}

void AutotileEngine::performToggleFloat(PhosphorTiles::TilingState* state, const QString& windowId,
                                        const QString& screenId)
{
    state->toggleFloating(windowId);
    m_overflow.clearOverflow(windowId); // User explicitly toggled, no longer overflow
    retileAfterOperation(screenId, true);

    const bool isNowFloating = state->isFloating(windowId);
    qCInfo(PhosphorTileEngine::lcTileEngine)
        << "Window" << windowId << (isNowFloating ? "now floating" : "now tiled") << "on screen" << screenId;
    Q_EMIT windowFloatingChanged(windowId, isNowFloating, screenId);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Cross-engine handoff (see IPlacementEngine.h for contract)
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileEngine::handoffReceive(const HandoffContext& ctx)
{
    if (ctx.windowId.isEmpty() || ctx.toScreenId.isEmpty() || !isAutotileScreen(ctx.toScreenId)) {
        return;
    }
    qCInfo(PhosphorTileEngine::lcTileEngine)
        << "AutotileEngine::handoffReceive:" << ctx.windowId << "to" << ctx.toScreenId << "from" << ctx.fromEngineId
        << "wasFloating=" << ctx.wasFloating;

    const QString windowId = canonicalizeWindowId(ctx.windowId);

    PhosphorTiles::TilingState* state = tilingStateForScreen(ctx.toScreenId);
    if (!state) {
        return;
    }

    // Already tracked on the destination screen — nothing to adopt; the float
    // toggle path is what the caller probably wants instead.
    const auto destKey = currentKeyForScreen(ctx.toScreenId);
    const auto trackedKeyIt = m_windowToStateKey.constFind(windowId);
    if (trackedKeyIt != m_windowToStateKey.constEnd() && trackedKeyIt.value() == destKey
        && state->containsWindow(windowId)) {
        return;
    }
    // Already tracked but on a DIFFERENT autotile state (cross-screen
    // handoff inside the same engine, or stale tracking after an aborted
    // prior handoff). Release the previous state first to avoid orphaning
    // the entry — handoffRelease is the correct primitive for "drop
    // tracking without mutating geometry" within this engine too.
    if (trackedKeyIt != m_windowToStateKey.constEnd() && trackedKeyIt.value() != destKey) {
        handoffRelease(windowId);
    }

    // Insert at the position dictated by the insertion-order setting (a
    // directional cross-mode move should land where new windows land), except:
    //   - a cross-mode SWAP carries an explicit insertIndex so the arriving
    //     window takes the departed partner's exact slot;
    //   - a drag-drop carrying a cursor position, which the drag-insert path
    //     places separately — there we keep the simple append so the drop wins.
    if (ctx.insertIndex >= 0 && ctx.dropPos.isNull()) {
        state->addWindow(windowId, ctx.insertIndex);
    } else if (ctx.dropPos.isNull()) {
        insertWindowByConfigOrder(state, windowId);
    } else {
        state->addWindow(windowId);
    }
    // Autotile-engine policy on receive: a window arriving as "floating in
    // the source" stays floating here too — drag-from-snap typically falls
    // into this branch, and the user's drop position is where they want it.
    // A non-floating arrival gets tiled (the layout engine picks the slot)
    // — drag-from-another-autotile-screen typically falls here.
    state->setFloating(windowId, ctx.wasFloating);
    // Keep the memory algorithm's bookkeeping consistent for a non-floating
    // arrival — symmetric with the removal hook in handoffRelease. Floating
    // arrivals are not in tiledWindows(), so indexOf misses and the hook is
    // correctly skipped.
    PhosphorTiles::TilingAlgorithm* algo = effectiveAlgorithm(ctx.toScreenId);
    if (algo && algo->supportsLifecycleHooks()) {
        const int idx = state->tiledWindows().indexOf(windowId);
        if (idx >= 0) {
            algo->onWindowAdded(state, idx);
        }
    }
    m_windowToStateKey[windowId] = destKey;

    // Trigger a retile so a non-floating arrival actually lands in a tile;
    // floating arrivals retile too because their displacement may free a
    // slot for the remaining tiled set.
    retileAfterOperation(ctx.toScreenId, true);
}

void AutotileEngine::handoffRelease(const QString& windowId)
{
    if (windowId.isEmpty()) {
        return;
    }
    const QString canonical = canonicalizeWindowId(windowId);
    qCInfo(PhosphorTileEngine::lcTileEngine) << "AutotileEngine::handoffRelease:" << canonical;

    auto it = m_windowToStateKey.constFind(canonical);
    if (it == m_windowToStateKey.constEnd()) {
        return; // Not ours; nothing to release.
    }
    const auto key = it.value();
    auto stateIt = m_screenStates.find(key);
    if (stateIt != m_screenStates.end() && stateIt.value()) {
        // Tracking-only release: drop from layout, drop from floating set.
        // No retile of the rest is requested here — the orchestrator will
        // call receiveWindow on the destination engine which (if also
        // autotile) will retile its own state.
        // Keep the memory algorithm's bookkeeping consistent (e.g.
        // dwindle-memory's split tree) — same lifecycle hook every other
        // removal path runs before removeWindow.
        PhosphorTiles::TilingAlgorithm* algo = effectiveAlgorithm(key.screenId);
        if (algo && algo->supportsLifecycleHooks()) {
            const int idx = stateIt.value()->tiledWindows().indexOf(canonical);
            if (idx >= 0) {
                algo->onWindowRemoved(stateIt.value(), idx);
            }
        }
        stateIt.value()->removeWindow(canonical);
    }
    m_windowToStateKey.remove(canonical);
}

void AutotileEngine::setWindowFloat(const QString& rawWindowId, bool shouldFloat)
{
    if (!warnIfEmptyWindowId(rawWindowId, shouldFloat ? "floatWindow" : "unfloatWindow")) {
        return;
    }
    const QString windowId = canonicalizeWindowId(rawWindowId);

    // floatWindow checks autotile screen membership; unfloatWindow does not
    // (window might be on a screen that was removed from autotile after it was floated)
    if (shouldFloat && !isAutotileScreen(m_windowToStateKey.value(windowId).screenId)) {
        return;
    }

    PhosphorTiles::TilingState* state = stateForWindow(windowId);
    if (!state) {
        qCDebug(PhosphorTileEngine::lcTileEngine)
            << (shouldFloat ? "floatWindow" : "unfloatWindow") << "- window not tracked=" << windowId;
        return;
    }

    if (state->isFloating(windowId) == shouldFloat) {
        qCDebug(PhosphorTileEngine::lcTileEngine)
            << (shouldFloat ? "floatWindow: already floating" : "unfloatWindow: not floating") << "-" << windowId;
        return;
    }

    state->setFloating(windowId, shouldFloat);
    m_overflow.clearOverflow(windowId);

    // Clear cached min-size when unfloating so the next retile starts fresh.
    // The window's minimum size may have changed while floating/minimized
    // (e.g. browser finished loading media, terminal resized). Stale min-sizes
    // can override the user's split ratio by inflating enforceMinSizes
    // constraints. The centering code in the KWin effect will re-discover and
    // report the actual min-size if the window can't fill its assigned zone.
    if (!shouldFloat) {
        const bool hadMinSize = m_windowMinSizes.contains(windowId);
        const QSize clearedMinSize = m_windowMinSizes.value(windowId, QSize(0, 0));
        m_windowMinSizes.remove(windowId);
        if (hadMinSize) {
            qCDebug(PhosphorTileEngine::lcTileEngine)
                << "unfloat: cleared stale minSize=" << clearedMinSize << "for" << windowId;
        }
    }

    const QString screenId = m_windowToStateKey.value(windowId).screenId;
    retileAfterOperation(screenId, true);

    qCInfo(PhosphorTileEngine::lcTileEngine)
        << "Window" << (shouldFloat ? "floated from" : "unfloated to") << "autotile -" << windowId;
    // Use windowFloatingStateSynced (not windowFloatingChanged): the only caller
    // of setWindowFloat is WindowTrackingAdaptor::setWindowFloatingForScreen,
    // invoked by the KWin effect for drag drops, minimize→float, and
    // unminimize→tile. None of those scenarios want the daemon to restore
    // pre-tile geometry — the effect manages drop position locally, and
    // minimize/unminimize don't show the window. Routing through
    // windowFloatingChanged would call applyGeometryForFloat and teleport
    // the window away from where the user dropped it (regression #271).
    // User float toggles (Meta+F) go through performToggleFloat, which
    // continues to emit windowFloatingChanged so geometry is restored.
    Q_EMIT windowFloatingStateSynced(windowId, shouldFloat, screenId);
}

void AutotileEngine::floatWindow(const QString& windowId)
{
    setWindowFloat(windowId, true);
}

void AutotileEngine::unfloatWindow(const QString& windowId)
{
    setWindowFloat(windowId, false);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Public window event handlers (called by Daemon via D-Bus signals)
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileEngine::windowOpened(const QString& rawWindowId, const QString& screenId, int minWidth, int minHeight)
{
    if (!warnIfEmptyWindowId(rawWindowId, "windowOpened")) {
        return;
    }
    // First observation of this window — canonicalize locks the canonical key
    // used by every internal map from here on. Subsequent arrivals with a
    // mutated appId (Electron/CEF apps) resolve back to the same string so
    // m_windowToStateKey / PhosphorTiles::TilingState / m_windowMinSizes stay consistent.
    const QString windowId = canonicalizeWindowId(rawWindowId);

    qCInfo(PhosphorTileEngine::lcTileEngine)
        << "windowOpened:" << windowId << "screen=" << screenId << "minSize=" << minWidth << "x" << minHeight;

    // Cross-engine coordination (the reciprocal of SnapEngine::resolveWindowRestore's
    // recorded-screen ownership gate). On FIRST observation, if this window carries a
    // SNAPPED placement record whose RECORDED screen is itself in snapping mode, snap
    // will restore it cross-screen to that monitor — it only landed on this autotile
    // screen because KWin's session restore placed it here. Autotile must NOT track or
    // tile it, or both engines would claim the same window (snap moves it to its snap
    // monitor while autotile tiles it here). Bail BEFORE m_windowToStateKey is set so
    // autotile leaves no trace. The peek does NOT consume the record — snap's restore
    // is the consumer. A snapped record whose recorded screen is THIS (autotile) screen
    // is not in snapping mode, so the check fails and autotile keeps the window — the
    // screen's own mode owns it, symmetric with snap's same-screen defer.
    //
    // Restricted to windows NOT already autotile-tracked: a window autotile already
    // manages (re-emitted on a runtime screen/desktop move, or explicitly handed off)
    // is autotile's — its snap slot is then frozen cross-mode memory, not a pending
    // restore. Deferring such a window would both yank a live tile and strand a ghost
    // in its current TilingState (the cross-screen cleanup below is skipped on an early
    // return). Only a first-observation open — the login/session-restore case — races
    // snap, and that is the only case this guard fires for.
    //
    // Gated on snappingPreferred() (the global snap toggle): when snapping is disabled,
    // SnapEngine::resolveWindowRestore returns early (isEnabled() false) and will NEVER
    // claim the window, so deferring here would strand it unmanaged. In that state
    // autotile keeps the window and tiles it normally.
    if (!screenId.isEmpty() && m_windowTracker && m_layoutManager && m_layoutManager->snappingPreferred()
        && !m_windowToStateKey.contains(windowId)) {
        const QString appId = currentAppIdFor(windowId);
        if (!appId.isEmpty() && appId != windowId) {
            const auto snapCrossRestorePending = [&](const PhosphorEngine::WindowPlacement& p) {
                if (p.slotFor(PhosphorEngine::WindowPlacement::snapEngineId()).state
                    != PhosphorEngine::WindowPlacement::stateSnapped()) {
                    return false;
                }
                // Resolve the recorded screen's mode in the RECORD'S OWN (desktop,
                // activity) context — the same fields snap's reciprocal gate
                // (SnapEngine::recordedSnapScreenIsSnapping) reads off the same record.
                // Keying both engines on the record (not on each engine's live current
                // desktop, which can differ under per-screen virtual-desktop overrides)
                // guarantees they reach an identical verdict, so a window is never both
                // deferred-and-claimed or both-skipped.
                const QString recScreen = p.screenId.isEmpty() ? screenId : p.screenId;
                return m_layoutManager->modeForScreen(recScreen, p.virtualDesktop, p.activity)
                    == PhosphorZones::AssignmentEntry::Mode::Snapping;
            };
            if (m_windowTracker->placementStore().peek(windowId, appId, snapCrossRestorePending).has_value()) {
                qCInfo(PhosphorTileEngine::lcTileEngine)
                    << "windowOpened:" << windowId << "on autotile screen" << screenId
                    << "defers to snap — carries a cross-screen snap restore";
                return;
            }
        }
    }

    // If the window is already tracked on a DIFFERENT screen (e.g., dragged from
    // VS2 to VS1), remove it from the old screen's PhosphorTiles::TilingState first. Without this,
    // the window remains in the old PhosphorTiles::TilingState as a ghost entry — the old screen
    // retiles around a window that's no longer there, and zone assignments stay stale.
    if (!screenId.isEmpty()) {
        const TilingStateKey newKey = currentKeyForScreen(screenId);
        auto existingIt = m_windowToStateKey.constFind(windowId);
        if (existingIt != m_windowToStateKey.constEnd() && existingIt.value() != newKey) {
            const TilingStateKey oldKey = existingIt.value();
            PhosphorTiles::TilingState* oldState = m_screenStates.value(oldKey);
            if (oldState && oldState->containsWindow(windowId)) {
                // Use the algorithm's lifecycle hook for clean removal
                // (e.g., dwindle-memory needs to update its split tree).
                PhosphorTiles::TilingAlgorithm* oldAlgo = effectiveAlgorithm(oldKey.screenId);
                if (oldAlgo && oldAlgo->supportsLifecycleHooks()) {
                    const int idx = oldState->tiledWindows().indexOf(windowId);
                    if (idx >= 0) {
                        oldAlgo->onWindowRemoved(oldState, idx);
                    }
                }
                oldState->removeWindow(windowId);
                qCInfo(PhosphorTileEngine::lcTileEngine) << "windowOpened: removed" << windowId << "from old screen"
                                                         << oldKey.screenId << "before adding to" << screenId;
                scheduleRetileForScreen(oldKey.screenId);
            }
        }
        m_windowToStateKey[windowId] = newKey;
    }

    // Store window minimum size from KWin (used by enforceMinSizes)
    if (minWidth > 0 || minHeight > 0) {
        storeWindowMinSize(windowId, minWidth, minHeight);
    }

    onWindowAdded(windowId);
}

void AutotileEngine::windowMinSizeUpdated(const QString& rawWindowId, int minWidth, int minHeight)
{
    if (!warnIfEmptyWindowId(rawWindowId, "windowMinSizeUpdated")) {
        return;
    }
    const QString windowId = canonicalizeWindowId(rawWindowId);

    qCDebug(PhosphorTileEngine::lcTileEngine)
        << "windowMinSizeUpdated:" << windowId << "minSize=" << minWidth << "x" << minHeight;

    if (!storeWindowMinSize(windowId, minWidth, minHeight)) {
        return; // No change
    }

    // Retile the screen this window is on
    const auto stateKey = m_windowToStateKey.value(windowId);
    const QString screenId = stateKey.screenId;
    if (!screenId.isEmpty() && m_screenStates.contains(stateKey)) {
        scheduleRetileForScreen(screenId);
    }
}

bool AutotileEngine::storeWindowMinSize(const QString& rawWindowId, int minWidth, int minHeight)
{
    const QString windowId = canonicalizeWindowId(rawWindowId);
    // Cap min-sizes against the screen geometry to prevent a single window's
    // min-size from overwhelming the split ratio. Without this cap, a transiently
    // inflated min-size (e.g., from a browser loading media) can dominate the
    // master/stack split and get stuck at ~90% or full width.
    const auto stateKey = m_windowToStateKey.value(windowId);
    const QString screenId = stateKey.screenId;
    if (!screenId.isEmpty()) {
        const QRect screen = screenGeometry(screenId);
        if (screen.isValid()) {
            const int maxMinW = static_cast<int>(screen.width() * PhosphorTiles::AutotileDefaults::MaxSplitRatio);
            const int maxMinH = static_cast<int>(screen.height() * PhosphorTiles::AutotileDefaults::MaxSplitRatio);
            minWidth = qMin(qMax(0, minWidth), maxMinW);
            minHeight = qMin(qMax(0, minHeight), maxMinH);
        }
    }

    const QSize newMin(qMax(0, minWidth), qMax(0, minHeight));
    const QSize oldMin = m_windowMinSizes.value(windowId, QSize(0, 0));

    if (newMin == oldMin) {
        return false; // No change
    }

    if (newMin.width() > 0 || newMin.height() > 0) {
        m_windowMinSizes[windowId] = newMin;
        qCInfo(PhosphorTileEngine::lcTileEngine)
            << "storeWindowMinSize:" << windowId << "min=" << newMin << "old=" << oldMin;
    } else {
        m_windowMinSizes.remove(windowId);
    }

    if (Q_UNLIKELY(PhosphorTileEngine::lcTileEngine().isDebugEnabled()) && !screenId.isEmpty()) {
        const QRect screen = screenGeometry(screenId);
        if (screen.isValid()) {
            qCDebug(PhosphorTileEngine::lcTileEngine)
                << "storeWindowMinSize: cap="
                << static_cast<int>(screen.width() * PhosphorTiles::AutotileDefaults::MaxSplitRatio) << "x"
                << static_cast<int>(screen.height() * PhosphorTiles::AutotileDefaults::MaxSplitRatio)
                << "screen=" << screen.size();
        }
    }
    return true;
}

void AutotileEngine::dropClosedWindowFromDragPreview(const QString& windowId)
{
    if (!m_dragInsertPreview) {
        return;
    }
    if (m_dragInsertPreview->windowId == windowId) {
        // Dragged window gone mid-preview — drop the preview entirely.
        // Cannot "restore" or "commit" a gone window; clear and move on.
        m_dragInsertPreview.reset();
    } else if (m_dragInsertPreview->evictedWindowId == windowId) {
        // Evicted neighbour gone mid-preview — forget the eviction so
        // commit/cancel don't try to operate on it.
        m_dragInsertPreview->evictedWindowId.clear();
    }
}

void AutotileEngine::windowClosed(const QString& rawWindowId)
{
    if (!warnIfEmptyWindowId(rawWindowId, "windowClosed")) {
        return;
    }
    const QString windowId = canonicalizeWindowId(rawWindowId);

    // Drag-insert preview bookkeeping: react before onWindowRemoved tears
    // down state. Leaving a stale reference would later drive setFloating or
    // windowsBatchFloated on a dead window id.
    dropClosedWindowFromDragPreview(windowId);

    m_autotileFloatedWindows.remove(windowId);
    // Min-size cleanup must not depend on tracking: a window released from
    // tracking (autotile toggle-off, orphaned VS) and later closed would hit
    // onWindowRemoved's empty-stored-key early return and keep its entry for
    // the session — a later re-entry reporting min 0x0 never clears it
    // (windowOpened only stores when minWidth/minHeight > 0), inflating
    // enforceMinSizes constraints with a stale value.
    m_windowMinSizes.remove(windowId);

    onWindowRemoved(windowId);
    // Release the canonical translation last — downstream cleanup above may
    // still need to resolve lookups keyed by this window's instance id.
    cleanupCanonical(rawWindowId);
}

void AutotileEngine::windowFocused(const QString& rawWindowId, const QString& screenId)
{
    if (!warnIfEmptyWindowId(rawWindowId, "windowFocused")) {
        return;
    }
    const QString windowId = canonicalizeWindowId(rawWindowId);

    // Detect cross-screen moves. When a window's focus moves to a different
    // screen, migrate its PhosphorTiles::TilingState membership so m_windowToStateKey and the
    // PhosphorTiles::TilingState remain consistent. This handles both overflow-floated windows
    // and windows that were previously migrated (preventing the Screen1->2->1
    // rapid-migration desync where the second hop was silently skipped).
    //
    // Only update m_windowToStateKey for windows already tracked via windowOpened().
    // The KWin effect sends focus events for ALL handleable windows (including
    // transients and non-tileable windows that pass shouldHandleWindow but fail
    // isTileableWindow). Creating entries for these phantom windows causes
    // backfillWindows() to insert them on algorithm switches, inflating the
    // tiled window count.
    const auto trackedIt = m_windowToStateKey.constFind(windowId);
    const bool tracked = trackedIt != m_windowToStateKey.constEnd();
    const TilingStateKey oldKey = tracked ? trackedIt.value() : TilingStateKey{};
    const QString oldScreen = oldKey.screenId;
    if (!screenId.isEmpty() && tracked) {
        if (oldKey.screenId == screenId) {
            // SAME SCREEN: the window has NOT moved monitors, so any key delta
            // is a context-only delta — the current desktop/activity changed
            // underneath the window, not the window's location. This fires when
            // a focus/activation event for the previously-active window lands
            // during a desktop switch: KWin re-activates the last-focused window
            // (e.g. the active window when the user left the desktop), and that
            // focus arrives with the daemon's current desktop already advanced.
            //
            // isAutotileScreen() is evaluated against the CURRENT desktop, so it
            // reads FALSE here whenever the window's own desktop differs from the
            // one just switched to — even though the screen IS autotile on the
            // window's real desktop. The old code took that false reading as
            // "window moved to a non-autotile screen" and removed it from its
            // (still-live, off-desktop) TilingState, silently dropping the window
            // from that desktop's tiling so the survivors reflowed on return.
            //
            // Never migrate or remove on a same-screen focus. Defer one event
            // loop pass: revalidateWindowContext migrates ONLY if a real
            // desktop/activity move persists after the in-flight context push
            // settles, and leaves the window untouched in its owning state when
            // the screen is still non-autotile then (the window genuinely
            // belongs to another desktop — windowDesktopsChanged owns real
            // desktop moves). The map is left untouched here so the catch-scan's
            // windowOpened ghost-removal still detects a genuine move meanwhile.
            const bool alreadyCorrect = isAutotileScreen(screenId) && currentKeyForScreen(screenId) == oldKey;
            if (!alreadyCorrect) {
                QMetaObject::invokeMethod(
                    this,
                    [this, windowId, screenId]() {
                        revalidateWindowContext(windowId, screenId);
                    },
                    Qt::QueuedConnection);
            }
        } else if (isAutotileScreen(screenId)) {
            // Genuine cross-screen move to an autotile screen: migrate now.
            m_windowToStateKey[windowId] = currentKeyForScreen(screenId);
            migrateWindowBetweenKeys(windowId, oldKey, screenId);
        } else {
            // Genuine cross-screen move to a non-autotile screen — remove
            // tracking entirely. Leaving a stale entry pointing at a snap screen
            // causes phantom lookups and prevents clean re-entry if the window
            // returns. Drop the per-window caches too: removeWindow()/
            // windowClosed() clear these on their paths, and a lingering
            // autotile-floated marker would keep feeding the daemon's mode-flip
            // logic while a stored min-size would survive a later re-entry stale.
            m_windowToStateKey.remove(windowId);
            m_windowMinSizes.remove(windowId);
            m_autotileFloatedWindows.remove(windowId);
            if (!oldScreen.isEmpty()) {
                migrateWindowBetweenKeys(windowId, oldKey, screenId);
            }
        }
    }

    onWindowFocused(windowId);
}

void AutotileEngine::releaseScreenStateForTeardown(const QString& screenId, PhosphorTiles::TilingState* state,
                                                   QStringList& releasedWindows, bool drainOverflow)
{
    // Snapshot each window's autotile slot into the unified record BEFORE the
    // PhosphorTiles::TilingState is torn down — the record is the SINGLE
    // source of truth for cross-mode state (no parallel saved-floating set).
    // capturePlacement records a USER float as floating and a tiled/overflow
    // window as tiled (so it re-tiles on re-entry); the shared free geometry
    // rides from the engine's own float-back cache.
    const QStringList tiled = state->tiledWindows();
    const QStringList floated = state->floatingWindows();
    if (m_windowTracker) {
        // Two passes instead of `floated + tiled` — this runs once per
        // context in the orphaned-VS teardown loop and the concatenation
        // would allocate a temporary list each time.
        const auto captureAll = [this](const QStringList& wids) {
            for (const QString& wid : wids) {
                auto rec = capturePlacement(wid);
                if (!rec) {
                    continue;
                }
                m_windowTracker->placementStore().record(*rec);
            }
        };
        captureAll(floated);
        captureAll(tiled);
    }
    // Drop the overflow set AFTER capture: capturePlacement's
    // overflow-vs-user-float discriminator (isOverflow) must still see this
    // screen's overflow windows, or they'd be mis-recorded as user floats and
    // stick floating instead of re-tiling on re-entry. Callers tearing down
    // SEVERAL states for the same screenId (the orphaned-VS loop spans every
    // desktop/activity context) pass drainOverflow=false and drain once per
    // screen AFTER all captures — the overflow bucket is keyed per screenId
    // only, so an in-helper drain on the first state would blind the
    // discriminator for every later state of the same screen.
    if (drainOverflow) {
        m_overflow.takeForScreen(screenId);
    }
    releasedWindows.append(tiled);
    releasedWindows.append(floated);
    m_pendingInitialOrders.remove(screenId);
    m_pendingOrderGeneration.remove(screenId);
    m_strictInitialOrderScreens.remove(screenId);
    state->deleteLater();
}

void AutotileEngine::migrateWindowBetweenKeys(const QString& windowId, const TilingStateKey& oldKey,
                                              const QString& newScreenId)
{
    PhosphorTiles::TilingState* oldState = m_screenStates.value(oldKey);
    if (!oldState || !oldState->containsWindow(windowId)) {
        return;
    }
    // Use the algorithm's lifecycle hook for clean removal (e.g.
    // dwindle-memory updates its split tree) — mirrors windowOpened's
    // migration path.
    PhosphorTiles::TilingAlgorithm* oldAlgo = effectiveAlgorithm(oldKey.screenId);
    if (oldAlgo && oldAlgo->supportsLifecycleHooks()) {
        const int idx = oldState->tiledWindows().indexOf(windowId);
        if (idx >= 0) {
            oldAlgo->onWindowRemoved(oldState, idx);
        }
    }
    oldState->removeWindow(windowId);
    m_overflow.migrateWindow(windowId);
    qCInfo(PhosphorTileEngine::lcTileEngine)
        << "Window" << windowId << "moved from" << oldKey.screenId << "to" << newScreenId << "- migrating";
    // Close the hole the departing window left on the SOURCE screen — the
    // destination's own insert schedules a retile there, but nothing else
    // retiles the source (mirrors windowOpened's migration path).
    scheduleRetileForScreen(oldKey.screenId);
    if (isAutotileScreen(newScreenId)) {
        // Re-add to the new screen's normal flow (will be overflow-checked
        // on next retile).
        onWindowAdded(windowId);
    }
    // Re-adding on a non-autotile destination would route through
    // screenForWindow()'s primary-screen fallback and re-tile a window that
    // just left autotile — the cross-engine misroute class the tracking
    // removal exists to prevent.
}

void AutotileEngine::revalidateWindowContext(const QString& windowId, const QString& screenId)
{
    // Deferred half of windowFocused's context-only key-delta handling. By
    // now any context push that was in flight when the focus event arrived
    // has been processed, so a persisting mismatch means the window REALLY
    // moved desktop/activity (the catch-scan race the full-key migration
    // exists for), not that the focus outran the push.
    auto it = m_windowToStateKey.constFind(windowId);
    if (it == m_windowToStateKey.constEnd() || !isAutotileScreen(screenId)) {
        return; // closed / untracked / screen left autotile meanwhile
    }
    const TilingStateKey oldKey = it.value();
    if (oldKey.screenId != screenId) {
        return; // a genuine cross-screen event superseded this re-check
    }
    const TilingStateKey newKey = currentKeyForScreen(screenId);
    if (newKey == oldKey) {
        return; // the context push arrived — nothing actually moved
    }
    m_windowToStateKey[windowId] = newKey;
    migrateWindowBetweenKeys(windowId, oldKey, screenId);
    // Re-record focus on the DESTINATION state: the original focus event
    // ran onWindowFocused against the old key, and the migration's
    // removeWindow just cleared that marker — without this, the window the
    // user is actively focused on stays unmarked in its owning state until
    // the next activation (mirrors the pre-deferral ordering, harmless
    // no-op when nothing relies on it).
    onWindowFocused(windowId);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Private slot event handlers
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileEngine::onWindowAdded(const QString& windowId)
{
    const QString screenId = screenForWindow(windowId);
    if (!isAutotileScreen(screenId) || !shouldTileWindow(windowId)) {
        qCDebug(PhosphorTileEngine::lcTileEngine) << "onWindowAdded: skipping" << windowId << "screen=" << screenId
                                                  << "isAutotile=" << isAutotileScreen(screenId);
        return;
    }

    PhosphorTiles::TilingState* state = tilingStateForScreen(screenId);
    const int maxWin = effectiveMaxWindows(screenId);
    // A window matched by a "Float this app" rule must bypass the tiled-window
    // cap: it opens floating and so consumes no tile slot (tiledWindowCount
    // excludes floats), and insertWindow marks it floating once inserted. Dropping
    // it here would leave it untracked — neither floating in autotile (so the
    // IsFloating match field stays false) nor re-tileable via Meta+F.
    const bool ruleWillFloat = m_floatPredicate && m_floatPredicate(windowId);
    if (state && state->tiledWindowCount() >= maxWin && !ruleWillFloat) {
        qCDebug(PhosphorTileEngine::lcTileEngine)
            << "Max window limit reached for screen" << screenId << "(max=" << maxWin << ")";
        // Purge this window from pending initial orders so the order doesn't
        // leak waiting for a window that will never be inserted.
        for (auto pit = m_pendingInitialOrders.begin(); pit != m_pendingInitialOrders.end(); ++pit) {
            pit.value().removeAll(windowId);
        }
        return;
    }

    const bool inserted = insertWindow(windowId, screenId);

    if (inserted) {
        emitInsertFloatStateSync(windowId, screenId);
    }

    if (inserted && m_config && m_config->focusNewWindows) {
        // Defer focus until after applyTiling emits windowsTiled. The KWin effect's
        // onComplete raises windows in tiling order; emitting focus before retile
        // causes the raise loop to bury the new window behind existing ones.
        m_pendingFocusWindowId = windowId;
    }

    if (inserted) {
        // Notify algorithm via lifecycle hook before retile
        PhosphorTiles::TilingAlgorithm* algo = effectiveAlgorithm(screenId);
        if (algo && algo->supportsLifecycleHooks() && state) {
            const int idx = state->tiledWindows().indexOf(windowId);
            if (idx >= 0) {
                algo->onWindowAdded(state, idx);
            }
        }
        scheduleRetileForScreen(screenId);
    }
}

QString AutotileEngine::removeTrackedWindowNoRetile(const QString& windowId)
{
    const QString screenId = m_windowToStateKey.value(windowId).screenId;
    if (screenId.isEmpty()) {
        return {};
    }

    // Notify algorithm via lifecycle hook before removal. Resolve the state
    // through the window's STORED key (mirrors handoffRelease /
    // migrateWindowBetweenKeys), not tilingStateForScreen(): the latter keys
    // on the CURRENT desktop/activity — for a window owned by another
    // context's state it would miss the hook on the owning state AND lazily
    // create a spurious empty TilingState for the current context.
    PhosphorTiles::TilingState* state = m_screenStates.value(m_windowToStateKey.value(windowId));
    PhosphorTiles::TilingAlgorithm* algo = effectiveAlgorithm(screenId);
    if (algo && algo->supportsLifecycleHooks() && state) {
        const int idx = state->tiledWindows().indexOf(windowId);
        if (idx >= 0) {
            algo->onWindowRemoved(state, idx);
        } else {
            qCDebug(PhosphorTileEngine::lcTileEngine)
                << "removeTrackedWindow: window" << windowId << "not found in tiling state — lifecycle hook skipped";
        }
    }

    removeWindow(windowId);
    return screenId;
}

void AutotileEngine::onWindowRemoved(const QString& windowId)
{
    const QString screenId = removeTrackedWindowNoRetile(windowId);
    if (screenId.isEmpty()) {
        return;
    }
    qCInfo(PhosphorTileEngine::lcTileEngine) << "onWindowRemoved:" << windowId << "screen=" << screenId;
    // Retile immediately (not deferred like onWindowAdded). Removals need instant
    // layout recalculation to avoid visible holes. Unlike additions, removals don't
    // arrive in bursts, so coalescing provides no benefit. (The batch prune path
    // in pruneStaleWindows is the exception — it retiles each affected screen once.)
    retileAfterOperation(screenId, true);
}

void AutotileEngine::onWindowFocused(const QString& windowId)
{
    PhosphorTiles::TilingState* state = stateForWindow(windowId);
    if (!state) {
        // Not an error — non-autotiled windows (dialogs, floating, etc.) report
        // focus changes too, so this is the normal case for most window activations
        qCDebug(PhosphorTileEngine::lcTileEngine) << "onWindowFocused: window not tracked" << windowId;
        return;
    }

    // Track which screen has the active focus (used by tiledWindowsForFocusedScreen
    // to avoid non-deterministic QHash iteration when multiple screens have focused windows)
    m_activeScreen = m_windowToStateKey.value(windowId).screenId;

    state->setFocusedWindow(windowId);
}

void AutotileEngine::onWindowResized(const QString& rawWindowId, const QRect& oldFrame, const QRect& newFrame,
                                     const QString& screenId)
{
    if (rawWindowId.isEmpty() || !oldFrame.isValid() || !newFrame.isValid()) {
        return;
    }

    // Resolve to the canonical instance id that keys m_windowToStateKey, the
    // TilingState, and the SplitTree. The daemon calls this public boundary with
    // the raw id (like every other IPlacementEngine override here); without this
    // a window whose app class was renamed mid-session would pass the adaptor's
    // canonicalizing screenForTrackedWindow guard but then miss every lookup
    // below and silently drop the reflow. Lookup-only (no canonical-key mutation)
    // since a resize is not a window-registration point.
    const QString windowId = canonicalizeForLookup(rawWindowId);

    // The daemon resolved screenId from the same window→state map, so treat it as
    // authoritative. Resolve the owning state with a pure lookup: stateForWindow
    // never creates state, whereas tilingStateForScreen would insert an empty
    // TilingState for a known-but-stateless screen that then just fails the guards
    // below. stateForWindow returns the state stored for the window's
    // TilingStateKey; the ownerScreen != resolvedScreen check below then enforces
    // that the state's screen agrees with the daemon-supplied one.
    const QString resolvedScreen = screenId;
    if (resolvedScreen.isEmpty() || !isAutotileScreen(resolvedScreen)) {
        return;
    }
    QString ownerScreen;
    PhosphorTiles::TilingState* state = stateForWindow(windowId, &ownerScreen);
    if (!state || ownerScreen != resolvedScreen) {
        return;
    }

    // Floating windows are not part of the tiling — they never reflow neighbours.
    if (state->isFloating(windowId)) {
        return;
    }

    // Need at least two tiled windows for a neighbour to absorb the resize.
    if (state->tiledWindowCount() < 2) {
        return;
    }

    // Cross-output guard: if the resize carried the window's centre off its
    // screen, this is a monitor handoff — let windowScreenChanged own the
    // reassignment rather than reflowing a layout the window is leaving. Use the
    // full monitor rect, not the strut-inset work area, so a window whose centre
    // legitimately lands under a panel is not misread as having left the screen.
    // Virtual screens are region-bounded sub-rects, so keep the virtual-aware
    // available-geometry resolver for them.
    QRect screen;
    if (m_screenManager && !PhosphorIdentity::VirtualScreenId::isVirtual(resolvedScreen)) {
        screen = m_screenManager->screenGeometry(resolvedScreen);
    }
    if (!screen.isValid()) {
        screen = screenGeometry(resolvedScreen);
    }
    if (screen.isValid() && !screen.contains(newFrame.center())) {
        return;
    }

    PhosphorTiles::TilingAlgorithm* algo = effectiveAlgorithm(resolvedScreen);
    if (!algo) {
        return;
    }

    // Tier A — tree/memory algorithms reflow gap-free by adjusting the split
    // ratio of the ancestor split that owns each moved edge.
    if (algo->supportsMemory()) {
        if (applyTreeResizeReflow(state, windowId, oldFrame, newFrame, resolvedScreen)) {
            retileAfterOperation(resolvedScreen, true);
        }
        return;
    }

    // Tier B — a non-tree algorithm that opts into the resize hook records the
    // adjustment (typically into TilingState::scriptState) before we retile; the
    // follow-up retile then lays the windows out honouring it. Algorithms without
    // the hook have no reflow model and leave the user's manual geometry as-is.
    if (algo->supportsResizeHook()) {
        const int threshold = PhosphorTiles::AutotileDefaults::ResizeEdgeMoveThresholdPx;
        const bool leftMoved = std::abs(newFrame.x() - oldFrame.x()) > threshold;
        const bool rightMoved =
            std::abs((newFrame.x() + newFrame.width()) - (oldFrame.x() + oldFrame.width())) > threshold;
        const bool topMoved = std::abs(newFrame.y() - oldFrame.y()) > threshold;
        const bool bottomMoved =
            std::abs((newFrame.y() + newFrame.height()) - (oldFrame.y() + oldFrame.height())) > threshold;
        PhosphorTiles::ResizeEvent ev;
        ev.index = state->tiledWindows().indexOf(windowId);
        // Defensive backstop: the window cleared the floating and tracked guards
        // above, so under both current overflow modes it is present in
        // tiledWindows() (Float floats over-cap windows — they return at the
        // floating guard — and Unlimited has no cap), meaning indexOf normally
        // succeeds. Guard the result anyway so a future overflow mode that keeps
        // a non-floating window out of the tiled list can never hand a -1 index
        // to the script hook.
        if (ev.index < 0) {
            return;
        }
        ev.oldRect = oldFrame;
        ev.newRect = newFrame;
        // Report at most one edge per axis. When both edges of an axis moved
        // together that axis translated (a move, not a resize), so neither edge
        // is reported — mirroring applyTreeResizeReflow and the per-axis
        // mutual-exclusion the ResizeEvent contract guarantees to scripts.
        ev.left = leftMoved && !rightMoved;
        ev.right = rightMoved && !leftMoved;
        ev.top = topMoved && !bottomMoved;
        ev.bottom = bottomMoved && !topMoved;
        if (ev.left || ev.right || ev.top || ev.bottom) {
            // The hook may apply a new split ratio to the state (ratio-based
            // algorithms reflow this way). If it did, mark the state user-tuned so
            // the change stays local to this screen+desktop and survives a settings
            // refresh — exactly like an interactive master-ratio keystroke.
            const qreal ratioBefore = state->splitRatio();
            algo->onWindowResized(state, ev);
            if (!qFuzzyCompare(1.0 + state->splitRatio(), 1.0 + ratioBefore)) {
                noteSplitRatioUserTuned(resolvedScreen);
            }
            retileAfterOperation(resolvedScreen, true);
        }
    }
}

bool AutotileEngine::applyTreeResizeReflow(PhosphorTiles::TilingState* state, const QString& windowId,
                                           const QRect& oldFrame, const QRect& newFrame, const QString& screenId)
{
    using Edge = PhosphorTiles::SplitTree::Edge;

    PhosphorTiles::SplitTree* tree = state->splitTree();
    if (!tree || tree->leafCount() < 2 || !tree->leafForWindow(windowId)) {
        return false;
    }

    const QStringList tiled = state->tiledWindows();
    const QVector<QRect> zones = state->calculatedZones();
    // The reflow reads split extents from the rendered zones, so they must be in
    // lockstep with the tiled-window list. The divergence is transient: while a
    // capped layout (recalculateLayout sizes calculatedZones to
    // min(tiledCount, maxWindows)) is being applied, applyTiling has not yet
    // floated the over-cap windows out of tiledWindows(), so the lists briefly
    // differ in length. In steady state they match again under both overflow
    // modes (Float floats over-cap windows out of tiledWindows(); Unlimited
    // never caps). Bail rather than read a stale/short vector — resizing during
    // that transient is a no-op, which is fine.
    if (zones.isEmpty() || zones.size() != tiled.size()) {
        return false;
    }

    const int innerGap = effectiveInnerGap(screenId);
    const int threshold = PhosphorTiles::AutotileDefaults::ResizeEdgeMoveThresholdPx;

    // Identify which edge(s) moved. A resize moves at most one edge per axis; a
    // corner moves one on each axis. If both edges of an axis shifted together
    // that axis describes a translation (move), not a resize — skip it.
    struct EdgeMove
    {
        Edge edge;
        int newPos;
    };
    QVarLengthArray<EdgeMove, 2> moves;
    const int oldL = oldFrame.x();
    const int newL = newFrame.x();
    const int oldR = oldFrame.x() + oldFrame.width();
    const int newR = newFrame.x() + newFrame.width();
    const int oldT = oldFrame.y();
    const int newT = newFrame.y();
    const int oldB = oldFrame.y() + oldFrame.height();
    const int newB = newFrame.y() + newFrame.height();
    const bool leftMoved = std::abs(newL - oldL) > threshold;
    const bool rightMoved = std::abs(newR - oldR) > threshold;
    const bool topMoved = std::abs(newT - oldT) > threshold;
    const bool bottomMoved = std::abs(newB - oldB) > threshold;
    if (leftMoved != rightMoved) {
        moves.push_back(rightMoved ? EdgeMove{Edge::Right, newR} : EdgeMove{Edge::Left, newL});
    }
    if (topMoved != bottomMoved) {
        moves.push_back(bottomMoved ? EdgeMove{Edge::Bottom, newB} : EdgeMove{Edge::Top, newT});
    }
    if (moves.isEmpty()) {
        return false;
    }

    for (const EdgeMove& move : moves) {
        PhosphorTiles::SplitNode* split = tree->splitOwningEdge(windowId, move.edge);
        if (!split) {
            continue; // edge coincides with a screen boundary — nothing to resize
        }

        const QRect splitRect = subtreeBoundingRect(split, tiled, zones);
        if (!splitRect.isValid()) {
            continue;
        }

        const bool alongY = (move.edge == Edge::Top || move.edge == Edge::Bottom);
        const int axisStart = alongY ? splitRect.y() : splitRect.x();
        const int content = (alongY ? splitRect.height() : splitRect.width()) - innerGap;
        if (content <= 0) {
            continue;
        }

        // firstSize is the first child's extent up to the moved boundary. A
        // Right/Bottom edge belongs to a first-child window and sits on the
        // split line (firstSize = pos - start). A Left/Top edge belongs to a
        // second-child window and sits one gap past it (firstSize = pos - start - gap).
        const bool secondSide = (move.edge == Edge::Left || move.edge == Edge::Top);
        const int firstSize = secondSide ? (move.newPos - axisStart - innerGap) : (move.newPos - axisStart);
        const qreal ratio = static_cast<qreal>(firstSize) / static_cast<qreal>(content);

        tree->resizeSplitNode(split, ratio); // clamps to [MinSplitRatio, MaxSplitRatio]
    }

    // At least one edge moved past the threshold (moves is non-empty, checked
    // above), so the compositor has already committed an out-of-tile geometry
    // for the dragged window. Always retile to re-snap it onto its zone — even
    // when no split ratio actually changed because the edge was a screen
    // boundary or was already pinned at Min/MaxSplitRatio. Without this the
    // window would be stranded at its dragged size until the next incidental
    // retile.
    return true;
}

void AutotileEngine::onScreenGeometryChanged(const QString& screenId)
{
    if (!isAutotileScreen(screenId) || !m_screenStates.contains(currentKeyForScreen(screenId))) {
        return;
    }

    qCInfo(PhosphorTileEngine::lcTileEngine)
        << "onScreenGeometryChanged:" << screenId << "geometry=" << screenGeometry(screenId);

    // Min-sizes are NOT cleared here. Stored min-sizes represent the window's
    // actual compositor-declared minimum (from windowOpened or the centering
    // code's reportDiscoveredMinSize), not stale zone widths. Clearing them
    // forces a retile with zero constraints, which produces zones that oversized
    // windows can't fill — the centering code then pushes them off-screen.
    // The old feedback loop (zone width → stored min → expanded zone) was
    // eliminated by removing the targetZone.width() fallback in
    // reportDiscoveredMinSize (commit c1d0ea16). Without that feedback loop,
    // indiscriminate clearing does more harm than good.

    retileAfterOperation(screenId, true);
}

void AutotileEngine::onLayoutChanged(PhosphorZones::Layout* layout)
{
    Q_UNUSED(layout)
    // Autotile screens are managed by per-screen assignments, not the global
    // active layout. Retile is triggered by setAutotileScreens() and
    // onScreenGeometryChanged() instead.
}

// ═══════════════════════════════════════════════════════════════════════════════
// Internal implementation
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileEngine::emitInsertFloatStateSync(const QString& windowId, const QString& screenId)
{
    // Read-only lookup — must NOT lazily materialize a state. tilingStateForScreen
    // would create one for a known-but-stateless screen; this method only reads
    // isFloating right after a successful insertWindow, so the state already exists.
    PhosphorTiles::TilingState* state = m_screenStates.value(currentKeyForScreen(screenId));
    if (!state) {
        return;
    }
    // Sync floating state to daemon. Float state is per-mode:
    // - Restored as floating from autotile's saved set → notify daemon to set WTS floating
    // - Inserted as tiled but WTS says floating (stale snap-mode float) → clear WTS floating
    //
    // Use windowFloatingStateSynced (not windowFloatingChanged): this is a
    // passive state-sync on window insertion, not a user float toggle. The
    // daemon must NOT restore pre-tile geometry here — the window was just
    // added (e.g. dropped onto an autotile VS from a snap VS) and already
    // has a valid position. Routing through windowFloatingChanged causes
    // syncAutotileFloatState to call applyGeometryForFloat, which teleports
    // the window to a cross-screen-adjusted rect and resizes it.
    if (state->isFloating(windowId)) {
        Q_EMIT windowFloatingStateSynced(windowId, true, screenId);
    } else if (m_windowTracker && m_windowTracker->isWindowFloating(windowId)) {
        Q_EMIT windowFloatingStateSynced(windowId, false, screenId);
    }
}

bool AutotileEngine::insertWindow(const QString& windowId, const QString& screenId)
{
    PhosphorTiles::TilingState* state = tilingStateForScreen(screenId);
    if (!state) {
        qCWarning(PhosphorTileEngine::lcTileEngine)
            << "AutotileEngine::insertWindow: failed to get state for screen" << screenId;
        return false;
    }

    // Check if window already tracked in this screen's tiling state
    // Note: We check the PhosphorTiles::TilingState (not m_windowToStateKey) because windowOpened()
    // stores the screen mapping in m_windowToStateKey *before* calling onWindowAdded(),
    // so m_windowToStateKey.contains() would always be true via that path.
    if (state->containsWindow(windowId)) {
        return false;
    }

    // Resolve appId via the registry so mid-session class mutations (Emby and
    // friends) land in the correct restore bucket. Falls back to parsing the
    // canonical windowId when no registry is attached (unit tests).
    const QString appId = currentAppIdFor(windowId);
    const bool hasStableAppId = !appId.isEmpty() && (appId != windowId);

    // Check if this window has a pre-seeded position from zone-ordered transition.
    // Take a value copy of the pending list — the erase below invalidates iterators/refs.
    bool inserted = false;
    auto pendingIt = m_pendingInitialOrders.find(screenId);
    if (pendingIt != m_pendingInitialOrders.end()) {
        const QStringList pendingOrder = pendingIt.value(); // copy, not reference (BUG-1 fix)
        int desiredPos = pendingOrder.indexOf(windowId);
        // An exact windowId match means KWin held this window across the
        // daemon's lifetime gap (i.e. only the daemon reloaded; the window's
        // compositor-assigned identity is unchanged). The saved position is
        // therefore an authoritative restoration target, not yesterday's
        // historical hint — treat it as strict below so daemon-reload bursts
        // restore the prior layout even when arrivals are out of sequence.
        const bool exactWindowIdMatch = (desiredPos >= 0);

        // Fallback: match by appId when exact windowId not found (KWin restart
        // changes UUIDs, so saved windowIds have stale suffixes). FIFO consumption
        // prevents multi-instance apps from all matching the first entry.
        if (desiredPos < 0 && hasStableAppId) {
            for (int i = 0; i < pendingOrder.size(); ++i) {
                // Compare using currentAppIdFor so both sides resolve to the
                // latest class — an entry saved before a rename still matches.
                if (currentAppIdFor(pendingOrder.at(i)) == appId && !state->containsWindow(pendingOrder.at(i))) {
                    desiredPos = i;
                    // Replace stale UUID in the live map so it won't match again
                    m_pendingInitialOrders[screenId][i] = windowId;
                    qCDebug(PhosphorTileEngine::lcTileEngine)
                        << "AppId fallback matched" << windowId << "to pending position" << i;
                    break;
                }
            }
        }

        if (desiredPos >= 0) {
            // Count ALL pre-seeded windows (including floating) with lower desired position
            // already in state. addWindow() inserts into m_windowOrder which includes both
            // tiled and floating windows, so the offset must account for all of them.
            int insertAt = 0;
            for (int i = 0; i < desiredPos; ++i) {
                const QString& earlier = pendingOrder.at(i);
                if (state->containsWindow(earlier)) {
                    ++insertAt;
                }
            }
            // Strict ordering applies in two cases:
            //   1. Mode transition via setInitialWindowOrder — the daemon
            //      pre-computed an order from the prior mode's zones and
            //      intentionally wants it preserved.
            //   2. Cross-session restore where the arriving windowId matches
            //      the saved entry exactly. Exact match means KWin retained
            //      the window across the gap (i.e. only the daemon
            //      reloaded), so the saved position is a real restoration
            //      target — pushing live entries to honor it just rebuilds
            //      yesterday's layout, which is the user's intent on a
            //      daemon reload.
            //
            // Advisory ordering applies to cross-session arrivals matched by
            // appId fallback (UUID drift after a KWin restart, or a new
            // window today that happens to share an app class with a saved
            // entry). For those, the saved position is yesterday's hint
            // rather than today's reality — honor it only when it appends
            // at the current tail. If it would push existing windows, fall
            // through to insertPosition so the user's "After existing" /
            // "After focused" / "As main window" setting wins for new
            // arrivals.
            const bool strict = m_strictInitialOrderScreens.contains(screenId) || exactWindowIdMatch;
            if (strict || insertAt >= state->windowCount()) {
                state->addWindow(windowId, insertAt);
                inserted = true;
                qCDebug(PhosphorTileEngine::lcTileEngine)
                    << "Inserted pre-seeded window" << windowId << "at position=" << insertAt
                    << "desired=" << desiredPos << (strict ? "(strict)" : "(advisory)");
            } else {
                qCDebug(PhosphorTileEngine::lcTileEngine)
                    << "Advisory pre-seeded window" << windowId << "at desired=" << desiredPos
                    << "would push existing windows (insertAt=" << insertAt << " < windowCount=" << state->windowCount()
                    << ") — falling back to insertPosition";
            }
        }
        // Clean up pending order when all pre-seeded windows have been inserted (or closed)
        if (inserted) {
            cleanupPendingOrderIfResolved(screenId);
        }
    }

    // Close/reopen restore takes precedence over the insert-position config: a
    // window closed while FLOATING reopens at its floated geometry, STILL
    // FLOATING — never inserted into the tile layout (marked floating in
    // TilingState; onWindowAdded then emits windowFloatingStateSynced → daemon
    // passive float-sync, NO geometry teleport). inserted=true → the tile-insert
    // paths below are all skipped; the function tail still records
    // m_windowToStateKey and returns true.
    // Close/reopen restore from the unified placement store: ONE record per window
    // holds both engines' slots + the shared per-screen free geometry. Take it once
    // and branch on the autotile slot — a FLOATING slot restores the window floating
    // (consumed only when the record's screen matches the opening screen or is
    // empty; the GEOMETRY move below uses ONLY the screen-local recorded rect for
    // restoreScreen — no cross-screen fallback); a TILED slot restores it at its saved
    // order in the SAME context (index-based — best-effort if neighbours moved;
    // wasFloating is not relevant since the slot state IS the intent). Re-record
    // bound to the live windowId so the snap slot + per-screen free geometry survive
    // and a second instance of the same app takes the next FIFO entry.
    const TilingStateKey currentKey = currentKeyForScreen(screenId);
    if (!inserted && hasStableAppId && m_windowTracker) {
        using PhosphorEngine::WindowPlacement;
        auto rec = m_windowTracker->placementStore().take(windowId, appId, [&](const WindowPlacement& p) {
            const PhosphorEngine::EngineSlot s = p.slotFor(engineId());
            if (s.state == WindowPlacement::stateFloating()) {
                return p.screenId.isEmpty() || p.screenId == screenId;
            }
            if (s.state == WindowPlacement::stateTiled()) {
                return p.screenId == currentKey.screenId && p.virtualDesktop == currentKey.desktop
                    && p.activity == currentKey.activity;
            }
            return false;
        });
        if (rec) {
            // Re-record bound to the LIVE windowId so the autotile slot + per-screen
            // free/float geometry survive the reopen. KWin assigns a NEW uuid at
            // logout/login, so the record matches by appId FIFO (not uuid-exact);
            // without re-binding, a FIFO reopen consumes the record and the window
            // loses its remembered float-back. Re-binding appends under the live uuid
            // (newest in the appId bucket), so a SECOND instance of the same app still
            // takes an OLDER sibling record first on its own reopen — multi-instance
            // FIFO distribution is preserved.
            const PhosphorEngine::EngineSlot slot = rec->slotFor(engineId());
            const QString restoreScreen = rec->screenId.isEmpty() ? screenId : rec->screenId;
            rec->windowId = windowId;
            m_windowTracker->placementStore().record(*rec);
            if (slot.state == WindowPlacement::stateFloating()) {
                state->addWindow(windowId);
                state->setFloating(windowId, true);
                inserted = true;
                // SCREEN-LOCAL recorded position only — deliberately NOT the
                // anyFreeGeometry() cross-screen fallback (mirroring snap's
                // resolveWindowRestore). The free geometry is in global compositor
                // coordinates; applying a rect captured on a DIFFERENT screen while
                // the float tracking points at restoreScreen would teleport the
                // window to a third monitor with the state saying otherwise — a
                // visible/state desync. No recorded rect for restoreScreen → nothing
                // meaningful to restore, so the move is skipped.
                const QRect freeGeo = rec->freeGeometryFor(restoreScreen);
                // The window is marked floating unconditionally above; the geometry
                // MOVE is gated on the floated-position-restore opt-in (daemon-wired
                // autotileRestoreFloatedWindowsOnLogin setting + per-window
                // RestorePosition rule). When the predicate is unset (tests / no
                // daemon) the move always fires, preserving historical behaviour.
                const bool restorePosition = !m_restorePositionPredicate || m_restorePositionPredicate(windowId);
                if (freeGeo.isValid() && restorePosition) {
                    Q_EMIT geometryRestoreRequested(windowId, freeGeo, restoreScreen);
                }
                qCInfo(PhosphorTileEngine::lcTileEngine)
                    << "insertWindow: float-restore for" << windowId << "to" << freeGeo << "on" << restoreScreen
                    << "move=" << (freeGeo.isValid() && restorePosition);
            } else {
                const int savedPos = slot.order;
                const int clampedPos = savedPos < 0 ? state->windowCount() : qMin(savedPos, state->windowCount());
                state->addWindow(windowId, clampedPos);
                inserted = true;
                qCDebug(PhosphorTileEngine::lcTileEngine)
                    << "insertWindow: restored" << windowId << "from placement store at position=" << clampedPos
                    << "(saved=" << savedPos << ")";
            }
        }
    }

    if (!inserted) {
        // Insert based on config preference
        insertWindowByConfigOrder(state, windowId);
    }

    // Float restore is handled entirely by the record take() above (a floating
    // autotile slot inserts floating + applies the shared free geometry). No
    // parallel saved-floating set — the WindowPlacement record is the single
    // source of truth for cross-mode float state.

    // A matched "Float this app" window rule opens the window floating: it is
    // inserted above (so it stays managed and Meta+F can re-tile it), then marked
    // floating here, identical to a manual float toggle. Guarded on not-already-
    // floating so the placement-record float-restore branch above is not
    // re-applied. onWindowAdded then emits windowFloatingStateSynced so the daemon
    // mirrors the state.
    if (m_floatPredicate && !state->isFloating(windowId) && m_floatPredicate(windowId)) {
        state->setFloating(windowId, true);
    }

    m_windowToStateKey.insert(windowId, currentKey);
    return true;
}

void AutotileEngine::insertWindowByConfigOrder(PhosphorTiles::TilingState* state, const QString& windowId)
{
    switch (m_config->insertPosition) {
    case AutotileConfig::InsertPosition::End:
        state->addWindow(windowId);
        break;
    case AutotileConfig::InsertPosition::AfterFocused:
        state->insertAfterFocused(windowId);
        break;
    case AutotileConfig::InsertPosition::AsMaster:
        state->addWindow(windowId);
        state->moveToFront(windowId);
        break;
    }
}

void AutotileEngine::removeWindow(const QString& windowId)
{
    m_windowMinSizes.remove(windowId);
    m_overflow.clearOverflow(windowId);
    const TilingStateKey key = m_windowToStateKey.take(windowId);
    if (key.screenId.isEmpty()) {
        return;
    }

    PhosphorTiles::TilingState* state = m_screenStates.value(key);
    if (state) {
        // No position is saved here. The window's autotiled placement (its position)
        // is captured into the unified WindowPlacementStore by the common close hook
        // (WindowTrackingAdaptor::windowClosed → capturePlacement) BEFORE this
        // removal runs, and by the save-time snapshot for still-open windows. The
        // reopen consumes that record in insertWindow().
        state->removeWindow(windowId);
    }

    // Purge closed window from pending initial orders.
    // If a pre-seeded window closes before arriving at the autotile engine,
    // the pending order would leak indefinitely without this cleanup.
    for (auto pit = m_pendingInitialOrders.begin(); pit != m_pendingInitialOrders.end();) {
        pit.value().removeAll(windowId);
        if (pit.value().isEmpty()) {
            m_pendingOrderGeneration.remove(pit.key());
            m_strictInitialOrderScreens.remove(pit.key());
            pit = m_pendingInitialOrders.erase(pit);
        } else {
            const QString screen = pit.key();
            ++pit; // advance before potential erase by helper
            cleanupPendingOrderIfResolved(screen);
        }
    }
}

bool AutotileEngine::recalculateLayout(const QString& screenId)
{
    if (screenId.isEmpty()) {
        qCWarning(PhosphorTileEngine::lcTileEngine) << "AutotileEngine::recalculateLayout: empty screen name";
        return false;
    }

    PhosphorTiles::TilingState* state = tilingStateForScreen(screenId);
    if (!state) {
        return false;
    }

    PhosphorTiles::TilingAlgorithm* algo = effectiveAlgorithm(screenId);
    if (!algo) {
        qCWarning(PhosphorTileEngine::lcTileEngine) << "AutotileEngine::recalculateLayout: no algorithm set";
        return false;
    }

    const int tiledCount = state->tiledWindowCount();
    if (tiledCount == 0) {
        state->setCalculatedZones({}); // Clear zones when no windows
        return true; // Successfully computed (empty) layout
    }

    // Cap to user's max windows setting — excess windows are not tiled
    const int windowCount = std::min(tiledCount, effectiveMaxWindows(screenId));

    const QRect screen = screenGeometry(screenId);
    if (!screen.isValid()) {
        qCWarning(PhosphorTileEngine::lcTileEngine)
            << "AutotileEngine::recalculateLayout: invalid screen geometry for" << screenId;
        return false;
    }

    const QString algoId = effectiveAlgorithmId(screenId);

    qCDebug(PhosphorTileEngine::lcTileEngine)
        << "recalculateLayout: screen=" << screenId << "geometry=" << screen << "windows=" << windowCount
        << "algo=" << algoId << "splitRatio=" << state->splitRatio();

    // Calculate zone geometries using the algorithm, with gap-aware zones.
    // Algorithms apply gaps directly using their topology knowledge, eliminating
    // the fragile post-processing step that previously guessed adjacency.
    const bool skipGaps = effectiveSmartGaps(screenId) && windowCount == 1;
    const int innerGap = skipGaps ? 0 : effectiveInnerGap(screenId);
    ::PhosphorLayout::EdgeGaps outerGaps =
        skipGaps ? ::PhosphorLayout::EdgeGaps::uniform(0) : effectiveOuterGaps(screenId);

    // Canonical per-window minimum sizes (logical pixels — same units as zone/
    // screen geometry; do not divide by devicePixelRatio or we under-report).
    // Always populated regardless of effectiveRespectMinimumSize: KWin enforces
    // min sizes whether the user opted in or not, so the bounds clamp below
    // must run unconditionally. The flag only gates whether the *algorithm*
    // sees them (and therefore whether enforceMinSizes runs).
    const QStringList tiled = state->tiledWindows();
    QVector<QSize> windowMinSizes(windowCount, QSize(0, 0));
    for (int i = 0; i < windowCount && i < tiled.size(); ++i) {
        windowMinSizes[i] = m_windowMinSizes.value(tiled[i], QSize(0, 0));
    }
    const bool respectMin = effectiveRespectMinimumSize(screenId);
    const QVector<QSize> minSizes = respectMin ? windowMinSizes : QVector<QSize>{};
    if (respectMin && Q_UNLIKELY(PhosphorTileEngine::lcTileEngine().isDebugEnabled())) {
        for (int i = 0; i < windowCount && i < tiled.size(); ++i) {
            if (windowMinSizes[i].width() > 0 || windowMinSizes[i].height() > 0) {
                qCDebug(PhosphorTileEngine::lcTileEngine)
                    << "  minSize[" << i << "]:" << tiled[i] << "=" << windowMinSizes[i];
            }
        }
    }

    // Build per-window metadata for algorithm context. Live class lookup
    // via currentAppIdFor so scripted algorithms see the CURRENT app class
    // rather than a first-seen string baked into the instance id.
    int focusedIndex = -1;
    QVector<PhosphorTiles::WindowInfo> windowInfos = PhosphorTiles::buildWindowInfos(
        state, windowCount,
        [this](const QString& wid) {
            return currentAppIdFor(wid);
        },
        focusedIndex);

    // Build screen metadata for orientation-aware algorithms
    PhosphorTiles::TilingScreenInfo screenInfo;
    screenInfo.id = screenId;
    {
        QScreen* qscreen = PhosphorScreens::ScreenIdentity::findByIdOrName(screenId);
        if (qscreen) {
            const QRect geom = qscreen->geometry();
            screenInfo.portrait = geom.height() > geom.width();
            screenInfo.aspectRatio = geom.height() > 0 ? static_cast<qreal>(geom.width()) / geom.height() : 0.0;
        } else if (m_screenManager) {
            // Virtual screen IDs have no QScreen — use PhosphorScreens::ScreenManager geometry
            const QRect geom = m_screenManager->screenGeometry(screenId);
            if (geom.isValid()) {
                screenInfo.portrait = geom.height() > geom.width();
                screenInfo.aspectRatio =
                    (geom.width() > 0 && geom.height() > 0) ? static_cast<double>(geom.width()) / geom.height() : 0.0;
            }
        }
    }

    // Resolve custom params for this algorithm from saved settings.
    // Filter out stale params that no longer match the algorithm's declarations
    // (e.g., user edited the .luau file and renamed/removed a custom param).
    QVariantMap customParams;
    if (m_config) {
        const auto it = m_config->savedAlgorithmSettings.constFind(algoId);
        if (it != m_config->savedAlgorithmSettings.constEnd() && !it->customParams.isEmpty()) {
            if (algo->supportsCustomParams()) {
                for (auto pit = it->customParams.constBegin(); pit != it->customParams.constEnd(); ++pit) {
                    if (algo->hasCustomParam(pit.key())) {
                        customParams[pit.key()] = pit.value();
                    }
                }
            }
            // else: algorithm doesn't support custom params — don't pass any
        }
    }

    // Let memory-based algorithms prepare their state (e.g., lazily create a PhosphorTiles::SplitTree)
    // before calculateZones(). Virtual dispatch avoids concrete type casts here.
    algo->prepareTilingState(state);

    // Pass minSizes to algorithm so it can incorporate them directly into zone
    // calculations using its topology knowledge (split tree, column structure, etc.)
    PhosphorTiles::TilingParams tilingParams;
    tilingParams.windowCount = windowCount;
    tilingParams.screenGeometry = screen;
    tilingParams.state = state;
    tilingParams.innerGap = innerGap;
    tilingParams.outerGaps = outerGaps;
    tilingParams.minSizes = minSizes;
    tilingParams.windowInfos = windowInfos;
    tilingParams.focusedIndex = focusedIndex;
    tilingParams.screenInfo = screenInfo;
    tilingParams.customParams = customParams;
    // Previous applied zones, exposed to scripts as ctx.currentGeometries.
    // Captured before the algorithm runs (state->calculatedZones() is not
    // overwritten until setCalculatedZones below), so it is the prior layout.
    tilingParams.currentGeometries = state->calculatedZones();
    QVector<QRect> zones = algo->calculateZones(tilingParams);

    qCInfo(PhosphorTileEngine::lcTileEngine)
        << "recalculateLayout: screen=" << screenId << "tiledCount=" << tiledCount << "windowCount=" << windowCount
        << "splitRatio=" << state->splitRatio() << "zones=" << zones;

    // Validate algorithm returned correct number of zones
    if (zones.size() != windowCount) {
        qCWarning(PhosphorTileEngine::lcTileEngine) << "AutotileEngine::recalculateLayout: algorithm returned"
                                                    << zones.size() << "zones for" << windowCount << "windows";
        return false;
    }

    // Lightweight safety net: the algorithm handles min sizes directly, but
    // enforceMinSizes catches any residual deficits from rounding or
    // edge cases the algorithm couldn't fully solve (e.g., unsatisfiable
    // constraints). Skip for any algorithm where producesOverlappingZones()
    // is true (Monocle, Cascade, Stair, Deck, Paper, Spread, horizontal-deck
    // and any future opt-in): zones intentionally overlap and the implicit
    // removeRectOverlaps inside enforceMinSizes would destroy the
    // intended layout.
    // minSizes is populated iff respectMin (see above); windowCount > 0 is
    // already guaranteed by the early return at the top of this function.
    if (respectMin && !algo->producesOverlappingZones()) {
        const int threshold = effectiveInnerGap(screenId) + PhosphorTiles::AutotileDefaults::GapEdgeThresholdPx;
        const QVector<QRect> preEnforceZones = zones;
        PhosphorGeometry::enforceMinSizes(zones, minSizes, threshold, innerGap);
        if (Q_UNLIKELY(PhosphorTileEngine::lcTileEngine().isDebugEnabled()) && zones != preEnforceZones) {
            qCDebug(PhosphorTileEngine::lcTileEngine) << "enforceMinSizes: zones adjusted"
                                                      << "before=" << preEnforceZones << "after=" << zones;
        }
    }

    // Bounds clamp: shift zone position so the effective window rect (after
    // the compositor enforces declared min sizes) stays inside the screen.
    // Without this, a zone narrower/shorter than the window's minSize lets
    // KWin grow the window past the screen edge — and if an adjacent monitor
    // is butted up to that edge, the window's center crosses into it, KWin
    // reassigns its output, and the autotile engine ejects the window.
    //
    // Runs unconditionally — the user's "respect minimum size" preference
    // controls whether the algorithm reflows around min sizes, but it does
    // NOT change KWin's compositor-side enforcement, so we always need this
    // safety net. Position-only; never grows or shrinks zones (size changes
    // are owned by enforceMinSizes, which is unsafe for any algorithm
    // where producesOverlappingZones() is true).
    //
    // Post-clamp zones may overlap. For overlap stacks (any algo with
    // producesOverlappingZones() true — Cascade, Stair, etc.) this is
    // intentional and a shift just changes the visible offset slightly. For
    // non-overlapping algorithms it can also occur when a window's min-size
    // pressure shifts a zone leftward/upward into its neighbor; we
    // deliberately do NOT re-run removeRectOverlaps because it splits the
    // overlap at the midpoint, moving the shifted zone back toward the edge
    // it was just clamped away from — exactly re-introducing the overflow we
    // just fixed.
    //
    // Most downstream consumers (applyTiling, geometry batch builders) index
    // calculatedZones by window position, so they're insensitive to overlap.
    // The one spatial consumer is computeDragInsertIndexAtPoint, which does
    // zones[i].contains(cursorPos) and returns the first hit. In the rare
    // overlap region produced by clamp shift, the lower-indexed zone wins —
    // an acceptable tie-break given (a) overlap area is bounded by the
    // min-size deficit (typically a few hundred pixels at most), (b) the
    // alternative is window ejection to an adjacent monitor, and (c) the
    // cascade/stair algorithms already exercised first-match-wins for years.
    const QVector<QRect> preClampZones = zones;
    PhosphorGeometry::clampZonesToScreen(zones, windowMinSizes, screen);
    if (Q_UNLIKELY(PhosphorTileEngine::lcTileEngine().isDebugEnabled()) && zones != preClampZones) {
        qCDebug(PhosphorTileEngine::lcTileEngine) << "clampZonesToScreen: zones adjusted"
                                                  << "before=" << preClampZones << "after=" << zones;
    }

    // Clamp zones to minimum 1x1 — algorithms or the constraint solver can
    // produce non-positive dimensions when minimum sizes exceed available space.
    for (QRect& zone : zones) {
        if (zone.width() < 1) {
            zone.setWidth(1);
        }
        if (zone.height() < 1) {
            zone.setHeight(1);
        }
    }

    // Store calculated zones in the state for later application
    state->setCalculatedZones(zones);
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// Drag-insert preview
// ═══════════════════════════════════════════════════════════════════════════

bool AutotileEngine::beginDragInsertPreview(const QString& windowId, const QString& screenId)
{
    if (windowId.isEmpty() || screenId.isEmpty() || !isAutotileScreen(screenId)) {
        return false;
    }
    if (m_dragInsertPreview) {
        // A preview is already active — cancel it first so we don't leak state.
        cancelDragInsertPreview();
    }
    PhosphorTiles::TilingState* targetState = tilingStateForScreen(screenId);
    if (!targetState) {
        return false;
    }

    DragInsertPreview preview;
    preview.windowId = windowId;
    preview.targetScreenId = screenId;

    const TilingStateKey targetKey = currentKeyForScreen(screenId);

    // Capture prior engine state (if any) for restoration on cancel.
    // Look up the prior PhosphorTiles::TilingState once and reuse below to avoid a redundant
    // m_screenStates hash lookup in the cross-screen branch.
    PhosphorTiles::TilingState* priorState = nullptr;
    auto it = m_windowToStateKey.constFind(windowId);
    if (it != m_windowToStateKey.constEnd()) {
        preview.hadPriorState = true;
        preview.priorKey = it.value();
        preview.priorSameScreen = (preview.priorKey == targetKey);
        priorState = m_screenStates.value(preview.priorKey);
        if (priorState) {
            preview.priorRawIndex = priorState->windowOrder().indexOf(windowId);
            preview.priorFloating = priorState->isFloating(windowId);
        }
    }

    if (preview.hadPriorState && preview.priorSameScreen) {
        // Same-screen reorder: unfloat if it was floating, otherwise leave in place.
        // The first updateDragInsertPreview() call will reposition within the stack.
        if (preview.priorFloating) {
            targetState->setFloating(windowId, false);
            m_overflow.clearOverflow(windowId);
        }
    } else {
        // Cross-screen adoption or fresh adoption: remove from prior state (if any)
        // and append to the target state's tiled list.
        if (preview.hadPriorState && priorState) {
            priorState->removeWindow(windowId);
        }
        if (targetState->containsWindow(windowId)) {
            // Defensive: stale m_screenStates entry left the window in the target
            // state without a matching m_windowToStateKey mapping. Remove it first
            // so addWindow() can place it cleanly at the end.
            targetState->removeWindow(windowId);
        }
        targetState->addWindow(windowId);
        m_windowToStateKey[windowId] = targetKey;
    }

    // Evict last tiled neighbour if adoption pushed us over the cap for the
    // target screen. Skipped when the dragged window was already tiled on
    // target (same-screen reorder with priorFloating=false) because that
    // doesn't grow the count.
    //
    // Uses effectiveMaxWindows(screenId) so per-screen MaxWindows overrides
    // and global Unlimited mode are both honored consistently — the per-
    // screen override wins even when global is Unlimited, matching the
    // PerScreenConfigResolver priority chain.
    //
    // setFloating preserves the victim's raw position in m_windowOrder, so
    // cancel can restore it with a simple unfloat — no index bookkeeping
    // needed.
    const int maxWindows = effectiveMaxWindows(screenId);
    if (maxWindows > 0 && targetState->tiledWindowCount() > maxWindows) {
        const QStringList tiled = targetState->tiledWindows();
        for (int i = tiled.size() - 1; i >= 0; --i) {
            if (tiled[i] != windowId) {
                preview.evictedWindowId = tiled[i];
                targetState->setFloating(tiled[i], true);
                break;
            }
        }
    }

    preview.lastInsertIndex = targetState->tiledWindowIndex(windowId);

    if (preview.lastInsertIndex < 0) {
        // Something went wrong: window isn't in the tiled list after the setup above.
        // Roll back the prior-state capture so we don't leave the engine in a bad state.
        if (!preview.evictedWindowId.isEmpty()) {
            targetState->setFloating(preview.evictedWindowId, false);
        }
        if (preview.hadPriorState && preview.priorSameScreen) {
            // Same-screen: we only mutated the floating flag (if priorFloating).
            // Re-float to restore the original state.
            if (preview.priorFloating) {
                targetState->setFloating(windowId, true);
            }
        } else if (preview.hadPriorState) {
            // Cross-screen: we removed from prior state and added to target.
            // Undo both.
            targetState->removeWindow(windowId);
            if (PhosphorTiles::TilingState* priorState = m_screenStates.value(preview.priorKey)) {
                priorState->addWindow(windowId, preview.priorRawIndex);
                if (preview.priorFloating) {
                    priorState->setFloating(windowId, true);
                }
                m_windowToStateKey[windowId] = preview.priorKey;
            } else {
                m_windowToStateKey.remove(windowId);
            }
        } else {
            // Fresh adoption: just undo the add.
            targetState->removeWindow(windowId);
            m_windowToStateKey.remove(windowId);
        }
        return false;
    }

    m_dragInsertPreview = preview;
    // Retile target (filtered) so the dragged window is skipped in the batch
    // while neighbours animate into the new layout.
    retileAfterOperation(screenId, /*operationSucceeded=*/true);
    // If we removed the window from a different screen, retile that one too so
    // its remaining windows fill the gap left by the departure.
    if (preview.hadPriorState && !preview.priorSameScreen) {
        retileAfterOperation(preview.priorKey.screenId, /*operationSucceeded=*/true);
    }
    return true;
}

void AutotileEngine::updateDragInsertPreview(int insertIndex)
{
    if (!m_dragInsertPreview) {
        return;
    }
    PhosphorTiles::TilingState* state = tilingStateForScreen(m_dragInsertPreview->targetScreenId);
    if (!state) {
        return;
    }
    const int tileCount = state->tiledWindowCount();
    if (tileCount <= 0) {
        return;
    }
    const int clamped = std::clamp(insertIndex, 0, tileCount - 1);
    if (clamped == m_dragInsertPreview->lastInsertIndex) {
        return; // No change — avoid redundant retile.
    }
    if (!state->moveToTiledPosition(m_dragInsertPreview->windowId, clamped)) {
        return;
    }
    m_dragInsertPreview->lastInsertIndex = clamped;
    retileAfterOperation(m_dragInsertPreview->targetScreenId, /*operationSucceeded=*/true);
}

void AutotileEngine::commitDragInsertPreview()
{
    if (!m_dragInsertPreview) {
        return;
    }
    const QString targetScreenId = m_dragInsertPreview->targetScreenId;
    const QString windowId = m_dragInsertPreview->windowId;
    const QString evictedWindowId = m_dragInsertPreview->evictedWindowId;
    const bool crossScreenAdoption = m_dragInsertPreview->hadPriorState && !m_dragInsertPreview->priorSameScreen;
    const bool freshAdoption = !m_dragInsertPreview->hadPriorState;
    // Same-screen reorders that unfloated the window also need the WTS sync
    // below so the daemon drops its stale "floating" bookkeeping.
    const bool sameScreenUnfloat = m_dragInsertPreview->hadPriorState && m_dragInsertPreview->priorSameScreen
        && m_dragInsertPreview->priorFloating;
    m_dragInsertPreview.reset();
    // Retile target without the filter so the dragged window's geometry is
    // applied on the next windowsTiled emission (KWin's interactive move has
    // ended and will accept the geometry set).
    retileAfterOperation(targetScreenId, /*operationSucceeded=*/true);
    // Emit a float-state sync signal whenever the window's tiling/floating
    // state changed as a result of this drag: cross-screen adoption, fresh
    // adoption, or a same-screen unfloat. Passing floating=false routes
    // through the no-restore path (windowFloatingStateSynced), avoiding the
    // geometry-restore teleport of windowFloatingChanged.
    if (crossScreenAdoption || freshAdoption || sameScreenUnfloat) {
        Q_EMIT windowFloatingStateSynced(windowId, false, targetScreenId);
    }
    // Evicted neighbour: route through the batch-float path so the daemon
    // restores its pre-tile geometry in one pass. Without this the victim
    // would stay stuck in its old tile rect visually while flagged floating
    // in PhosphorTiles::TilingState.
    if (!evictedWindowId.isEmpty()) {
        Q_EMIT windowsBatchFloated(QStringList{evictedWindowId}, targetScreenId);
    }
}

void AutotileEngine::cancelDragInsertPreview()
{
    if (!m_dragInsertPreview) {
        return;
    }
    const DragInsertPreview p = *m_dragInsertPreview;
    m_dragInsertPreview.reset();

    PhosphorTiles::TilingState* targetState = tilingStateForScreen(p.targetScreenId);

    // Restore eviction first: setFloating(false) returns the victim to its
    // original raw-order slot, making the subsequent restoration logic see
    // the same tiled list the user started with.
    if (!p.evictedWindowId.isEmpty() && targetState) {
        targetState->setFloating(p.evictedWindowId, false);
    }

    if (p.hadPriorState && p.priorSameScreen) {
        // Same-screen path: window was already in targetState at begin time.
        // Move it back to its original raw index and restore floating flag.
        if (targetState) {
            targetState->moveToPosition(p.windowId, p.priorRawIndex);
            if (p.priorFloating) {
                targetState->setFloating(p.windowId, true);
            }
        }
    } else {
        // Cross-screen / fresh adoption path. If the window came from another
        // state that still exists, restore it there. If the prior state was
        // evicted (desktop/VS reconfigure between begin and cancel) we cannot
        // restore — in that case leave the window in target rather than
        // orphaning it, and notify WTS so bookkeeping stays consistent.
        PhosphorTiles::TilingState* priorState = (p.hadPriorState) ? m_screenStates.value(p.priorKey) : nullptr;
        if (p.hadPriorState && !priorState) {
            // m_windowToStateKey already points at target from begin(); leave
            // it there and let the window live in target state.
            Q_EMIT windowFloatingStateSynced(p.windowId, false, p.targetScreenId);
        } else {
            if (targetState) {
                targetState->removeWindow(p.windowId);
            }
            m_windowToStateKey.remove(p.windowId);
            if (priorState) {
                // Defensive: if the prior state was torn down and rebuilt
                // between begin() and cancel(), it may already contain an
                // entry for this window (e.g. the rebuild re-adopted it from
                // a pending order). Mirror the begin() guard so addWindow()
                // can place it at the captured raw index cleanly.
                if (priorState->containsWindow(p.windowId)) {
                    priorState->removeWindow(p.windowId);
                }
                priorState->addWindow(p.windowId, p.priorRawIndex);
                if (p.priorFloating) {
                    priorState->setFloating(p.windowId, true);
                }
                m_windowToStateKey[p.windowId] = p.priorKey;
            }
        }
    }

    retileAfterOperation(p.targetScreenId, /*operationSucceeded=*/true);
    if (p.hadPriorState && !p.priorSameScreen) {
        retileAfterOperation(p.priorKey.screenId, /*operationSucceeded=*/true);
    }
}

int AutotileEngine::computeDragInsertIndexAtPoint(const QString& screenId, const QPoint& cursorPos) const
{
    // Const-correct lookup: avoid tilingStateForScreen() which may create state.
    auto it = m_screenStates.constFind(currentKeyForScreen(screenId));
    if (it == m_screenStates.constEnd() || !it.value()) {
        return -1;
    }
    const PhosphorTiles::TilingState* state = it.value();
    const QVector<QRect> zones = state->calculatedZones();
    if (zones.isEmpty()) {
        return 0;
    }
    const QStringList tiled = state->tiledWindows();
    // Walk zones in order; return the first zone whose rect contains the cursor.
    // Do NOT skip the dragged window's own zone — cursor-over-own-zone must be a
    // stable identity (return its current index), otherwise we force a shuffle
    // to some neighbour slot which immediately re-matches under the cursor and
    // oscillates every dragMoved tick.
    //
    // maxWindows may cap the layout so zones.size() < tiled.size(). In that
    // case, windows past `limit` have no zone and can't be hit-tested. If the
    // dragged window fell past the cap (e.g. evicted-to-floating in a tight
    // monocle-style layout), the stable-identity contract can't hold — hold
    // the preview at its last index instead.
    const int limit = std::min(zones.size(), tiled.size());
    const int draggedIdx = m_dragInsertPreview ? tiled.indexOf(m_dragInsertPreview->windowId) : -1;
    const bool draggedBeyondCap = draggedIdx >= 0 && draggedIdx >= limit;
    if (!draggedBeyondCap) {
        for (int i = 0; i < limit; ++i) {
            if (zones[i].contains(cursorPos)) {
                return i;
            }
        }
    }
    // Cursor isn't over any zone (or the dragged window is past the cap) —
    // hold the preview at its current index to avoid snapping to an endpoint.
    if (m_dragInsertPreview && m_dragInsertPreview->lastInsertIndex >= 0) {
        return m_dragInsertPreview->lastInsertIndex;
    }
    return tiled.isEmpty() ? 0 : tiled.size() - 1;
}

void AutotileEngine::applyTiling(const QString& screenId)
{
    PhosphorTiles::TilingState* state = tilingStateForScreen(screenId);
    if (!state) {
        return;
    }

    // Drag-insert preview: skip emitting geometry for the dragged window so
    // KWin's interactive move isn't fought. Other windows still animate to
    // their new tile positions, producing the OrderingPage-style shift.
    const bool filterForPreview = m_dragInsertPreview && m_dragInsertPreview->targetScreenId == screenId;
    const QString filteredWindowId = filterForPreview ? m_dragInsertPreview->windowId : QString();

    const QStringList windows = state->tiledWindows();
    const QVector<QRect> zones = state->calculatedZones();

    // zones.size() may be less than windows.size() when maxWindows caps the layout.
    // Only the first zones.size() windows receive tiled geometries; the rest are untouched.
    if (zones.isEmpty()) {
        qCDebug(PhosphorTileEngine::lcTileEngine)
            << "AutotileEngine::applyTiling: no zones calculated for screen" << screenId;
        return;
    }
    if (zones.size() > windows.size()) {
        qCWarning(PhosphorTileEngine::lcTileEngine)
            << "AutotileEngine::applyTiling: zone count exceeds window count" << windows.size() << "vs" << zones.size();
        return;
    }

    const int tileCount = zones.size();

    // Auto-float overflow windows that exceed maxWindows cap.
    // Daemon's windowFloatingChanged handler restores their pre-autotile geometry.
    // Batch: mutate state first, then collect signals for deferred emission.
    QStringList newlyOverflowed = m_overflow.applyOverflow(screenId, windows, tileCount);
    for (const QString& wid : std::as_const(newlyOverflowed)) {
        state->setFloating(wid, true);
    }

    // Build batch JSON and emit once to avoid race when effect applies many geometries.
    // Flag monocle-style layouts where all zones share identical geometry,
    // so the KWin effect can set maximize state on stacked windows.
    // This catches both intentional monocle layouts and degenerate fillArea
    // fallbacks (tiny screens) — maximize is appropriate in both cases since
    // windows are stacked identically.
    // Requires >= 2 zones: a single window is just normal tiling, not monocle.
    const bool useMonocleMode = tileCount >= 2 && std::all_of(zones.begin() + 1, zones.end(), [&](const QRect& z) {
                                    return z == zones[0];
                                });
    QJsonArray arr;
    for (int i = 0; i < tileCount; ++i) {
        if (filterForPreview && windows[i] == filteredWindowId) {
            continue;
        }
        const QRect& geo = zones[i];
        QJsonObject obj;
        obj[QLatin1String("windowId")] = windows[i];
        obj[QLatin1String("screenId")] = screenId;
        obj[QLatin1String("x")] = geo.x();
        obj[QLatin1String("y")] = geo.y();
        obj[QLatin1String("width")] = geo.width();
        obj[QLatin1String("height")] = geo.height();
        // Flag monocle entries so the effect can set KWin maximize state,
        // which makes Plasma panels recognize the window and unfloat.
        if (useMonocleMode) {
            obj[QLatin1String("monocle")] = true;
        }
        arr.append(obj);
    }
    // Include overflow windows in the batch with "floating" flag so the effect
    // can restore their pre-autotile geometry in one pass, instead of receiving
    // individual D-Bus windowFloatingChanged + applyGeometryRequested per window.
    for (const QString& wid : std::as_const(newlyOverflowed)) {
        QJsonObject obj;
        obj[QLatin1String("windowId")] = wid;
        obj[QLatin1String("floating")] = true;
        obj[QLatin1String("screenId")] = screenId;
        arr.append(obj);
    }

    if (Q_UNLIKELY(PhosphorTileEngine::lcTileEngine().isDebugEnabled())) {
        for (int i = 0; i < tileCount; ++i) {
            qCDebug(PhosphorTileEngine::lcTileEngine) << "  applyTiling:" << windows[i] << "zone=" << zones[i];
        }
    }

    Q_EMIT windowsTiled(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));

    // Emit deferred focus AFTER windowsTiled so KWin processes tiles first
    // (including the onComplete raise loop), then focuses the new window on top.
    if (!m_pendingFocusWindowId.isEmpty()) {
        Q_EMIT activateWindowRequested(m_pendingFocusWindowId);
        m_pendingFocusWindowId.clear();
    }

    // Batch-notify daemon of overflow float state (replaces per-window
    // windowFloatingChanged). The daemon handler updates WTS state without
    // emitting per-window D-Bus signals since the effect processes float entries
    // from the windowsTileRequested batch.
    if (!newlyOverflowed.isEmpty()) {
        Q_EMIT windowsBatchFloated(newlyOverflowed, screenId);
    }
}

bool AutotileEngine::shouldTileWindow(const QString& rawWindowId) const
{
    if (rawWindowId.isEmpty()) {
        return false;
    }
    const QString windowId = canonicalizeForLookup(rawWindowId);

    // Respect autotile-specific sticky window handling setting.
    // IgnoreAll: sticky windows are never autotiled.
    // RestoreOnly: sticky windows are not auto-managed (autotiling is active management).
    // TreatAsNormal: sticky windows are tiled like any other window.
    if (m_windowTracker && m_windowTracker->isWindowSticky(windowId)) {
        auto* s = autotileSettings();
        if (s) {
            const auto handling = s->autotileStickyWindowHandling();
            if (handling == PhosphorEngine::StickyWindowHandling::IgnoreAll
                || handling == PhosphorEngine::StickyWindowHandling::RestoreOnly) {
                qCDebug(PhosphorTileEngine::lcTileEngine)
                    << "Window" << windowId << "is sticky, handling=" << static_cast<int>(handling)
                    << ", skipping tile";
                return false;
            }
        }
    }

    // Check if window is floating in any screen's PhosphorTiles::TilingState
    // (floating windows are excluded from autotiling).
    // Only check states for the current desktop/activity.
    for (const QString& screen : m_autotileScreens) {
        const TilingStateKey key = currentKeyForScreen(screen);
        auto it = m_screenStates.constFind(key);
        if (it != m_screenStates.constEnd() && it.value() && it.value()->isFloating(windowId)) {
            qCDebug(PhosphorTileEngine::lcTileEngine) << "Window" << windowId << "is floating, skipping tile";
            return false;
        }
    }

    // Note: Other exclusions (special windows, dialogs, fullscreen, etc.)
    // are already handled by KWin effect's shouldHandleWindow() before
    // sending window events to daemon.

    return true;
}

QString AutotileEngine::screenForWindow(const QString& rawWindowId) const
{
    const QString windowId = canonicalizeForLookup(rawWindowId);
    // Check if already tracked
    auto it = m_windowToStateKey.constFind(windowId);
    if (it != m_windowToStateKey.constEnd()) {
        return it->screenId;
    }

    // R6 fix: Warn when falling back to primary screen — this may indicate a
    // missing screen name in windowOpened() or a stale m_windowToStateKey entry.
    if (m_screenManager) {
        const PhosphorScreens::PhysicalScreen primary = m_screenManager->primaryScreen();
        if (primary.isValid()) {
            qCWarning(PhosphorTileEngine::lcTileEngine)
                << "screenForWindow: window" << windowId << "not in m_windowToStateKey, falling back to primary screen";
            // If the primary monitor is subdivided into virtual screens, return
            // the first virtual screen ID instead of the physical ID.
            const QString physId = primary.identifier;
            const QStringList vsIds = m_screenManager->virtualScreenIdsFor(physId);
            return vsIds.isEmpty() ? physId : vsIds.first();
        }
    }

    qCWarning(PhosphorTileEngine::lcTileEngine) << "screenForWindow: no screen found for window" << windowId;
    return QString();
}

QRect AutotileEngine::screenGeometry(const QString& screenId) const
{
    if (!m_screenManager) {
        return QRect();
    }

    // Virtual screens: use PhosphorScreens::ScreenManager's virtual-aware geometry
    if (PhosphorIdentity::VirtualScreenId::isVirtual(screenId)) {
        return m_screenManager->screenAvailableGeometry(screenId);
    }

    // Physical screens: resolve through the manager's cache-backed string
    // overload, which reads the tracked-screen snapshot (from the screen
    // provider) rather than a live QScreen. This is behaviourally identical to
    // the old findByIdOrName + actualAvailableGeometry(QScreen*) path on a real
    // system (both feed the same available-geometry/strut cache) AND resolves
    // synthetic, QScreen-less screens from a test provider — without it,
    // directional cross-output navigation cannot be exercised headlessly.
    const QRect geom = m_screenManager->screenAvailableGeometry(screenId);
    if (geom.isValid()) {
        return geom;
    }

    // Last resort: a live QScreen the manager has not tracked yet (a hotplug
    // race). The QScreen* overload resolves the connector and falls back to
    // QScreen::availableGeometry().
    QScreen* screen = PhosphorScreens::ScreenIdentity::findByIdOrName(screenId);
    if (!screen) {
        return QRect();
    }
    return m_screenManager->actualAvailableGeometry(screen);
}

bool AutotileEngine::isKnownScreen(const QString& screenId) const
{
    if (!m_screenManager) {
        // Without PhosphorScreens::ScreenManager, skip validation (test environments)
        return true;
    }
    if (PhosphorIdentity::VirtualScreenId::isVirtual(screenId)) {
        return m_screenManager->screenGeometry(screenId).isValid();
    }
    // Physical screens: resolve via the manager's tracked-screen snapshot
    // (the screen provider), which is equivalent to a live-QScreen lookup on a
    // real system but also recognises synthetic, QScreen-less screens from a
    // test provider — keeping this consistent with screenGeometry() above.
    // Fall back to findByIdOrName for a not-yet-tracked hotplug race.
    if (m_screenManager->screenGeometry(screenId).isValid()) {
        return true;
    }
    return PhosphorScreens::ScreenIdentity::findByIdOrName(screenId) != nullptr;
}

void AutotileEngine::resetMaxWindowsForAlgorithmSwitch(PhosphorTiles::TilingAlgorithm* oldAlgo,
                                                       PhosphorTiles::TilingAlgorithm* newAlgo)
{
    if (!oldAlgo || !newAlgo)
        return;
    if (m_config->maxWindows == oldAlgo->defaultMaxWindows()) {
        m_config->maxWindows = newAlgo->defaultMaxWindows();
    }
}

void AutotileEngine::propagateGlobalSplitRatio()
{
    // Only propagate to current desktop/activity states — per-desktop split
    // ratio adjustments (via increaseMasterRatio) are preserved on other desktops.
    // States the user explicitly tuned (m_userTunedSplitRatio) and screens with a
    // per-screen override are skipped, so a local ratio tweak is never clobbered
    // by a settings refresh.
    for (auto it = m_screenStates.constBegin(); it != m_screenStates.constEnd(); ++it) {
        if (it.key().desktop != currentKeyForScreen(it.key().screenId).desktop
            || it.key().activity != m_currentActivity) {
            continue;
        }
        if (it.value() && !hasPerScreenOverride(it.key().screenId, PerScreenKeys::SplitRatio)
            && !m_userTunedSplitRatio.contains(it.key())) {
            it.value()->setSplitRatio(m_config->splitRatio);
        }
    }
}

void AutotileEngine::propagateGlobalMasterCount()
{
    // Only propagate to current desktop/activity states — per-desktop master
    // count adjustments are preserved on other desktops. States the user
    // explicitly tuned (m_userTunedMasterCount) and per-screen-override screens
    // are skipped, so a local master-count tweak is never clobbered by a refresh.
    for (auto it = m_screenStates.constBegin(); it != m_screenStates.constEnd(); ++it) {
        if (it.key().desktop != currentKeyForScreen(it.key().screenId).desktop
            || it.key().activity != m_currentActivity) {
            continue;
        }
        if (it.value() && !hasPerScreenOverride(it.key().screenId, PerScreenKeys::MasterCount)
            && !m_userTunedMasterCount.contains(it.key())) {
            it.value()->setMasterCount(m_config->masterCount);
        }
    }
}

void AutotileEngine::backfillWindows()
{
    // Algorithm lifecycle ADD hooks are deliberately not fired here (nor by
    // the strict pending-order eager-consume in setAutotileScreens or the
    // drag-insert-preview reorders): backfill runs on algorithm SWITCHES,
    // where the incoming algorithm builds its bookkeeping from the full
    // tiledWindows() list on its first retile rather than incrementally;
    // per-window add hooks before that retile would double-count. The
    // incremental hooks cover steady-state add/remove/migrate only.
    // The same reconciliation covers the drag-insert preview's PRIOR-state
    // mutations too: beginDragInsertPreview's cross-screen adoption
    // (priorState->removeWindow) and cancelDragInsertPreview's restore skip
    // the REMOVE/ADD hooks, and the scheduled retile on the prior screen
    // reconciles that algorithm's bookkeeping against the state's full
    // window list via prepareTilingState().
    for (const QString& screenId : m_autotileScreens) {
        // Overflow recovery is NOT done here — it is handled by retileScreen()
        // which defers signal emission until after the full retile cycle.
        // Doing it here would emit windowFloatingChanged synchronously before
        // the deferred retile fires, creating a feedback loop where the KWin
        // effect processes float state changes mid-transition.

        PhosphorTiles::TilingState* state = tilingStateForScreen(screenId);
        if (!state) {
            continue;
        }
        const int maxWin = effectiveMaxWindows(screenId);
        if (state->tiledWindowCount() >= maxWin) {
            continue;
        }
        // Collect candidates to avoid modifying m_windowToStateKey during iteration
        // (insertWindow calls m_windowToStateKey.insert which is unsafe during const iteration)
        QStringList candidates;
        for (auto it = m_windowToStateKey.constBegin(); it != m_windowToStateKey.constEnd(); ++it) {
            if (it.value().screenId == screenId
                && it.value().desktop == currentKeyForScreen(it.value().screenId).desktop
                && it.value().activity == m_currentActivity && !state->containsWindow(it.key())
                && shouldTileWindow(it.key())) {
                candidates.append(it.key());
            }
        }
        for (const QString& windowId : candidates) {
            const bool inserted = insertWindow(windowId, screenId);
            // Same passive float-state sync onWindowAdded does: a window that
            // insertWindow floats here (matched Float rule / restored saved float)
            // — or whose stale WTS float must be cleared because it was placed
            // tiled — would otherwise desync from the daemon until its next add.
            // emitInsertFloatStateSync uses windowFloatingStateSynced (NOT
            // windowFloatingChanged), so it applies no geometry and cannot drive
            // the mid-transition feedback loop the overflow-recovery note warns of.
            if (inserted) {
                emitInsertFloatStateSync(windowId, screenId);
            }
            if (state->tiledWindowCount() >= maxWin) {
                break;
            }
        }
    }
}

void AutotileEngine::retileScreen(const QString& screenId)
{
    PhosphorTiles::TilingState* state = tilingStateForScreen(screenId);
    if (!state) {
        return;
    }

    // Pre-validate screen geometry BEFORE mutating state (overflow recovery).
    // QScreen can be transiently unavailable during Wayland desktop switches
    // (Plasma rebuilds the output list). If invalid, schedule a bounded retry
    // rather than proceeding — without this, the ratio change / window add
    // that triggered this retile is silently dropped, leaving stale zones.
    //
    // Only gate on geometry when a PhosphorScreens::ScreenManager exists (production). Without
    // one (unit tests), the existing recalculateLayout gracefully handles
    // the empty geometry as a structural no-op, not a transient failure.
    if (m_screenManager) {
        const QRect preValidatedGeometry = screenGeometry(screenId);
        if (!preValidatedGeometry.isValid()) {
            qCWarning(PhosphorTileEngine::lcTileEngine)
                << "retileScreen: screen geometry transiently invalid for" << screenId << "- deferring retile";
            scheduleRetileRetry(screenId);
            return;
        }

        // Geometry valid — clear any pending retry state for this screen.
        m_retileRetryCount.remove(screenId);
        m_retileRetryScreens.remove(screenId);
    }

    // Step 1: Recover overflow windows when room is available.
    // Collect recovery list first, mutate state, then defer signal emission
    // until after the entire retile cycle completes (prevents re-entrant
    // signal handlers from seeing partially-modified state).
    QStringList unfloated;
    if (!m_overflow.isEmpty()) {
        unfloated = m_overflow.recoverIfRoom(
            screenId, state->tiledWindowCount(), effectiveMaxWindows(screenId),
            [state](const QString& wid) {
                return state->isFloating(wid);
            },
            [state](const QString& wid) {
                return state->containsWindow(wid);
            });
        for (const QString& wid : unfloated) {
            state->setFloating(wid, false);
            m_windowMinSizes.remove(wid);
        }
    }

    // Step 2-3: Recalculate layout and apply tiling (applyTiling also handles
    // new overflow detection and collects overflow signals internally).
    // On failure, zones are unchanged from the last successful recalc —
    // applyTiling must still run to handle overflow recovery from step 1.
    if (!recalculateLayout(screenId)) {
        qCWarning(PhosphorTileEngine::lcTileEngine)
            << "retileScreen: recalculateLayout failed for" << screenId << "- applying previous zone layout";
    }
    applyTiling(screenId);

    // Step 4: Emit all deferred signals after state is fully consistent.
    // Recovery signals first (unfloated windows), then overflow signals
    // (newly floated windows) were already handled inside applyTiling's
    // batch emit, and placementChanged is emitted last.
    for (const QString& wid : unfloated) {
        Q_EMIT windowFloatingChanged(wid, false, screenId);
    }
    Q_EMIT placementChanged(screenId);
}

void AutotileEngine::retileAfterOperation(const QString& screenId, bool operationSucceeded)
{
    if (!operationSucceeded) {
        return; // No change, no signal
    }

    if (!isAutotileScreen(screenId)) {
        return;
    }

    // This synchronous retile recomputes from the current state, so any deferred
    // retile already queued for the SAME screen is now redundant. Drop it —
    // otherwise processPendingRetiles fires a second batch for this screen
    // microseconds later, and that duplicate supersedes the staggered apply of
    // this one, stranding every window past the first (a cross-output move left
    // the source monitor with windows that never reflowed).
    m_pendingRetileScreens.remove(screenId);

    // When already inside retile(), still recalc and apply for this screen so
    // navigation (rotate, swap, etc.) is never dropped — user expects geometry
    // to update immediately. Do not clear m_retiling; let the outer retile() do that.
    if (m_retiling) {
        retileScreen(screenId);
        return;
    }

    QScopeGuard guard([this] {
        m_retiling = false;
    });
    m_retiling = true;
    retileScreen(screenId);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Helper Methods
// ═══════════════════════════════════════════════════════════════════════════════

bool AutotileEngine::warnIfEmptyWindowId(const QString& windowId, const char* operation) const
{
    if (windowId.isEmpty()) {
        qCWarning(PhosphorTileEngine::lcTileEngine) << operation << "called with empty windowId";
        return false;
    }
    return true;
}

void AutotileEngine::setWindowRegistry(QObject* registry)
{
    m_windowRegistry = dynamic_cast<PhosphorEngine::IWindowRegistry*>(registry);
    if (!m_windowRegistry) {
        return;
    }
    auto resolver = [this](const QString& windowId) {
        return currentAppIdFor(windowId);
    };
    auto* algoRegistry = m_algorithmRegistry;
    if (!algoRegistry) {
        return;
    }
    for (PhosphorTiles::TilingAlgorithm* algo : algoRegistry->allAlgorithms()) {
        if (algo) {
            algo->setAppIdResolver(resolver);
        }
    }
    connect(algoRegistry, &PhosphorTiles::ITileAlgorithmRegistry::algorithmRegistered, this,
            [this, resolver](const QString& id) {
                auto* reg = m_algorithmRegistry;
                if (!reg) {
                    return;
                }
                if (auto* algo = reg->algorithm(id)) {
                    algo->setAppIdResolver(resolver);
                }
            });
}

QString AutotileEngine::canonicalizeWindowId(const QString& rawWindowId)
{
    if (rawWindowId.isEmpty()) {
        return rawWindowId;
    }
    // When a registry is attached (production daemon), delegate so every
    // service in the daemon agrees on the same canonical form for a given
    // instance id. Unit tests construct the engine without a registry and
    // fall back to a local map to keep the canonicalization invariant.
    if (m_windowRegistry) {
        return m_windowRegistry->canonicalizeWindowId(rawWindowId);
    }
    const QString instanceId = PhosphorIdentity::WindowId::extractInstanceId(rawWindowId);
    auto it = m_canonicalByInstance.constFind(instanceId);
    if (it != m_canonicalByInstance.constEnd()) {
        return it.value();
    }
    m_canonicalByInstance.insert(instanceId, rawWindowId);
    return rawWindowId;
}

void AutotileEngine::cleanupCanonical(const QString& anyWindowId)
{
    if (anyWindowId.isEmpty()) {
        return;
    }
    // Do NOT release the registry-owned canonical map here: the registry is
    // shared across services and only the compositor bridge's close path
    // (via WindowTrackingAdaptor::windowClosed) is authorized to release it.
    // Other services might still resolve this instance id after the engine
    // has cleaned up its own state.
    if (m_windowRegistry) {
        return;
    }
    const QString instanceId = PhosphorIdentity::WindowId::extractInstanceId(anyWindowId);
    m_canonicalByInstance.remove(instanceId);
}

QString AutotileEngine::canonicalizeForLookup(const QString& rawWindowId) const
{
    if (rawWindowId.isEmpty()) {
        return rawWindowId;
    }
    if (m_windowRegistry) {
        return m_windowRegistry->canonicalizeForLookup(rawWindowId);
    }
    const QString instanceId = PhosphorIdentity::WindowId::extractInstanceId(rawWindowId);
    auto it = m_canonicalByInstance.constFind(instanceId);
    return (it != m_canonicalByInstance.constEnd()) ? it.value() : rawWindowId;
}

QString AutotileEngine::currentAppIdFor(const QString& anyWindowId) const
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
    // Fallback: parse the string. Note this returns the FIRST-seen class for
    // canonical ids; accurate only when the window has never renamed.
    return PhosphorIdentity::WindowId::extractAppId(anyWindowId);
}

bool AutotileEngine::cleanupPendingOrderIfResolved(const QString& screenId)
{
    auto pit = m_pendingInitialOrders.find(screenId);
    if (pit == m_pendingInitialOrders.end()) {
        return false;
    }

    PhosphorTiles::TilingState* state = tilingStateForScreen(screenId);
    if (!state) {
        return false;
    }

    for (const QString& pendingWin : std::as_const(pit.value())) {
        if (!state->containsWindow(pendingWin)) {
            return false;
        }
    }

    qCDebug(PhosphorTileEngine::lcTileEngine) << "All pre-seeded windows resolved for screen" << screenId;
    m_pendingInitialOrders.erase(pit);
    m_pendingOrderGeneration.remove(screenId);
    m_strictInitialOrderScreens.remove(screenId);
    return true;
}

PhosphorTiles::TilingState* AutotileEngine::stateForWindow(const QString& windowId, QString* outScreenId)
{
    auto it = m_windowToStateKey.constFind(windowId);
    if (it == m_windowToStateKey.constEnd() || it->screenId.isEmpty()) {
        if (outScreenId) {
            outScreenId->clear();
        }
        return nullptr;
    }

    if (outScreenId) {
        *outScreenId = it->screenId;
    }
    // Use the stored key directly — this returns the state that owns the window,
    // even if the current desktop/activity has changed since the window was added.
    return m_screenStates.value(*it);
}

void AutotileEngine::setInnerGap(int gap)
{
    gap = std::clamp(gap, PhosphorTiles::AutotileDefaults::MinGap, PhosphorTiles::AutotileDefaults::MaxGap);
    if (m_config && m_config->innerGap != gap) {
        m_config->innerGap = gap;
        retile(QString());
    }
}

void AutotileEngine::setOuterGap(int gap)
{
    gap = std::clamp(gap, PhosphorTiles::AutotileDefaults::MinGap, PhosphorTiles::AutotileDefaults::MaxGap);
    if (m_config && m_config->outerGap != gap) {
        m_config->outerGap = gap;
        retile(QString());
    }
}

void AutotileEngine::setSmartGaps(bool enabled)
{
    if (m_config && m_config->smartGaps != enabled) {
        m_config->smartGaps = enabled;
        retile(QString());
    }
}

void AutotileEngine::setFocusNewWindows(bool enabled)
{
    if (m_config) {
        m_config->focusNewWindows = enabled;
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// IPlacementEngine — navigation overrides
//
// Each method absorbs what AutotileNavigationAdapter did: translate
// the user-intent-shaped IPlacementEngine call into the existing
// concrete AutotileEngine method with the right parameters.
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileEngine::focusInDirection(const QString& direction, const NavigationContext& ctx)
{
    // Daemon-authoritative windowId overrides the per-state focusedWindow()
    // tracker, which can drift when focus moves through floating, snapped, or
    // never-tracked windows that don't update it (same root cause as the
    // toggleFocusedFloat fix).
    m_navigation->focusInDirection(direction, QStringLiteral("focus"), canonicalizeForLookup(ctx.windowId));
}

void AutotileEngine::moveFocusedInDirection(const QString& direction, const NavigationContext& ctx)
{
    // In autotile, "move in direction" is implemented as swap-with-neighbour
    // in the tiling order — the only way to move is to trade places with
    // the neighbour. OSD label "move" keeps the user-facing wording.
    m_navigation->swapFocusedInDirection(direction, QStringLiteral("move"), canonicalizeForLookup(ctx.windowId));
}

void AutotileEngine::swapFocusedInDirection(const QString& direction, const NavigationContext& ctx)
{
    m_navigation->swapFocusedInDirection(direction, QStringLiteral("swap"), canonicalizeForLookup(ctx.windowId));
}

QString AutotileEngine::entryWindowForCrossing(const QString& screenId, const QString& direction) const
{
    return m_navigation->entryWindowOnScreen(screenId, direction);
}

int AutotileEngine::windowOrderIndexForWindow(const QString& screenId, const QString& windowId) const
{
    return m_navigation->windowOrderIndexOnScreen(screenId, canonicalizeForLookup(windowId));
}

void AutotileEngine::moveFocusedToPosition(int position, const NavigationContext& ctx)
{
    m_navigation->moveFocusedToPosition(position, canonicalizeForLookup(ctx.windowId));
}

void AutotileEngine::rotateWindows(bool clockwise, const NavigationContext& ctx)
{
    rotateWindows(clockwise, ctx.screenId);
}

void AutotileEngine::reapplyLayout(const NavigationContext& ctx)
{
    retile(ctx.screenId);
}

void AutotileEngine::reapplyManagedWindowAppearance()
{
    // Re-emit the tile geometry + borderless state for every tracked window so
    // the compositor re-applies each window's border / hidden title bar after a
    // bridge reconnect. retile() recomputes the current layout, but with the
    // same windows and layout it yields identical geometry — so no window moves;
    // it just re-drives the tile-request signal the compositor consumes to
    // re-apply chrome. Empty screenId = all autotile screens.
    retile(QString());
}

std::optional<PhosphorEngine::WindowPlacement> AutotileEngine::capturePlacement(const QString& windowId) const
{
    using PhosphorEngine::WindowPlacement;
    const QString wid = canonicalizeForLookup(windowId);
    const auto keyIt = m_windowToStateKey.constFind(wid);
    if (keyIt == m_windowToStateKey.constEnd()) {
        return std::nullopt;
    }
    const PhosphorEngine::TilingStateKey key = keyIt.value();
    PhosphorTiles::TilingState* state = m_screenStates.value(key, nullptr);
    if (!state) {
        return std::nullopt;
    }

    WindowPlacement p;
    // Canonical id, not the raw argument: every engine map is keyed on the
    // canonical form, and a record persisted under a mutated-appId alias
    // would never exact-match again (only the appId FIFO fallback rescues it).
    p.windowId = wid;
    p.appId = currentAppIdFor(windowId);
    p.screenId = key.screenId;
    p.virtualDesktop = key.desktop;
    p.activity = key.activity;

    // The slot carries only the autotile engine's STATE + slot reference (tile
    // order) — NEVER a rectangle. The shared free/float geometry is filled by the
    // capture orchestrator from the live frame, and ONLY when floating.
    //
    // A genuine float persists across mode toggles. The discriminator is the
    // OVERFLOW set, NOT the user-float marker: a window can be floating via the
    // float shortcut (marker set) OR via drag-to-float (marker NOT set — it emits
    // windowFloatingStateSynced, the passive path), and BOTH are real user floats
    // that must persist. Only an OVERFLOW float (auto-floated by the maxWindows cap)
    // is recorded as tiled so it re-tiles — and overflows again if it still doesn't
    // fit — on restore, rather than sticking as a phantom user float.
    PhosphorEngine::EngineSlot slot;
    if (state->isFloating(wid) && !m_overflow.isOverflow(wid)) {
        slot.state = WindowPlacement::stateFloating();
    } else {
        slot.state = WindowPlacement::stateTiled();
        slot.order = state->windowOrder().indexOf(wid);
    }
    p.engines.insert(engineId(), slot);
    return p;
}

void AutotileEngine::snapAllWindows(const NavigationContext& ctx)
{
    // Autotile has no distinct "snap all" — retile picks up every window
    // the engine is tracking and inserts any new ones into the layout.
    retile(ctx.screenId);
}

void AutotileEngine::toggleFocusedFloat(const NavigationContext& ctx)
{
    // Prefer the daemon-provided windowId from KWin's authoritative focus
    // tracking. The legacy toggleFocusedWindowFloat() uses state->focusedWindow()
    // which is updated only when KWin emits windowActivated for an
    // autotile-tracked window — focus moves through floating, snapped, or
    // never-tracked windows leave it stale, and the next float shortcut then
    // toggles the wrong window.
    //
    // Fall back to the legacy "find a focused state" lookup only when ctx
    // doesn't carry a windowId (some test paths and direct invocations).
    if (!ctx.windowId.isEmpty()) {
        const QString screenId = ctx.screenId.isEmpty() ? m_activeScreen : ctx.screenId;
        toggleWindowFloat(ctx.windowId, screenId);
        return;
    }
    toggleFocusedWindowFloat();
}

void AutotileEngine::cycleFocus(bool forward, const NavigationContext& ctx)
{
    const QString dir = forward ? QStringLiteral("right") : QStringLiteral("left");
    m_navigation->focusInDirection(dir, QStringLiteral("cycle"), canonicalizeForLookup(ctx.windowId));
}

void AutotileEngine::pushToEmptyZone(const NavigationContext& /*ctx*/)
{
    // Autotile has no concept of empty zones — every tracked window is
    // placed by the layout algorithm. Deliberate no-op so the shortcut
    // becomes a harmless press in autotile mode.
}

void AutotileEngine::restoreFocusedWindow(const NavigationContext& ctx)
{
    // "Restore" in autotile means pulling the focused window out of the
    // tiling layout — toggling its float state achieves exactly that.
    // Same daemon-authoritative routing as toggleFocusedFloat: prefer
    // ctx.windowId over the engine's per-state focusedWindow() tracker.
    if (!ctx.windowId.isEmpty()) {
        const QString screenId = ctx.screenId.isEmpty() ? m_activeScreen : ctx.screenId;
        toggleWindowFloat(ctx.windowId, screenId);
        return;
    }
    toggleFocusedWindowFloat();
}

// ═══════════════════════════════════════════════════════════════════════════════
// IPlacementEngine — state access
// ═══════════════════════════════════════════════════════════════════════════════

PhosphorEngine::IPlacementState* AutotileEngine::stateForScreen(const QString& screenId)
{
    return tilingStateForScreen(screenId);
}

const PhosphorEngine::IPlacementState* AutotileEngine::stateForScreen(const QString& screenId) const
{
    if (screenId.isEmpty()) {
        return nullptr;
    }
    const TilingStateKey key = currentKeyForScreen(screenId);
    return m_screenStates.value(key, nullptr);
}

} // namespace PhosphorTileEngine
