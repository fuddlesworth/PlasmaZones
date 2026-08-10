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
#include "dbus/tilingadaptor/tilingadaptor.h"
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
#include <QScopeGuard>

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

bool Daemon::intersectsAnyLiveScreen(const QRect& geometry) const
{
    if (!geometry.isValid() || !m_screenManager) {
        return false;
    }
    const QStringList effectiveIds = m_screenManager->effectiveScreenIds();
    for (const QString& screenId : effectiveIds) {
        const QRect geom = m_screenManager->screenGeometry(screenId);
        if (geom.isValid() && geom.intersects(geometry)) {
            return true;
        }
    }
    return false;
}

void Daemon::updateEngineScreens()
{
    // Pre-init calls bail here; after initEnginesAndWiring the factory
    // contract guarantees BOTH engines exist (asserted there), so this
    // early return can never freeze the scrolling half on its own.
    // m_algorithmRegistry joins the preamble: the per-screen override loop
    // below derefs it for the algorithm-default MaxWindows injection, and a
    // guard only on its siblings left that deref as the odd one out.
    if (!m_autotileEngine || !m_layoutManager || !m_screenManager || !m_algorithmRegistry) {
        return;
    }
    // Re-entrancy latch: the engines' placementChanged fires SYNCHRONOUSLY
    // from setActiveScreens/scheduleRetileForScreen inside this pass, and
    // the tiled-count gates recompute through here — running the
    // capture/seed/apply phases against partially-applied state. Defer the
    // nested request to a queued re-run instead.
    if (m_updateEngineScreensInProgress) {
        if (!m_updateEngineScreensQueued) {
            m_updateEngineScreensQueued = true;
            QMetaObject::invokeMethod(
                this,
                [this]() {
                    m_updateEngineScreensQueued = false;
                    updateEngineScreens();
                },
                Qt::QueuedConnection);
        }
        return;
    }
    m_updateEngineScreensInProgress = true;
    const auto latchReset = qScopeGuard([this]() {
        m_updateEngineScreensInProgress = false;
    });
    // Every entry path into this function is wired in init() or later
    // (settingsChanged, layoutAssigned, virtual-screen reconfigure), so
    // the resolver is always live by the time we run. The earlier guard
    // here had a settings-cascade fallback — that path was unreachable
    // and let isContextDisabled stay alive in the daemon as dead code.
    if (!m_contextResolver) {
        return;
    }

    const QString activity = currentActivity();

    // ONE restore batch per recompute: both engines' windowsReleased fire
    // synchronously inside this pass (autotile's setActiveScreens below,
    // then updateScrollingScreens'), and each release APPENDS to
    // m_pendingSnapFloatRestores. Clearing at handler entry instead would
    // wipe the first engine's entries when both release in one flip.
    m_pendingSnapFloatRestores.clear();

    QSet<QString> autotileScreens;
    QSet<QString> scrollingScreens;
    QHash<QString, QString> screenAlgorithms;
    const QStringList effectiveIds = m_screenManager->effectiveScreenIds();
    for (const QString& screenId : effectiveIds) {
        // Per-output virtual desktops (#648): each screen resolves its own desktop.
        const int desktop = currentDesktopForScreen(screenId);
        // Scrolling-mode assignment is resolved from the same cascade; it has
        // no layout id of its own, so the mode lookup is the discriminator.
        // The master switch gates here (the peer of the snapping/autotile
        // feature flags): with scrolling disabled the branch is skipped
        // entirely, the "scrolling:" assignment falls through as a
        // non-autotile id, and the screen is treated as snapping — the same
        // downgrade the router applies for an unclaimed scrolling cascade.
        if (m_settings && m_settings->scrollingEnabled()
            && m_layoutManager->modeForScreen(screenId, desktop, activity)
                == PhosphorZones::AssignmentEntry::Scrolling) {
            if (!m_contextResolver->isDisabled(
                    m_contextResolver->handleForMode(screenId, PhosphorZones::AssignmentEntry::Scrolling))) {
                scrollingScreens.insert(screenId);
                // Pre-save snap floats for a screen entering scrolling FROM
                // SNAPPING, using the PRE-FLIP engine state as the
                // discriminator (neither engine's live set holds it). This
                // must run before any setActiveScreens: once the cascade
                // answer is applied, snap's capturePlacement refuses the
                // no-longer-Snapping screen and the presave writes nothing.
                if (m_scrollEngine && !m_scrollEngine->isActiveOnScreen(screenId)
                    && !m_autotileEngine->isActiveOnScreen(screenId)) {
                    presaveSnapFloats(screenId);
                }
            }
            continue;
        }
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
            // Pre-save snap floats for a screen entering autotile FROM
            // SNAPPING — the exact twin of the scrolling branch above, and
            // for the same reason: after the flip snap's capturePlacement
            // refuses the no-longer-Snapping screen. Without this, only the
            // mode-toggle shortcut and the global feature enable presaved,
            // so a cascade-driven flip (KCM apply, rule, layout picker)
            // silently dropped every window's snap-mode float bit.
            if (!m_autotileEngine->isActiveOnScreen(screenId)
                && !(m_scrollEngine && m_scrollEngine->isActiveOnScreen(screenId))) {
                presaveSnapFloats(screenId);
            }
            if (!algoId.isEmpty()) {
                screenAlgorithms[screenId] = algoId;
            }
        }
    }

    // Snapshot the derived sets BEFORE any engine set is applied: the
    // windowsReleased handler fires synchronously inside the applies below
    // and must know where each released screen is HEADED (the other
    // engine's live set still lags at that moment, and the raw cascade
    // cannot see context-disable exclusions).
    m_derivedAutotileScreens = autotileScreens;
    m_derivedScrollingScreens = scrollingScreens;

    // Capture window order for screens LEAVING autotile before PhosphorTiles::TilingState is destroyed.
    // This preserves the tiling arrangement so re-entering autotile (e.g. cycling back)
    // restores the same window positions. Without this, only the settingsChanged path
    // (handleAutotileDisabled) captured orders — layout cycling lost them.
    const QSet<QString> currentAutotileScreens = m_autotileEngine->activeScreens();
    const QSet<QString> removedScreens = currentAutotileScreens - autotileScreens;
    for (const QString& screenId : removedScreens) {
        // Per-output virtual desktops (#648): each screen resolves its own desktop.
        const int desktop = currentDesktopForScreen(screenId);
        // Stored unconditionally, INCLUDING an empty order: the capture is the
        // authoritative "what was tiled at toggle-off", and an empty one must
        // overwrite a stale non-empty entry from an earlier toggle so re-entry
        // does not resurrect windows that have since closed or left the
        // screen (seedAutotileOrderForScreen falls back to the zone-ordered
        // list when the saved order is empty).
        QStringList order = m_autotileEngine->managedWindowOrder(screenId);
        m_lastEngineOrders[TilingStateKey{screenId, desktop, activity}] = order;
    }

    // Capture the SCROLLING engine's leaving orders in the same phase, before
    // EITHER engine seeds: a screen flipping scrolling→autotile in this very
    // pass must find its fresh column order in m_lastEngineOrders when
    // seedAutotileOrderForScreen runs below (and the reverse flip finds the
    // tiled order when updateScrollingScreens seeds). Capture-all →
    // seed-all → apply-all; the map is shared between the engines by design.
    captureScrollingOrders(scrollingScreens);

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
                            qCDebug(lcDaemon) << "updateEngineScreens: global algorithm" << globalAlgo
                                              << "not found - injecting per-screen default MaxWindows";
                        }
                        // Use the engine's runtime maxWindows (not m_settings->
                        // autotileMaxWindows()) — during cycling, settings may
                        // be stale if updateEngineScreens runs before
                        // setAlgorithm syncs settings via QSignalBlocker.
                        const int runtimeMaxWindows = m_autotileEngine->runtimeMaxWindows();
                        if (!globalAlgoPtr || runtimeMaxWindows == globalAlgoPtr->defaultMaxWindows()) {
                            overrides[PerScreenKeys::MaxWindows] = screenAlgoPtr->defaultMaxWindows();
                        }
                    } else {
                        qCWarning(lcDaemon) << "updateEngineScreens: unknown per-screen algorithm" << screenAlgo
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
            // next updateEngineScreens (window open, desktop switch, cycle) rather
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
    // applyEntry() after updateEngineScreens), producing a SINGLE retile pass
    // with fully consistent state. An immediate retile() here would fire BEFORE
    // setAlgorithm() updates the global algorithm/splitRatio, causing a second
    // windowsTiled D-Bus signal whose stagger generation increment invalidates
    // the first signal's pending stagger timers — leaving windows at old
    // positions and producing the left-overlapping-right bug.

    // The scrolling twin runs from the same recompute points so the two
    // engines' screen sets flip atomically per context switch; its body —
    // set derivation details, order seeding, context-param push — lives in
    // scrolling.cpp.
    updateScrollingScreens(scrollingScreens);

    // Propagate to overlay service so initializeOverlay() skips
    // engine-managed screens (autotile and scrolling both run without the
    // drag overlay).
    if (m_overlayService) {
        m_overlayService->setExcludedScreens(autotileScreens + scrollingScreens);
    }

    qCDebug(lcDaemon) << "Updated autotile screens=" << autotileScreens << "scrolling screens=" << scrollingScreens;

    // Drain the FLOAT half of the restore batch at the tail, for every
    // caller — the leak class was "some caller has no downstream consumer,
    // and the stale batch teleports windows when the NEXT consumer runs".
    // Floats are excluded from every downstream resnap, so this batch is
    // window-disjoint from whatever a consumer emits later. The snap-ZONE
    // half is PRESERVED: the mode-toggle and autotile-disable consumers
    // run after this recompute and feed it into preClaimedZoneIds and the
    // batched restore — consuming it here strands previously-floated
    // windows off their zones (found the hard way).
    emitPendingSnapFloatRestoresForResnapBuffer(/*preserveZoneEntries=*/true);

    // Push the per-screen RULES-VISIBLE active layout map to the effect so
    // window-domain (appearance/animation) rules can match Field::ActiveLayout
    // with the same vocabulary the daemon's context rules see (snapping UUID,
    // "autotile:<algo>", "scrolling:<templateUuid>", bare sentinel). Runs
    // UNCONDITIONALLY at this tail and must never move behind a
    // sets-unchanged short-circuit: a desktop switch changes the map while
    // both engines' screen sets stay identical. The adaptor owns the
    // emit-on-change dedup, so the unconditional push costs one map compare.
    if (m_tilingAdaptor) {
        QVariantMap activeLayouts;
        for (const QString& screenId : effectiveIds) {
            activeLayouts.insert(
                screenId,
                m_layoutManager->rulesVisibleActiveLayoutId(screenId, currentDesktopForScreen(screenId), activity));
        }
        m_tilingAdaptor->setActiveLayouts(activeLayouts);
    }
}

