// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Own header
#include <PhosphorTileEngine/AutotileEngine.h>

// Project headers
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
#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/VirtualScreen.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include "engine_internal.h"

// Qt and std
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QScopeGuard>
#include <QScreen>
#include <QTimer>
#include <QVarLengthArray>
#include <algorithm>
#include <cmath>

namespace PhosphorTileEngine {

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
        // Through the registry's OWN default resolution, not the static id:
        // defaultAlgorithm() already models the case where the configured
        // default is absent (a Luau script that failed to load) and falls back
        // to the first registered algorithm. Assigning the static id blindly
        // could leave m_algorithmId naming nothing, and then currentAlgorithm()
        // and every effectiveAlgorithm() answer null — autotiling silently does
        // nothing on every screen, with no user-visible error.
        PhosphorTiles::TilingAlgorithm* fallback = registry->defaultAlgorithm();
        if (!fallback) {
            qCWarning(PhosphorTileEngine::lcTileEngine) << "AutotileEngine: unknown algorithm" << newId
                                                        << "and the registry has no default — keeping" << m_algorithmId;
            return;
        }
        qCWarning(PhosphorTileEngine::lcTileEngine)
            << "AutotileEngine: unknown algorithm" << newId << "- using" << fallback->registryId();
        newId = fallback->registryId();
    }

    if (m_algorithmId == newId) {
        // Set HERE, on the equality path only — not before it. The ctor
        // pre-seeds m_algorithmId with the static default, so for a user whose
        // configured default is that same algorithm every setAlgorithm() call
        // early-returned and the flag stayed false all session, and the first
        // genuine switch away then skipped the save block below and lost that
        // algorithm's live tuning. That is the case this set fixes.
        //
        // Setting it unconditionally ABOVE the return went too far: it made
        // the `m_algorithmEverSet &&` term in the save block permanently true,
        // killing the ctor-defaults protection the comment there describes. A
        // genuine FIRST switch would then stamp a slot from never-initialised
        // struct defaults (maxWindows 5 against the pre-seeded algorithm's 6)
        // and writeBackTuning would persist it. The tail of this function sets
        // the flag for every real switch, so those are covered anyway.
        m_algorithmEverSet = true;
        return;
    }

    // A per-screen Algorithm override pins that screen's effective algorithm, so
    // this global switch does not touch it. Shared by the tuning drop here, the
    // re-seed loop, and the split-tree/script-state clear loop below.
    const auto hasAlgoOverride = [this](const QString& screenId) {
        return hasPerScreenOverride(screenId, PerScreenKeys::Algorithm);
    };

    // Switching algorithms resets ratios/counts to the new algorithm's saved or
    // default values, so per-desktop user tunings no longer apply — drop them.
    // Only for states whose screen follows the global algorithm, though: an
    // Algorithm-overridden screen keeps its effective algorithm across this
    // switch, so its tunings are still live and must survive (same gate as the
    // state-clear loop below). The re-seed loop below refreshes current-context
    // states synchronously; other desktops re-seed on their own next propagate.
    m_userTunedSplitRatio.removeIf([&](const auto& key) {
        return !hasAlgoOverride(key.screenId);
    });
    m_userTunedMasterCount.removeIf([&](const auto& key) {
        return !hasAlgoOverride(key.screenId);
    });

    PhosphorTiles::TilingAlgorithm* oldAlgo = registry->algorithm(m_algorithmId);
    PhosphorTiles::TilingAlgorithm* newAlgo = registry->algorithm(newId);
    const int oldMaxWindows = m_config->maxWindows;

    // Save current algorithm's ratio + master count before switching.
    // Only save after the first setAlgorithm() call has completed, to avoid
    // persisting uninitialised struct defaults from the constructor.
    //
    // Skipped on the settings-refresh path: there the saved map was just
    // reloaded from disk and the live scalars have already been overwritten by
    // the refresh's global SYNC_FIELDs, so this stamp would replace the
    // freshly-saved slot with global-default values (discussion #853). The
    // cost is bounded: live shortcut tunings not yet written back are dropped
    // for the outgoing algorithm on a settings-driven switch, in favour of the
    // values the user explicitly saved.
    if (m_algorithmEverSet && oldAlgo && !m_refreshingFromSettings) {
        // Only stamp a slot when the live values actually DIFFER from the
        // outgoing algorithm's own defaults. operator[] created one
        // unconditionally, and a slot that merely echoes the defaults is
        // persisted by writeBackTuning() and then shows up in the config
        // profile diff as a change the user never made — exactly what the
        // no-slot fallback in restorePerAlgoSettings below exists to avoid.
        // The offset form: qFuzzyCompare is documented as not working when
        // either operand is 0.0, and an algorithm may legitimately default
        // splitRatio to 0.0. Matches AutotileConfig::operator== and
        // persistablePerAlgoSettings, which both use `1.0 + x`.
        const bool differsFromDefaults = !qFuzzyCompare(1.0 + m_config->splitRatio, 1.0 + oldAlgo->defaultSplitRatio())
            || m_config->masterCount != PhosphorTiles::AutotileDefaults::DefaultMasterCount
            || m_config->maxWindows != oldAlgo->defaultMaxWindows();
        // Create-OR-update, not write-only. The gate exists to avoid MINTING a
        // slot that merely echoes the defaults, but an existing slot must
        // still be refreshed: a user who tunes splitRatio, switches away and
        // back, then resets it to the algorithm's default would otherwise
        // leave the stale tuned value in the slot, and restorePerAlgoSettings
        // hands it back on the next switch — the reset silently reverts. This
        // is the shape of the Max Windows silent no-op.
        if (differsFromDefaults || m_config->savedAlgorithmSettings.contains(m_algorithmId)) {
            auto& entry = m_config->savedAlgorithmSettings[m_algorithmId];
            entry.splitRatio = m_config->splitRatio;
            entry.masterCount = m_config->masterCount;
            entry.maxWindows = m_config->maxWindows;
            // customParams are not touched here — only splitRatio/masterCount/maxWindows are engine-managed
        }
        // Deliberately NO else-branch. Not stamping a defaults-echoing slot is
        // the whole point of the gate; REMOVING an existing one would take
        // customParams with it, which this function does not own and the engine
        // reads back on every layout apply. A user who tuned only a custom
        // param would lose it by switching algorithms. persistablePerAlgoSettings
        // already drops genuinely-empty slots at persist time, and does so
        // correctly (it requires customParams.isEmpty() first, and baselines
        // maxWindows against the global override rather than the algorithm
        // default — the discrepancy behind the Max Windows silent no-op).
    }

    // Look up saved settings AFTER the save above — insertion may rehash the
    // QHash, invalidating any iterator obtained before the insert.
    auto savedIt = m_config->savedAlgorithmSettings.constFind(newId);

    // Restore per-algorithm split ratio, master count, and max windows from
    // saved settings, falling back to the algorithm's defaults when no saved
    // entry exists. Each algorithm keeps its own tuning across switches.
    auto restorePerAlgoSettings = [this](PhosphorTiles::TilingAlgorithm* algo,
                                         QHash<QString, AlgorithmSettings>::const_iterator it) {
        if (it != m_config->savedAlgorithmSettings.constEnd()) {
            m_config->splitRatio = it->splitRatio;
            m_config->masterCount = it->masterCount;
            m_config->maxWindows = it->maxWindows;
            return;
        }
        // No saved slot: fall back to the algorithm's own defaults, but do NOT
        // create a slot for them. A slot that merely echoes the defaults would be
        // persisted by writeBackTuning() and then show up in the config profile
        // diff as a change the user never made. The no-slot fallback is instead
        // reapplied on demand — here on switch, and in refreshConfigFromSettings
        // for the algorithm-unchanged path.
        m_config->splitRatio = algo->defaultSplitRatio();
        m_config->masterCount = PhosphorTiles::AutotileDefaults::DefaultMasterCount;
        m_config->maxWindows = algo->defaultMaxWindows();
    };

    if (newAlgo) {
        // Restore the new algorithm's saved tuning, or its defaults when it has
        // no saved entry. Identical whether switching from another algorithm or
        // initializing on the first-ever call (oldAlgo null): the save block
        // above already persisted the outgoing algorithm's values when present.
        restorePerAlgoSettings(newAlgo, savedIt);
        // Re-seed the restored ratio/count onto current-context states. Mirrors
        // propagateGlobalSplitRatio()/propagateGlobalMasterCount() with one
        // extra skip: Algorithm-overridden screens keep their effective
        // algorithm across this switch, so their live ratios/counts must
        // survive. That skip stays out of the shared propagate helpers because
        // the settings-refresh path must keep reaching those screens. No
        // m_userTuned* check needed: the selective drop above pruned the tuned
        // sets to exactly the Algorithm-overridden states skipped here.
        for (auto it = m_states.states().constBegin(); it != m_states.states().constEnd(); ++it) {
            const auto& key = it.key();
            if (!it.value() || key.desktop != currentKeyForScreen(key.screenId).desktop
                || key.activity != m_context.currentActivity() || hasAlgoOverride(key.screenId)) {
                continue;
            }
            if (!hasPerScreenOverride(key.screenId, PerScreenKeys::SplitRatio)) {
                it.value()->setSplitRatio(m_config->splitRatio);
            }
            if (!hasPerScreenOverride(key.screenId, PerScreenKeys::MasterCount)) {
                it.value()->setMasterCount(m_config->masterCount);
            }
        }
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
    // The outgoing global id, captured before the assignment below. The state
    // clear further down needs it to tell a screen that FOLLOWED the old global
    // from one pinned to its own algorithm, and by then m_algorithmId is the
    // incoming id and can no longer answer that.
    const QString previousAlgorithmId = m_algorithmId;
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
    // ONLY through the settings app's library pages and their context menus.
    // Per-screen / per-context applies that route through this method —
    // e.g. UnifiedLayoutController applying an autotile entry on the
    // current screen, or AutotileAdaptor::setAlgorithm from a script —
    // must not silently overwrite that global preference. Per-screen
    // assignments already carry the algorithm in the (screen, desktop,
    // activity) entry; the engine's m_algorithmId tracks the runtime
    // ambient algorithm and resyncs from defaultAutotileAlgorithm on the
    // next session start, which is the intended behaviour.
    //
    // maxWindows is deliberately NOT written back to the global
    // Tiling.Algorithm/MaxWindows key here. It is per-algorithm data, carried by
    // savedAlgorithmSettings and persisted by writeBackTuning. Writing the
    // incoming algorithm's defaultMaxWindows to the global key made a plain
    // algorithm switch look like a user edit of a setting the user never touched,
    // which then showed up as a spurious profile diff row.
    {
        m_writeBackGuardTimer.start();
        const QSignalBlocker blocker(engineSettings());
        writeBackTuning();
    }

    // Clear stale per-algorithm state, but only on states whose effective
    // algorithm follows the global one. Screens with a per-screen Algorithm
    // override keep their effective algorithm across this global switch (see
    // the retile loop below), so their split trees and script state are still
    // live and must survive.
    //
    // Split trees: cleared when switching away from a memory algorithm.
    // Without this, deserialized trees from a previous DwindleMemory session
    // persist after algorithm switch, wasting memory and risking confusion.
    //
    // Script state: the per-algorithm script-state bag is opaque state private
    // to the previous algorithm (e.g. an aligned grid's column fractions) with
    // no meaning to the next — a different scripted algorithm that also opts
    // into supportsScriptState must not inherit it. Unlike the split tree
    // (which two memory algorithms can meaningfully share), script state has
    // no cross-algorithm validity, so it is wiped on every effective change.
    //
    // Must happen BEFORE emitting algorithmChanged so that listeners see
    // consistent state (no stale trees from the old algorithm). Safe because
    // this point is reached only when the algorithm id changed (early return
    // above), so every non-overridden state's effective algorithm changed.
    // The resolver owns this: it is the same wipe a per-screen effective change
    // runs, and it is the only place that knows what each screen's states were
    // BUILT UNDER. Deriving that here from hasAlgoOverride cannot work — a
    // toggle-off has already dropped the in-memory override, so a screen pinned
    // by persisted settings reads as a follower and its rescued bag is destroyed
    // one step after the teardown saved it.
    m_configResolver->applyGlobalAlgorithmChange(previousAlgorithmId, m_algorithmId);

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
        // been updated yet (updateEngineScreens runs after). An immediate retile
        // would use effectiveAlgorithm() with the stale per-screen override (OLD algo),
        // producing wrong geometries and emitting a bad windowsTiled signal to KWin.
        // Deferring to the next event loop pass ensures per-screen overrides are current.
        //
        // Only retile screens that actually use the global algorithm (no per-screen
        // override). Screens with per-screen algorithm overrides are unaffected by
        // this global change and are handled by updateEngineScreens() when the
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
    // monitor power-off during a desktop switch). Only gate NEW state creation
    // (the factory is invoked by forKey only on a miss).
    return m_states.forKey(key, [&]() -> PhosphorTiles::TilingState* {
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
        // Recover a bag a teardown rescued for this key, when it was written by
        // the algorithm still in effect. Usually a no-op — the stash is empty
        // unless this key was torn down earlier in the session.
        restoreStashedScriptState(key, state);
        return state;
    });
}

