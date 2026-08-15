// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "daemon/daemon.h"
#include "daemon/overlayservice.h"
#include "daemon/controllers/unifiedlayoutcontroller.h"
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/LayoutComputeService.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>
#include <PhosphorWorkspaces/ActivityManager.h>
#include "core/utils/geometryutils.h"
#include "core/platform/logging.h"
#include "core/types/constants.h"
#include "core/utils/utils.h"
#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorContext/ContextResolver.h>
#include "config/settings.h"
#include "dbus/layoutadaptor/layoutadaptor.h"
#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"
#include <PhosphorEngine/PlacementEngineBase.h>
#include <PhosphorEngine/IPlacementEngine.h>
#include <PhosphorEngine/WindowPlacement.h>
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/TilingAlgorithm.h>
#include <QGuiApplication>
#include <memory>
#include <QScreen>
#include <PhosphorScreens/ScreenIdentity.h>
#include <PhosphorIdentity/VirtualScreenId.h>
#include "seedorderfilter.h"

namespace PlasmaZones {

namespace {
// Follow-up panel geometry requery delay (ms): after the debounced geometry update
// completes, re-query panel geometry once so we pick up settled state (e.g. panel editor close).
// Conceptually distinct from GEOMETRY_UPDATE_DEBOUNCE_MS in daemon.cpp (which coalesces
// rapid geometry change events into a single update).
constexpr int DELAYED_PANEL_REQUERY_MS = 400;
// Reapply requested on next event loop (0); daemon state is already updated when we start the timer.
constexpr int REAPPLY_DELAY_MS = 0;
// Watchdog for the geometry-recalc completion barrier: a screen removed
// (hot-unplug, VS teardown) after its recalc was requested never delivers a
// result, which would leave the barrier's pending set non-empty forever and
// the overlay refresh lost. Generous relative to a worker-thread recalc
// (single-digit ms per layout).
constexpr int COMPUTE_BARRIER_TIMEOUT_MS = 3000;
} // anonymous namespace

bool Daemon::isAnyScreenAutotile() const
{
    if (!m_layoutManager || !m_screenManager) {
        return false;
    }
    const QString activity = currentActivity();
    const QStringList effectiveIds = m_screenManager->effectiveScreenIds();
    for (const QString& screenId : effectiveIds) {
        const QString assignmentId =
            m_layoutManager->assignmentIdForScreen(screenId, currentDesktopForScreen(screenId), activity);
        if (PhosphorLayout::LayoutId::isAutotile(assignmentId)) {
            return true;
        }
    }
    return false;
}

void Daemon::updateAutotileScreens()
{
    // m_algorithmRegistry joins the preamble: the per-screen override loop
    // below derefs it for the algorithm-default MaxWindows injection, and a
    // guard only on its siblings left that deref as the odd one out.
    if (!m_autotileEngine || !m_layoutManager || !m_screenManager || !m_algorithmRegistry) {
        return;
    }
    // Every entry path into this function is wired in init() or later
    // (settingsChanged, layoutAssigned, virtual-screen reconfigure), so
    // the resolver is always live by the time we run. The earlier guard
    // here had a settings-cascade fallback — that path was unreachable
    // and let isContextDisabled stay alive in the daemon as dead code.
    if (!m_contextResolver) {
        return;
    }

    const QString activity = currentActivity();

    QSet<QString> autotileScreens;
    QHash<QString, QString> screenAlgorithms;
    const QStringList effectiveIds = m_screenManager->effectiveScreenIds();
    for (const QString& screenId : effectiveIds) {
        // Per-output virtual desktops (#648): each screen resolves its own desktop.
        const int desktop = currentDesktopForScreen(screenId);
        // Skip screens/desktops/activities where PlasmaZones is disabled.
        // Single cascade path through the resolver — see
        // libs/phosphor-context-resolver/README.md.
        if (m_contextResolver->isDisabled(
                m_contextResolver->handleForMode(screenId, PhosphorZones::AssignmentEntry::Autotile))) {
            continue;
        }
        QString assignmentId = m_layoutManager->assignmentIdForScreen(screenId, desktop, activity);
        if (PhosphorLayout::LayoutId::isAutotile(assignmentId)) {
            const QString algoId = PhosphorLayout::LayoutId::extractAlgorithmId(assignmentId);
            // Bare autotile (mode set, no concrete algorithm — e.g. a mode-only
            // rule or a plain mode swap) draws its algorithm from the global
            // default, which the suppress setting disables. Don't tile such a
            // context when its default is suppressed (globally or by a
            // per-context rule): tiling is active and would rearrange windows
            // with a default the user opted out of. A concrete assigned
            // algorithm is explicit and always tiles.
            if (algoId.isEmpty()
                && m_layoutManager->isDefaultAssignmentSuppressedForContext(screenId, desktop, activity)) {
                continue;
            }
            autotileScreens.insert(screenId);
            if (!algoId.isEmpty()) {
                screenAlgorithms[screenId] = algoId;
            }
        }
    }

    // Capture window order for screens LEAVING autotile before PhosphorTiles::TilingState is destroyed.
    // This preserves the tiling arrangement so re-entering autotile (e.g. cycling back)
    // restores the same window positions. Without this, only the settingsChanged path
    // (handleAutotileDisabled) captured orders — layout cycling lost them.
    const QSet<QString> currentAutotileScreens = m_autotileEngine->activeScreens();
    const QSet<QString> removedScreens = currentAutotileScreens - autotileScreens;
    for (const QString& screenId : removedScreens) {
        // Capture ONLY when the engine is about to destroy a state for this
        // screen's current key. stateForScreen is non-creating and keyed on the
        // engine's own currentKeyForScreen — the very key its teardown predicate
        // matches on (removeStatesIf, autotileengine/context.cpp) — so a null
        // here means nothing is being torn down and there is nothing to save.
        //
        // This is what keeps the read context and the write key in agreement,
        // which the unconditional store below depends on. Without it the
        // per-output desktop-switch path corrupted saved orders: that path sets
        // the engine's desktop for the screen BEFORE calling us, so
        // capturedWindowOrder resolved through the NEW desktop's key (usually
        // empty) and the store filed it under the new desktop, wiping whatever
        // that desktop had saved from an earlier toggle. The capture was never
        // needed there in the first place — a desktop switch is a pure context
        // swap that PRESERVES the departed desktop's TilingState (the engine's
        // setCurrentDesktopForScreen says so, and its prune only removes states
        // whose desktop is the current one), so the departed order is still live
        // in the engine and reappears when the screen returns to that desktop.
        // A genuine toggle-off does destroy the current key's state, and there
        // this lookup is non-null and the capture runs exactly as before.
        if (!m_autotileEngine || !m_autotileEngine->stateForScreen(screenId)) {
            continue;
        }
        // Per-output virtual desktops (#648): each screen resolves its own desktop.
        const int desktop = currentDesktopForScreen(screenId);
        // Stored unconditionally, INCLUDING an empty order: the capture is the
        // authoritative "what was tiled at toggle-off", and an empty one must
        // overwrite a stale non-empty entry from an earlier toggle so re-entry
        // does not resurrect windows that have since closed or left the
        // screen (seedAutotileOrderForScreen falls back to the zone-ordered
        // list when the saved order is empty).
        // Unconditional is safe BECAUSE of the state-existence gate above: it
        // guarantees this key is the one being torn down, so an empty order here
        // genuinely means "nothing was tiled at toggle-off" rather than "we asked
        // the wrong context".
        QStringList order = m_autotileEngine->managedWindowOrder(screenId);
        m_lastAutotileOrders[TilingStateKey{screenId, desktop, activity}] = order;
    }

    // Seed window order for screens ENTERING autotile from saved state.
    // Must happen before setActiveScreens() which retiles added screens.
    const QSet<QString> addedScreens = autotileScreens - currentAutotileScreens;
    for (const QString& screenId : addedScreens) {
        seedAutotileOrderForScreen(screenId);
    }

    // Apply per-screen overrides BEFORE setActiveScreens so that newly added
    // screens are retiled with the correct per-screen algorithm (not the global
    // fallback).  applyPerScreenConfig lazily creates TilingStates via
    // tilingStateForScreen(), which setActiveScreens reuses for added screens.
    if (m_settings) {
        for (const QString& screenId : effectiveIds) {
            if (!autotileScreens.contains(screenId))
                continue;
            // Virtual->physical fallback: a per-screen autotile override stored on
            // a physical monitor must still apply when this screenId is one of its
            // virtual sub-screens.
            QVariantMap overrides = m_settings->getPerScreenAutotileSettings(screenId);
            if (overrides.isEmpty() && PhosphorIdentity::VirtualScreenId::isVirtual(screenId)) {
                overrides = m_settings->getPerScreenAutotileSettings(
                    PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenId));
            }
            // Resolve per-context tiling-parameter RULES up front — the effective
            // overflow behavior gates the algorithm-default MaxWindows injection
            // below (an Unlimited context must not receive a finite injected cap).
            // Same context sources as the disable/assignment checks in the first
            // loop (VirtualDesktopManager + ActivityManager, not the registry's
            // push-updated mirror): mixing sources inside one pass would let a
            // mirror lag resolve a SetMaxWindows/SetSplitRatio rule against a
            // different context than the assignment it layers onto.
            const int ctxDesktop = currentDesktopForScreen(screenId);
            const QString& ctxActivity = activity;
            const PhosphorZones::ContextTilingParams tilingParams =
                m_layoutManager->resolveContextTilingParams(screenId, ctxDesktop, ctxActivity);
            // Effective overflow mirrors effectiveOverflowBehavior's cascade: a
            // SetOverflowBehavior rule wins, else a per-screen config override, else
            // global config. Clamp before the enum compare exactly as the resolver
            // does (qBound), so a corrupt out-of-range stored value can't make the two
            // determinations drift (which would reintroduce the injected-cap defeat).
            const int effectiveOverflow = qBound(
                PhosphorTiles::AutotileDefaults::MinOverflowBehavior,
                tilingParams.overflowBehavior.value_or(overrides.contains(PerScreenKeys::OverflowBehavior)
                                                           ? overrides.value(PerScreenKeys::OverflowBehavior).toInt()
                                                           : m_settings->autotileOverflowBehaviorInt()),
                PhosphorTiles::AutotileDefaults::MaxOverflowBehavior);
            const bool contextUnlimited =
                effectiveOverflow == static_cast<int>(PhosphorTiles::AutotileOverflowBehavior::Unlimited);
            // Inject algorithm from layout assignment (authoritative source)
            if (screenAlgorithms.contains(screenId)) {
                const QString screenAlgo = screenAlgorithms.value(screenId);
                overrides[PerScreenKeys::Algorithm] = screenAlgo;

                // When the per-screen algorithm differs from the engine's
                // current global algorithm and there's no explicit MaxWindows
                // override, inject that algorithm's cap: the user's saved
                // per-algorithm slot when one exists, else the algorithm's own
                // default — the default only if the global maxWindows is still
                // at the global algorithm's default (a customized global is
                // respected).
                //
                // Use the engine's runtime algorithm (m_autotileEngine->algorithm())
                // instead of m_settings->defaultAutotileAlgorithm(). During layout cycling,
                // the settings algorithm retains the initial KCM value while the
                // engine's algorithm changes with each cycle. Using the stale settings
                // value caused incorrect MaxWindows injection and unpredictable
                // per-screen overrides.
                //
                // Note: in applyEntry(), this runs BEFORE setAlgorithm() updates
                // the global ID, so globalAlgo is the OLD algorithm. This is safe:
                // effectiveMaxWindows() has identical fallback logic (its step 3
                // mirrors this cascade) that dynamically derives the correct
                // MaxWindows at retile time even without a per-screen override.
                // The override here is an optimization.
                //
                // Skip the injection entirely when the context is Unlimited: the
                // injected finite default would sit at effectiveMaxWindows step 1,
                // ahead of the Unlimited sentinel at step 2, silently defeating the
                // SetOverflowBehavior=Unlimited request. The injection is only an
                // optimization for the mixed-algorithm case (step 3), which never
                // applies under Unlimited (step 2 returns first). A user's explicit
                // per-screen-config or rule MaxWindows still caps even under Unlimited
                // — those land in `overrides` directly and are untouched here.
                const QString globalAlgo = m_autotileEngine->algorithmId();
                if (screenAlgo != globalAlgo && !overrides.contains(PerScreenKeys::MaxWindows) && !contextUnlimited) {
                    // The user's saved per-algorithm tuning is more specific
                    // than either the global maxWindows or the screen
                    // algorithm's built-in default — mirror effectiveMaxWindows
                    // step 3, which consults the saved slot first. Without
                    // this, the injected built-in default sat at step 1 of
                    // effectiveMaxWindows and pinned the cap over the user's
                    // saved value for the screen's algorithm.
                    if (const auto saved = m_autotileEngine->savedMaxWindowsForAlgorithm(screenAlgo)) {
                        overrides[PerScreenKeys::MaxWindows] = *saved;
                    } else if (auto* screenAlgoPtr = m_algorithmRegistry->algorithm(screenAlgo)) {
                        auto* globalAlgoPtr = m_algorithmRegistry->algorithm(globalAlgo);
                        if (!globalAlgoPtr) {
                            qCDebug(lcDaemon) << "updateAutotileScreens: global algorithm" << globalAlgo
                                              << "not found - injecting per-screen default MaxWindows";
                        }
                        // Use the engine's runtime maxWindows (not m_settings->
                        // autotileMaxWindows()) — during cycling, settings may
                        // be stale if updateAutotileScreens runs before
                        // setAlgorithm syncs settings via QSignalBlocker.
                        const int runtimeMaxWindows = m_autotileEngine->runtimeMaxWindows();
                        if (!globalAlgoPtr || runtimeMaxWindows == globalAlgoPtr->defaultMaxWindows()) {
                            overrides[PerScreenKeys::MaxWindows] = screenAlgoPtr->defaultMaxWindows();
                        }
                    } else {
                        qCWarning(lcDaemon) << "updateAutotileScreens: unknown per-screen algorithm" << screenAlgo
                                            << "for screen" << screenId;
                    }
                }
            }
            // Layer per-context tiling-parameter RULES on top of the config-derived
            // override map (config stays the base; a matched SetMaxWindows /
            // SetSplitRatio / SetMasterCount rule wins, and also overrides the
            // algorithm-default MaxWindows injected above). Resolved above (the
            // overflow result gated the MaxWindows injection).
            if (tilingParams.maxWindows) {
                overrides[PerScreenKeys::MaxWindows] = *tilingParams.maxWindows;
            }
            if (tilingParams.splitRatio) {
                overrides[PerScreenKeys::SplitRatio] = *tilingParams.splitRatio;
            }
            if (tilingParams.masterCount) {
                overrides[PerScreenKeys::MasterCount] = *tilingParams.masterCount;
            }
            if (tilingParams.insertPosition) {
                overrides[PerScreenKeys::InsertPosition] = *tilingParams.insertPosition;
            }
            if (tilingParams.overflowBehavior) {
                overrides[PerScreenKeys::OverflowBehavior] = *tilingParams.overflowBehavior;
            }
            // Custom-parameter override applies only when the rule's target
            // algorithm is this screen's effective algorithm — the daemon knows
            // both, so the algorithm guard lives here. The engine's hasCustomParam
            // filter is a second guard for the current algo's declared params.
            //
            // A bare-autotile screen (mode on, no concrete assigned algorithm) is
            // absent from screenAlgorithms yet still runs the global-default
            // algorithm (m_autotileEngine->algorithmId(), the value the engine's
            // effectiveAlgorithm resolves at steady state), so fall back to it rather
            // than gating on contains() — otherwise a rule targeting the default
            // algorithm is silently dropped on those screens. During a mid-cycle
            // applyEntry this runs before setAlgorithm() updates the global id, so a
            // bare screen may see the prior algorithm for one pass. Unlike the
            // MaxWindows staleness above (which the engine re-derives at retile time
            // via effectiveMaxWindows), the engine has no retile-time re-resolution
            // for custom params, so a dropped CustomParams override persists until the
            // next updateAutotileScreens (window open, desktop switch, cycle) rather
            // than healing at the next retile.
            const QString effectiveAlgo = screenAlgorithms.value(screenId, m_autotileEngine->algorithmId());
            if (!tilingParams.algorithmParamTarget.isEmpty() && tilingParams.algorithmParamTarget == effectiveAlgo) {
                overrides[PerScreenKeys::CustomParams] = tilingParams.algorithmParams;
            }

            // Compare against currently applied overrides to avoid redundant retiles
            QVariantMap current = m_autotileEngine->perScreenOverrides(screenId);
            if (overrides != current) {
                if (!overrides.isEmpty()) {
                    m_autotileEngine->applyPerScreenConfig(screenId, overrides);
                } else {
                    m_autotileEngine->clearPerScreenConfig(screenId);
                }
            }
        }
    }

