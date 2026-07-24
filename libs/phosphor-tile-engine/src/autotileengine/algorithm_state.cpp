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
#include "engine_internal.h"

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
        qCWarning(PhosphorTileEngine::lcTileEngine)
            << "AutotileEngine: unknown algorithm" << newId << "- using default";
        newId = PhosphorTiles::AlgorithmRegistry::staticDefaultAlgorithmId();
    }

    if (m_algorithmId == newId) {
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
    if (m_algorithmEverSet && oldAlgo) {
        auto& entry = m_config->savedAlgorithmSettings[m_algorithmId];
        entry.splitRatio = m_config->splitRatio;
        entry.masterCount = m_config->masterCount;
        entry.maxWindows = m_config->maxWindows;
        // customParams are not touched here — only splitRatio/masterCount/maxWindows are engine-managed
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
    // ONLY through the Layouts page (or its sub-pages / context menus).
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
        // and Daemon::updateAutotileScreens seeds window order for added screens
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
    m_states.removeStatesIf(
        [&](const TilingStateKey& key, PhosphorTiles::TilingState*) {
            return key.desktop == removedDesktop;
        },
        [&](const TilingStateKey& key, PhosphorTiles::TilingState* state) {
            // Drop the per-key user-tuned flags with the state so a reused desktop
            // number can't inherit a stale "tuned" skip in propagateGlobal*.
            m_userTunedSplitRatio.remove(key);
            m_userTunedMasterCount.remove(key);
            state->deleteLater();
            ++pruned;
        });
    // Clean up reverse-map entries that reference the pruned desktop. Stale
    // entries would pollute backfillWindows() and could incorrectly match if
    // desktop numbers are reused.
    m_states.removeWindowsIf([&](const QString&, const TilingStateKey& key) {
        return key.desktop == removedDesktop;
    });
    // Stashed bags for the dead desktop go with it. Desktop NUMBERS are reused
    // after a renumber, so leaving them would hand a recreated key someone
    // else's layout.
    std::erase_if(m_scriptStateStash, [&](const auto& entry) {
        return entry.first.desktop == removedDesktop;
    });
    // Clear the sticky-pin override and the per-output virtual-desktop map (#648)
    // for entries referencing the removed desktop — a screen pinned to a
    // now-deleted desktop number must drop the entry; the effect re-reports the
    // screen's true (renumbered) desktop shortly after.
    m_context.pruneDesktop(removedDesktop);
    if (pruned > 0) {
        qCInfo(PhosphorTileEngine::lcTileEngine)
            << "Pruned" << pruned << "TilingStates for removed desktop" << removedDesktop;
    }
}

void AutotileEngine::pruneStatesForActivities(const QStringList& validActivities)
{
    const QSet<QString> valid(validActivities.begin(), validActivities.end());
    int pruned = 0;
    m_states.removeStatesIf(
        [&](const TilingStateKey& key, PhosphorTiles::TilingState*) {
            return !key.activity.isEmpty() && !valid.contains(key.activity);
        },
        [&](const TilingStateKey& key, PhosphorTiles::TilingState* state) {
            m_userTunedSplitRatio.remove(key);
            m_userTunedMasterCount.remove(key);
            state->deleteLater();
            ++pruned;
        });
    // Clean up reverse-map entries that reference pruned activities
    m_states.removeWindowsIf([&](const QString&, const TilingStateKey& key) {
        return !key.activity.isEmpty() && !valid.contains(key.activity);
    });
    std::erase_if(m_scriptStateStash, [&](const auto& entry) {
        return !entry.first.activity.isEmpty() && !valid.contains(entry.first.activity);
    });
    if (pruned > 0) {
        qCInfo(PhosphorTileEngine::lcTileEngine) << "Pruned" << pruned << "TilingStates for removed activities";
    }
}

} // namespace PhosphorTileEngine