void AutotileEngine::stashScriptState(const TilingStateKey& key, PhosphorTiles::TilingState* state)
{
    if (!state) {
        return;
    }
    const QJsonObject bag = state->scriptState();
    // Moves the tree out of the dying state rather than copying it: nothing else
    // will use it, and SplitTree is move-only by design.
    std::unique_ptr<PhosphorTiles::SplitTree> tree =
        state->splitTree() && !state->splitTree()->isEmpty() ? state->takeSplitTree() : nullptr;
    if (bag.isEmpty() && !tree) {
        // The state IS the truth for this key, so having neither erases any entry
        // stashed earlier rather than leaving it to shadow the emptiness. That is
        // what stops a wiped bag coming back: an algorithm switch clears the live
        // state, and without this erase a stale entry from a previous teardown
        // would still be sitting there, tagged with the algorithm the user has now
        // switched back to, ready to be handed to the next state.
        m_scriptStateStash.erase(key);
        return;
    }
    // The screen's effective algorithm RIGHT NOW is the one whose script wrote
    // this bag, which is why every caller harvests before dropping per-screen
    // overrides — after the drop, effectiveAlgorithmId() has already fallen back
    // to the global algorithm and would mislabel the bag.
    m_scriptStateStash.insert_or_assign(
        key, StashedScriptState{bag, std::move(tree), m_configResolver->effectiveAlgorithmId(key.screenId)});
}