    // setActiveScreens creates TilingStates for newly added screens (reusing
    // any already created by applyPerScreenConfig above) and retiles them.
    // Because per-screen overrides are set first, retileAfterOperation inside
    // setActiveScreens uses effectiveAlgorithm() with the correct per-screen algo.
    m_autotileEngine->setActiveScreens(autotileScreens);

    // setActiveScreens only retiles screens that were newly ADDED to the set; a
    // screen that was already active gets no retile from it. Force a retile for
    // every ALREADY-ACTIVE screen so (1) a mode-swap toggle with a stable set takes
    // effect, and (2) a rule edit that changes tiling GEOMETRY without changing the
    // per-screen overrides map is still applied live. Case (2) is load-bearing for
    // GAP rules (SetInnerGap / SetOuterGap*): they resolve through the context-gap
    // PROVIDER at retile time, not the overrides map, so `overrides != current` above
    // never sees them — only a retile pulls the fresh gaps (via the provider's
    // authoritative "tiling"-mode context). Skipping the added screens avoids
    // retiling them twice (setActiveScreens already did). Diffing gaps here to skip
    // the retile on truly-unrelated edits (appearance/lock/exclude) would have to
    // replicate that exact provider context and risk silently dropping gap
    // application; the blanket retile is the simple correct choice. Cost is bounded:
    // rulesChanged fires only on a user rule save, the retile is deferred + coalesced,
    // and it produces identical geometry (no window movement) when nothing changed.
    for (const QString& screenId : autotileScreens) {
        if (!addedScreens.contains(screenId)) {
            m_autotileEngine->scheduleRetileForScreen(screenId);
        }
    }