QSet<QString> Daemon::diffActiveAssignments()
{
    QSet<QString> changed;
    if (!m_screenManager || !m_layoutManager) {
        return changed;
    }
    const QString activity = currentActivity();
    QHash<QString, ActiveAssignmentSnapshot> next;
    const QStringList effectiveIds = m_screenManager->effectiveScreenIds();
    next.reserve(effectiveIds.size());
    for (const QString& screenId : effectiveIds) {
        // Per-output virtual desktops (#648): each screen resolves its own desktop.
        const int desktop = currentDesktopForScreen(screenId);
        // assignmentIdForScreen returns the ACTIVE id (snapping layout uuid, or
        // "autotile:<algo>"), so this fires only when the visible layout changes
        // — e.g. a tiling-algorithm edit while the screen is in snapping mode
        // resolves to the same snapping id and is correctly ignored.
        ActiveAssignmentSnapshot snapshot;
        snapshot.assignmentId = m_layoutManager->assignmentIdForScreen(screenId, desktop, activity);
        next.insert(screenId, snapshot);
        if (m_activeAssignmentByScreen.value(screenId).assignmentId != snapshot.assignmentId) {
            changed.insert(screenId);
        }
    }
    // Replace wholesale so screens that went away drop out of the snapshot.
    m_activeAssignmentByScreen = std::move(next);
    return changed;
}