void AutotileEngine::restoreStashedScriptState(const TilingStateKey& key, PhosphorTiles::TilingState* state)
{
    if (!state) {
        return;
    }
    const auto it = m_scriptStateStash.find(key);
    if (it == m_scriptStateStash.end()) {
        return;
    }
    const QString effectiveId = m_configResolver->effectiveAlgorithmId(key.screenId);
    if (it->second.algorithmId != effectiveId) {
        // Refuse, but do NOT erase. This is the only thing enforcing "bags never
        // cross algorithms" for stashed state, and it has to stay purely
        // read-only, because a mismatch here does not prove the bag is dead — it
        // may only mean the resolver is not authoritative yet.
        //
        // Concretely: this runs from a find-or-CREATE factory with many callers,
        // and Daemon::updateEngineScreens seeds window order for added screens
        // BEFORE its applyPerScreenConfig loop reinstates per-screen overrides.
        // Seeding materialises the state whenever it resolves a non-empty order.
        // In that window a screen pinned to its own algorithm resolves to the
        // GLOBAL one, so the tag mismatches for a bag that is about to become
        // valid again a few statements later. Erasing on that reading destroyed
        // the rescued bag on exactly the screens the stash exists for.
        //
        // Nothing accumulates as a result: a genuinely dead entry is cleared by
        // the next teardown of its key, whose harvest sees the state's empty bag
        // and erases (see stashScriptState).
        qCDebug(PhosphorTileEngine::lcTileEngine)
            << "Not restoring stashed script state for" << key.screenId << "desktop" << key.desktop << "activity"
            << key.activity << "- stashed under" << it->second.algorithmId << "but screen currently resolves to"
            << effectiveId;
        return;
    }
    // An entry can hold a tree and no bag, once an algorithm change has cleared
    // the bag but left a tree the incoming memory algorithm still owns. Writing
    // the empty bag through would clear a live one for no reason.
    if (it->second.scriptState.isEmpty()) {
        return;
    }
    // Left in the stash on a match too, so a transient lookup that materialises a
    // state and then discards it (updateStickyScreenPins takes and deletes
    // exactly such a state) cannot consume the bag with the state nobody kept.
    state->setScriptState(it->second.scriptState);
}