    // Retile for existing screens whose overrides changed is handled by the
    // deferred retile scheduled inside applyPerScreenConfig()/clearPerScreenConfig().
    // This coalesces with deferred retiles from setAlgorithm() (called by
    // applyEntry() after updateAutotileScreens), producing a SINGLE retile pass
    // with fully consistent state. An immediate retile() here would fire BEFORE
    // setAlgorithm() updates the global algorithm/splitRatio, causing a second
    // windowsTiled D-Bus signal whose stagger generation increment invalidates
    // the first signal's pending stagger timers — leaving windows at old
    // positions and producing the left-overlapping-right bug.

    // Propagate to overlay service so initializeOverlay() skips autotile screens
    if (m_overlayService) {
        m_overlayService->setExcludedScreens(autotileScreens);
    }

    qCDebug(lcDaemon) << "Updated autotile screens=" << autotileScreens;
}

QSet<QString> Daemon::diffActiveAssignments()
{
    QSet<QString> changed;
    if (!m_screenManager || !m_layoutManager) {
        return changed;
    }
    const QString activity = currentActivity();
    QHash<QString, QString> next;
    const QStringList effectiveIds = m_screenManager->effectiveScreenIds();
    next.reserve(effectiveIds.size());
    for (const QString& screenId : effectiveIds) {
        // Per-output virtual desktops (#648): each screen resolves its own desktop.
        const int desktop = currentDesktopForScreen(screenId);
        // assignmentIdForScreen returns the ACTIVE id (snapping layout uuid, or
        // "autotile:<algo>"), so this fires only when the visible layout changes
        // — e.g. a tiling-algorithm edit while the screen is in snapping mode
        // resolves to the same snapping id and is correctly ignored.
        const QString id = m_layoutManager->assignmentIdForScreen(screenId, desktop, activity);
        next.insert(screenId, id);
        if (m_activeAssignmentByScreen.value(screenId) != id) {
            changed.insert(screenId);
        }
    }
    // A screen that went away is a change too: it drops out of `next`, so the
    // loop above never visits it and it would otherwise vanish silently. The
    // effect caches these ids per screen and stamps them onto its window-rule
    // queries, so a dropped screen has to be announced or its last layout id
    // lingers in that cache for the rest of the session.
    //
    // Only when it HAD a layout: a screen that was already resolving to nothing
    // and then went away is an empty-to-empty transition, and broadcasting it
    // would emit a signal for a value that did not change (the effect's handler
    // would remove an entry that is not there). Skipping it also keeps dead ids
    // out of the returned set, which reconcileActiveAssignments feeds to the
    // resnap and OSD paths.
    for (auto it = m_activeAssignmentByScreen.constBegin(); it != m_activeAssignmentByScreen.constEnd(); ++it) {
        if (!next.contains(it.key()) && !it.value().isEmpty()) {
            changed.insert(it.key());
        }
    }
    // Replace wholesale so screens that went away drop out of the snapshot.
    m_activeAssignmentByScreen = std::move(next);
    // Mirror the fresh snapshot onto the bus-facing adaptor and broadcast the
    // screens that moved. This is the ONE resolution of "which layout is active
    // on which screen" in the daemon, and every edge that can move that value
    // already routes through here, so publishing from this spot is what keeps a
    // remote ActiveLayout matcher (the KWin effect's window-rule queries) from
    // needing to re-derive the cascade itself.
    if (m_layoutAdaptor) {
        m_layoutAdaptor->publishActiveAssignments(m_activeAssignmentByScreen, changed);
    }
    return changed;
}

