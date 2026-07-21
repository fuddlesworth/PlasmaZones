// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorTileEngine/PerScreenConfigResolver.h>
#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/ITileAlgorithmRegistry.h>
#include <PhosphorTileEngine/AutotileConfig.h>
#include <PhosphorTiles/TilingAlgorithm.h>
#include <PhosphorTiles/TilingState.h>
#include <PhosphorEngine/GapResolution.h>
#include <PhosphorEngine/PerScreenKeys.h>
#include <PhosphorTiles/AutotileConstants.h>
#include "tileenginelogging.h"

namespace PhosphorTileEngine {

namespace PerScreenKeys = PhosphorEngine::PerScreenKeys;

PerScreenConfigResolver::PerScreenConfigResolver(AutotileEngine* engine)
    : m_engine(engine)
{
}

// ═══════════════════════════════════════════════════════════════════════════════
// Per-screen override storage
// ═══════════════════════════════════════════════════════════════════════════════

void PerScreenConfigResolver::applyPerScreenConfig(const QString& screenId, const QVariantMap& overrides)
{
    if (screenId.isEmpty()) {
        return;
    }

    if (overrides.isEmpty()) {
        clearPerScreenConfig(screenId);
        return;
    }

    // Capture the previous override map BEFORE overwriting so removed stateful
    // keys can be reverted below (splitRatio / masterCount live on TilingState,
    // not re-resolved per retile like maxWindows / insertPosition / overflow).
    const QVariantMap previous = m_perScreenOverrides.value(screenId);

    // Store overrides so effective*() helpers and connectToSettings handlers
    // can resolve per-screen values and skip screens with overrides.
    m_perScreenOverrides[screenId] = overrides;

    // The PhosphorTiles::TilingState-level writes below act on the CURRENT
    // context's state and are skipped when there is none. The wipe and the retile
    // schedule that follow are NOT gated on it: the wipe walks every
    // (desktop, activity) state of the screen itself, and tilingStateForScreen
    // returns nullptr for an UNKNOWN screen even when other contexts' states for
    // that screen exist and survive. Returning early here would record the new
    // effective algorithm while those states kept the old algorithm's split tree
    // and script state forever — the next applyPerScreenConfig sees `previous`
    // already carrying the new id and reads the effective algorithm as unchanged.
    if (PhosphorTiles::TilingState* state = m_engine->tilingStateForScreen(screenId)) {
        // Apply PhosphorTiles::TilingState-level overrides (splitRatio, masterCount).
        // These are STATEFUL: an unset key normally means "leave the state alone" so a
        // user's live drag-adjusted split / master count survives an unrelated
        // updateAutotileScreens pass. But if the key was PRESENT in the previous map
        // and is now gone (a SetSplitRatio / SetMasterCount rule was removed while the
        // map stayed non-empty), the state would keep the stale rule value — so revert
        // it to the global config baseline, matching clearPerScreenConfig.
        auto it = overrides.constFind(QString(PerScreenKeys::SplitRatio));
        if (it != overrides.constEnd()) {
            state->setSplitRatio(qBound(PhosphorTiles::AutotileDefaults::MinSplitRatio, it->toDouble(),
                                        PhosphorTiles::AutotileDefaults::MaxSplitRatio));
        } else if (previous.contains(QString(PerScreenKeys::SplitRatio))) {
            // Removed (was an override, now gone) → revert to the config baseline. NOT
            // gated on the Algorithm key: concrete-algorithm screens ALWAYS carry the
            // injected Algorithm key, so an Algorithm-presence guard would leave the stale
            // removed-rule ratio on exactly those screens. Branch 3 below runs AFTER this,
            // so on a GENUINE algorithm change it overwrites this baseline with the new
            // algo's default (the correct end state); on an unchanged / absent algorithm
            // this baseline stands. A never-was-an-override live value is untouched (the
            // previous.contains guard). The state now holds the global baseline, so the
            // tuned flag that protected the old value from propagateGlobalSplitRatio
            // goes with it — same key as the write, matching the Algorithm arm below.
            state->setSplitRatio(m_engine->config()->splitRatio);
            m_engine->m_userTunedSplitRatio.remove(m_engine->currentKeyForScreen(screenId));
        }

        it = overrides.constFind(QString(PerScreenKeys::MasterCount));
        if (it != overrides.constEnd()) {
            state->setMasterCount(qBound(PhosphorTiles::AutotileDefaults::MinMasterCount, it->toInt(),
                                         PhosphorTiles::AutotileDefaults::MaxMasterCount));
        } else if (previous.contains(QString(PerScreenKeys::MasterCount))) {
            // Removed → revert to the global config baseline (no per-algorithm default).
            // Drop the tuned flag with the value it protected, current key only —
            // same rule as the SplitRatio arm above.
            state->setMasterCount(m_engine->config()->masterCount);
            m_engine->m_userTunedMasterCount.remove(m_engine->currentKeyForScreen(screenId));
        }

        // If the per-screen algorithm ACTUALLY CHANGED and split ratio wasn't
        // explicitly overridden, reset it to the new algorithm's default — matching
        // setAlgorithm(), which early-returns when the id is unchanged. The daemon
        // always injects the Algorithm key for concrete-algorithm autotile screens, so
        // gating on mere key PRESENCE would reset the split ratio on every unrelated
        // override change (e.g. a SetMaxWindows rule edit, now that a rule edit rebuilds
        // the overrides map and re-applies), silently discarding a user's live
        // drag-tuned ratio. Comparing against the previous EFFECTIVE algorithm (the
        // previous map's entry, falling back to the global id exactly like the wipe
        // helper below) fires the reset only on a genuine switch: an override added
        // with the same id the screen already followed globally is not a change.
        // Dropping the tuning on a real change is correct (setAlgorithm clears user
        // tunings on a real change too), so the tuned-ratio flag is erased alongside
        // the value it described — for the CURRENT key only, the one state actually
        // reset here. Dropping every (desktop, activity) key of the screen would
        // strand the other contexts' tuned VALUES without the flag that protects
        // them, and the next propagateGlobalSplitRatio would overwrite them with the
        // GLOBAL algorithm's ratio once their desktop becomes current — wrong for a
        // screen pinned to a per-screen algorithm. (setAlgorithm's all-contexts drop
        // is coherent because m_config->splitRatio is by then the incoming
        // algorithm's restored value; here it is not.)
        it = overrides.constFind(QString(PerScreenKeys::Algorithm));
        if (it != overrides.constEnd()) {
            const QString algoId = it->toString();
            const QString previousAlgo =
                previous.value(QString(PerScreenKeys::Algorithm), m_engine->m_algorithmId).toString();
            if (algoId != previousAlgo && !overrides.contains(QString(PerScreenKeys::SplitRatio))) {
                // Guard the registry locally: AutotileEngine's ctor deliberately
                // does not assert its dependencies, so every method that
                // dereferences one guards it here (same rule as the wipe helper).
                auto* registry = m_engine->algorithmRegistry();
                if (auto* newAlgo = registry ? registry->algorithm(algoId) : nullptr) {
                    state->setSplitRatio(newAlgo->defaultSplitRatio());
                    m_engine->m_userTunedSplitRatio.remove(m_engine->currentKeyForScreen(screenId));
                }
            }
        }
    }

    // Wipe stale per-algorithm state when this override change moved the
    // screen's effective algorithm (Algorithm key added, swapped, or removed).
    // The new effective id falls back to the engine's global algorithm when the
    // key is absent, so an Algorithm-override REMOVAL that lands the screen on a
    // different global algorithm wipes too — setAlgorithm()'s own clear loop
    // deliberately skips overridden screens, and in the applyEntry transaction
    // it runs while the stale override is still stored, so this is the only
    // place that sees the removal.
    //
    // Both fallbacks read m_algorithmId LIVE, and that is correct under either
    // order the two halves of a global-switch transaction can run in:
    //
    //  * overrides first (applyEntry: assignLayoutById → layoutAssigned →
    //    updateAutotileScreens → here, and only then setAlgorithm at
    //    UnifiedLayoutController::applyEntry) — m_algorithmId is still the OLD
    //    global, which is exactly what a screen with no Algorithm key in
    //    `previous` was following, so an override pinning that same id reads as
    //    "no change" and nothing is wiped or reset.
    //  * setAlgorithm first (Daemon::handleSnappingToAutotile, applySettings)
    //    — m_algorithmId is the NEW global, and that is equally truthful: a
    //    screen reaching the fallback has no Algorithm override, so
    //    setAlgorithm's own clear loop did not skip it. It already wiped that
    //    screen's bags and re-seeded its ratio to the new global algorithm's
    //    value, so the new global IS the algorithm the screen's state currently
    //    describes.
    //
    // The fallback is only ever consulted when `previous` carries no Algorithm
    // key, which is precisely the case setAlgorithm keeps up to date — so do
    // NOT replace it with a pre-switch snapshot threaded in from the caller:
    // that would report a change the screen's state had already been moved past.
    wipeStateBagsOnEffectiveAlgorithmChange(
        screenId, previous.value(QString(PerScreenKeys::Algorithm), m_engine->m_algorithmId).toString(),
        overrides.value(QString(PerScreenKeys::Algorithm), m_engine->m_algorithmId).toString());

    // The remaining overrides are NOT written to the state here — they are
    // resolved at retile time via effective*() helpers in recalculateLayout():
    // gaps (InnerGap, OuterGap, SmartGaps), RespectMinimumSize, MaxWindows,
    // InsertPosition, OverflowBehavior, and CustomParams. Only SplitRatio and
    // MasterCount are stateful (handled above), which is why only those two need
    // an explicit revert-on-removal.

    // Schedule a deferred retile so the new config takes effect. Deferred (not
    // immediate) to coalesce with other pending retiles — e.g., when applyEntry()
    // triggers both updateAutotileScreens() → applyPerScreenConfig() and
    // setAlgorithm() → scheduleRetileForScreen(), a single retile fires with
    // all state consistent, avoiding the double-D-Bus-signal problem that caused
    // stagger generation conflicts and window overlap during algorithm switches.
    if (m_engine->isAutotileScreen(screenId)) {
        m_engine->scheduleRetileForScreen(screenId);
    }

    qCDebug(PhosphorTileEngine::lcTileEngine)
        << "Applied per-screen config for" << screenId << "keys:" << overrides.keys();
}

void PerScreenConfigResolver::clearPerScreenConfig(const QString& screenId)
{
    // take() instead of remove(): the outgoing map is needed below to detect a
    // removed Algorithm override. An empty map means the screen had no
    // overrides (applyPerScreenConfig never stores an empty map).
    const QVariantMap previous = m_perScreenOverrides.take(screenId);
    if (previous.isEmpty()) {
        return;
    }
    // Restore global defaults on PhosphorTiles::TilingState
    PhosphorTiles::TilingState* state = m_engine->tilingStateForScreen(screenId);
    if (state) {
        // The state now holds the global baseline, so the tuned flag that
        // protected the old value from propagateGlobalSplitRatio goes with it —
        // current key only, matching the one state written here (same rule as
        // applyPerScreenConfig's revert and Algorithm arms).
        state->setSplitRatio(m_engine->config()->splitRatio);
        m_engine->m_userTunedSplitRatio.remove(m_engine->currentKeyForScreen(screenId));
        state->setMasterCount(m_engine->config()->masterCount);
        m_engine->m_userTunedMasterCount.remove(m_engine->currentKeyForScreen(screenId));
    }

    // Removing an Algorithm override lands the screen back on the global
    // algorithm; if that differs from the overridden one, the old algorithm's
    // state bags are stale (same rationale as in applyPerScreenConfig).
    wipeStateBagsOnEffectiveAlgorithmChange(
        screenId, previous.value(QString(PerScreenKeys::Algorithm), m_engine->m_algorithmId).toString(),
        m_engine->m_algorithmId);

    // Schedule deferred retile (same rationale as applyPerScreenConfig)
    if (m_engine->isAutotileScreen(screenId)) {
        m_engine->scheduleRetileForScreen(screenId);
    }

    qCDebug(PhosphorTileEngine::lcTileEngine) << "Cleared per-screen config for" << screenId;
}

QVariantMap PerScreenConfigResolver::perScreenOverrides(const QString& screenId) const
{
    return m_perScreenOverrides.value(screenId);
}

bool PerScreenConfigResolver::hasPerScreenOverride(const QString& screenId, const QString& key) const
{
    auto it = m_perScreenOverrides.constFind(screenId);
    return it != m_perScreenOverrides.constEnd() && it->contains(key);
}

void PerScreenConfigResolver::updatePerScreenOverride(const QString& screenId, const QString& key,
                                                      const QVariant& value)
{
    auto it = m_perScreenOverrides.find(screenId);
    if (it == m_perScreenOverrides.end()) {
        qCWarning(PhosphorTileEngine::lcTileEngine)
            << "updatePerScreenOverride: no override map for screen" << screenId << "- cannot update key" << key;
        return;
    }
    (*it)[key] = value;
}

void PerScreenConfigResolver::removeOverridesForScreen(const QString& screenId)
{
    // take(), not remove(): dropping an Algorithm override moves the screen's
    // effective algorithm back to the global one, so the same wipe the other two
    // drop paths run is owed here. Both callers tear down only SOME of the
    // screen's states — the autotile toggle-off path prunes the current
    // (desktop, activity) context only and leaves the others live — and those
    // survivors' split trees and script state were built under the OVERRIDDEN
    // algorithm. Without the wipe they cross algorithms and nothing later
    // notices: a re-enable that lands the screen on a bare-autotile map with no
    // Algorithm key computes old == new == global and wipes nothing.
    //
    // Safe from inside the callers' PerScreenStates::removeStatesIf() callbacks:
    // the wipe only reads states() (a const ref, no detach) and mutates the
    // TilingState objects, never the hash the caller is iterating. The
    // orphaned-virtual-screen caller removes every context of the id anyway, so
    // there the wipe is a harmless no-op on states about to be destroyed. The
    // helper itself no-ops when the two ids match, so a screen whose override
    // named the global algorithm keeps its bags.
    const QVariantMap previous = m_perScreenOverrides.take(screenId);
    wipeStateBagsOnEffectiveAlgorithmChange(
        screenId, previous.value(QString(PerScreenKeys::Algorithm), m_engine->m_algorithmId).toString(),
        m_engine->m_algorithmId);
}

void PerScreenConfigResolver::wipeStateBagsOnEffectiveAlgorithmChange(const QString& screenId,
                                                                      const QString& oldEffectiveId,
                                                                      const QString& newEffectiveId)
{
    // Invariant: state bags never cross algorithms. Script state is opaque
    // state private to the algorithm that wrote it (e.g. an aligned grid's
    // column fractions) with no meaning to any other; split trees only carry
    // over between memory algorithms. AutotileEngine::setAlgorithm() enforces
    // this for global switches but deliberately skips Algorithm-overridden
    // screens, so per-screen effective changes — an override added, swapped, or
    // removed — are enforced here, on every (desktop, activity) state of the
    // screen (they all followed the old effective algorithm). The deferred
    // retile the callers schedule does NOT rebuild these bags; it reads them.
    if (oldEffectiveId == newEffectiveId) {
        return;
    }
    auto* registry = m_engine->algorithmRegistry();
    PhosphorTiles::TilingAlgorithm* newAlgo = registry ? registry->algorithm(newEffectiveId) : nullptr;
    const bool clearSplitTrees = newAlgo && !newAlgo->supportsMemory();
    const auto& states = m_engine->m_states.states();
    for (auto it = states.constBegin(); it != states.constEnd(); ++it) {
        if (!it.value() || it.key().screenId != screenId) {
            continue;
        }
        if (clearSplitTrees) {
            it.value()->clearSplitTree();
        }
        it.value()->setScriptState({});
    }
    // Stashed bags are deliberately NOT dropped here. Each carries its own
    // algorithm tag and AutotileEngine::restoreStashedScriptState re-checks it
    // per key, which is strictly more precise than a screen-wide drop: this
    // helper's old/new pair describes the screen as a whole, while the stash
    // holds one entry per (desktop, activity) context, including contexts this
    // wipe never walked because their state was already torn down.
    //
    // A screen-wide drop here also broke the toggle-off rescue outright. That
    // path harvests the bag and THEN calls removeOverridesForScreen, whose
    // in-memory override teardown reaches this helper as override -> global.
    // The ids differ, so the drop erased the bag one step after it was rescued,
    // on exactly the screens pinned to their own algorithm. The persisted
    // per-screen settings survive that teardown and re-derive the same
    // algorithm on re-enable, so the tag matches and the bag is handed back —
    // which is the correct outcome and the one the drop prevented.
}

// ═══════════════════════════════════════════════════════════════════════════════
// Effective per-screen values
// ═══════════════════════════════════════════════════════════════════════════════

std::optional<QVariant> PerScreenConfigResolver::perScreenOverride(const QString& screenId, const QString& key) const
{
    auto it = m_perScreenOverrides.constFind(screenId);
    if (it != m_perScreenOverrides.constEnd()) {
        auto git = it->constFind(key);
        if (git != it->constEnd()) {
            return *git;
        }
    }
    return std::nullopt;
}

namespace {
// Clamp a raw gap value to [MinGap, MaxGap]. The ceiling is tied to the
// snapping side by a static_assert in the daemon; the floor is a fixed 0 on
// both sides (snapping has no MinGap constant to tie against).
int clampGap(int v)
{
    return qBound(PhosphorTiles::AutotileDefaults::MinGap, v, PhosphorTiles::AutotileDefaults::MaxGap);
}
} // namespace

QVariantMap PerScreenConfigResolver::contextGapMap(const QString& screenId) const
{
    if (!m_contextGapProvider) {
        return {};
    }
    return m_contextGapProvider(screenId);
}

std::optional<int> PerScreenConfigResolver::contextGapFromMap(const QVariantMap& ctx, QLatin1String key)
{
    return PhosphorEngine::GapResolution::gapFromOverrideMap(ctx, key, clampGap);
}

std::optional<::PhosphorLayout::EdgeGaps> PerScreenConfigResolver::contextOuterGapsFromMap(const QVariantMap& ctx) const
{
    // Resolve the per-context (window-rule) outer-gap override as ONE atomic
    // layer via the shared PhosphorEngine::GapResolution (the snapping
    // pipeline's resolveOuterGapsFromMap in src/core/geometryutils.cpp calls
    // the same helper): per-side values are honoured only when the rule set
    // UsePerSideOuterGap, and if the layer yields any outer gap it wins
    // wholesale — no per-key blending with the static per-screen layer below.
    // Autotile clamps every map-sourced value; missing sides fall back to the
    // layer's own uniform OuterGap, else the global config.
    return PhosphorEngine::GapResolution::outerGapsFromOverrideMap(ctx, m_engine->config()->outerGap, clampGap);
}

int PerScreenConfigResolver::effectiveInnerGap(const QString& screenId) const
{
    if (auto ctx = contextGapFromMap(contextGapMap(screenId), PerScreenKeys::InnerGap))
        return *ctx;
    if (auto v = perScreenOverride(screenId, QString(PerScreenKeys::InnerGap)))
        return clampGap(v->toInt());
    return m_engine->config()->innerGap;
}

int PerScreenConfigResolver::outerGapBase(const QString& screenId, const QVariantMap& ctx) const
{
    if (auto c = contextGapFromMap(ctx, PerScreenKeys::OuterGap))
        return *c;
    if (auto v = perScreenOverride(screenId, QString(PerScreenKeys::OuterGap)))
        return clampGap(v->toInt());
    return m_engine->config()->outerGap;
}

::PhosphorLayout::EdgeGaps PerScreenConfigResolver::effectiveOuterGaps(const QString& screenId) const
{
    // Resolve the context layer ONCE: the provider runs a full
    // LayoutRegistry::resolveContextGaps on every call, and both the atomic
    // context layer and the per-side base below consult it.
    const QVariantMap ctx = contextGapMap(screenId);

    // Highest precedence: the per-context override, resolved as one atomic layer.
    if (auto ctxGaps = contextOuterGapsFromMap(ctx))
        return *ctxGaps;

    // Per-screen per-side overrides next.
    auto topOv = perScreenOverride(screenId, QString(PerScreenKeys::OuterGapTop));
    auto bottomOv = perScreenOverride(screenId, QString(PerScreenKeys::OuterGapBottom));
    auto leftOv = perScreenOverride(screenId, QString(PerScreenKeys::OuterGapLeft));
    auto rightOv = perScreenOverride(screenId, QString(PerScreenKeys::OuterGapRight));

    // If any per-screen per-side override exists, build from those
    if (topOv || bottomOv || leftOv || rightOv) {
        // Use the uniform gap as base, then per-side overrides on top
        const int base = outerGapBase(screenId, ctx);
        return ::PhosphorLayout::EdgeGaps{
            topOv ? clampGap(topOv->toInt()) : base, bottomOv ? clampGap(bottomOv->toInt()) : base,
            leftOv ? clampGap(leftOv->toInt()) : base, rightOv ? clampGap(rightOv->toInt()) : base};
    }

    // Check per-screen uniform outer gap
    if (auto v = perScreenOverride(screenId, QString(PerScreenKeys::OuterGap))) {
        return ::PhosphorLayout::EdgeGaps::uniform(clampGap(v->toInt()));
    }

    // Fall back to global config
    const AutotileConfig* cfg = m_engine->config();
    if (cfg->usePerSideOuterGap) {
        return ::PhosphorLayout::EdgeGaps{cfg->outerGapTop, cfg->outerGapBottom, cfg->outerGapLeft, cfg->outerGapRight};
    }
    return ::PhosphorLayout::EdgeGaps::uniform(cfg->outerGap);
}

bool PerScreenConfigResolver::effectiveSmartGaps(const QString& screenId) const
{
    if (auto v = perScreenOverride(screenId, QString(PerScreenKeys::SmartGaps)))
        return v->toBool();
    return m_engine->config()->smartGaps;
}

QVariantMap PerScreenConfigResolver::effectiveCustomParamsOverride(const QString& screenId) const
{
    if (auto v = perScreenOverride(screenId, QString(PerScreenKeys::CustomParams))) {
        return v->toMap();
    }
    return {};
}

PhosphorTiles::AutotileOverflowBehavior
PerScreenConfigResolver::effectiveOverflowBehavior(const QString& screenId) const
{
    // Per-screen override (config store OR a folded-in context rule) → global config.
    if (auto v = perScreenOverride(screenId, QString(PerScreenKeys::OverflowBehavior))) {
        const int clamped = qBound(PhosphorTiles::AutotileDefaults::MinOverflowBehavior, v->toInt(),
                                   PhosphorTiles::AutotileDefaults::MaxOverflowBehavior);
        return static_cast<PhosphorTiles::AutotileOverflowBehavior>(clamped);
    }
    return m_engine->config()->overflowBehavior;
}

PhosphorTiles::AutotileInsertPosition PerScreenConfigResolver::effectiveInsertPosition(const QString& screenId) const
{
    // Per-screen override (config store OR a folded-in tiling rule) → global config.
    // The stored value is the enum's underlying int, clamped to the valid range.
    if (auto v = perScreenOverride(screenId, QString(PerScreenKeys::InsertPosition))) {
        const int clamped = qBound(PhosphorTiles::AutotileDefaults::MinInsertPosition, v->toInt(),
                                   PhosphorTiles::AutotileDefaults::MaxInsertPosition);
        return static_cast<PhosphorTiles::AutotileInsertPosition>(clamped);
    }
    return m_engine->config()->insertPosition;
}

bool PerScreenConfigResolver::effectiveRespectMinimumSize(const QString& screenId) const
{
    if (auto v = perScreenOverride(screenId, QString(PerScreenKeys::RespectMinimumSize)))
        return v->toBool();
    return m_engine->config()->respectMinimumSize;
}

int PerScreenConfigResolver::effectiveMaxWindows(const QString& screenId) const
{
    // 1. Explicit per-screen MaxWindows override — highest priority. Wins even
    //    over global Unlimited mode so users can clamp individual screens
    //    (e.g. keep a secondary monitor at maxWindows=3 while the primary
    //    runs unlimited).
    if (auto v = perScreenOverride(screenId, QString(PerScreenKeys::MaxWindows)))
        return qBound(PhosphorTiles::AutotileDefaults::MinMaxWindows, v->toInt(),
                      PhosphorTiles::AutotileDefaults::MaxMaxWindows);

    // 2. Unlimited: Krohnkite-style "no cap". Per-screen (a context
    //    SetOverflowBehavior rule or per-screen config override else global). The
    //    sentinel (PhosphorTiles::AutotileDefaults::UnlimitedMaxWindowsSentinel) is
    //    passed to std::min in recalculateLayout, making the clamp idempotent. Also
    //    opens onWindowAdded's gate (tiledWindowCount >= maxWin is never true).
    if (effectiveOverflowBehavior(screenId) == PhosphorTiles::AutotileOverflowBehavior::Unlimited) {
        return PhosphorTiles::AutotileDefaults::UnlimitedMaxWindowsSentinel;
    }

    // 3. When the per-screen algorithm differs from the global algorithm,
    //    the global m_config->maxWindows may be for the WRONG algorithm.
    //    E.g. global=master-stack(maxWindows=4) but per-screen=bsp(default=5).
    //    Use the per-screen algorithm's default — but only if the user hasn't
    //    explicitly customized global maxWindows away from the global algo's default.
    const QString screenAlgo = effectiveAlgorithmId(screenId);
    if (screenAlgo != m_engine->m_algorithmId) {
        // Registry guarded locally (AutotileEngine's ctor asserts nothing). With
        // no registry both lookups yield null, so this falls to the warning below
        // and returns the global maxWindows — the same outcome as an unknown id.
        auto* registry = m_engine->algorithmRegistry();
        auto* screenAlgoPtr = registry ? registry->algorithm(screenAlgo) : nullptr;
        auto* globalAlgoPtr = registry ? registry->algorithm(m_engine->m_algorithmId) : nullptr;
        if (screenAlgoPtr) {
            // Only override with per-screen default if global is still at its algo's default
            if (!globalAlgoPtr || m_engine->config()->maxWindows == globalAlgoPtr->defaultMaxWindows()) {
                return screenAlgoPtr->defaultMaxWindows();
            }
            // User explicitly customized global maxWindows — honor it
            return m_engine->config()->maxWindows;
        }
        qCWarning(PhosphorTileEngine::lcTileEngine)
            << "effectiveMaxWindows: unknown per-screen algorithm" << screenAlgo << "for screen" << screenId
            << "- falling back to global maxWindows";
    }

    // 4. Same algorithm globally and per-screen — use the global setting
    return m_engine->config()->maxWindows;
}

qreal PerScreenConfigResolver::effectiveSplitRatioStep(const QString& screenId) const
{
    if (auto v = perScreenOverride(screenId, QString(PerScreenKeys::SplitRatioStep)))
        return qBound(PhosphorTiles::AutotileDefaults::MinSplitRatioStep, v->toDouble(),
                      PhosphorTiles::AutotileDefaults::MaxSplitRatioStep);
    return m_engine->config()->splitRatioStep;
}

QString PerScreenConfigResolver::effectiveAlgorithmId(const QString& screenId) const
{
    // An override storing an empty id names no algorithm — treat it as absent
    // rather than returning "", which reads as "differs from the global id" and
    // sends effectiveMaxWindows down its unknown-algorithm warning arm on every
    // call for the same global fallback it would reach anyway.
    if (auto v = perScreenOverride(screenId, QString(PerScreenKeys::Algorithm))) {
        const QString id = v->toString();
        if (!id.isEmpty())
            return id;
    }
    return m_engine->m_algorithmId;
}

PhosphorTiles::TilingAlgorithm* PerScreenConfigResolver::effectiveAlgorithm(const QString& screenId) const
{
    // Registry guarded locally (AutotileEngine's ctor asserts nothing); null
    // registry resolves to no algorithm, same as an unknown id.
    auto* registry = m_engine->algorithmRegistry();
    return registry ? registry->algorithm(effectiveAlgorithmId(screenId)) : nullptr;
}

} // namespace PhosphorTileEngine