void AutotileEngine::restoreStashedSplitTree(const TilingStateKey& key, PhosphorTiles::TilingState* state,
                                             const PhosphorTiles::TilingAlgorithm* algo)
{
    // Only memory algorithms own a tree at all, so there is nothing to hand back
    // to any other kind. Note this deliberately does NOT skip a state that
    // already holds a tree — see the replace-rather-than-fill note below, which
    // is why the bag's "a live one is always newer" rule does not apply here.
    if (!state || !algo || !algo->supportsMemory()) {
        return;
    }
    const auto it = m_scriptStateStash.find(key);
    if (it == m_scriptStateStash.end() || !it->second.splitTree) {
        return;
    }
    const PhosphorTiles::SplitTree* const tree = it->second.splitTree.get();
    // The tree must describe THIS state's tiled windows, no more and no less.
    // Anything else means the window set moved while the state was gone, and a
    // tree that disagrees with the order would make the next syncTreeInsert
    // index against a layout that is not there.
    const QStringList leaves = tree->leafOrder();
    const QStringList tiled = state->tiledWindows();
    if (leaves.size() != tiled.size()
        || QSet<QString>(leaves.begin(), leaves.end()) != QSet<QString>(tiled.begin(), tiled.end())) {
        qCDebug(PhosphorTileEngine::lcTileEngine)
            << "Not restoring stashed split tree for" << key.screenId << "desktop" << key.desktop << "- describes"
            << leaves.size() << "windows but the state holds" << tiled.size();
        return;
    }
    // Replaces rather than fills a gap. TilingState::addWindow lazily creates a
    // tree as each window is re-added, so by now the state always holds a
    // freshly-built one carrying uniform default ratios — that regenerated tree
    // IS the layout reset this restore exists to undo.
    state->setSplitTree(std::move(it->second.splitTree));
    // One-shot, unlike the bag, and structurally so: the move above leaves the
    // entry's pointer null, so a later retile takes the early return. That
    // matters because this runs on EVERY retile — a tree left readable would be
    // re-applied over the user's next resize and pin the layout to the rescued
    // one. The bag's restore is deliberately non-consuming instead, because a
    // mismatched tag there may only mean the resolver is not authoritative yet;
    // there is no such ambiguity for a tree.
    //
    // The erase below is housekeeping, not the one-shot: it drops an entry that
    // now holds neither a bag nor a tree.
    if (it->second.scriptState.isEmpty()) {
        m_scriptStateStash.erase(it);
    }
}