void Daemon::reconcileActiveAssignments()
{
    const QSet<QString> changed = diffActiveAssignments();
    // Per-context tiling rules change a screen's resolved layout WITHOUT changing
    // its assignment id, so they never appear in `changed` (diffActiveAssignments
    // only tracks the active snapping-layout uuid / "autotile:<algo>" id). Two
    // families need updateAutotileScreens() to apply them live: tiling-PARAM rules
    // (SetMaxWindows / SetSplitRatio / SetMasterCount / SetInsertPosition /
    // SetOverflowBehavior / SetAlgorithmParam), which land in the per-screen overrides
    // map and self-retile via applyPerScreenConfig; and GAP rules, which resolve
    // through the context-gap provider at retile time and rely on the force-retile
    // inside updateAutotileScreens (see the comment there). SetDragBehavior needs no
    // retile — it is read live by the drag adaptor.
    updateAutotileScreens();
    if (changed.isEmpty()) {
        return;
    }
    // Snapping screens resnap through the same assignmentChangesApplied handler
    // the KCM batch uses, so rule-driven and assignment-driven changes run
    // identical code. Gated on `changed` because only an assignment-id change
    // moves windows.
    //
    // Emitted for THIS set explicitly rather than staged into the adaptor's
    // m_changedScreenIds: that buffer is the bus client's save batch, and every
    // assignment write reaches here synchronously via RuleStore::rulesChanged,
    // so staging would drain the client's batch mid-save and leave its closing
    // apply with an empty set (which downstream means every screen).
    if (m_layoutAdaptor) {
        m_layoutAdaptor->applyAssignmentChangesFor(changed);
    }
}

/**
 * @brief Deactivate autotile: clear assignments, restore manual layout, resnap windows.
 *
 * @note Callers MUST call updateAutotileScreens() + updateLayoutFilter()
 *       afterward to derive per-screen state and update the layout model.
 */
