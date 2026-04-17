// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Qt headers
#include <algorithm>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QScopeGuard>
#include <QScreen>
#include <QTimer>

// Project headers
#include "AutotileEngine.h"
#include <PhosphorTiles/AlgorithmRegistry.h>
#include "core/geometryutils.h"
#include "core/utils.h"
#include "AutotileConfig.h"
#include "NavigationController.h"
#include "PerScreenConfigResolver.h"
#include "SettingsBridge.h"
#include <PhosphorTiles/TilingAlgorithm.h>
#include "config/settings.h"
// DwindleMemoryAlgorithm.h no longer needed — prepareTilingState() is virtual on PhosphorTiles::TilingAlgorithm
#include <PhosphorTiles/TilingState.h>
#include "core/constants.h"
#include <PhosphorTiles/AutotileConstants.h>
#include <PhosphorZones/Layout.h>
#include "core/layoutmanager.h"
#include "core/logging.h"
#include "core/screenmanager.h"
#include "core/virtualscreen.h"
#include "core/windowregistry.h"
#include "core/windowtrackingservice.h"
#include <PhosphorZones/Zone.h>

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
    , m_algorithmId(PhosphorTiles::AlgorithmRegistry::defaultAlgorithmId())
{
    // In production (Daemon::start) all three dependencies are non-null.
    // Headless unit tests deliberately pass nullptr to construct an engine
    // with minimal parents for testing peripheral classes (adaptors, bridges,
    // sub-controllers) — every method that dereferences a dependency guards
    // it locally. Do not Q_ASSERT here.

    // Zero-delay timer to coalesce promoteSavedWindowOrders() calls during
    // simultaneous desktop+activity switches. Fires on the next event loop
    // pass after both m_currentDesktop and m_currentActivity are updated.
    m_promoteOrdersTimer.setSingleShot(true);
    m_promoteOrdersTimer.setInterval(0);
    connect(&m_promoteOrdersTimer, &QTimer::timeout, this, &AutotileEngine::promoteSavedWindowOrders);

    // Bounded retry timer for transient screen geometry failures.
    // When QScreen is unavailable during desktop switch, retileScreen defers
    // to this timer rather than silently dropping the retile.
    m_retileRetryTimer.setSingleShot(true);
    m_retileRetryTimer.setInterval(RetileRetryIntervalMs);
    connect(&m_retileRetryTimer, &QTimer::timeout, this, &AutotileEngine::processRetileRetries);

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
            QPointer<QScreen> guardedScreen = screen;
            if (!guardedScreen) {
                return;
            }
            const QString physId = Utils::screenIdentifier(guardedScreen);
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
        connect(m_screenManager, &ScreenManager::virtualScreensChanged, this, [this](const QString& physicalScreenId) {
            const QStringList newVsIds = m_screenManager->virtualScreenIdsFor(physicalScreenId);
            const QSet<QString> newVsSet(newVsIds.begin(), newVsIds.end());

            // Find and release orphaned virtual screen states for this physical screen
            QStringList releasedWindows;
            QSet<QString> orphanedVsIds;
            QMutableHashIterator<TilingStateKey, PhosphorTiles::TilingState*> it(m_screenStates);
            while (it.hasNext()) {
                it.next();
                const QString& sid = it.key().screenId;
                if (!VirtualScreenId::isVirtual(sid)) {
                    continue;
                }
                if (VirtualScreenId::extractPhysicalId(sid) != physicalScreenId) {
                    continue;
                }
                if (newVsSet.contains(sid)) {
                    continue;
                }
                // This virtual screen no longer exists — release its windows.
                // Save user-floated windows (excluding overflow) so they stay
                // floating if autotile is re-enabled on a new VS config — mirrors
                // the preservation logic in setAutotileScreens.
                orphanedVsIds.insert(sid);
                QSet<QString> screenOverflow = m_overflow.takeForScreen(sid);
                const QStringList floated = it.value()->floatingWindows();
                for (const QString& fid : floated) {
                    if (!screenOverflow.contains(fid)) {
                        m_savedFloatingWindows[it.key()].insert(fid);
                    }
                }
                releasedWindows.append(it.value()->tiledWindows());
                releasedWindows.append(floated);
                m_pendingInitialOrders.remove(sid);
                m_pendingOrderGeneration.remove(sid);
                it.value()->deleteLater();
                it.remove();
            }
            for (const QString& windowId : std::as_const(releasedWindows)) {
                m_windowToStateKey.remove(windowId);
            }
            if (!releasedWindows.isEmpty()) {
                Q_EMIT windowsReleasedFromTiling(releasedWindows, orphanedVsIds);
            }

            // Clean up per-screen autotile settings for removed virtual screens.
            // Orphaned AutotileScreen: groups would otherwise accumulate indefinitely
            // as virtual screen IDs are never reused after reconfiguration.
            if (!orphanedVsIds.isEmpty()) {
                if (Settings* settings = m_settingsBridge->settings()) {
                    for (const QString& orphanId : std::as_const(orphanedVsIds)) {
                        settings->clearPerScreenAutotileSettings(orphanId);
                    }
                }
            }

            // Clean up desktop overrides for removed virtual screens on this physical screen.
            // Use newVsSet (freshly-computed from ScreenManager) rather than m_autotileScreens
            // which reflects mode assignments and may not yet be updated for the new config.
            auto overrideIt = m_screenDesktopOverride.begin();
            while (overrideIt != m_screenDesktopOverride.end()) {
                if (VirtualScreenId::isVirtual(overrideIt.key())
                    && VirtualScreenId::extractPhysicalId(overrideIt.key()) == physicalScreenId
                    && !newVsSet.contains(overrideIt.key()))
                    overrideIt = m_screenDesktopOverride.erase(overrideIt);
                else
                    ++overrideIt;
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
        connect(m_screenManager, &ScreenManager::virtualScreenRegionsChanged, this,
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

bool AutotileEngine::isWindowTiled(const QString& windowId) const
{
    auto it = m_windowToStateKey.constFind(windowId);
    if (it == m_windowToStateKey.constEnd()) {
        return false;
    }
    const PhosphorTiles::TilingState* state = m_screenStates.value(it.value());
    return state && !state->isFloating(windowId);
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
        qCWarning(lcAutotile) << "moveToPosition: window" << windowId << "not found in any tiling state";
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
    schedulePromoteSavedWindowOrders();
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
    schedulePromoteSavedWindowOrders();
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
                qCInfo(lcAutotile) << "Pinning screen" << screenId << "to desktop" << key.desktop << "(all"
                                   << (tiled.size() + floating.size()) << "windows sticky)";
            }
        } else {
            if (m_screenDesktopOverride.contains(screenId)) {
                int pinnedDesktop = m_screenDesktopOverride.take(screenId);
                qCInfo(lcAutotile) << "Unpinning screen" << screenId << "from desktop" << pinnedDesktop;

                // Migrate PhosphorTiles::TilingState from pinned key to current desktop key
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
                        PhosphorTiles::TilingState* migratedState = oldIt.value();
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
    // stateForScreen() creates the PhosphorTiles::TilingState lazily, so windows that arrive
    // shortly after (via KWin effect re-notification) have a state ready.
    for (const QString& screenId : added) {
        stateForScreen(screenId);
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
        if (m_pendingInitialOrders.contains(screenId)) {
            // Pending initial order exists — the windows are already known (seeded
            // from zone-ordered window list during mode toggle). Consume the order
            // now and insert windows into the PhosphorTiles::TilingState so the retile has something
            // to work with.  Previously we skipped the retile here expecting the
            // KWin effect to re-send windowOpened D-Bus calls, but during a per-screen
            // mode toggle the windows are already open — they never arrive via D-Bus.
            const QStringList order = m_pendingInitialOrders.take(screenId);
            m_pendingOrderGeneration.remove(screenId);
            PhosphorTiles::TilingState* ts = stateForScreen(screenId);
            if (ts) {
                const TilingStateKey key = currentKeyForScreen(screenId);
                auto savedIt = m_savedFloatingWindows.find(key);
                for (const QString& windowId : order) {
                    if (!ts->containsWindow(windowId)) {
                        ts->addWindow(windowId);
                        // Restore floating state from saved set (populated by
                        // deserializeWindowOrders). Without this, windows added
                        // from pending orders lose their floating state because
                        // windowOpened's floating restore is skipped when the
                        // window already exists in the PhosphorTiles::TilingState.
                        if (savedIt != m_savedFloatingWindows.end() && savedIt.value().remove(windowId)) {
                            ts->setFloating(windowId, true);
                        }
                    }
                }
                // Only erase the saved-floating entry once it's empty. Floating
                // IDs for windows NOT in this pending order (e.g. session restore
                // where setInitialWindowOrder narrowed the order to snap-zone
                // members) must remain so windowOpened/insertWindow can restore
                // them when KWin notifies the daemon about those windows.
                if (savedIt != m_savedFloatingWindows.end() && savedIt.value().isEmpty()) {
                    m_savedFloatingWindows.erase(savedIt);
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
        m_pendingOrderGeneration.remove(key.screenId);
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
        Q_EMIT windowsReleasedFromTiling(releasedWindows, removed);
    }

    // Clean up any remaining overflow entries for removed screens.
    m_overflow.clearForRemovedScreens(m_autotileScreens);

    // Clear desktop overrides for removed screens
    for (const QString& screenId : removed) {
        m_screenDesktopOverride.remove(screenId);
    }

    // Clear pending restore entries for removed screens. Stale entries would
    // restore windows to positions from the old layout if autotile is re-enabled.
    if (!m_pendingAutotileRestores.isEmpty() && !removed.isEmpty()) {
        const auto keys = m_pendingAutotileRestores.keys();
        for (const QString& appId : keys) {
            pruneStaleRestores(appId);
        }
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
    // Validate algorithm exists
    auto* registry = PhosphorTiles::AlgorithmRegistry::instance();
    QString newId = algorithmId;

    if (!registry->hasAlgorithm(newId)) {
        qCWarning(lcAutotile) << "AutotileEngine: unknown algorithm" << newId << "- using default";
        newId = PhosphorTiles::AlgorithmRegistry::defaultAlgorithmId();
    }

    if (m_algorithmId == newId) {
        return;
    }

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
            m_config->masterCount = ConfigDefaults::autotileMasterCount();
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

PhosphorTiles::TilingAlgorithm* AutotileEngine::currentAlgorithm() const
{
    return PhosphorTiles::AlgorithmRegistry::instance()->algorithm(m_algorithmId);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Tiling state access
// ═══════════════════════════════════════════════════════════════════════════════

PhosphorTiles::TilingState* AutotileEngine::stateForScreen(const QString& screenId)
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
    if (!isKnownScreen(screenId)) {
        qCWarning(lcAutotile) << "AutotileEngine::stateForScreen: unknown screen" << screenId;
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

    // Reject unknown screens (same validation as stateForScreen)
    if (!isKnownScreen(key.screenId)) {
        qCWarning(lcAutotile) << "AutotileEngine::stateForKey: unknown screen" << key.screenId;
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
    PhosphorTiles::TilingState* state = stateForScreen(screenId);
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
            m_pendingOrderGeneration.remove(screenId);
            qCWarning(lcAutotile) << "Pending initial order for screen" << screenId << "timed out after"
                                  << PendingOrderTimeoutMs << "ms - cleaning up stale entry";
        }
    });
}

void AutotileEngine::clearSavedFloatingForWindows(const QStringList& windowIds)
{
    for (const QString& raw : windowIds) {
        removeSavedFloatingEntry(canonicalizeWindowId(raw));
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

void AutotileEngine::removeSavedFloatingEntry(const QString& windowId)
{
    for (auto it = m_savedFloatingWindows.begin(); it != m_savedFloatingWindows.end();) {
        it.value().remove(windowId);
        if (it.value().isEmpty()) {
            it = m_savedFloatingWindows.erase(it);
        } else {
            ++it;
        }
    }
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

void AutotileEngine::updatePerScreenOverride(const QString& screenId, const QString& key, const QVariant& value)
{
    m_configResolver->updatePerScreenOverride(screenId, key, value);
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

QJsonArray AutotileEngine::serializeWindowOrders() const
{
    return m_settingsBridge->serializeWindowOrders();
}

void AutotileEngine::deserializeWindowOrders(const QJsonArray& orders)
{
    m_settingsBridge->deserializeWindowOrders(orders);
}

QJsonObject AutotileEngine::serializePendingRestores() const
{
    return m_settingsBridge->serializePendingRestores();
}

void AutotileEngine::deserializePendingRestores(const QJsonObject& obj)
{
    m_settingsBridge->deserializePendingRestores(obj);
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

void AutotileEngine::scheduleRetileRetry(const QString& screenId)
{
    int& count = m_retileRetryCount[screenId];
    if (count >= MaxRetileRetries) {
        qCWarning(lcAutotile) << "scheduleRetileRetry: exhausted" << MaxRetileRetries << "retries for screen"
                              << screenId << "- screen geometry may be permanently unavailable";
        m_retileRetryCount.remove(screenId);
        m_retileRetryScreens.remove(screenId);
        return;
    }
    ++count;
    m_retileRetryScreens.insert(screenId);
    qCInfo(lcAutotile) << "scheduleRetileRetry: attempt" << count << "/" << MaxRetileRetries << "for screen" << screenId
                       << "in" << RetileRetryIntervalMs << "ms";
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
        qCInfo(lcAutotile) << "processRetileRetries: retrying screen" << screenId;
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
        qCWarning(lcAutotile) << "AutotileEngine::swapWindows: window not found";
        return;
    }

    if (screen1 != screen2) {
        qCWarning(lcAutotile) << "AutotileEngine::swapWindows: windows on different screens";
        return;
    }

    // Use the stored key — not stateForScreen(currentContext) — to avoid
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

void AutotileEngine::syncShortcutAdjustmentToSettings()
{
    // Update per-algorithm saved settings so algorithm switches preserve the value
    if (!m_algorithmId.isEmpty()) {
        auto& entry = m_config->savedAlgorithmSettings[m_algorithmId];
        entry.splitRatio = m_config->splitRatio;
        entry.masterCount = m_config->masterCount;
    }

    // Write to Settings (signal-blocked) so syncFromSettings() won't revert
    if (m_settingsBridge) {
        m_settingsBridge->syncShortcutAdjustment(m_config->splitRatio, m_config->masterCount);
    }
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
        qCWarning(lcAutotile) << "toggleWindowFloat: empty screenId for window" << windowId;
        Q_EMIT navigationFeedbackRequested(false, QStringLiteral("float"), QStringLiteral("no_screen"), QString(),
                                           QString(), QString());
        return;
    }

    // Try the given screen first
    QString resolvedScreen = screenId;
    PhosphorTiles::TilingState* state = nullptr;

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
        // Window not tracked by autotile — if it's floating (daemon-side) on an autotile
        // screen, adopt it into the tiling layout. This handles the flow:
        // float on snap screen → move to autotile screen → toggle float to tile.
        // The windowFloating flag is checked via the callback to avoid coupling to WTS.
        if (isAutotileScreen(screenId) && m_isWindowFloatingFn && m_isWindowFloatingFn(windowId)) {
            state = stateForScreen(screenId);
            if (state && !state->containsWindow(windowId)) {
                state->addWindow(windowId);
                state->setFloating(windowId, true);
                m_windowToStateKey[windowId] = currentKeyForScreen(screenId);
                resolvedScreen = screenId;
                qCInfo(lcAutotile) << "toggleWindowFloat: adopted floating window" << windowId << "into autotile on"
                                   << screenId;
                // Fall through to performToggleFloat which will unfloat it
            }
        }
        if (!state) {
            qCWarning(lcAutotile) << "toggleWindowFloat: window" << windowId << "not found in any autotile state";
            Q_EMIT navigationFeedbackRequested(false, QStringLiteral("float"), QStringLiteral("window_not_tracked"),
                                               QString(), QString(), screenId);
            return;
        }
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
    qCInfo(lcAutotile) << "Window" << windowId << (isNowFloating ? "now floating" : "now tiled") << "on screen"
                       << screenId;
    Q_EMIT windowFloatingChanged(windowId, isNowFloating, screenId);
}

void AutotileEngine::adoptWindowAsFloating(const QString& windowId, const QString& screenId)
{
    if (windowId.isEmpty() || screenId.isEmpty() || !isAutotileScreen(screenId)) {
        return;
    }
    // Already tracked — nothing to adopt
    if (m_windowToStateKey.contains(windowId)) {
        return;
    }
    PhosphorTiles::TilingState* state = stateForScreen(screenId);
    if (!state || state->containsWindow(windowId)) {
        return;
    }
    state->addWindow(windowId);
    state->setFloating(windowId, true);
    m_windowToStateKey[windowId] = currentKeyForScreen(screenId);
    qCInfo(lcAutotile) << "adoptWindowAsFloating:" << windowId << "on" << screenId;
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

    // Clear cached min-size when unfloating so the next retile starts fresh.
    // The window's minimum size may have changed while floating/minimized
    // (e.g. browser finished loading media, terminal resized). Stale min-sizes
    // can override the user's split ratio by inflating enforceWindowMinSizes
    // constraints. The centering code in the KWin effect will re-discover and
    // report the actual min-size if the window can't fill its assigned zone.
    if (!shouldFloat) {
        const bool hadMinSize = m_windowMinSizes.contains(windowId);
        const QSize clearedMinSize = m_windowMinSizes.value(windowId, QSize(0, 0));
        m_windowMinSizes.remove(windowId);
        if (hadMinSize) {
            qCDebug(lcAutotile) << "unfloat: cleared stale minSize=" << clearedMinSize << "for" << windowId;
        }
    }

    const QString screenId = m_windowToStateKey.value(windowId).screenId;
    retileAfterOperation(screenId, true);

    qCInfo(lcAutotile) << "Window" << (shouldFloat ? "floated from" : "unfloated to") << "autotile -" << windowId;
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

    qCInfo(lcAutotile) << "windowOpened:" << windowId << "screen=" << screenId << "minSize=" << minWidth << "x"
                       << minHeight;

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
                qCInfo(lcAutotile) << "windowOpened: removed" << windowId << "from old screen" << oldKey.screenId
                                   << "before adding to" << screenId;
                scheduleRetileForScreen(oldKey.screenId);
            }
        }
        m_windowToStateKey[windowId] = newKey;
    }

    // Store window minimum size from KWin (used by enforceWindowMinSizes)
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

    qCDebug(lcAutotile) << "windowMinSizeUpdated:" << windowId << "minSize=" << minWidth << "x" << minHeight;

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
        qCInfo(lcAutotile) << "storeWindowMinSize:" << windowId << "min=" << newMin << "old=" << oldMin;
    } else {
        m_windowMinSizes.remove(windowId);
    }

    if (Q_UNLIKELY(lcAutotile().isDebugEnabled()) && !screenId.isEmpty()) {
        const QRect screen = screenGeometry(screenId);
        if (screen.isValid()) {
            qCDebug(lcAutotile) << "storeWindowMinSize: cap="
                                << static_cast<int>(screen.width() * PhosphorTiles::AutotileDefaults::MaxSplitRatio)
                                << "x"
                                << static_cast<int>(screen.height() * PhosphorTiles::AutotileDefaults::MaxSplitRatio)
                                << "screen=" << screen.size();
        }
    }
    return true;
}

void AutotileEngine::windowClosed(const QString& rawWindowId)
{
    if (!warnIfEmptyWindowId(rawWindowId, "windowClosed")) {
        return;
    }
    const QString windowId = canonicalizeWindowId(rawWindowId);

    // Drag-insert preview bookkeeping: if the closed window is involved in
    // an active preview (as either the dragged window or the evicted
    // neighbour), we must react before onWindowRemoved tears down state.
    // Leaving a stale evictedWindowId would later drive setFloating or
    // windowsBatchFloated on a dead window id.
    if (m_dragInsertPreview) {
        if (m_dragInsertPreview->windowId == windowId) {
            // Dragged window closed mid-preview — drop the preview entirely.
            // Cannot "restore" or "commit" a gone window; clear and move on.
            m_dragInsertPreview.reset();
        } else if (m_dragInsertPreview->evictedWindowId == windowId) {
            // Evicted neighbour closed mid-preview — forget the eviction so
            // commit/cancel don't try to operate on it.
            m_dragInsertPreview->evictedWindowId.clear();
        }
    }

    // Clean up saved floating state even if window isn't currently tracked
    // (it may have been floating when autotile was disabled on its screen).
    // This must happen before onWindowRemoved because removeWindow() early-returns
    // when the window isn't in m_windowToStateKey (not tracked in any PhosphorTiles::TilingState).
    removeSavedFloatingEntry(windowId);

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
    const TilingStateKey oldKey = m_windowToStateKey.value(windowId);
    const QString oldScreen = oldKey.screenId;
    if (!screenId.isEmpty() && m_windowToStateKey.contains(windowId)) {
        if (isAutotileScreen(screenId)) {
            m_windowToStateKey[windowId] = currentKeyForScreen(screenId);
        } else {
            // Window moved to a non-autotile screen — remove tracking entirely.
            // Leaving a stale entry pointing at a snap screen causes phantom
            // lookups and prevents clean re-entry if the window returns.
            m_windowToStateKey.remove(windowId);
        }
    }

    if (!oldScreen.isEmpty() && !screenId.isEmpty() && oldScreen != screenId) {
        PhosphorTiles::TilingState* oldState = m_screenStates.value(oldKey);
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
        qCDebug(lcAutotile) << "onWindowAdded: skipping" << windowId << "screen=" << screenId
                            << "isAutotile=" << isAutotileScreen(screenId);
        return;
    }

    PhosphorTiles::TilingState* state = stateForScreen(screenId);
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
    //
    // Use windowFloatingStateSynced (not windowFloatingChanged): this is a
    // passive state-sync on window insertion, not a user float toggle. The
    // daemon must NOT restore pre-tile geometry here — the window was just
    // added (e.g. dropped onto an autotile VS from a snap VS) and already
    // has a valid position. Routing through windowFloatingChanged causes
    // syncAutotileFloatState to call applyGeometryForFloat, which teleports
    // the window to a cross-screen-adjusted rect and resizes it.
    if (inserted && state) {
        if (state->isFloating(windowId)) {
            Q_EMIT windowFloatingStateSynced(windowId, true, screenId);
        } else if (m_windowTracker && m_windowTracker->isWindowFloating(windowId)) {
            Q_EMIT windowFloatingStateSynced(windowId, false, screenId);
        }
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

void AutotileEngine::onWindowRemoved(const QString& windowId)
{
    const QString screenId = m_windowToStateKey.value(windowId).screenId;
    if (screenId.isEmpty()) {
        return;
    }

    qCInfo(lcAutotile) << "onWindowRemoved:" << windowId << "screen=" << screenId;

    // Notify algorithm via lifecycle hook before removal
    PhosphorTiles::TilingState* state = stateForScreen(screenId);
    PhosphorTiles::TilingAlgorithm* algo = effectiveAlgorithm(screenId);
    if (algo && algo->supportsLifecycleHooks() && state) {
        const int idx = state->tiledWindows().indexOf(windowId);
        if (idx >= 0) {
            algo->onWindowRemoved(state, idx);
        } else {
            qCDebug(lcAutotile) << "onWindowRemoved: window" << windowId
                                << "not found in tiling state — lifecycle hook skipped";
        }
    }

    removeWindow(windowId);
    // Retile immediately (not deferred like onWindowAdded). Removals need instant
    // layout recalculation to avoid visible holes. Unlike additions, removals don't
    // arrive in bursts, so coalescing provides no benefit.
    retileAfterOperation(screenId, true);
}

void AutotileEngine::onWindowFocused(const QString& windowId)
{
    PhosphorTiles::TilingState* state = stateForWindow(windowId);
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

    qCInfo(lcAutotile) << "onScreenGeometryChanged:" << screenId << "geometry=" << screenGeometry(screenId);

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

bool AutotileEngine::insertWindow(const QString& windowId, const QString& screenId)
{
    PhosphorTiles::TilingState* state = stateForScreen(screenId);
    if (!state) {
        qCWarning(lcAutotile) << "AutotileEngine::insertWindow: failed to get state for screen" << screenId;
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
                    qCDebug(lcAutotile) << "AppId fallback matched" << windowId << "to pending position" << i;
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

    // Fallback: check pending restore queue (close/reopen restore).
    // When a window was removed from autotile and the same app reopens,
    // restore it to the saved position. FIFO consumption per appId.
    // Note: position is index-based, so if other windows were reordered after
    // the close, the restored position is best-effort (correct index, but
    // neighbours may have changed). This matches snapping's behavior.
    bool restoredFromPendingQueue = false;
    const TilingStateKey currentKey = currentKeyForScreen(screenId);
    if (!inserted && hasStableAppId) {
        auto restoreIt = m_pendingAutotileRestores.find(appId);
        if (restoreIt != m_pendingAutotileRestores.end() && !restoreIt.value().isEmpty()) {
            // Find the first entry matching the current context
            for (int i = 0; i < restoreIt.value().size(); ++i) {
                const PendingAutotileRestore& entry = restoreIt.value().at(i);
                if (entry.context == currentKey) {
                    // Clamp position to current window count (windows may have been
                    // added/removed since the position was saved)
                    const int clampedPos = qMin(entry.position, state->windowCount());
                    state->addWindow(windowId, clampedPos);
                    inserted = true;
                    restoredFromPendingQueue = true;

                    // Note: entry.wasFloating is intentionally NOT restored here.
                    // The pending queue records the float state at the moment the
                    // window was last removed from this autotile context, but a
                    // window arriving via snap→autotile drag (the dominant
                    // consumer of this path) carries the user's *current* drop
                    // intent, which is "tile me here". Restoring a stale
                    // wasFloating=true from a prior session leaves the window
                    // stuck floating on drop, then the user's next Meta+F float
                    // teleports it via applyGeometryForFloat reading a corrupted
                    // pre-tile rect (regression #271). Tile-by-default matches
                    // the historical behavior the user expects from drop-on-
                    // autotile and matches snapping's restore semantics.

                    qCDebug(lcAutotile) << "Restored window" << windowId
                                        << "from pending queue at position=" << clampedPos
                                        << "(saved=" << entry.position << ")";

                    // Consume this entry (FIFO). After removeAt/erase, restoreIt
                    // is potentially invalid — use the safe pruneStaleRestores()
                    // helper below instead of continuing to use it.
                    restoreIt.value().removeAt(i);
                    if (restoreIt.value().isEmpty()) {
                        m_pendingAutotileRestores.erase(restoreIt);
                    }
                    break;
                }
            }

            // Prune entries whose screen is no longer active. Only needed
            // after consuming an entry (the erase above may have invalidated
            // restoreIt, so pruneStaleRestores does a fresh lookup).
            if (restoredFromPendingQueue) {
                pruneStaleRestores(appId);
            }
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
    // Skip when the pending restore queue already handled floating — the queue's
    // wasFloating reflects the state at close time, which is more recent than
    // m_savedFloatingWindows (set at mode-toggle time).
    if (!restoredFromPendingQueue) {
        auto savedIt = m_savedFloatingWindows.find(currentKey);
        if (savedIt != m_savedFloatingWindows.end() && savedIt.value().remove(windowId)) {
            state->setFloating(windowId, true);
            qCInfo(lcAutotile) << "Restored saved floating state for window" << windowId << "on screen" << screenId;
            if (savedIt.value().isEmpty()) {
                m_savedFloatingWindows.erase(savedIt);
            }
        }
    }

    m_windowToStateKey.insert(windowId, currentKey);
    return true;
}

void AutotileEngine::pruneStaleRestores(const QString& appId)
{
    auto it = m_pendingAutotileRestores.find(appId);
    if (it == m_pendingAutotileRestores.end()) {
        return;
    }
    it.value().removeIf([this](const PendingAutotileRestore& r) {
        return !m_autotileScreens.contains(r.context.screenId);
    });
    if (it.value().isEmpty()) {
        m_pendingAutotileRestores.erase(it);
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
        // Save position to pending restore queue before removal.
        // This enables close/reopen restore: when the same app reopens,
        // insertWindow() restores it to the saved position (FIFO per appId).
        const int pos = state->windowOrder().indexOf(windowId);
        if (pos >= 0) {
            // Save under the CURRENT class, not the first-seen one, so a
            // mid-session rename still routes the restore to the right bucket.
            const QString appId = currentAppIdFor(windowId);
            if (!appId.isEmpty() && appId != windowId) {
                PendingAutotileRestore entry(pos, key, state->isFloating(windowId));
                auto& queue = m_pendingAutotileRestores[appId];
                // Cap per-appId queue to prevent unbounded growth from windows
                // that are closed repeatedly without reopening.
                if (queue.size() >= MaxPendingRestoresPerApp) {
                    queue.removeFirst();
                }
                queue.append(entry);
                qCDebug(lcAutotile) << "Saved pending restore for" << appId << "position=" << pos
                                    << "screen=" << key.screenId;
            }
        }
        state->removeWindow(windowId);
    }

    // Clean up saved floating state for closed windows
    removeSavedFloatingEntry(windowId);

    // Purge closed window from pending initial orders.
    // If a pre-seeded window closes before arriving at the autotile engine,
    // the pending order would leak indefinitely without this cleanup.
    for (auto pit = m_pendingInitialOrders.begin(); pit != m_pendingInitialOrders.end();) {
        pit.value().removeAll(windowId);
        if (pit.value().isEmpty()) {
            m_pendingOrderGeneration.remove(pit.key());
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
        qCWarning(lcAutotile) << "AutotileEngine::recalculateLayout: empty screen name";
        return false;
    }

    PhosphorTiles::TilingState* state = stateForScreen(screenId);
    if (!state) {
        return false;
    }

    PhosphorTiles::TilingAlgorithm* algo = effectiveAlgorithm(screenId);
    if (!algo) {
        qCWarning(lcAutotile) << "AutotileEngine::recalculateLayout: no algorithm set";
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
        qCWarning(lcAutotile) << "AutotileEngine::recalculateLayout: invalid screen geometry for" << screenId;
        return false;
    }

    const QString algoId = effectiveAlgorithmId(screenId);

    qCDebug(lcAutotile) << "recalculateLayout: screen=" << screenId << "geometry=" << screen
                        << "windows=" << windowCount << "algo=" << algoId << "splitRatio=" << state->splitRatio();

    // Calculate zone geometries using the algorithm, with gap-aware zones.
    // Algorithms apply gaps directly using their topology knowledge, eliminating
    // the fragile post-processing step that previously guessed adjacency.
    const bool skipGaps = effectiveSmartGaps(screenId) && windowCount == 1;
    const int innerGap = skipGaps ? 0 : effectiveInnerGap(screenId);
    ::PhosphorLayout::EdgeGaps outerGaps =
        skipGaps ? ::PhosphorLayout::EdgeGaps::uniform(0) : effectiveOuterGaps(screenId);

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
        if (Q_UNLIKELY(lcAutotile().isDebugEnabled())) {
            for (int i = 0; i < windowCount && i < windows.size(); ++i) {
                if (minSizes[i].width() > 0 || minSizes[i].height() > 0) {
                    qCDebug(lcAutotile) << "  minSize[" << i << "]:" << windows[i] << "=" << minSizes[i];
                }
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
        QScreen* qscreen = Utils::findScreenByIdOrName(screenId);
        if (qscreen) {
            const QRect geom = qscreen->geometry();
            screenInfo.portrait = geom.height() > geom.width();
            screenInfo.aspectRatio = Utils::screenAspectRatio(qscreen);
        } else if (m_screenManager) {
            // Virtual screen IDs have no QScreen — use ScreenManager geometry
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
    // (e.g., user edited the JS file and renamed/removed a @param).
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
    QVector<QRect> zones = algo->calculateZones(tilingParams);

    qCInfo(lcAutotile) << "recalculateLayout: screen=" << screenId << "tiledCount=" << tiledCount
                       << "windowCount=" << windowCount << "splitRatio=" << state->splitRatio() << "zones=" << zones;

    // Validate algorithm returned correct number of zones
    if (zones.size() != windowCount) {
        qCWarning(lcAutotile) << "AutotileEngine::recalculateLayout: algorithm returned" << zones.size() << "zones for"
                              << windowCount << "windows";
        return false;
    }

    // Lightweight safety net: the algorithm handles min sizes directly, but
    // enforceWindowMinSizes catches any residual deficits from rounding or
    // edge cases the algorithm couldn't fully solve (e.g., unsatisfiable constraints).
    // Skip for overlapping algorithms (Monocle, Cascade, Stair): zones intentionally
    // overlap and removeZoneOverlaps would destroy the intended layout.
    if (effectiveRespectMinimumSize(screenId) && !minSizes.isEmpty() && !algo->producesOverlappingZones()) {
        const int threshold =
            effectiveInnerGap(screenId) + qMax(PhosphorTiles::AutotileDefaults::GapEdgeThresholdPx, 12);
        const QVector<QRect> preEnforceZones = zones;
        GeometryUtils::enforceWindowMinSizes(zones, minSizes, threshold, innerGap);
        if (Q_UNLIKELY(lcAutotile().isDebugEnabled()) && zones != preEnforceZones) {
            qCDebug(lcAutotile) << "enforceWindowMinSizes: zones adjusted"
                                << "before=" << preEnforceZones << "after=" << zones;
        }
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
    PhosphorTiles::TilingState* targetState = stateForScreen(screenId);
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
    PhosphorTiles::TilingState* state = stateForScreen(m_dragInsertPreview->targetScreenId);
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

    PhosphorTiles::TilingState* targetState = stateForScreen(p.targetScreenId);

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
    // Const-correct lookup: avoid stateForScreen() which may create state.
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
    PhosphorTiles::TilingState* state = stateForScreen(screenId);
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

    if (Q_UNLIKELY(lcAutotile().isDebugEnabled())) {
        for (int i = 0; i < tileCount; ++i) {
            qCDebug(lcAutotile) << "  applyTiling:" << windows[i] << "zone=" << zones[i];
        }
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
        Settings* settings = m_settingsBridge ? m_settingsBridge->settings() : nullptr;
        if (settings) {
            auto handling = settings->autotileStickyWindowHandling();
            if (handling == StickyWindowHandling::IgnoreAll || handling == StickyWindowHandling::RestoreOnly) {
                qCDebug(lcAutotile) << "Window" << windowId << "is sticky, handling=" << static_cast<int>(handling)
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
            qCDebug(lcAutotile) << "Window" << windowId << "is floating, skipping tile";
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
    if (m_screenManager && m_screenManager->primaryScreen()) {
        qCWarning(lcAutotile) << "screenForWindow: window" << windowId
                              << "not in m_windowToStateKey, falling back to primary screen";
        // If the primary monitor is subdivided into virtual screens, return
        // the first virtual screen ID instead of the physical ID.
        const QString physId = Utils::screenIdentifier(m_screenManager->primaryScreen());
        const QStringList vsIds = m_screenManager->virtualScreenIdsFor(physId);
        return vsIds.isEmpty() ? physId : vsIds.first();
    }

    qCWarning(lcAutotile) << "screenForWindow: no screen found for window" << windowId;
    return QString();
}

QRect AutotileEngine::screenGeometry(const QString& screenId) const
{
    if (!m_screenManager) {
        return QRect();
    }

    // Virtual screens: use ScreenManager's virtual-aware geometry
    if (VirtualScreenId::isVirtual(screenId)) {
        return m_screenManager->screenAvailableGeometry(screenId);
    }

    // Physical screens: existing behavior
    QScreen* screen = Utils::findScreenByIdOrName(screenId);
    if (!screen) {
        return QRect();
    }

    return ScreenManager::actualAvailableGeometry(screen);
}

bool AutotileEngine::isKnownScreen(const QString& screenId) const
{
    if (!m_screenManager) {
        // Without ScreenManager, skip validation (test environments)
        return true;
    }
    if (VirtualScreenId::isVirtual(screenId)) {
        return m_screenManager->screenGeometry(screenId).isValid();
    }
    return Utils::findScreenByIdOrName(screenId) != nullptr;
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
    for (auto it = m_screenStates.constBegin(); it != m_screenStates.constEnd(); ++it) {
        if (it.key().desktop != m_currentDesktop || it.key().activity != m_currentActivity) {
            continue;
        }
        if (it.value() && !hasPerScreenOverride(it.key().screenId, PerScreenKeys::SplitRatio)) {
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
        if (it.value() && !hasPerScreenOverride(it.key().screenId, PerScreenKeys::MasterCount)) {
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

        PhosphorTiles::TilingState* state = stateForScreen(screenId);
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
    PhosphorTiles::TilingState* state = stateForScreen(screenId);
    if (!state) {
        return;
    }

    // Pre-validate screen geometry BEFORE mutating state (overflow recovery).
    // QScreen can be transiently unavailable during Wayland desktop switches
    // (Plasma rebuilds the output list). If invalid, schedule a bounded retry
    // rather than proceeding — without this, the ratio change / window add
    // that triggered this retile is silently dropped, leaving stale zones.
    //
    // Only gate on geometry when a ScreenManager exists (production). Without
    // one (unit tests), the existing recalculateLayout gracefully handles
    // the empty geometry as a structural no-op, not a transient failure.
    if (m_screenManager) {
        const QRect preValidatedGeometry = screenGeometry(screenId);
        if (!preValidatedGeometry.isValid()) {
            qCWarning(lcAutotile) << "retileScreen: screen geometry transiently invalid for" << screenId
                                  << "- deferring retile";
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
        qCWarning(lcAutotile) << "retileScreen: recalculateLayout failed for" << screenId
                              << "- applying previous zone layout";
    }
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

void AutotileEngine::setWindowRegistry(WindowRegistry* registry)
{
    m_windowRegistry = registry;
    if (!registry) {
        return;
    }
    // Push a live-class resolver onto every existing algorithm so
    // PhosphorTiles::ScriptedAlgorithm lifecycle hooks (buildJsState) can expose the
    // current appId to user JS. The resolver closes over `this` and calls
    // currentAppIdFor, which itself consults the registry and falls back
    // to parsing for legacy composites.
    //
    // Algorithms are shared singletons (one instance per id in the
    // PhosphorTiles::AlgorithmRegistry), so setting the resolver here is global for the
    // daemon. That's fine — there's a single AutotileEngine per daemon.
    auto resolver = [this](const QString& windowId) {
        return currentAppIdFor(windowId);
    };
    auto* algoRegistry = PhosphorTiles::AlgorithmRegistry::instance();
    for (PhosphorTiles::TilingAlgorithm* algo : algoRegistry->allAlgorithms()) {
        if (algo) {
            algo->setAppIdResolver(resolver);
        }
    }
    // Newly-registered (hot-reloaded) scripted algorithms must also pick up
    // the resolver at the moment they appear, otherwise the first
    // post-registration windowAdded hook would see empty class strings.
    connect(algoRegistry, &PhosphorTiles::AlgorithmRegistry::algorithmRegistered, this,
            [this, resolver](const QString& id) {
                auto* reg = PhosphorTiles::AlgorithmRegistry::instance();
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
    const QString instanceId = Utils::extractInstanceId(rawWindowId);
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
    const QString instanceId = Utils::extractInstanceId(anyWindowId);
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
    const QString instanceId = Utils::extractInstanceId(rawWindowId);
    auto it = m_canonicalByInstance.constFind(instanceId);
    return (it != m_canonicalByInstance.constEnd()) ? it.value() : rawWindowId;
}

QString AutotileEngine::currentAppIdFor(const QString& anyWindowId) const
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
    // Fallback: parse the string. Note this returns the FIRST-seen class for
    // canonical ids; accurate only when the window has never renamed.
    return Utils::extractAppId(anyWindowId);
}

bool AutotileEngine::cleanupPendingOrderIfResolved(const QString& screenId)
{
    auto pit = m_pendingInitialOrders.find(screenId);
    if (pit == m_pendingInitialOrders.end()) {
        return false;
    }

    PhosphorTiles::TilingState* state = stateForScreen(screenId);
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
    m_pendingOrderGeneration.remove(screenId);
    return true;
}

void AutotileEngine::schedulePromoteSavedWindowOrders()
{
    if (!m_savedWindowOrders.isEmpty()) {
        m_promoteOrdersTimer.start();
    }
}

void AutotileEngine::promoteSavedWindowOrders()
{
    if (m_savedWindowOrders.isEmpty()) {
        return;
    }

    // Promote saved window orders matching the new desktop/activity context
    // into m_pendingInitialOrders so that windows arriving on this desktop
    // get their saved ordering restored. Replaces any stale pending order
    // from the previous context (e.g., desktop 1's unconsumed pending order
    // is replaced by desktop 2's order when switching to desktop 2).
    for (auto it = m_savedWindowOrders.begin(); it != m_savedWindowOrders.end();) {
        const TilingStateKey& key = it.key();
        const bool matchesContext =
            key.desktop == m_currentDesktop && (key.activity.isEmpty() || key.activity == m_currentActivity);

        if (matchesContext) {
            m_pendingInitialOrders[key.screenId] = it.value();
            qCDebug(lcAutotile) << "Promoted saved window order for screen" << key.screenId << "desktop" << key.desktop
                                << "(" << it.value().size() << "windows)";
            // Erase specific-activity entries (consumed once). Keep wildcard entries
            // (activity="") so they can be re-promoted on future context switches —
            // erasing them here would lose the order for other activities on the same
            // desktop, and cause incorrect results when setCurrentDesktop() and
            // setCurrentActivity() both call this method during a simultaneous switch.
            if (!key.activity.isEmpty()) {
                it = m_savedWindowOrders.erase(it);
            } else {
                ++it;
            }
        } else {
            ++it;
        }
    }
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

} // namespace PlasmaZones