void AutotileEngine::dropStashedScriptStatesForAlgorithmChange(const QString& screenId, const QString& newAlgorithmId)
{
    // Mirrors what the live wipe does to a state on the same change: the bag
    // always goes, the tree only when the incoming algorithm has no memory to
    // carry it. Two memory algorithms genuinely share a tree, so dropping it
    // here would lose a layout the live path would have kept.
    auto* registry = algorithmRegistry();
    PhosphorTiles::TilingAlgorithm* const newAlgo = registry ? registry->algorithm(newAlgorithmId) : nullptr;
    const bool keepTrees = newAlgo && newAlgo->supportsMemory();
    for (auto it = m_scriptStateStash.begin(); it != m_scriptStateStash.end();) {
        if (it->first.screenId != screenId || it->second.algorithmId == newAlgorithmId) {
            ++it;
            continue;
        }
        it->second.scriptState = QJsonObject{};
        if (!keepTrees) {
            it->second.splitTree.reset();
        }
        // A surviving tree now belongs to the incoming algorithm, so retag —
        // otherwise the next change would compare against the algorithm that
        // wrote a bag which is no longer there.
        it->second.algorithmId = newAlgorithmId;
        if (it->second.scriptState.isEmpty() && !it->second.splitTree) {
            it = m_scriptStateStash.erase(it);
        } else {
            ++it;
        }
    }
}

// Replaces an older screenStates() accessor that returned a const-ref to a
// QHash<TilingStateKey, PhosphorTiles::TilingState*> — that accessor leaked
// mutable PhosphorTiles::TilingState pointers via the const-reference loophole
// (const on the hash doesn't propagate to the pointed-to values), for a single
// caller that only needed desktop numbers. Callers that need the raw state map
// should add a purpose-built query method rather than iterating private state.
// The m_states map stays private (no public map accessor exists); per-screen
// lookup is available through tilingStateForScreen(screenId), which returns a
// (non-const) PhosphorTiles::TilingState* for the read/mutate sites that
// explicitly key off one screen. That accessor is public, so the restraint on
// mutating through it is convention only (not enforced by access level or
// friend): the intended writers are the engine's own call paths and the
// per-screen config resolver, while tests use it for read-only access.
QSet<int> AutotileEngine::desktopsWithActiveState() const
{
    QSet<int> out;
    out.reserve(m_states.stateCount());
    for (auto it = m_states.states().constBegin(); it != m_states.states().constEnd(); ++it) {
        out.insert(it.key().desktop);
    }
    return out;
}

void AutotileEngine::pruneStatesForDesktop(int removedDesktop)
{
    int pruned = 0;
    QStringList releasedWindows;
    QSet<QString> releasedScreens;
    m_states.removeStatesIf(
        [&](const TilingStateKey& key, PhosphorTiles::TilingState*) {
            return key.desktop == removedDesktop;
        },
        [&](const TilingStateKey& key, PhosphorTiles::TilingState* state) {
            // Drop the per-key user-tuned flags with the state so a reused desktop
            // number can't inherit a stale "tuned" skip in propagateGlobal*.
            m_userTunedSplitRatio.remove(key);
            m_userTunedMasterCount.remove(key);
            // Through the FULL teardown, not a bare deleteLater. A deleted
            // desktop's windows are ALIVE (KWin relocates them), so skipping it
            // lost each one's autotile slot snapshot into the unified record,
            // leaked its m_windowMinSizes entry for the session, and emitted no
            // windowsReleased — leaving the daemon's WTS and the effect's float
            // cache holding entries for windows this engine no longer manages.
            //
            // Both scope flags are false: the SCREEN survives this prune, only
            // one of its desktop contexts is going away. Draining the
            // screen-keyed overflow bucket would strip the surviving contexts'
            // overflow windows of their classification (capturePlacement then
            // mis-reads them as user floats and they stick floating), and
            // clearing the screen-keyed seed maps would destroy an in-flight
            // strict order for the current desktop.
            releaseScreenStateForTeardown(key.screenId, state, releasedWindows, /*drainOverflow=*/false,
                                          /*clearScreenOrderMaps=*/false);
            releasedScreens.insert(key.screenId);
            ++pruned;
        });
    // Clean up reverse-map entries that reference the pruned desktop BEFORE
    // emitting. Stale entries would pollute backfillWindows() and could
    // incorrectly match if desktop numbers are reused — and the daemon's
    // windowsReleased handler resolves each released window's screen and zone,
    // so emitting first would let it see phantom candidates keyed to the
    // desktop that just went away. Same ordering as pruneStatesForRemovedScreen.
    m_states.removeWindowsIf([&](const QString&, const TilingStateKey& key) {
        return key.desktop == removedDesktop;
    });
    // The tuned flags are inserted keyed by currentKeyForScreen whether or not a
    // state exists at that key, so the onRemove hook above only reaches the ones
    // whose key currently HOLDS a state. An orphaned flag would survive the
    // desktop's death and renumberDesktopState would then shift it onto a live
    // number — exactly the stale "tuned" skip the hook's own comment prevents.
    // Sweep both sets unconditionally.
    const auto eraseTunedForDesktop = [removedDesktop](QSet<TilingStateKey>& set) {
        for (auto it = set.begin(); it != set.end();) {
            if (it->desktop == removedDesktop) {
                it = set.erase(it);
            } else {
                ++it;
            }
        }
    };
    eraseTunedForDesktop(m_userTunedSplitRatio);
    eraseTunedForDesktop(m_userTunedMasterCount);
    // Stashed bags for the dead desktop go with it. Desktop NUMBERS are reused
    // after a renumber, so leaving them would hand a recreated key someone
    // else's layout.
    std::erase_if(m_scriptStateStash, [&](const auto& entry) {
        return entry.first.desktop == removedDesktop;
    });
    // Clear the sticky-pin override for entries referencing the removed desktop:
    // a screen pinned to a now-deleted desktop number must drop the entry (the
    // per-output desktop map is deliberately left alone — see pruneDesktop).
    m_context.pruneDesktop(removedDesktop);
    // Announce LAST, after every context sweep above: the daemon's
    // windowsReleased handler re-resolves each released window's screen and
    // zone, and it must not see this engine's half-swept context. Matches the
    // scroll engine's ordering and this file's own removed-screen arm.
    if (!releasedWindows.isEmpty()) {
        Q_EMIT windowsReleased(releasedWindows, releasedScreens);
    }
    if (pruned > 0) {
        qCInfo(PhosphorTileEngine::lcTileEngine)
            << "Pruned" << pruned << "TilingStates for removed desktop" << removedDesktop;
    }
}