void Daemon::handleAutotileDisabled()
{
    // Feature disabled: flip every autotile assignment's mode to Snapping.
    // clearAutotileAssignments() preserves each screen's saved snappingLayout
    // and tilingAlgorithm fields, so a screen that was previously autotile
    // reverts to the snap layout it had before entering autotile. Screens
    // already in Snapping mode are untouched — this is what makes mixed-mode
    // setups (screen A snap + screen B autotile) recover correctly.
    if (m_layoutManager) {
        QSignalBlocker blocker(m_layoutManager.get());
        // Block the RULE STORE too, not just the registry. Every assignment write
        // goes through upsertAssignmentRule and emits RuleStore::rulesChanged
        // synchronously, which drives reconcileActiveAssignments — so blocking the
        // registry alone still ran a full reconcile (updateAutotileScreens, resnap
        // apply, OSD) per iteration, re-entrantly, over a half-written assignment
        // set. That is the very thing this blocker exists to prevent. Both are
        // released together, and the caller's own diffActiveAssignments()
        // republishes the snapshot the blocked rulesChanged would have driven.
        QSignalBlocker ruleBlocker(m_ruleStore.get());
        m_layoutManager->clearAutotileAssignments();
    }

    // Some screens may have entered autotile without ever having a snap layout
    // assigned (fresh installs, imported configs). For those, assignmentEntry's
    // snappingLayout is empty, and layoutForScreen() would fall through to the
    // default layout — which is fine except the overlay/layout model prefers
    // an explicit per-screen assignment. Fill those in individually without
    // clobbering screens that already have a valid snap layout.
    if (m_layoutManager && m_screenManager) {
        const QString activity = currentActivity();
        const QStringList effectiveIds = m_screenManager->effectiveScreenIds();

        PhosphorZones::Layout* fallbackLayout = m_layoutManager->activeLayout();
        if (!fallbackLayout && !m_layoutManager->layouts().isEmpty()) {
            fallbackLayout = m_layoutManager->layouts().first();
        }

        {
            QSignalBlocker blocker(m_layoutManager.get());
            // Block the RULE STORE too, not just the registry. Every assignment write
            // goes through upsertAssignmentRule and emits RuleStore::rulesChanged
            // synchronously, which drives reconcileActiveAssignments — so blocking the
            // registry alone still ran a full reconcile (updateAutotileScreens, resnap
            // apply, OSD) per iteration, re-entrantly, over a half-written assignment
            // set. That is the very thing this blocker exists to prevent. Both are
            // released together and one reconcile is driven explicitly at the end.
            QSignalBlocker ruleBlocker(m_ruleStore.get());
            for (const QString& screenId : effectiveIds) {
                // Per-output virtual desktops (#648): each screen resolves its own desktop.
                const int desktop = currentDesktopForScreen(screenId);
                const QString existingSnapId = m_layoutManager->snappingLayoutForScreen(screenId, desktop, activity);
                const auto existingSnapUuid = Utils::parseUuid(existingSnapId);
                PhosphorZones::Layout* existing =
                    existingSnapUuid ? m_layoutManager->layoutById(*existingSnapUuid) : nullptr;
                if (existing) {
                    continue; // Per-screen snap layout already valid — don't overwrite.
                }
                if (!fallbackLayout) {
                    continue;
                }
                if (!activity.isEmpty()) {
                    m_layoutManager->clearAssignment(screenId, desktop, activity);
                }
                m_layoutManager->assignLayout(screenId, desktop, QString(), fallbackLayout);
            }
        }
        // Set active layout OUTSIDE the blocker so activeLayoutChanged fires
        // and UnifiedLayoutController syncs its internal state.
        if (fallbackLayout && !m_layoutManager->activeLayout()) {
            m_layoutManager->setActiveLayout(fallbackLayout);
        }
    }
    // Deliberately NO reconcile / diff here. The sole caller (the consolidated
    // settings handler in init_services.cpp) ends with its own
    // diffActiveAssignments(), which republishes the snapshot the blocked
    // rulesChanged would otherwise have driven. Calling reconcileActiveAssignments
    // here instead ran updateAutotileScreens BEFORE that caller does, which moved
    // the synchronous windowsReleased that populates m_pendingSnapFloatRestores
    // ahead of the caller's own release, and the resnap apply it triggers then
    // cleared that buffer — losing the snap-float restore the caller depends on,
    // and costing a second full resnap plus an unbudgeted OSD burst.
    // No parallel saved-floating sets to clear — each window's cross-mode state
    // lives only in its unified WindowPlacement record (single source of truth).
    // Note: resnap happens at the call site AFTER updateAutotileScreens() so that
    // windowsReleased clears floating state before windows are resnapped.
}

/**
 * @brief Activate autotile on all screens using the last algorithm.
 *
 * Assigns autotile layout to every screen and sets mode to Autotile.
 * @note Callers MUST call updateAutotileScreens() + updateLayoutFilter()
 *       afterward to derive per-screen state and update the layout model.
 */