void Daemon::reconcileActiveAssignments()
{
    const QSet<QString> changed = diffActiveAssignments();
    // Per-context tiling rules change a screen's resolved layout WITHOUT changing
    // its assignment id, so they never appear in `changed` (diffActiveAssignments
    // only tracks the active snapping-layout uuid / "autotile:<algo>" id). Three
    // families need updateEngineScreens() to apply them live: tiling-PARAM rules
    // (SetMaxWindows / SetSplitRatio / SetMasterCount / SetInsertPosition /
    // SetOverflowBehavior / SetAlgorithmParam), which land in the per-screen overrides
    // map and self-retile via applyPerScreenConfig; GAP rules, which resolve
    // through the context-gap provider at retile time and rely on the force-retile
    // inside updateEngineScreens (see the comment there); and SCROLLING TEMPLATE
    // rules (SetScrollingTemplate), whose id stays the bare "scrolling:" sentinel
    // while the resolved template — and so the pushed preset vocabulary — changes.
    // SetDragBehavior needs no
    // retile — it is read live by the drag adaptor.
    updateEngineScreens();
    // A rule edit that demotes a screen from tiling to snapping releases its
    // windows in the recompute above, and nothing on this path consumes the
    // preserved snap-ZONE half (the KCM apply below drains it without
    // applying). Put those windows back on their recorded zones.
    flushPendingSnapZoneRestores();
    if (changed.isEmpty()) {
        return;
    }
    // Snapping screens resnap via the shared legacy apply path: mark the changed
    // screens on the adaptor and trigger the same assignmentChangesApplied handler
    // the KCM batch uses, so rule-driven and assignment-driven changes run identical
    // code. Gated on `changed` because only an assignment-id change moves windows.
    if (m_layoutAdaptor) {
        m_layoutAdaptor->markScreensChanged(changed);
        m_layoutAdaptor->applyAssignmentChanges();
    }
}