void AutotileEngine::reapDesktopState(int desktop)
{
    // The prune below (shared with the count-derived caller) covers everything
    // except the drag-insert preview,
    // which it never cancels (screen removal does; the desktop axis predates
    // identity-based reaps). A preview anchored in the dying context must not
    // survive into a renumbered world. The live anchor is the TARGET SCREEN's
    // current desktop, not the global one — per-output desktops make those
    // diverge routinely, and a preview judged against the global current
    // could survive the reap and lazily recreate state at the dead key.
    if (m_dragInsertPreview
        && ((m_dragInsertPreview->hadPriorState && m_dragInsertPreview->priorKey.desktop == desktop)
            || m_context.currentKeyForScreen(m_dragInsertPreview->targetScreenId).desktop == desktop)) {
        cancelDragInsertPreview();
    }
    pruneStatesForDesktop(desktop);
}

void AutotileEngine::renumberDesktopState(const QHash<int, int>& oldToNew)
{
    // Gate the WHOLE pass on the shared validity rule, not just the two arms
    // that gate themselves. m_states.renumberDesktops and
    // m_context.renumberDesktops refuse a poisoned mapping outright, so
    // applying it to the tuned flags and the script stash below would shift
    // those onto numbers no key or pin moved to — the split state the
    // all-or-nothing rule exists to prevent.
    if (oldToNew.isEmpty() || !PhosphorEngine::desktopRenumberMappingIsValid(oldToNew)) {
        return;
    }
    // Cancelled unconditionally, even when the mapping does not touch the
    // preview's desktop. A drag-insert preview is transient by contract (it is
    // unwound by every prune and every teardown on this path), so the
    // over-cancel costs a preview the user is still holding a drag over and
    // nothing else, while a surviving preview anchored on a key that moved
    // under it would commit into the wrong context. The scroll engine's
    // renumber makes the same call for the same reason.
    if (m_dragInsertPreview) {
        cancelDragInsertPreview();
    }
    m_states.renumberDesktops(oldToNew);
    m_context.renumberDesktops(oldToNew);
    // Per-key tuned flags and stashed script bags move with their keys.
    const auto renumberKeySet = [&oldToNew](QSet<TilingStateKey>& set) {
        QSet<TilingStateKey> next;
        next.reserve(set.size());
        for (const TilingStateKey& key : std::as_const(set)) {
            TilingStateKey moved = key;
            moved.desktop = oldToNew.value(key.desktop, key.desktop);
            next.insert(moved);
        }
        set = next;
    };
    renumberKeySet(m_userTunedSplitRatio);
    renumberKeySet(m_userTunedMasterCount);
    // PRECONDITION: oldToNew is injective. A renumber names surviving desktops
    // after a removal, so two old numbers never map to one new one. If that
    // ever stopped holding, the emplace below would keep the first arrival and
    // silently drop the colliding stash (QSet's insert in renumberKeySet
    // dedupes the same way), which is the documented behaviour rather than an
    // arbitrary merge — a bag belongs to exactly one context.
    std::unordered_map<TilingStateKey, StashedScriptState> movedStash;
    movedStash.reserve(m_scriptStateStash.size());
    for (auto& [key, stash] : m_scriptStateStash) {
        TilingStateKey moved = key;
        moved.desktop = oldToNew.value(key.desktop, key.desktop);
        movedStash.emplace(moved, std::move(stash));
    }
    m_scriptStateStash = std::move(movedStash);
}