void Daemon::handleSnappingToAutotile()
{
    if (!m_settings || !m_unifiedLayoutController || !m_layoutManager || !m_screenManager) {
        return;
    }

    // Resolve the FALLBACK algorithm from settings. Per screen, a preserved
    // per-screen algorithm wins over it (see the assignment loop below).
    QString defaultAlgoId = m_settings->defaultAutotileAlgorithm();
    if (defaultAlgoId.isEmpty()) {
        defaultAlgoId = PhosphorTiles::AlgorithmRegistry::staticDefaultAlgorithmId();
    }

    // FIRST restore every context a previous global disable neutered. The
    // disable walks all desktops and activities; the per-screen loop below only
    // ever writes the current desktop, so without this an off/on round trip left
    // desktops 2..N and every activity-pinned context stranded in Snapping with
    // no way back. Running it ahead of the loop also means a restored
    // current-desktop screen is already Autotile by the time the loop runs, so
    // the "skip screens already on autotile" guard preserves the algorithm the
    // restore just put back rather than overwriting it with the global default.
    //
    // Blocked the same way the loop below is, and for the same reason: each
    // flipped rule emits RuleStore::rulesChanged synchronously, so an unblocked
    // restore would drive a full reconcile per rule over a half-restored set.
    {
        QSignalBlocker blocker(m_layoutManager.get());
        QSignalBlocker ruleBlocker(m_ruleStore.get());
        m_layoutManager->restoreAutotileAssignments();
    }

    // Determine which screens need to be converted to autotile. Skip screens
    // that already have an autotile assignment so we preserve their per-screen
    // algorithm customization (mixed-mode: screen A snap → autotile, screen B
    // already autotile stays on its configured algorithm).
    const QString activity = currentActivity();
    QStringList screensToConvert;
    const QStringList effectiveIds = m_screenManager->effectiveScreenIds();
    for (const QString& screenId : effectiveIds) {
        // Per-output virtual desktops (#648): each screen resolves its own desktop.
        const int desktop = currentDesktopForScreen(screenId);
        const QString existing = m_layoutManager->assignmentIdForScreen(screenId, desktop, activity);
        if (!PhosphorLayout::LayoutId::isAutotile(existing)) {
            screensToConvert.append(screenId);
        }
    }

    if (screensToConvert.isEmpty()) {
        return; // No-op enable: do NOT mutate engine algorithm or capture floats.
    }

    // Side effects deferred past the no-op check above so an enable with nothing
    // to convert leaves engine state untouched.
    if (m_autotileEngine) {
        m_autotileEngine->setAlgorithm(defaultAlgoId);
    }
    // Pre-save snap-float state before autotile entry (same rationale as toggle handler)
    presaveSnapFloats();

    // Pre-seed autotile engine with zone-ordered windows BEFORE layout switch
    // so we get deterministic window ordering (zone 1 → master, zone 2 → second).
    // Only seed screens that are actually being converted.
    for (const QString& sid : screensToConvert) {
        seedAutotileOrderForScreen(sid);
    }

    // Assign autotile layout to the screens being converted. Block signals to
    // avoid N intermediate updateAutotileScreens() from layoutAssigned — one
    // final call from the caller is sufficient. Write with empty activity so
    // the entry is visible to D-Bus/KCM queries that use empty activity for
    // cascading resolution.
    {
        QSignalBlocker blocker(m_layoutManager.get());
        // Block the RULE STORE too, not just the registry. Every assignment write
        // goes through upsertAssignmentRule and emits RuleStore::rulesChanged
        // synchronously, which drives reconcileActiveAssignments — so blocking the
        // registry alone still ran a full reconcile (updateAutotileScreens, resnap
        // apply, OSD) per iteration, re-entrantly, over a half-written assignment
        // set. That is the very thing this blocker exists to prevent. Both are
        // released together, and the caller's own diffActiveAssignments()
        // republishes the snapshot the blocked rulesChanged would have driven.
        QSignalBlocker ruleBlocker(m_ruleStore.get());
        for (const QString& screenId : screensToConvert) {
            // Per-output virtual desktops (#648): each screen resolves its own desktop.
            const int desktop = currentDesktopForScreen(screenId);
            // Restore this screen's own algorithm where one survives.
            // clearAutotileAssignments deliberately PRESERVES entry.tilingAlgorithm
            // when it flips a screen to Snapping, precisely so re-enabling can put
            // it back — but assignLayoutById overwrites tilingAlgorithm from the id
            // it is handed, so building one id from the global default silently
            // discarded every screen's customisation on the disable/enable round
            // trip. (The "skip screens already on autotile" guard above cannot
            // cover this: after a disable, no screen is on autotile.)
            const QString screenAlgo = m_layoutManager->tilingAlgorithmForScreen(screenId, desktop, activity);
            const QString algoForScreen = screenAlgo.isEmpty() ? defaultAlgoId : screenAlgo;
            if (!activity.isEmpty()) {
                m_layoutManager->clearAssignment(screenId, desktop, activity);
            }
            m_layoutManager->assignLayoutById(screenId, desktop, QString(),
                                              PhosphorLayout::LayoutId::makeAutotileId(algoForScreen));
        }
    }
    // No reconcile here either, for the same reason as handleAutotileDisabled:
    // the sole caller's own diffActiveAssignments() republishes the snapshot, and
    // reconciling here would fire a full resnap and a per-screen OSD burst
    // mid-enable, ahead of the caller's own updateAutotileScreens.
}

QHash<TilingStateKey, QStringList> Daemon::captureAutotileOrders() const
{
    QHash<TilingStateKey, QStringList> orders;
    if (!m_autotileEngine) {
        return orders;
    }
    const QString activity = currentActivity();
    for (const QString& screenId : m_autotileEngine->activeScreens()) {
        // Per-output virtual desktops (#648): each screen resolves its own desktop.
        const int desktop = currentDesktopForScreen(screenId);
        QStringList order = m_autotileEngine->managedWindowOrder(screenId);
        if (!order.isEmpty()) {
            orders[TilingStateKey{screenId, desktop, activity}] = order;
        }
    }
    return orders;
}