/**
 * @brief Deactivate autotile: clear assignments, restore manual layout, resnap windows.
 *
 * @note Callers MUST call updateEngineScreens() + updateLayoutFilter()
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
    // No parallel saved-floating sets to clear — each window's cross-mode state
    // lives only in its unified WindowPlacement record (single source of truth).
    // Note: resnap happens at the call site AFTER updateEngineScreens() so that
    // windowsReleased clears floating state before windows are resnapped.
}

/**
 * @brief Activate autotile on all screens using the last algorithm.
 *
 * Assigns autotile layout to every screen and sets mode to Autotile.
 * @note Callers MUST call updateEngineScreens() + updateLayoutFilter()
 *       afterward to derive per-screen state and update the layout model.
 */
void Daemon::handleSnappingToAutotile()
{
    if (!m_settings || !m_unifiedLayoutController || !m_layoutManager || !m_screenManager) {
        return;
    }

    // Resolve algorithm from settings (this is a global enable, not per-desktop toggle)
    QString algoId = m_settings->defaultAutotileAlgorithm();
    if (algoId.isEmpty()) {
        algoId = PhosphorTiles::AlgorithmRegistry::staticDefaultAlgorithmId();
    }
    const QString autotileLayoutId = PhosphorLayout::LayoutId::makeAutotileId(algoId);

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
        // Skip Scrolling screens too: a global autotile ENABLE must not
        // clobber an explicit scrolling assignment (its id is the
        // "scrolling:" sentinel, which is not an autotile id).
        if (!PhosphorLayout::LayoutId::isAutotile(existing) && !PhosphorLayout::LayoutId::isScrolling(existing)) {
            screensToConvert.append(screenId);
        }
    }

    if (screensToConvert.isEmpty()) {
        return; // No-op enable: do NOT mutate engine algorithm or capture floats.
    }

    // Side effects deferred past the no-op check above so an enable with nothing
    // to convert leaves engine state untouched.
    if (m_autotileEngine) {
        m_autotileEngine->setAlgorithm(algoId);
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
    // avoid N intermediate updateEngineScreens() from layoutAssigned — one
    // final call from the caller is sufficient. Write with empty activity so
    // the entry is visible to D-Bus/KCM queries that use empty activity for
    // cascading resolution.
    {
        QSignalBlocker blocker(m_layoutManager.get());
        for (const QString& screenId : screensToConvert) {
            // Per-output virtual desktops (#648): each screen resolves its own desktop.
            const int desktop = currentDesktopForScreen(screenId);
            if (!activity.isEmpty()) {
                m_layoutManager->clearAssignment(screenId, desktop, activity);
            }
            m_layoutManager->assignLayoutById(screenId, desktop, QString(), autotileLayoutId);
        }
    }
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
    if (!m_windowTrackingAdaptor || m_lastEngineOrders.isEmpty()) {
        return entries;
    }
    // No null-check on service(): the adaptor constructs and owns its service
    // (never null once the adaptor exists) — the sibling callers in this file
    // rely on the same invariant.
    PhosphorPlacement::WindowTrackingService* wts = m_windowTrackingAdaptor->service();
    for (auto it = m_lastEngineOrders.constBegin(); it != m_lastEngineOrders.constEnd(); ++it) {
        if (desktop >= 0 && (it.key().desktop != desktop || it.key().activity != activity)) {
            continue;
        }
        // Scope to the toggled screen when the caller says so. The per-screen
        // mode toggle merges captured orders for EVERY active autotile screen
        // into m_lastEngineOrders, and two screens routinely share a
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
    // Snapshot snap-mode window state (explicit floats AND plain free windows)
    // into the unified record BEFORE a screen leaves snapping for a tiling
    // mode, so the screen's return restores each window from the SINGLE source
    // of truth (the record's snap slot + shared free geometry) — no parallel
    // saved-float set.
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
    if (!m_windowTrackingAdaptor || !m_windowTrackingAdaptor->service() || !m_autotileEngine || !m_snapEngine) {
        return;
    }
    PhosphorPlacement::WindowTrackingService* wts = m_windowTrackingAdaptor->service();
    // EVERY open window, not only the explicitly-floated set. A free window in
    // snapping mode (never snapped, never floated) has no live state anywhere,
    // and its CURRENT frame at the flip instant is the only correct restore
    // target for the return trip — the periodic save-time sweep is dirty-gated,
    // so a plain user move/resize between saves would otherwise never reach the
    // record and the return restored a stale rect. captureWindowPlacement's
    // slot-state gate keeps this safe for the rest of the sweep: snapped and
    // engine-tiled windows refresh their slots without any geometry write, and
    // minimized windows take its preserve path.
    const QStringList allIds = m_windowTrackingAdaptor->knownWindowIds();
    for (const QString& fid : allIds) {
        // A window floated BY a tiling mode (either engine) is that mode's
        // own float, not a snap float: capturing it here would poison the
        // snap slot with a tiling-mode frame.
        if (m_autotileEngine->isModeSpecificFloated(fid)
            || (m_scrollEngine && m_scrollEngine->isModeSpecificFloated(fid))) {
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
    QStringList order = m_lastEngineOrders.value(orderKey);
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
        // Float is per mode: non-minimized entries always seed (a snap-mode
        // float must not make the window untileable here). Minimized entries
        // stay as positional placeholders, except user-floated-then-minimized
        // ones. See filterEngineSeedOrder's doc for the rationale.
        filterEngineSeedOrder(order, wts, registry, PhosphorEngine::WindowPlacement::autotileEngineId());
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

    // Re-derive the scrolling screens' per-screen overrides. Native
    // templates carry fractions, so no geometry feeds the push itself. What
    // this pass is for is the rest of the resolve: per-context rule params
    // and the context gaps both re-resolve here. The engine's equality guard
    // no-ops an unchanged override map, so the pass is cheap and idempotent.
    // What makes the re-resolved values LAND is updateScrollingScreens'
    // own push: the set it hands setActiveScreens is identical to the
    // engine's current one, and that branch retiles every screen
    // unconditionally (engine_core.cpp, `screens == m_scrollingScreens`) —
    // the same guarantee scrolling.cpp's LOAD-BEARING gate leans on. The
    // retile loop below now runs on every pass too (it was hoisted above
    // the compute-barrier early return), but this push stays the documented
    // mechanism: it is what re-resolves the override map, not just the
    // geometry.
    if (m_scrollEngine && !m_scrollEngine->activeScreens().isEmpty()) {
        updateScrollingScreens(m_scrollEngine->activeScreens());
    }

    // The panel-settle requery and the tiling-family retiles run BEFORE the
    // empty-barrier early return below: a geometry pass in which no screen
    // produced a compute request (all screens in a tiling mode, say) still
    // changed the work areas, so the settled-panel follow-up pass must still
    // arm and both engines must still pick up the new geometry. They are
    // independent of the compute barrier — the barrier watches the layout
    // compute service, the retiles drive the engines directly.
    //
    // Re-query panel geometry once after a delay to pick up settled state
    // (e.g. panel editor close). That completion emits
    // availableGeometryChanged → debounce → processPendingGeometryUpdates →
    // reapply.
    m_screenManager->scheduleDelayedPanelRequery(DELAYED_PANEL_REQUERY_MS);

    // Retile BOTH tiling-family engines to adapt to new screen geometry
    // (panels added/removed, resolution changes, etc.). The scroll engine
    // reads the available geometry only at relayout time and subscribes to
    // no ScreenManager signal of its own, so without this its columns keep
    // stale widths/offsets until an unrelated event happens to retile.
    if (m_autotileEngine && m_autotileEngine->isEnabled()) {
        m_autotileEngine->retile();
    }
    if (m_scrollEngine && m_scrollEngine->isEnabled()) {
        for (const QString& screenId : m_scrollEngine->activeScreens()) {
            m_scrollEngine->scheduleRetileForScreen(screenId);
        }
    }

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
}

} // namespace PlasmaZones