void AutotileEngine::pruneStatesForRemovedScreen(const QString& physicalScreenId)
{
    if (physicalScreenId.isEmpty()) {
        return;
    }
    // Match the physical id and every virtual sub-screen of it (samePhysical
    // strips the "/vs:N" suffix). All desktops/activities: this is the
    // whole-output reap that updateEngineScreens' current-context sweep
    // cannot perform.
    const auto matches = [&physicalScreenId](const QString& screenId) {
        return !screenId.isEmpty() && PhosphorIdentity::VirtualScreenId::samePhysical(screenId, physicalScreenId);
    };
    // Unwind a preview anchored on the dying output BEFORE the teardown, while
    // both its states still exist — a preview left over it would strand on a
    // dead key and commit would materialise a fresh empty state there. The
    // scroll twin's removed-screen prune cancels on exactly these two terms.
    if (m_dragInsertPreview
        && (matches(m_dragInsertPreview->targetScreenId)
            || (m_dragInsertPreview->hadPriorState && matches(m_dragInsertPreview->priorKey.screenId)))) {
        cancelDragInsertPreview();
    }
    int pruned = 0;
    QStringList releasedWindows;
    QSet<QString> releasedScreens;
    m_states.removeStatesIf(
        [&](const TilingStateKey& key, PhosphorTiles::TilingState*) {
            return matches(key.screenId);
        },
        [&](const TilingStateKey& key, PhosphorTiles::TilingState* state) {
            // Through the FULL teardown body, not a bare deleteLater: the
            // capture snapshots each window's autotile slot into the
            // unified record (the unplug used to get this via the
            // screens-set sweep), and the per-screen order maps are cleared
            // so a replug within PendingOrderTimeoutMs cannot seed
            // pre-unplug ghost ids. drainOverflow=false — several contexts
            // can share a screenId, so overflow drains once per screen
            // below, after all captures (same shape as the orphaned-VS
            // loop).
            releaseScreenStateForTeardown(key.screenId, state, releasedWindows, /*drainOverflow=*/false);
            releasedScreens.insert(key.screenId);
            m_userTunedSplitRatio.remove(key);
            m_userTunedMasterCount.remove(key);
            ++pruned;
        });
    for (const QString& screenId : std::as_const(releasedScreens)) {
        m_overflow.takeForScreen(screenId);
    }
    // One key-matching sweep only: it is a superset of a released-window
    // sweep for every window keyed to the removed screen, and a window
    // whose reverse key points at a SURVIVING screen must keep its entry
    // (dropping it while the surviving state still holds the window would
    // manufacture an untracked ghost).
    m_states.removeWindowsIf([&](const QString&, const TilingStateKey& key) {
        return matches(key.screenId);
    });
    // Stashed bags go too: a stale stash must not replay someone else's
    // layout if the connector id ever returns (same policy as the desktop
    // prune above; a replug is a fresh start).
    std::erase_if(m_scriptStateStash, [&](const auto& entry) {
        return matches(entry.first.screenId);
    });
    // Whole-screen reap: FORGET the resolver state for the physical id and
    // every virtual sub-screen (overrides AND remembered algorithm ids —
    // remembering for a screen with zero remaining states is dead
    // bookkeeping, the same treatment as the orphaned-VS teardown). The
    // persisted per-screen settings survive, matching the toggle-off
    // contract. Per-id, because the resolver maps key on the EFFECTIVE
    // (possibly "/vs:N") id, not the physical one.
    // removeOverridesMatching alone: samePhysical(physicalScreenId,
    // physicalScreenId) is true, so the predicate sweep already covers the
    // bare physical id along with every virtual sub-screen.
    m_configResolver->removeOverridesMatching(matches);
    // Order maps for STATELESS sub-screens (seed pushed before any window
    // arrived) — the teardown body above only cleared the stateful ones.
    for (auto it = m_pendingInitialOrders.begin(); it != m_pendingInitialOrders.end();) {
        if (matches(it.key())) {
            m_pendingOrderGeneration.remove(it.key());
            m_strictInitialOrderScreens.remove(it.key());
            it = m_pendingInitialOrders.erase(it);
        } else {
            ++it;
        }
    }
    // The four per-screen retile/focus maps the sibling teardown in
    // setAutotileScreens sweeps for removed screens — a replugged connector
    // must not consume a stale focus entry or retry a dead retile.
    m_pendingFocusByScreen.removeIf([&matches](const auto& entry) {
        return matches(entry.key());
    });
    m_pendingRetileScreens.removeIf([&matches](const QString& screenId) {
        return matches(screenId);
    });
    m_retileRetryScreens.removeIf([&matches](const QString& screenId) {
        return matches(screenId);
    });
    m_retileRetryCount.removeIf([&matches](const auto& entry) {
        return matches(entry.key());
    });
    if (matches(m_activeScreen)) {
        // A dead screen id must not keep feeding hint-less shortcut paths.
        m_activeScreen.clear();
    }
    m_context.removeScreensIf(matches);
    // Drop the dead output from the active set BEFORE the emit, the same way
    // the scroll twin does it (engine_context.cpp): the daemon's screenRemoved
    // path never runs updateEngineScreens, so until the next recompute
    // isActiveOnScreen would keep answering true for an output that is gone.
    // The windowsReleased handler asks exactly that question to tell a context
    // prune (engine still runs there) from a genuine release, so a stale claim
    // makes every unplugged autotile window skip its float clear and restore.
    for (auto it = m_autotileScreens.begin(); it != m_autotileScreens.end();) {
        it = matches(*it) ? m_autotileScreens.erase(it) : std::next(it);
    }
    if (!releasedWindows.isEmpty()) {
        Q_EMIT windowsReleased(releasedWindows, releasedScreens);
    }
    if (pruned > 0) {
        qCInfo(PhosphorTileEngine::lcTileEngine)
            << "Pruned" << pruned << "TilingStates for removed screen" << physicalScreenId;
    }
}