QVector<ZoneAssignmentEntry> Daemon::buildAutotileRestoreEntries(const QSet<QString>& excludeWindows, int desktop,
                                                                 const QString& activity, const QString& onlyScreenId)
{
    QVector<ZoneAssignmentEntry> entries;
    if (!m_windowTrackingAdaptor || m_lastAutotileOrders.isEmpty()) {
        return entries;
    }
    // No null-check on service(): the adaptor constructs and owns its service
    // (never null once the adaptor exists) — the sibling callers in this file
    // rely on the same invariant.
    PhosphorPlacement::WindowTrackingService* wts = m_windowTrackingAdaptor->service();
    for (auto it = m_lastAutotileOrders.constBegin(); it != m_lastAutotileOrders.constEnd(); ++it) {
        if (desktop >= 0 && (it.key().desktop != desktop || it.key().activity != activity)) {
            continue;
        }
        // Scope to the toggled screen when the caller says so. The per-screen
        // mode toggle merges captured orders for EVERY active autotile screen
        // into m_lastAutotileOrders, and two screens routinely share a
        // (desktop, activity) pair — without this filter the toggle emits
        // pre-tile restores for windows still happily tiled on OTHER screens
        // and teleports them to their old float positions.
        if (!onlyScreenId.isEmpty() && it.key().screenId != onlyScreenId) {
            continue;
        }
        const QString& screenId = it.key().screenId;
        for (const QString& windowId : it.value()) {
            if (excludeWindows.contains(windowId))
                continue;
            if (wts->isWindowSnapped(windowId))
                continue;
            // Minimize exception, mirroring the seed/order-resnap filters: a
            // minimized window reads as floating (suspension float) but must
            // keep its restore entry — the effect skips the geometry apply
            // for minimized windows and keeps only the bookkeeping, and the
            // unminimize path re-resolves placement.
            const bool minimized =
                wts->windowRegistry() && wts->windowRegistry()->minimizedState(windowId).value_or(false);
            if (wts->isWindowFloating(windowId) && !minimized)
                continue;
            // Strict per-instance lookup — no appId fallback. A window that was
            // only ever auto-tiled (never explicitly snapped, never explicitly
            // floated) has no authoritative pre-float position to restore to.
            // Falling back to the cross-session appId entry would teleport the
            // window to stale coordinates left behind by a ghost instance.
            // Leaving the window at its current tiled position is the least
            // surprising outcome.
            auto geo = wts->validatedUnmanagedGeometry(windowId, screenId, /*exactOnly=*/true);
            if (geo) {
                ZoneAssignmentEntry entry;
                entry.windowId = windowId;
                entry.targetZoneId = RestoreSentinel;
                entry.targetGeometry = *geo;
                entries.append(entry);
                qCInfo(lcDaemon) << "Batched float-restore: windowId=" << windowId << "geo=" << *geo
                                 << "screen=" << screenId;
            } else {
                qCInfo(lcDaemon) << "Batched float-restore: skipping" << windowId
                                 << "— no exact per-instance pre-tile entry";
            }
        }
    }
    return entries;
}

void Daemon::presaveSnapFloats(const QString& screenId)
{
    // Snapshot snap-mode float state into the unified record BEFORE a screen leaves
    // snapping for autotile, so the screen's return restores the float from the
    // SINGLE source of truth (the record's snap slot) — no parallel saved-float set.
    // Runs while the screen is still in snapping mode, so captureWindowPlacement
    // routes to the snap engine and records the snap slot (= floating) plus the
    // shared free geometry from the live frame.
    //
    // MINIMIZED floating windows are a deliberate no-op inside
    // captureWindowPlacement (its minimize guard): their record still holds
    // the PRE-minimize state, which is exactly what the return path restores
    // — windowsReleased's snapSnapped branch qualifies minimized windows via
    // the registry, so a snapped-then-minimized window resnaps to its zone
    // rather than needing a suspension-float slot written here.
    //
    // Reachable from `Settings::settingsChanged` (init_services.cpp) before the
    // engines exist — any synchronous re-entry into settingsChanged during the
    // D-Bus retry loop in init() would hit this path with null engine pointers.
    if (!m_windowTrackingAdaptor || !m_autotileEngine || !m_snapEngine) {
        return;
    }
    PhosphorPlacement::WindowTrackingService* wts = m_windowTrackingAdaptor->service();
    const QStringList floatingIds = wts->floatingWindows();
    for (const QString& fid : floatingIds) {
        if (m_autotileEngine->isModeSpecificFloated(fid)) {
            continue;
        }
        // When scoped to a screen, only snapshot windows on that screen.
        // Windows floating on other screens are not entering autotile. A
        // window with NO tracked screen is deliberately INCLUDED on every
        // per-screen toggle: it might be on the toggling screen, and the
        // capture is an idempotent record refresh — skipping on unknown would
        // drop exactly the float this presave exists to preserve.
        if (!screenId.isEmpty()) {
            const QString windowScreen = wts->screenForWindow(fid);
            if (!windowScreen.isEmpty() && windowScreen != screenId) {
                continue;
            }
        }
        m_windowTrackingAdaptor->captureWindowPlacement(fid);
        qCDebug(lcDaemon) << "Captured snap-float to record for" << fid << "screen=" << screenId;
    }
}

void Daemon::seedAutotileOrderForScreen(const QString& screenId)
{
    if (!m_autotileEngine || !m_windowTrackingAdaptor) {
        return;
    }

    // Prefer saved autotile order from last mode toggle (deterministic re-entry).
    // Falls back to zone-ordered window list when no saved order exists (first
    // activation, or windows changed between toggles).
    TilingStateKey orderKey{screenId, currentDesktopForScreen(screenId), currentActivity()};
    QStringList order = m_lastAutotileOrders.value(orderKey);
    PhosphorPlacement::WindowTrackingService* wts = m_windowTrackingAdaptor->service();
    if (!wts) {
        // Fail CLOSED: without the WTS the seed filter cannot drop live user
        // floats or durable snap-slot floats, and seeding a saved order
        // UNFILTERED would violate the contract in seedorderfilter.h.
        // Unreachable under the "service() is never null once the adaptor
        // exists" invariant, but the contract must not depend on it silently.
        qCWarning(lcDaemon) << "seedAutotileOrderForScreen: no WindowTrackingService — refusing unfiltered seed for"
                            << screenId;
        return;
    }
    if (order.isEmpty()) {
        order = wts->buildZoneOrderedWindowList(screenId);
    }
    if (!order.isEmpty()) {
        const PhosphorEngine::WindowRegistry* registry = wts->windowRegistry();
        if (!registry) {
            // Without the registry every minimized window reads as
            // not-minimized here AND the engine's own strict-seed deferral
            // loses its second line of defence — the fail-open that seeds a
            // hidden window into a tile. Loud, because in production the
            // registry is always wired.
            qCWarning(lcDaemon) << "seedAutotileOrderForScreen: no window registry —"
                                << "minimized windows cannot be filtered for" << screenId;
        }
        // Drop entries that must not be seeded as tiled (live user floats,
        // durable snap-slot floats); minimized windows stay as positional
        // placeholders. See filterAutotileSeedOrder's doc for the rationale.
        filterAutotileSeedOrder(order, wts, registry);
    }

    if (!order.isEmpty()) {
        // A window's re-snap intent is already captured in its record (snap slot =
        // snapped) by the snap-commit path, so no saved-floating set needs clearing
        // here — the record is the single source of truth.
        m_autotileEngine->setInitialWindowOrder(screenId, order);
    }
}

