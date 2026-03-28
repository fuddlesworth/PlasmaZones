// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Qt headers
#include <algorithm>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QScopeGuard>
#include <QScreen>
#include <QTimer>

// Project headers
#include "AutotileEngine.h"
#include "AlgorithmRegistry.h"
#include "core/geometryutils.h"
#include "core/utils.h"
#include "AutotileConfig.h"
#include "NavigationController.h"
#include "PerScreenConfigResolver.h"
#include "SettingsBridge.h"
#include "TilingAlgorithm.h"
// DwindleMemoryAlgorithm.h no longer needed — prepareTilingState() is virtual on TilingAlgorithm
#include "TilingState.h"
#include "core/constants.h"
#include "core/layout.h"
#include "core/layoutmanager.h"
#include "core/logging.h"
#include "core/screenmanager.h"
#include "core/windowtrackingservice.h"
#include "core/zone.h"

namespace PlasmaZones {

namespace {
// Safety timeout for pending initial window orders that never arrive via D-Bus.
// If windows fail to open (e.g., app crash during startup), this prevents
// m_pendingInitialOrders from leaking state indefinitely.
constexpr int PendingOrderTimeoutMs = 10000;
} // namespace

AutotileEngine::AutotileEngine(LayoutManager* layoutManager, WindowTrackingService* windowTracker,
                               ScreenManager* screenManager, QObject* parent)
    : QObject(parent)
    , m_layoutManager(layoutManager)
    , m_windowTracker(windowTracker)
    , m_screenManager(screenManager)
    , m_config(std::make_unique<AutotileConfig>())
    , m_configResolver(std::make_unique<PerScreenConfigResolver>(this))
    , m_navigation(std::make_unique<NavigationController>(this))
    , m_settingsBridge(std::make_unique<SettingsBridge>(this))
    , m_algorithmId(AlgorithmRegistry::defaultAlgorithmId())
{
    connectSignals();
}

AutotileEngine::~AutotileEngine() = default;

// ═══════════════════════════════════════════════════════════════════════════════
// Signal connections
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileEngine::connectSignals()
{
    // Window tracking signals
    // Primary window events (open/close/focus) are received via public methods:
    // windowOpened(), windowClosed(), windowFocused() - connected by Daemon to
    // WindowTrackingAdaptor signals. This connection also handles zone changes:
    if (m_windowTracker) {
        // Use windowZoneChanged as a proxy until dedicated signals are added
        connect(m_windowTracker, &WindowTrackingService::windowZoneChanged, this,
                [this](const QString& windowId, const QString& zoneId) {
                    if (m_retiling)
                        return; // Ignore zone changes during retile
                    if (zoneId.isEmpty()) {
                        // Don't remove floating windows — clearing their zone assignment
                        // (e.g., by an external D-Bus caller or legacy code path) would
                        // cause onWindowRemoved to drop the window from autotile. Since
                        // floating windows are still managed by autotile, skip removal.
                        // Note: windowClosed() calls onWindowRemoved() directly and
                        // bypasses this guard, so closed floating windows are cleaned up.
                        // Only check current desktop/activity states — a window
                        // floating on desktop 1 should not block removal on desktop 2.
                        for (auto it = m_screenStates.constBegin(); it != m_screenStates.constEnd(); ++it) {
                            if (it.key().desktop != m_currentDesktop || it.key().activity != m_currentActivity) {
                                continue;
                            }
                            if (it.value() && it.value()->isFloating(windowId)) {
                                return;
                            }
                        }
                        onWindowRemoved(windowId);
                    }
                    // Non-empty zoneId for already-tracked windows: no action needed,
                    // the zone assignment is handled by the retile path.
                    // Untracked windows (snap-mode zone assignments from SnapEngine)
                    // are intentionally ignored — autotile windows are always added via
                    // windowOpened() which stores m_windowToStateKey before onWindowAdded.
                });
    }

    // Screen geometry changes
    if (m_screenManager) {
        connect(m_screenManager, &ScreenManager::availableGeometryChanged, this, [this](QScreen* screen, const QRect&) {
            if (screen) {
                onScreenGeometryChanged(Utils::screenIdentifier(screen));
            }
        });
    }

    // Layout changes — intentionally NOT connected.
    // Autotile screens are managed by per-screen assignments, not the global
    // active layout. Retile is triggered by setAutotileScreens() and
    // onScreenGeometryChanged() instead.
    // if (m_layoutManager) {
    //     connect(m_layoutManager, &LayoutManager::activeLayoutChanged,
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

void AutotileEngine::swapInDirection(const QString& direction, const QString& action)
{
    swapFocusedInDirection(direction, action);
}

void AutotileEngine::rotateWindows(bool clockwise, const QString& /*screenId*/)
{
    // AutotileEngine operates on the active screen internally
    rotateWindowOrder(clockwise);
}

void AutotileEngine::moveToPosition(const QString& /*windowId*/, int position, const QString& /*screenId*/)
{
    // NOTE: Currently operates on the focused window regardless of windowId.
    // The autotile engine tracks windows by focus, not by ID. This is a known
    // limitation of the IWindowEngine interface contract for autotiling.
    moveFocusedToPosition(position);
}

void AutotileEngine::setCurrentDesktop(int desktop)
{
    if (desktop == m_currentDesktop) {
        return;
    }
    qCInfo(lcAutotile) << "Switching autotile context: desktop" << m_currentDesktop << "->" << desktop;
    // Only flag as desktop switch if we already had a valid context (desktop > 0).
    // Initial setup (desktop 0 → 1 during daemon startup) is NOT a switch — the
    // effect must receive the normal enabledChanged/autotileScreensChanged sequence
    // to initialize window tracking. Without this, login with autotile enabled
    // suppresses enabledChanged and the effect treats the first autotileScreensChanged
    // as a "desktop return", skipping window notification to the daemon entirely.
    // Use |= so that a prior setCurrentActivity() flag is not lost when both
    // desktop AND activity change simultaneously (e.g., activity-per-desktop).
    m_isDesktopContextSwitch |= (m_currentDesktop > 0);
    m_currentDesktop = desktop;
}

void AutotileEngine::setCurrentActivity(const QString& activity)
{
    if (activity == m_currentActivity) {
        return;
    }
    qCInfo(lcAutotile) << "Switching autotile context: activity" << m_currentActivity << "->" << activity;
    // Only flag as desktop/activity switch if we already had a valid context.
    // Initial setup (activity "" → real during daemon startup) is NOT a switch.
    // Use |= so that a prior setCurrentDesktop() flag is not lost when both
    // desktop AND activity change simultaneously.
    m_isDesktopContextSwitch |= !m_currentActivity.isEmpty();
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

        const TilingState* state = stateIt.value();
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
                // the TilingState actually lives).
                m_screenDesktopOverride[screenId] = key.desktop;
                qCInfo(lcAutotile) << "Pinning screen" << screenId << "to desktop" << key.desktop << "(all"
                                   << (tiled.size() + floating.size()) << "windows sticky)";
            }
        } else {
            if (m_screenDesktopOverride.contains(screenId)) {
                int pinnedDesktop = m_screenDesktopOverride.take(screenId);
                qCInfo(lcAutotile) << "Unpinning screen" << screenId << "from desktop" << pinnedDesktop;

                // Migrate TilingState from pinned key to current desktop key
                if (pinnedDesktop != m_currentDesktop) {
                    TilingStateKey oldKey{screenId, pinnedDesktop, m_currentActivity};
                    TilingStateKey newKey{screenId, m_currentDesktop, m_currentActivity};

                    auto oldIt = m_screenStates.find(oldKey);
                    if (oldIt != m_screenStates.end()) {
                        // If a state already exists at the target key (e.g., created
                        // by stateForScreen() during a transient lookup), delete it —
                        // the pinned state has the actual windows.
                        auto existingIt = m_screenStates.find(newKey);
                        if (existingIt != m_screenStates.end()) {
                            existingIt.value()->deleteLater();
                            m_screenStates.erase(existingIt);
                        }
                        TilingState* migratedState = oldIt.value();
                        m_screenStates.erase(oldIt);
                        m_screenStates.insert(newKey, migratedState);

                        // Update window-to-key mapping
                        for (auto wit = m_windowToStateKey.begin(); wit != m_windowToStateKey.end(); ++wit) {
                            if (wit.value() == oldKey) {
                                wit.value() = newKey;
                            }
                        }

                        // Migrate saved floating windows
                        auto floatIt = m_savedFloatingWindows.find(oldKey);
                        if (floatIt != m_savedFloatingWindows.end()) {
                            m_savedFloatingWindows[newKey] = floatIt.value();
                            m_savedFloatingWindows.erase(floatIt);
                        }

                        qCInfo(lcAutotile) << "Migrated screen" << screenId << "state from desktop" << pinnedDesktop
                                           << "to" << m_currentDesktop;
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
        m_isDesktopContextSwitch = false;
        return;
    }

    const bool wasEnabled = !m_autotileScreens.isEmpty();
    const QSet<QString> added = screens - m_autotileScreens;
    const QSet<QString> removed = m_autotileScreens - screens;

    m_autotileScreens = screens;

    // R1 fix: Retile newly-added screens without requiring pre-existing state.
    // stateForScreen() creates the TilingState lazily, so windows that arrive
    // shortly after (via KWin effect re-notification) have a state ready.
    for (const QString& screenId : added) {
        stateForScreen(screenId);
        // Skip retile if windows are expected to arrive shortly (pending initial
        // order from seedAutotileOrderForScreen). The KWin effect sends windowOpened
        // D-Bus calls after receiving autotileScreensChanged, and each insertWindow
        // schedules its own retile. Retiling an empty screen here produces a wasted
        // empty windowsTiled signal + stagger generation increment, which can interfere
        // with the first real retile's animation timing.
        // For screen hotplug (no pending order), windows are already in the TilingState
        // and the retile is needed to reflow them on the new screen.
        //
        // Skip retile when pending initial order exists (windows arriving shortly
        // via D-Bus). For desktop return with existing tiled windows, still retile
        // to ensure geometry is up-to-date (screen geometry may have changed while
        // on another desktop, e.g., panel added/removed). The effect-side borderless
        // re-application handles the visual state; the retile ensures positions match
        // the current screen geometry.
        if (!m_pendingInitialOrders.contains(screenId)) {
            scheduleRetileForScreen(screenId);
        }
    }

    // Only prune states for the CURRENT desktop/activity. States belonging to
    // other desktops are preserved so desktop switching is a fast state swap
    // (no window release/re-add). windowsReleasedFromTiling MUST NOT fire
    // for desktop/activity transitions — only for true autotile disable.
    QStringList releasedWindows;
    QMutableHashIterator<TilingStateKey, TilingState*> it(m_screenStates);
    while (it.hasNext()) {
        it.next();
        const TilingStateKey& key = it.key();
        // Only prune states that match the current desktop/activity AND whose
        // screen is no longer in the autotile set. States for other desktops
        // are left untouched — they'll be pruned when that desktop is current.
        if (key.desktop != m_currentDesktop || key.activity != m_currentActivity) {
            continue;
        }
        if (!removed.contains(key.screenId)) {
            continue;
        }
        // Save user-floated windows so they stay floating when autotile is re-enabled.
        // Exclude overflow windows — they were auto-floated by maxWindows cap and
        // should tile normally when autotile is re-enabled.
        QSet<QString> screenOverflow = m_overflow.takeForScreen(key.screenId);
        const QStringList floated = it.value()->floatingWindows();
        for (const QString& fid : floated) {
            if (!screenOverflow.contains(fid)) {
                m_savedFloatingWindows[key].insert(fid);
            }
        }
        releasedWindows.append(it.value()->tiledWindows());
        releasedWindows.append(it.value()->floatingWindows());
        m_configResolver->removeOverridesForScreen(key.screenId);
        m_pendingInitialOrders.remove(key.screenId);
        it.value()->deleteLater();
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
        Q_EMIT windowsReleasedFromTiling(releasedWindows);
    }

    // Clean up any remaining overflow entries for removed screens.
    m_overflow.clearForRemovedScreens(m_autotileScreens);

    // Clear desktop overrides for removed screens
    for (const QString& screenId : removed) {
        m_screenDesktopOverride.remove(screenId);
    }

    // Clear any pending deferred retiles for removed screens
    for (auto pit = m_pendingRetileScreens.begin(); pit != m_pendingRetileScreens.end();) {
        if (!m_autotileScreens.contains(*pit)) {
            pit = m_pendingRetileScreens.erase(pit);
        } else {
            ++pit;
        }
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
    // Validate algorithm exists
    auto* registry = AlgorithmRegistry::instance();
    QString newId = algorithmId;

    if (!registry->hasAlgorithm(newId)) {
        qCWarning(lcAutotile) << "AutotileEngine: unknown algorithm" << newId << "- using default";
        newId = AlgorithmRegistry::defaultAlgorithmId();
    }

    if (m_algorithmId == newId) {
        return;
    }

    TilingAlgorithm* oldAlgo = registry->algorithm(m_algorithmId);
    TilingAlgorithm* newAlgo = registry->algorithm(newId);
    const int oldMaxWindows = m_config->maxWindows;

    // Save current algorithm's ratio + master count before switching.
    // Only save after the first setAlgorithm() call has completed, to avoid
    // persisting uninitialised struct defaults from the constructor.
    if (m_algorithmEverSet && oldAlgo) {
        m_config->savedAlgorithmSettings[m_algorithmId] = {m_config->splitRatio, m_config->masterCount};
    }

    // Look up saved settings AFTER the save above — insertion may rehash the
    // QHash, invalidating any iterator obtained before the insert.
    auto savedIt = m_config->savedAlgorithmSettings.constFind(newId);

    // Restore per-algorithm split ratio and master count from saved settings,
    // falling back to the algorithm's defaults when no saved entry exists.
    auto restorePerAlgoSettings = [this](TilingAlgorithm* algo, QHash<QString, AlgorithmSettings>::const_iterator it) {
        if (it != m_config->savedAlgorithmSettings.constEnd()) {
            m_config->splitRatio = it->splitRatio;
            m_config->masterCount = it->masterCount;
        } else {
            m_config->splitRatio = algo->defaultSplitRatio();
            m_config->masterCount = AutotileDefaults::DefaultMasterCount;
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

    // Persist ALL changed fields back to settings to avoid desync between
    // the engine's runtime state and the Settings object. Signal-blocked write
    // prevents recursive corruption (daemon settingsChanged → syncFromSettings →
    // setAlgorithm with stale KCM algo).
    m_settingsBridge->syncAlgorithmToSettings(newId, m_config->splitRatio, m_config->maxWindows, oldMaxWindows);

    m_algorithmEverSet = true;
    m_algorithmId = newId;
    m_config->algorithmId = newId;

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

TilingAlgorithm* AutotileEngine::currentAlgorithm() const
{
    return AlgorithmRegistry::instance()->algorithm(m_algorithmId);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Tiling state access
// ═══════════════════════════════════════════════════════════════════════════════

TilingState* AutotileEngine::stateForScreen(const QString& screenId)
{
    // Validate screenId - don't create state for empty name
    if (screenId.isEmpty()) {
        qCWarning(lcAutotile) << "AutotileEngine::stateForScreen: empty screen name";
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
    if (m_screenManager) {
        bool found = Utils::findScreenByIdOrName(screenId) != nullptr;
        if (!found) {
            qCWarning(lcAutotile) << "AutotileEngine::stateForScreen: unknown screen" << screenId;
            return nullptr;
        }
    }

    // Create new state for this screen+desktop+activity with parent ownership
    auto* state = new TilingState(screenId, this);

    // Initialize with config defaults
    state->setMasterCount(m_config->masterCount);
    state->setSplitRatio(m_config->splitRatio);

    m_screenStates.insert(key, state);
    return state;
}

TilingState* AutotileEngine::stateForKey(const TilingStateKey& key)
{
    if (key.screenId.isEmpty()) {
        return nullptr;
    }

    auto it = m_screenStates.find(key);
    if (it != m_screenStates.end()) {
        return it.value();
    }

    // Reject unknown screens (same validation as stateForScreen)
    if (m_screenManager) {
        bool found = Utils::findScreenByIdOrName(key.screenId) != nullptr;
        if (!found) {
            qCWarning(lcAutotile) << "AutotileEngine::stateForKey: unknown screen" << key.screenId;
            return nullptr;
        }
    }

    auto* state = new TilingState(key.screenId, this);
    state->setMasterCount(m_config->masterCount);
    state->setSplitRatio(m_config->splitRatio);
    m_screenStates.insert(key, state);
    return state;
}

void AutotileEngine::pruneStatesForDesktop(int removedDesktop)
{
    QMutableHashIterator<TilingStateKey, TilingState*> it(m_screenStates);
    int pruned = 0;
    while (it.hasNext()) {
        it.next();
        if (it.key().desktop == removedDesktop) {
            it.value()->deleteLater();
            it.remove();
            ++pruned;
        }
    }
    // Also prune saved floating windows for this desktop
    QMutableHashIterator<TilingStateKey, QSet<QString>> fit(m_savedFloatingWindows);
    while (fit.hasNext()) {
        fit.next();
        if (fit.key().desktop == removedDesktop) {
            fit.remove();
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
    if (pruned > 0) {
        qCInfo(lcAutotile) << "Pruned" << pruned << "TilingStates for removed desktop" << removedDesktop;
    }
}

void AutotileEngine::pruneStatesForActivities(const QStringList& validActivities)
{
    const QSet<QString> valid(validActivities.begin(), validActivities.end());
    QMutableHashIterator<TilingStateKey, TilingState*> it(m_screenStates);
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
    QMutableHashIterator<TilingStateKey, QSet<QString>> fit(m_savedFloatingWindows);
    while (fit.hasNext()) {
        fit.next();
        const QString& act = fit.key().activity;
        if (!act.isEmpty() && !valid.contains(act)) {
            fit.remove();
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
        qCInfo(lcAutotile) << "Pruned" << pruned << "TilingStates for removed activities";
    }
}

AutotileConfig* AutotileEngine::config() const noexcept
{
    return m_config.get();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Zone-ordered window transitions
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileEngine::setInitialWindowOrder(const QString& screenId, const QStringList& windowIds)
{
    if (windowIds.isEmpty()) {
        return;
    }
    // Only take effect when the screen's TilingState is empty (no prior windows —
    // including floating — from session restore). Uses windowCount() instead of
    // tiledWindows() to also detect floating-only states.
    TilingState* state = stateForScreen(screenId);
    if (state && state->windowCount() > 0) {
        qCDebug(lcAutotile) << "setInitialWindowOrder: screen" << screenId << "already has" << state->windowCount()
                            << "windows, ignoring pre-seeded order";
        return;
    }
    // Warn (but allow) if overwriting a pending order that hasn't been fully consumed
    if (m_pendingInitialOrders.contains(screenId)) {
        qCWarning(lcAutotile) << "setInitialWindowOrder: overwriting existing pending order for" << screenId;
    }
    m_pendingInitialOrders[screenId] = windowIds;
    uint64_t gen = ++m_pendingOrderGeneration[screenId];
    qCInfo(lcAutotile) << "Pre-seeded window order for screen=" << screenId << "windows=" << windowIds;

    // Safety timeout: clean up if windows never arrive (e.g., app crash during startup).
    // Use a generation counter so that stale timers from overwritten calls become no-ops.
    QTimer::singleShot(PendingOrderTimeoutMs, this, [this, screenId, gen]() {
        if (m_pendingOrderGeneration.value(screenId) != gen) {
            return; // superseded by a newer setInitialWindowOrder call
        }
        if (m_pendingInitialOrders.remove(screenId)) {
            qCWarning(lcAutotile) << "Pending initial order for screen" << screenId << "timed out after"
                                  << PendingOrderTimeoutMs << "ms - cleaning up stale entry";
        }
    });
}

void AutotileEngine::clearSavedFloatingForWindows(const QStringList& windowIds)
{
    for (auto it = m_savedFloatingWindows.begin(); it != m_savedFloatingWindows.end();) {
        for (const QString& id : windowIds) {
            if (it.value().remove(id)) {
                qCDebug(lcAutotile) << "Cleared stale saved-floating state for zone-snapped window" << id;
            }
        }
        if (it.value().isEmpty()) {
            it = m_savedFloatingWindows.erase(it);
        } else {
            ++it;
        }
    }
}

void AutotileEngine::clearAllSavedFloating()
{
    int total = 0;
    for (auto it = m_savedFloatingWindows.constBegin(); it != m_savedFloatingWindows.constEnd(); ++it) {
        total += it.value().size();
    }
    if (total > 0) {
        qCInfo(lcAutotile) << "Clearing all saved floating state -" << total << "windows";
        m_savedFloatingWindows.clear();
    }
}

QStringList AutotileEngine::tiledWindowOrder(const QString& screenId) const
{
    const TilingStateKey key{screenId, m_currentDesktop, m_currentActivity};
    TilingState* state = m_screenStates.value(key);
    if (!state) {
        return {};
    }
    return state->tiledWindows();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Settings synchronization
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileEngine::syncFromSettings(Settings* settings)
{
    m_settingsBridge->syncFromSettings(settings);
}

void AutotileEngine::connectToSettings(Settings* settings)
{
    m_settingsBridge->connectToSettings(settings);
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

int AutotileEngine::effectiveInnerGap(const QString& screenId) const
{
    return m_configResolver->effectiveInnerGap(screenId);
}

int AutotileEngine::effectiveOuterGap(const QString& screenId) const
{
    return m_configResolver->effectiveOuterGap(screenId);
}

EdgeGaps AutotileEngine::effectiveOuterGaps(const QString& screenId) const
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

QString AutotileEngine::effectiveAlgorithmId(const QString& screenId) const
{
    return m_configResolver->effectiveAlgorithmId(screenId);
}

TilingAlgorithm* AutotileEngine::effectiveAlgorithm(const QString& screenId) const
{
    return m_configResolver->effectiveAlgorithm(screenId);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Session Persistence
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileEngine::saveState()
{
    m_settingsBridge->saveState();
}

void AutotileEngine::loadState()
{
    m_settingsBridge->loadState();
}

void AutotileEngine::scheduleRetileForScreen(const QString& screenId)
{
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
            qCInfo(lcAutotile) << "processPendingRetiles: retiling screen" << screenId;
            retileAfterOperation(screenId, true);
        } else {
            qCWarning(lcAutotile) << "processPendingRetiles: skipping screen" << screenId << "isAutotile=" << isAt
                                  << "hasState=" << hasState;
        }
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

void AutotileEngine::swapWindows(const QString& windowId1, const QString& windowId2)
{
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
        qCWarning(lcAutotile) << "AutotileEngine::swapWindows: window not found";
        return;
    }

    if (screen1 != screen2) {
        qCWarning(lcAutotile) << "AutotileEngine::swapWindows: windows on different screens";
        return;
    }

    // Use the stored key — not stateForScreen(currentContext) — to avoid
    // wrong-desktop lookups from stale D-Bus calls after a context switch.
    TilingState* state = m_screenStates.value(key1);
    if (!state) {
        return;
    }

    const bool swapped = state->swapWindowsById(windowId1, windowId2);
    retileAfterOperation(screen1, swapped);
}

void AutotileEngine::promoteToMaster(const QString& windowId)
{
    QString screenId;
    TilingState* state = stateForWindow(windowId, &screenId);
    if (!state) {
        return;
    }

    const bool promoted = state->moveToTiledPosition(windowId, 0);
    retileAfterOperation(screenId, promoted);
}

void AutotileEngine::demoteFromMaster(const QString& windowId)
{
    QString screenId;
    TilingState* state = stateForWindow(windowId, &screenId);
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

void AutotileEngine::setFocusedWindow(const QString& windowId)
{
    onWindowFocused(windowId);
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
    m_navigation->increaseMasterCount();
}

void AutotileEngine::decreaseMasterCount()
{
    m_navigation->decreaseMasterCount();
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
    TilingState* state = nullptr;

    if (!m_activeScreen.isEmpty() && m_screenStates.contains(currentKeyForScreen(m_activeScreen))) {
        TilingState* s = m_screenStates.value(currentKeyForScreen(m_activeScreen));
        if (s && !s->focusedWindow().isEmpty()) {
            screenId = m_activeScreen;
            state = s;
        }
    }
    if (!state) {
        for (auto it = m_screenStates.constBegin(); it != m_screenStates.constEnd(); ++it) {
            if (it.value() && !it.value()->focusedWindow().isEmpty() && it.key().desktop == m_currentDesktop
                && it.key().activity == m_currentActivity) {
                screenId = it.key().screenId;
                state = it.value();
                break;
            }
        }
    }

    if (!state) {
        qCWarning(lcAutotile) << "toggleFocusedWindowFloat: no state found for focused screen"
                              << "- m_activeScreen=" << m_activeScreen;
        Q_EMIT navigationFeedbackRequested(false, QStringLiteral("float"), QStringLiteral("no_focused_screen"),
                                           QString(), QString(), m_activeScreen);
        return;
    }

    const QString focused = state->focusedWindow();
    if (focused.isEmpty()) {
        qCWarning(lcAutotile) << "toggleFocusedWindowFloat: no focused window on screen" << screenId;
        Q_EMIT navigationFeedbackRequested(false, QStringLiteral("float"), QStringLiteral("no_focused_window"),
                                           QString(), QString(), screenId);
        return;
    }

    performToggleFloat(state, focused, screenId);
}

void AutotileEngine::toggleWindowFloat(const QString& windowId, const QString& screenId)
{
    if (!warnIfEmptyWindowId(windowId, "toggleWindowFloat")) {
        return;
    }

    if (screenId.isEmpty()) {
        qCWarning(lcAutotile) << "toggleWindowFloat: empty screenId for window" << windowId;
        Q_EMIT navigationFeedbackRequested(false, QStringLiteral("float"), QStringLiteral("no_screen"), QString(),
                                           QString(), QString());
        return;
    }

    // Try the given screen first
    QString resolvedScreen = screenId;
    TilingState* state = nullptr;

    if (isAutotileScreen(screenId)) {
        state = stateForScreen(screenId);
        if (state && !state->containsWindow(windowId)) {
            state = nullptr; // Window not on this screen
        }
    }

    // Cross-screen fallback: the window may have been moved (e.g., pre-autotile
    // geometry restore put it on a different screen). Search current desktop/activity
    // states only — states for other desktops should not be considered.
    if (!state) {
        for (auto it = m_screenStates.constBegin(); it != m_screenStates.constEnd(); ++it) {
            if (it.key().desktop != m_currentDesktop || it.key().activity != m_currentActivity) {
                continue;
            }
            if (it.value() && it.value()->containsWindow(windowId)) {
                state = it.value();
                resolvedScreen = it.key().screenId;
                qCInfo(lcAutotile) << "toggleWindowFloat: window" << windowId << "found on screen" << resolvedScreen
                                   << "(caller reported" << screenId << ")";
                break;
            }
        }
    }

    if (!state) {
        qCWarning(lcAutotile) << "toggleWindowFloat: window" << windowId << "not found in any autotile state";
        Q_EMIT navigationFeedbackRequested(false, QStringLiteral("float"), QStringLiteral("window_not_tracked"),
                                           QString(), QString(), screenId);
        return;
    }

    performToggleFloat(state, windowId, resolvedScreen);
}

void AutotileEngine::performToggleFloat(TilingState* state, const QString& windowId, const QString& screenId)
{
    state->toggleFloating(windowId);
    m_overflow.clearOverflow(windowId); // User explicitly toggled, no longer overflow
    retileAfterOperation(screenId, true);

    const bool isNowFloating = state->isFloating(windowId);
    qCInfo(lcAutotile) << "Window" << windowId << (isNowFloating ? "now floating" : "now tiled") << "on screen"
                       << screenId;
    Q_EMIT windowFloatingChanged(windowId, isNowFloating, screenId);
}

void AutotileEngine::setWindowFloat(const QString& windowId, bool shouldFloat)
{
    if (!warnIfEmptyWindowId(windowId, shouldFloat ? "floatWindow" : "unfloatWindow")) {
        return;
    }

    // floatWindow checks autotile screen membership; unfloatWindow does not
    // (window might be on a screen that was removed from autotile after it was floated)
    if (shouldFloat && !isAutotileScreen(m_windowToStateKey.value(windowId).screenId)) {
        return;
    }

    TilingState* state = stateForWindow(windowId);
    if (!state) {
        qCDebug(lcAutotile) << (shouldFloat ? "floatWindow" : "unfloatWindow") << "- window not tracked=" << windowId;
        return;
    }

    if (state->isFloating(windowId) == shouldFloat) {
        qCDebug(lcAutotile) << (shouldFloat ? "floatWindow: already floating" : "unfloatWindow: not floating") << "-"
                            << windowId;
        return;
    }

    state->setFloating(windowId, shouldFloat);
    m_overflow.clearOverflow(windowId);
    const QString screenId = m_windowToStateKey.value(windowId).screenId;
    retileAfterOperation(screenId, true);

    qCInfo(lcAutotile) << "Window" << (shouldFloat ? "floated from" : "unfloated to") << "autotile -" << windowId;
    Q_EMIT windowFloatingChanged(windowId, shouldFloat, screenId);
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

void AutotileEngine::windowOpened(const QString& windowId, const QString& screenId, int minWidth, int minHeight)
{
    if (!warnIfEmptyWindowId(windowId, "windowOpened")) {
        return;
    }

    // Store window minimum size from KWin (used by enforceWindowMinSizes)
    if (minWidth > 0 || minHeight > 0) {
        m_windowMinSizes[windowId] = QSize(qMax(0, minWidth), qMax(0, minHeight));
        qCDebug(lcAutotile) << "Stored min size for" << windowId << "-" << minWidth << "x" << minHeight;
    }

    // Store screen mapping so onWindowAdded uses correct screen
    if (!screenId.isEmpty()) {
        m_windowToStateKey[windowId] = currentKeyForScreen(screenId);
    }
    onWindowAdded(windowId);
}

void AutotileEngine::windowMinSizeUpdated(const QString& windowId, int minWidth, int minHeight)
{
    if (!warnIfEmptyWindowId(windowId, "windowMinSizeUpdated")) {
        return;
    }

    const QSize newMin(qMax(0, minWidth), qMax(0, minHeight));
    const QSize oldMin = m_windowMinSizes.value(windowId, QSize(0, 0));

    if (newMin == oldMin) {
        return; // No change
    }

    if (newMin.width() > 0 || newMin.height() > 0) {
        m_windowMinSizes[windowId] = newMin;
    } else {
        m_windowMinSizes.remove(windowId);
    }

    qCDebug(lcAutotile) << "Updated min size for" << windowId << "-" << oldMin << "->" << newMin;

    // Retile the screen this window is on
    const auto stateKey = m_windowToStateKey.value(windowId);
    const QString screenId = stateKey.screenId;
    if (!screenId.isEmpty() && m_screenStates.contains(stateKey)) {
        scheduleRetileForScreen(screenId);
    }
}

void AutotileEngine::windowClosed(const QString& windowId)
{
    if (!warnIfEmptyWindowId(windowId, "windowClosed")) {
        return;
    }

    // Clean up saved floating state even if window isn't currently tracked
    // (it may have been floating when autotile was disabled on its screen)
    for (auto it = m_savedFloatingWindows.begin(); it != m_savedFloatingWindows.end();) {
        it.value().remove(windowId);
        if (it.value().isEmpty()) {
            it = m_savedFloatingWindows.erase(it);
        } else {
            ++it;
        }
    }

    onWindowRemoved(windowId);
}

void AutotileEngine::windowFocused(const QString& windowId, const QString& screenId)
{
    if (!warnIfEmptyWindowId(windowId, "windowFocused")) {
        return;
    }

    // Detect cross-screen moves. When a window's focus moves to a different
    // screen, migrate its TilingState membership so m_windowToStateKey and the
    // TilingState remain consistent. This handles both overflow-floated windows
    // and windows that were previously migrated (preventing the Screen1->2->1
    // rapid-migration desync where the second hop was silently skipped).
    //
    // Only update m_windowToStateKey for windows already tracked via windowOpened().
    // The KWin effect sends focus events for ALL handleable windows (including
    // transients and non-tileable windows that pass shouldHandleWindow but fail
    // isTileableWindow). Creating entries for these phantom windows causes
    // backfillWindows() to insert them on algorithm switches, inflating the
    // tiled window count.
    const TilingStateKey oldKey = m_windowToStateKey.value(windowId);
    const QString oldScreen = oldKey.screenId;
    if (!screenId.isEmpty() && m_windowToStateKey.contains(windowId)) {
        m_windowToStateKey[windowId] = currentKeyForScreen(screenId);
    }

    if (!oldScreen.isEmpty() && !screenId.isEmpty() && oldScreen != screenId) {
        TilingState* oldState = m_screenStates.value(oldKey);
        if (oldState && oldState->containsWindow(windowId)) {
            oldState->removeWindow(windowId);
            m_overflow.migrateWindow(windowId);
            qCInfo(lcAutotile) << "Window" << windowId << "moved from" << oldScreen << "to" << screenId
                               << "- migrating";
            // Re-add to the new screen's normal flow (will be overflow-checked on next retile)
            onWindowAdded(windowId);
        }
    }

    onWindowFocused(windowId);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Private slot event handlers
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileEngine::onWindowAdded(const QString& windowId)
{
    const QString screenId = screenForWindow(windowId);
    if (!isAutotileScreen(screenId) || !shouldTileWindow(windowId)) {
        return;
    }

    TilingState* state = stateForScreen(screenId);
    const int maxWin = effectiveMaxWindows(screenId);
    if (state && state->tiledWindowCount() >= maxWin) {
        qCDebug(lcAutotile) << "Max window limit reached for screen" << screenId << "(max=" << maxWin << ")";
        // Purge this window from pending initial orders so the order doesn't
        // leak waiting for a window that will never be inserted.
        for (auto pit = m_pendingInitialOrders.begin(); pit != m_pendingInitialOrders.end(); ++pit) {
            pit.value().removeAll(windowId);
        }
        return;
    }

    const bool inserted = insertWindow(windowId, screenId);

    // Sync floating state to daemon. Float state is per-mode:
    // - Restored as floating from autotile's saved set → notify daemon to set WTS floating
    // - Inserted as tiled but WTS says floating (stale snap-mode float) → clear WTS floating
    if (inserted && state) {
        if (state->isFloating(windowId)) {
            Q_EMIT windowFloatingChanged(windowId, true, screenId);
        } else if (m_windowTracker && m_windowTracker->isWindowFloating(windowId)) {
            Q_EMIT windowFloatingChanged(windowId, false, screenId);
        }
    }

    if (inserted && m_config && m_config->focusNewWindows) {
        // Defer focus until after applyTiling emits windowsTiled. The KWin effect's
        // onComplete raises windows in tiling order; emitting focus before retile
        // causes the raise loop to bury the new window behind existing ones.
        m_pendingFocusWindowId = windowId;
    }

    if (inserted) {
        scheduleRetileForScreen(screenId);
    }
}

void AutotileEngine::onWindowRemoved(const QString& windowId)
{
    const QString screenId = m_windowToStateKey.value(windowId).screenId;
    if (screenId.isEmpty()) {
        return;
    }

    removeWindow(windowId);
    // Retile immediately (not deferred like onWindowAdded). Removals need instant
    // layout recalculation to avoid visible holes. Unlike additions, removals don't
    // arrive in bursts, so coalescing provides no benefit.
    retileAfterOperation(screenId, true);
}

void AutotileEngine::onWindowFocused(const QString& windowId)
{
    TilingState* state = stateForWindow(windowId);
    if (!state) {
        // Not an error — non-autotiled windows (dialogs, floating, etc.) report
        // focus changes too, so this is the normal case for most window activations
        qCDebug(lcAutotile) << "onWindowFocused: window not tracked" << windowId;
        return;
    }

    // Track which screen has the active focus (used by tiledWindowsForFocusedScreen
    // to avoid non-deterministic QHash iteration when multiple screens have focused windows)
    m_activeScreen = m_windowToStateKey.value(windowId).screenId;

    state->setFocusedWindow(windowId);
}

void AutotileEngine::onScreenGeometryChanged(const QString& screenId)
{
    if (!isAutotileScreen(screenId) || !m_screenStates.contains(currentKeyForScreen(screenId))) {
        return;
    }

    retileAfterOperation(screenId, true);
}

void AutotileEngine::onLayoutChanged(Layout* layout)
{
    Q_UNUSED(layout)
    // Autotile screens are managed by per-screen assignments, not the global
    // active layout. Retile is triggered by setAutotileScreens() and
    // onScreenGeometryChanged() instead.
}

// ═══════════════════════════════════════════════════════════════════════════════
// Internal implementation
// ═══════════════════════════════════════════════════════════════════════════════

bool AutotileEngine::insertWindow(const QString& windowId, const QString& screenId)
{
    TilingState* state = stateForScreen(screenId);
    if (!state) {
        qCWarning(lcAutotile) << "AutotileEngine::insertWindow: failed to get state for screen" << screenId;
        return false;
    }

    // Check if window already tracked in this screen's tiling state
    // Note: We check the TilingState (not m_windowToStateKey) because windowOpened()
    // stores the screen mapping in m_windowToStateKey *before* calling onWindowAdded(),
    // so m_windowToStateKey.contains() would always be true via that path.
    if (state->containsWindow(windowId)) {
        return false;
    }

    // Check if this window has a pre-seeded position from zone-ordered transition.
    // Take a value copy of the pending list — the erase below invalidates iterators/refs.
    bool inserted = false;
    auto pendingIt = m_pendingInitialOrders.find(screenId);
    if (pendingIt != m_pendingInitialOrders.end()) {
        const QStringList pendingOrder = pendingIt.value(); // copy, not reference (BUG-1 fix)
        int desiredPos = pendingOrder.indexOf(windowId);
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
            state->addWindow(windowId, insertAt);
            inserted = true;
            qCDebug(lcAutotile) << "Inserted pre-seeded window" << windowId << "at position=" << insertAt
                                << "desired=" << desiredPos;
        }
        // Clean up pending order when all pre-seeded windows have been inserted (or closed)
        if (inserted) {
            cleanupPendingOrderIfResolved(screenId);
        }
    }

    if (!inserted) {
        // Insert based on config preference
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

    // Restore floating state from engine's saved set (populated when autotile was
    // previously deactivated). Float state is per-mode: snap-mode floats don't
    // carry into autotile and vice versa. Only m_savedFloatingWindows (the
    // engine's own memory, keyed by screen+desktop+activity) is authoritative.
    const TilingStateKey stateKey = currentKeyForScreen(screenId);
    auto savedIt = m_savedFloatingWindows.find(stateKey);
    if (savedIt != m_savedFloatingWindows.end() && savedIt.value().remove(windowId)) {
        state->setFloating(windowId, true);
        qCInfo(lcAutotile) << "Restored saved floating state for window" << windowId << "on screen" << screenId;
        if (savedIt.value().isEmpty()) {
            m_savedFloatingWindows.erase(savedIt);
        }
    }

    m_windowToStateKey.insert(windowId, stateKey);
    return true;
}

void AutotileEngine::removeWindow(const QString& windowId)
{
    m_windowMinSizes.remove(windowId);
    m_overflow.clearOverflow(windowId);
    const TilingStateKey key = m_windowToStateKey.take(windowId);
    if (key.screenId.isEmpty()) {
        return;
    }

    TilingState* state = m_screenStates.value(key);
    if (state) {
        state->removeWindow(windowId);
    }

    // Clean up saved floating state for closed windows
    for (auto it = m_savedFloatingWindows.begin(); it != m_savedFloatingWindows.end();) {
        it.value().remove(windowId);
        if (it.value().isEmpty()) {
            it = m_savedFloatingWindows.erase(it);
        } else {
            ++it;
        }
    }

    // Purge closed window from pending initial orders.
    // If a pre-seeded window closes before arriving at the autotile engine,
    // the pending order would leak indefinitely without this cleanup.
    for (auto pit = m_pendingInitialOrders.begin(); pit != m_pendingInitialOrders.end();) {
        pit.value().removeAll(windowId);
        if (pit.value().isEmpty()) {
            pit = m_pendingInitialOrders.erase(pit);
        } else {
            const QString screen = pit.key();
            ++pit; // advance before potential erase by helper
            cleanupPendingOrderIfResolved(screen);
        }
    }
}

void AutotileEngine::recalculateLayout(const QString& screenId)
{
    if (screenId.isEmpty()) {
        qCWarning(lcAutotile) << "AutotileEngine::recalculateLayout: empty screen name";
        return;
    }

    TilingState* state = stateForScreen(screenId);
    if (!state) {
        return;
    }

    TilingAlgorithm* algo = effectiveAlgorithm(screenId);
    if (!algo) {
        qCWarning(lcAutotile) << "AutotileEngine::recalculateLayout: no algorithm set";
        return;
    }

    const int tiledCount = state->tiledWindowCount();
    if (tiledCount == 0) {
        state->setCalculatedZones({}); // Clear zones when no windows
        return;
    }

    // Cap to user's max windows setting — excess windows are not tiled
    const int windowCount = std::min(tiledCount, effectiveMaxWindows(screenId));

    const QRect screen = screenGeometry(screenId);
    if (!screen.isValid()) {
        qCWarning(lcAutotile) << "AutotileEngine::recalculateLayout: invalid screen geometry";
        return;
    }

    const QString algoId = effectiveAlgorithmId(screenId);

    qCDebug(lcAutotile) << "recalculateLayout: screen=" << screenId << "geometry=" << screen
                        << "windows=" << windowCount << "algo=" << algoId;

    // Calculate zone geometries using the algorithm, with gap-aware zones.
    // Algorithms apply gaps directly using their topology knowledge, eliminating
    // the fragile post-processing step that previously guessed adjacency.
    const bool skipGaps = effectiveSmartGaps(screenId) && windowCount == 1;
    const int innerGap = skipGaps ? 0 : effectiveInnerGap(screenId);
    EdgeGaps outerGaps = skipGaps ? EdgeGaps::uniform(0) : effectiveOuterGaps(screenId);

    // Build minSizes vector for the algorithm (when respectMinimumSize is enabled)
    // Only include the first windowCount windows (capped by maxWindows above)
    QVector<QSize> minSizes;
    if (effectiveRespectMinimumSize(screenId)) {
        const QStringList windows = state->tiledWindows();
        // KWin reports min size in logical pixels (same as QScreen/zone geometry);
        // do not divide by devicePixelRatio or we under-report and steal too little.
        minSizes.resize(windowCount, QSize(0, 0));
        for (int i = 0; i < windowCount && i < windows.size(); ++i) {
            minSizes[i] = m_windowMinSizes.value(windows[i], QSize(0, 0));
        }
    }

    // Let memory-based algorithms prepare their state (e.g., lazily create a SplitTree)
    // before calculateZones(). Virtual dispatch avoids concrete type casts here.
    algo->prepareTilingState(state);

    // Pass minSizes to algorithm so it can incorporate them directly into zone
    // calculations using its topology knowledge (split tree, column structure, etc.)
    QVector<QRect> zones = algo->calculateZones({windowCount, screen, state, innerGap, outerGaps, minSizes});

    // Validate algorithm returned correct number of zones
    if (zones.size() != windowCount) {
        qCWarning(lcAutotile) << "AutotileEngine::recalculateLayout: algorithm returned" << zones.size() << "zones for"
                              << windowCount << "windows";
        return;
    }

    // Lightweight safety net: the algorithm handles min sizes directly, but
    // enforceWindowMinSizes catches any residual deficits from rounding or
    // edge cases the algorithm couldn't fully solve (e.g., unsatisfiable constraints).
    // Skip for overlapping algorithms (Monocle, Cascade, Stair): zones intentionally
    // overlap and removeZoneOverlaps would destroy the intended layout.
    if (effectiveRespectMinimumSize(screenId) && !minSizes.isEmpty() && !algo->producesOverlappingZones()) {
        const int threshold = effectiveInnerGap(screenId) + qMax(AutotileDefaults::GapEdgeThresholdPx, 12);
        GeometryUtils::enforceWindowMinSizes(zones, minSizes, threshold, innerGap);
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
}

void AutotileEngine::applyTiling(const QString& screenId)
{
    TilingState* state = stateForScreen(screenId);
    if (!state) {
        return;
    }

    const QStringList windows = state->tiledWindows();
    const QVector<QRect> zones = state->calculatedZones();

    // zones.size() may be less than windows.size() when maxWindows caps the layout.
    // Only the first zones.size() windows receive tiled geometries; the rest are untouched.
    if (zones.isEmpty()) {
        qCDebug(lcAutotile) << "AutotileEngine::applyTiling: no zones calculated for screen" << screenId;
        return;
    }
    if (zones.size() > windows.size()) {
        qCWarning(lcAutotile) << "AutotileEngine::applyTiling: zone count exceeds window count" << windows.size()
                              << "vs" << zones.size();
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
    // Requires >= 2 zones: a single window is just normal tiling, not monocle.
    // This intentionally catches degenerate narrow-screen fallbacks too.
    const bool allZonesIdentical = tileCount >= 2 && std::all_of(zones.begin() + 1, zones.end(), [&](const QRect& z) {
                                       return z == zones[0];
                                   });
    QJsonArray arr;
    for (int i = 0; i < tileCount; ++i) {
        const QRect& geo = zones[i];
        QJsonObject obj;
        obj[QLatin1String("windowId")] = windows[i];
        obj[QLatin1String("x")] = geo.x();
        obj[QLatin1String("y")] = geo.y();
        obj[QLatin1String("width")] = geo.width();
        obj[QLatin1String("height")] = geo.height();
        // Flag monocle entries so the effect can set KWin maximize state,
        // which makes Plasma panels recognize the window and unfloat.
        if (allZonesIdentical) {
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

    Q_EMIT windowsTiled(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));

    // Emit deferred focus AFTER windowsTiled so KWin processes tiles first
    // (including the onComplete raise loop), then focuses the new window on top.
    if (!m_pendingFocusWindowId.isEmpty()) {
        Q_EMIT focusWindowRequested(m_pendingFocusWindowId);
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

bool AutotileEngine::shouldTileWindow(const QString& windowId) const
{
    if (windowId.isEmpty()) {
        return false;
    }

    // Check if window is floating in any screen's TilingState
    // (floating windows are excluded from autotiling).
    // Only check states for the current desktop/activity.
    for (const QString& screen : m_autotileScreens) {
        const TilingStateKey key = currentKeyForScreen(screen);
        auto it = m_screenStates.constFind(key);
        if (it != m_screenStates.constEnd() && it.value() && it.value()->isFloating(windowId)) {
            qCDebug(lcAutotile) << "Window" << windowId << "is floating, skipping tile";
            return false;
        }
    }

    // Note: Other exclusions (special windows, dialogs, fullscreen, etc.)
    // are already handled by KWin effect's shouldHandleWindow() before
    // sending window events to daemon.

    return true;
}

QString AutotileEngine::screenForWindow(const QString& windowId) const
{
    // Check if already tracked
    auto it = m_windowToStateKey.constFind(windowId);
    if (it != m_windowToStateKey.constEnd()) {
        return it->screenId;
    }

    // R6 fix: Warn when falling back to primary screen — this may indicate a
    // missing screen name in windowOpened() or a stale m_windowToStateKey entry.
    if (m_screenManager && m_screenManager->primaryScreen()) {
        qCWarning(lcAutotile) << "screenForWindow: window" << windowId
                              << "not in m_windowToStateKey, falling back to primary screen";
        return Utils::screenIdentifier(m_screenManager->primaryScreen());
    }

    qCWarning(lcAutotile) << "screenForWindow: no screen found for window" << windowId;
    return QString();
}

QRect AutotileEngine::screenGeometry(const QString& screenId) const
{
    if (!m_screenManager) {
        return QRect();
    }

    QScreen* screen = Utils::findScreenByIdOrName(screenId);
    if (!screen) {
        return QRect();
    }

    return ScreenManager::actualAvailableGeometry(screen);
}

void AutotileEngine::resetMaxWindowsForAlgorithmSwitch(TilingAlgorithm* oldAlgo, TilingAlgorithm* newAlgo)
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
    for (auto it = m_screenStates.constBegin(); it != m_screenStates.constEnd(); ++it) {
        if (it.key().desktop != m_currentDesktop || it.key().activity != m_currentActivity) {
            continue;
        }
        if (it.value() && !hasPerScreenOverride(it.key().screenId, QLatin1String("SplitRatio"))) {
            it.value()->setSplitRatio(m_config->splitRatio);
        }
    }
}

void AutotileEngine::propagateGlobalMasterCount()
{
    // Only propagate to current desktop/activity states — per-desktop master
    // count adjustments are preserved on other desktops.
    for (auto it = m_screenStates.constBegin(); it != m_screenStates.constEnd(); ++it) {
        if (it.key().desktop != m_currentDesktop || it.key().activity != m_currentActivity) {
            continue;
        }
        if (it.value() && !hasPerScreenOverride(it.key().screenId, QLatin1String("MasterCount"))) {
            it.value()->setMasterCount(m_config->masterCount);
        }
    }
}

void AutotileEngine::backfillWindows()
{
    for (const QString& screenId : m_autotileScreens) {
        // Overflow recovery is NOT done here — it is handled by retileScreen()
        // which defers signal emission until after the full retile cycle.
        // Doing it here would emit windowFloatingChanged synchronously before
        // the deferred retile fires, creating a feedback loop where the KWin
        // effect processes float state changes mid-transition.

        TilingState* state = stateForScreen(screenId);
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
            if (it.value().screenId == screenId && it.value().desktop == m_currentDesktop
                && it.value().activity == m_currentActivity && !state->containsWindow(it.key())
                && shouldTileWindow(it.key())) {
                candidates.append(it.key());
            }
        }
        for (const QString& windowId : candidates) {
            insertWindow(windowId, screenId);
            if (state->tiledWindowCount() >= maxWin) {
                break;
            }
        }
    }
}

void AutotileEngine::retileScreen(const QString& screenId)
{
    TilingState* state = stateForScreen(screenId);
    if (!state) {
        return;
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
        }
    }

    // Step 2-3: Recalculate layout and apply tiling (applyTiling also handles
    // new overflow detection and collects overflow signals internally).
    recalculateLayout(screenId);
    applyTiling(screenId);

    // Step 4: Emit all deferred signals after state is fully consistent.
    // Recovery signals first (unfloated windows), then overflow signals
    // (newly floated windows) were already handled inside applyTiling's
    // batch emit, and tilingChanged is emitted last.
    for (const QString& wid : unfloated) {
        Q_EMIT windowFloatingChanged(wid, false, screenId);
    }
    Q_EMIT tilingChanged(screenId);
}

void AutotileEngine::retileAfterOperation(const QString& screenId, bool operationSucceeded)
{
    if (!operationSucceeded) {
        return; // No change, no signal
    }

    if (!isAutotileScreen(screenId)) {
        return;
    }

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
        qCWarning(lcAutotile) << operation << "called with empty windowId";
        return false;
    }
    return true;
}

bool AutotileEngine::cleanupPendingOrderIfResolved(const QString& screenId)
{
    auto pit = m_pendingInitialOrders.find(screenId);
    if (pit == m_pendingInitialOrders.end()) {
        return false;
    }

    TilingState* state = stateForScreen(screenId);
    if (!state) {
        return false;
    }

    for (const QString& pendingWin : std::as_const(pit.value())) {
        if (!state->containsWindow(pendingWin)) {
            return false;
        }
    }

    qCDebug(lcAutotile) << "All pre-seeded windows resolved for screen" << screenId;
    m_pendingInitialOrders.erase(pit);
    return true;
}

TilingState* AutotileEngine::stateForWindow(const QString& windowId, QString* outScreenId)
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
    gap = std::clamp(gap, AutotileDefaults::MinGap, AutotileDefaults::MaxGap);
    if (m_config && m_config->innerGap != gap) {
        m_config->innerGap = gap;
        retile(QString());
    }
}

void AutotileEngine::setOuterGap(int gap)
{
    gap = std::clamp(gap, AutotileDefaults::MinGap, AutotileDefaults::MaxGap);
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

} // namespace PlasmaZones