void AutotileEngine::pruneStatesForActivities(const QStringList& validActivities)
{
    const QSet<QString> valid(validActivities.begin(), validActivities.end());
    const auto stale = [&valid](const QString& activity) {
        return !activity.isEmpty() && !valid.contains(activity);
    };
    // Same preview unwind as the desktop and removed-screen prunes, on the
    // activity axis (the scroll sibling's activity prune cancels on exactly
    // these terms). The TARGET's live key, not the global current: per-output
    // desktops aside, the preview's target screen resolves its own context.
    if (m_dragInsertPreview
        && ((m_dragInsertPreview->hadPriorState && stale(m_dragInsertPreview->priorKey.activity))
            || stale(m_context.currentKeyForScreen(m_dragInsertPreview->targetScreenId).activity))) {
        cancelDragInsertPreview();
    }
    int pruned = 0;
    QStringList releasedWindows;
    QSet<QString> releasedScreens;
    m_states.removeStatesIf(
        [&](const TilingStateKey& key, PhosphorTiles::TilingState*) {
            return !key.activity.isEmpty() && !valid.contains(key.activity);
        },
        [&](const TilingStateKey& key, PhosphorTiles::TilingState* state) {
            m_userTunedSplitRatio.remove(key);
            m_userTunedMasterCount.remove(key);
            // Full teardown, for the same reasons as the desktop prune above:
            // record snapshot, min-size cleanup and a windowsReleased so the
            // daemon and effect stop tracking windows this engine has dropped.
            // Both scope flags false for the same reason too — the screen
            // survives, only this activity's contexts are going away.
            releaseScreenStateForTeardown(key.screenId, state, releasedWindows, /*drainOverflow=*/false,
                                          /*clearScreenOrderMaps=*/false);
            releasedScreens.insert(key.screenId);
            ++pruned;
        });
    // Reverse-map cleanup BEFORE the emit, same reason as the desktop prune
    // above: the daemon's windowsReleased handler resolves each window's
    // screen and zone, and a stale mapping to the pruned activity would hand
    // it a phantom candidate.
    m_states.removeWindowsIf([&](const QString&, const TilingStateKey& key) {
        return !key.activity.isEmpty() && !valid.contains(key.activity);
    });
    // Unconditional tuned-flag sweep, for the same reason as the desktop prune:
    // the flags are keyed by currentKeyForScreen whether or not a state exists
    // there, so the hook above cannot reach an orphaned one.
    const auto eraseTunedForStaleActivity = [&stale](QSet<TilingStateKey>& set) {
        for (auto it = set.begin(); it != set.end();) {
            if (stale(it->activity)) {
                it = set.erase(it);
            } else {
                ++it;
            }
        }
    };
    eraseTunedForStaleActivity(m_userTunedSplitRatio);
    eraseTunedForStaleActivity(m_userTunedMasterCount);
    std::erase_if(m_scriptStateStash, [&](const auto& entry) {
        return !entry.first.activity.isEmpty() && !valid.contains(entry.first.activity);
    });
    // The tracked current activity can name one that just died, and it keeps
    // naming it until the compositor's currentActivityChanged arrives. Until
    // then currentKeyForScreen hands out keys under a dead activity and
    // PerScreenStates::forKey lazily rebuilds state there, undoing this prune
    // one placement at a time.
    const QString currentActivity = m_context.currentActivity();
    if (stale(currentActivity)) {
        m_context.pruneActivity(currentActivity);
    }
    // Announce LAST, after every context sweep above, matching the desktop arm.
    if (!releasedWindows.isEmpty()) {
        Q_EMIT windowsReleased(releasedWindows, releasedScreens);
    }
    if (pruned > 0) {
        qCInfo(PhosphorTileEngine::lcTileEngine) << "Pruned" << pruned << "TilingStates for removed activities";
    }
}

} // namespace PhosphorTileEngine