void Daemon::processPendingGeometryUpdates()
{
    if (!m_geometryUpdatePending) {
        return;
    }
    // Timer-driven entry (the geometry debounce fires from the event loop),
    // so unlike the signal-wired paths nothing upstream vouches for these
    // members during a stop() teardown window.
    if (!m_screenManager || !m_layoutManager || !m_layoutComputeService || !m_overlayService) {
        return;
    }

    // Recalculate zone geometries for each effective screen (virtual or physical)
    // so fixed-mode zones stay normalized correctly against the correct screen geometry.
    // Async: each screen's computation runs on the worker thread. A barrier
    // tracks pending screens. Supersession is tracked by per-screen
    // GENERATION (see the completion barrier below); a result below the
    // screen's current generation just advances the expectation.
    const QString activity = currentActivity();
    const QStringList screenIds = m_screenManager->effectiveScreenIds();

    auto pending = std::make_shared<QHash<QString, uint64_t>>();

    auto requestFor = [this, pending](PhosphorZones::Layout* layout, const QString& screenId, const QRectF& geom) {
        if (!layout) {
            return;
        }
        if (m_layoutComputeService->requestRecalculate(layout, screenId, geom)) {
            pending->insert(screenId, m_layoutComputeService->currentGeneration(screenId));
        }
    };

    for (const QString& screenId : screenIds) {
        // Per-output virtual desktops (#648): each screen resolves its own desktop.
        const int desktop = currentDesktopForScreen(screenId);
        PhosphorZones::Layout* layout = m_layoutManager->layoutForScreen(screenId, desktop, activity);
        if (layout) {
            requestFor(layout, screenId,
                       GeometryUtils::effectiveScreenGeometry(m_screenManager.get(), layout, screenId));
        }
    }

    // Layouts not currently displayed on any screen are NOT recomputed here.
    // They get a fresh recalc on demand: LayoutRegistry::activeLayoutChanged and
    // layoutAssigned both trigger requestRecalculate against the relevant screen.
    // Eagerly recomputing every unassigned layout against the primary's physical
    // id used to flood the worker with O(layouts) requests per panel-change
    // burst, all keyed by one screenId, which inflated m_screenGeneration and
    // turned earlier-round results stale en masse.

    m_geometryUpdatePending = false;

    if (pending->isEmpty()) {
        m_overlayService->updateGeometries();
        m_reapplyGeometriesTimer.setInterval(REAPPLY_DELAY_MS);
        m_reapplyGeometriesTimer.start();
        return;
    }

    // Completion barrier: only the service's current generation can complete a
    // screen. If another request supersedes ours, advance the expected generation
    // and wait for that result. The layout pointer in the emission is NOT
    // inspected — generation alone decides. A destroyed layout's result
    // arrives null AT the current generation and completes the screen exactly
    // like an applied one, which is the point: the barrier waits for the
    // newest outcome, whatever it was.
    //
    // Keyed by screenId ALONE, deliberately. The service's generation counter is
    // per screen and bumped by every request, so the result carrying the
    // screen's current generation IS the newest request for that screen —
    // whatever layout it computed. A competing same-screen request for a
    // DIFFERENT layout only exists when the screen's displayed layout changed
    // mid-flight (activeLayoutChanged / layoutAssigned), and then ITS result is
    // exactly the geometry the overlay refresh needs; re-keying by
    // (screen, layout) would instead strand the barrier waiting for a
    // generation that can never arrive.
    auto conn = std::make_shared<QMetaObject::Connection>();
    *conn = connect(
        m_layoutComputeService.get(), &PhosphorZones::LayoutComputeService::geometriesComputedForGeneration, this,
        [this, pending, conn](const QString& screenId, const QUuid&, PhosphorZones::Layout*, uint64_t generation) {
            auto expected = pending->find(screenId);
            if (expected == pending->end() || generation < expected.value()) {
                return;
            }
            const uint64_t currentGeneration = m_layoutComputeService->currentGeneration(screenId);
            if (generation < currentGeneration) {
                expected.value() = currentGeneration;
                return;
            }
            pending->remove(screenId);
            if (pending->isEmpty()) {
                QObject::disconnect(*conn);
                m_overlayService->updateGeometries();
                m_reapplyGeometriesTimer.setInterval(REAPPLY_DELAY_MS);
                m_reapplyGeometriesTimer.start();
            }
        });
    // Watchdog (see COMPUTE_BARRIER_TIMEOUT_MS): force-complete a barrier whose
    // screen disappeared mid-flight so the overlay refresh still happens and
    // the connection does not leak. A barrier that completed normally has an
    // empty pending set and an already-disconnected conn — the timeout then
    // no-ops.
    QTimer::singleShot(COMPUTE_BARRIER_TIMEOUT_MS, this, [this, pending, conn]() {
        if (pending->isEmpty()) {
            return;
        }
        qCWarning(lcDaemon) << "Geometry recalc barrier timed out with screens still pending:" << pending->keys()
                            << "— forcing overlay refresh";
        pending->clear();
        QObject::disconnect(*conn);
        m_overlayService->updateGeometries();
        m_reapplyGeometriesTimer.setInterval(REAPPLY_DELAY_MS);
        m_reapplyGeometriesTimer.start();
    });

    // Re-query panel geometry once after a delay to pick up settled state (e.g. panel editor close).
    // That completion emits availableGeometryChanged → debounce → processPendingGeometryUpdates → reapply.
    m_screenManager->scheduleDelayedPanelRequery(DELAYED_PANEL_REQUERY_MS);

    // Retile autotile windows to adapt to new screen geometry
    // (panels added/removed, resolution changes, etc.)
    if (m_autotileEngine && m_autotileEngine->isEnabled()) {
        m_autotileEngine->retile();
    }
}

} // namespace PlasmaZones
