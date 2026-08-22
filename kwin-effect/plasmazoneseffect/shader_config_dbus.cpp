// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plasmazoneseffect.h"
#include "shader_internal.h"
#include "shader_resolve.h"
#include "window_query.h"

#include "compositor/windowanimator.h"
#include "tilinghandler/tilinghandler.h"

#include <PhosphorAnimation/AnimationLimits.h>
#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorAnimation/ProfileTree.h>
#include <PhosphorAnimation/ShaderProfile.h>
#include <PhosphorAnimation/ShaderProfileTree.h>
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorRules/ExclusionRules.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorRules/Rule.h>
#include <PhosphorRules/RuleSet.h>
#include <PhosphorRules/WindowQuery.h>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>

#include <QByteArray>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QLoggingCategory>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariant>
#include <QVariantMap>

#include <functional>
#include <memory>
#include <optional>
#include <utility>

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

namespace {

/// Bounded retry for a failed getAllRules fetch, matching the tiling
/// handler's bring-up fetch budget: a fresh external trigger resets it,
/// only the retry chain's own failures consume it.
constexpr int kRuleFetchRetryMax = 3;
constexpr int kRuleFetchRetryDelayMs = 1000;

/// Dedicated bound for the getAllRules round-trip. Deliberately NOT
/// Service::SyncCallTimeoutMs (500 ms), which sizes a single scalar property
/// Get: this reply carries the whole serialised RuleSet, and the daemon reads
/// and re-serialises the store to produce it. Qt's unbounded default (25 s) is
/// the other extreme — multiplied by the retry chain above it leaves
/// ActiveLayout-referencing rules withheld from the evaluator for over a
/// minute after a wedged daemon.
constexpr int kRuleFetchTimeoutMs = 5000;

/// Hard cap on the getAllRules payload before it reaches QJsonDocument::fromJson.
/// The receive-side QString allocation is bounded by libdbus's own
/// message-size limit, not by this cap — what it genuinely bounds is the
/// JSON parse (and, via the call site's pre-check on the QString length,
/// the UTF-8 conversion copy). Sized far above
/// any plausible rule store (a rule serialises to a few hundred bytes, so this
/// admits tens of thousands of them) — the cap is a safety net, not a limit
/// users can reach by authoring rules.
constexpr qsizetype kRuleFetchMaxPayloadBytes = 16 * 1024 * 1024;

/// Filter a daemon-published shader search-path array down to the entries the
/// registry may safely be pointed at.
///
/// Boundary validation on a list that crosses D-Bus and then decides where the
/// compositor reads shader source from. An entry must be a non-empty ABSOLUTE
/// path with no `..` component: a relative path resolves against the
/// compositor's cwd (which is neither the daemon's nor a meaningful shader
/// root), and a traversal component lets a mis-built or hand-edited entry aim
/// the registry at an arbitrary tree. The count cap bounds the per-lookup cost
/// the registry pays walking every registered root; it is sized far above any
/// real pack layout, so reaching it means the list is wrong.
QStringList validatedShaderSearchPaths(const QJsonArray& arr)
{
    constexpr int kMaxShaderSearchPaths = 64;
    QStringList paths;
    for (const QJsonValue& entry : arr) {
        if (!entry.isString()) {
            continue;
        }
        const QString path = entry.toString();
        if (path.isEmpty()) {
            continue;
        }
        if (!QFileInfo(path).isAbsolute()) {
            qCWarning(lcEffect) << "loadShaderRegistryFromDbus: rejecting non-absolute search path" << path;
            continue;
        }
        // The RAW components, not QDir::cleanPath's: cleanPath collapses `..`
        // away, so testing its output would pass every traversal through.
        // Rejecting the raw form keeps the registered root exactly what the
        // daemon named.
        if (path.split(QLatin1Char('/')).contains(QLatin1String(".."))) {
            qCWarning(lcEffect) << "loadShaderRegistryFromDbus: rejecting search path with a traversal component"
                                << path;
            continue;
        }
        if (paths.size() >= kMaxShaderSearchPaths) {
            qCWarning(lcEffect) << "loadShaderRegistryFromDbus: search-path list exceeds" << kMaxShaderSearchPaths
                                << "entries — dropping the remainder";
            break;
        }
        paths.append(path);
    }
    return paths;
}

/// Context fields NO effect-side resolver stamps onto its WindowQuery —
/// the effect twin of the daemon open-path's neverStampedFields()
/// (src/dbus/windowtrackingadaptor/rules.cpp), for the same reason: an
/// unstamped field is not inert. WindowQuery::valueForField returns an
/// ENGAGED empty string for string-valued context fields, so a positive
/// leaf on one correctly never matches, but a NEGATED leaf
/// (`None{TiledWindowCount ...}`) matches precisely BECAUSE the inner
/// leaf failed, and the rule fires for EVERY window. Dropping rules that
/// reference an unstamped field closes both polarities.
///
/// ScreenOrientation is deliberately NOT in this set, but the reason is
/// narrower than "always stamped": ruleQuery (window_filtering.cpp) stamps it
/// whenever the window's screen id resolves to an output, and falls back to a
/// centre-derived answer otherwise. A window whose screen resolves to neither
/// (an output that just disconnected, before the screen-change handling
/// catches up) keeps the engaged-empty stamp, so a negated orientation leaf
/// over-matches exactly those windows for that interval. That residual is
/// known and accepted: it is bounded by a real screen-topology transition
/// rather than by every session's bring-up, and holding orientation rules out
/// of the evaluator over it would cost more than it saves.
///
/// TiledWindowCount is the one context-cascade field with no effect-side
/// source at all.
///
/// ActiveLayout is CONDITIONAL, which is what @p activeLayoutsSeeded selects.
/// ruleQuery stamps it from the daemon's per-screen map, but that map does
/// not exist until the daemon's first push lands, and an unstamped
/// ActiveLayout reads as an engaged empty string for EVERY window — exactly
/// the negation hazard above. So while the map is unseeded the field joins
/// the never-stamped set and its rules are held out of the evaluator; the
/// seeding edge in TilingHandler::setActiveLayouts re-drives
/// loadRuleAnimationsFromDbus, which re-runs this filter with the field
/// admitted again.
const QSet<PhosphorRules::Field>& effectNeverStampedFields(bool activeLayoutsSeeded)
{
    // ColorScheme is never stamped effect-side (the daemon owns the palette
    // derivation; kwin's own palette is not the authority), so a rule
    // referencing it must be held out here for the same negated-leaf reason
    // as the other two — the daemon-side resolvers still honour it.
    static const QSet<PhosphorRules::Field> seeded = {
        PhosphorRules::Field::TiledWindowCount,
        PhosphorRules::Field::ColorScheme,
    };
    static const QSet<PhosphorRules::Field> unseeded = {
        PhosphorRules::Field::TiledWindowCount,
        PhosphorRules::Field::ColorScheme,
        PhosphorRules::Field::ActiveLayout,
    };
    return activeLayoutsSeeded ? seeded : unseeded;
}

/// The conditional half of the never-stamped set on its own, for asking
/// whether a dropped rule was dropped BECAUSE the map is unseeded (as
/// opposed to referencing TiledWindowCount, which no seeding edge ever
/// rescues).
const QSet<PhosphorRules::Field>& activeLayoutField()
{
    static const QSet<PhosphorRules::Field> field = {
        PhosphorRules::Field::ActiveLayout,
    };
    return field;
}

/// Drop the rules referencing a never-stamped field from an exclusion
/// slice before it reaches an effect-bound rule set (see
/// effectNeverStampedFields for why).
///
/// @p outActiveLayoutWithheld, when given, is set to true (never cleared —
/// the caller ORs the whole pass together) if any rule was dropped for
/// referencing ActiveLayout while the map is unseeded. That is the signal
/// the seeding edge in TilingHandler::setActiveLayouts gates its re-drive
/// on: with no such rule anywhere in the store, seeding admits nothing new
/// and the whole getAllRules + parse + updateAllDecorations pass is waste.
/// The caller's slices are already filtered to real candidates for their
/// respective rule sets, so a removal here is always a rule the effect
/// would otherwise have bound.
QList<PhosphorRules::Rule> withoutNeverStampedRules(QList<PhosphorRules::Rule> rules, bool activeLayoutsSeeded,
                                                    bool* outActiveLayoutWithheld = nullptr)
{
    const QSet<PhosphorRules::Field>& fields = effectNeverStampedFields(activeLayoutsSeeded);
    rules.removeIf([&fields, activeLayoutsSeeded, outActiveLayoutWithheld](const PhosphorRules::Rule& rule) {
        if (!rule.match.referencesAnyField(fields)) {
            return false;
        }
        if (!activeLayoutsSeeded && outActiveLayoutWithheld && rule.match.referencesAnyField(activeLayoutField())) {
            *outActiveLayoutWithheld = true;
        }
        return true;
    });
    return rules;
}

/// Parse a D-Bus setting variant containing a JSON-encoded string and
/// dispatch to one of two callers based on the document's top-level
/// shape. Used by the three `load*FromDbus` setting fetchers in
/// `shader_transitions.cpp` — `loadShaderProfileFromDbus`,
/// `loadMotionProfileTreeFromDbus`, `loadShaderRegistryFromDbus`. Each
/// loader differs only in (a) which shape it expects and (b) what it
/// does with the parsed JSON, so every other piece (UTF-8 decode,
/// document-shape check, malformed-payload warning text) collapses
/// into a single helper call. `loadRuleAnimationsFromDbus` is the
/// odd one out — it issues a raw `QDBusMessage::createMethodCall` to
/// `getAllRules` and parses with `QJsonDocument::fromJson` directly,
/// because it slices the parsed rules through
/// `excludePlacementRulesFrom` / `excludeDecorationsRulesFrom` /
/// `excludeAnimationsRulesFrom` before sinking.
///
/// The `name` argument feeds the warning so the failure site is
/// identifiable in journals; pass the same `SettingProperty` constant
/// the loader requested.
///
/// `objectSink` runs when the document is a top-level JSON object;
/// `arraySink` runs when it is a top-level JSON array. Pass a
/// no-op (empty std::function) for the shape the caller doesn't
/// expect — a payload of the wrong shape logs and is dropped.
inline void dispatchJsonSetting(QLatin1String name, const QVariant& v,
                                std::function<void(const QJsonObject&)> objectSink,
                                std::function<void(const QJsonArray&)> arraySink)
{
    const QJsonDocument doc = QJsonDocument::fromJson(v.toString().toUtf8());
    if (doc.isObject() && objectSink) {
        objectSink(doc.object());
    } else if (doc.isArray() && arraySink) {
        arraySink(doc.array());
    } else {
        // Name the expected shape explicitly from which sink the caller
        // wired — covers all four combinations (object-only, array-only,
        // both, neither). Picking from the truthy ternary would lie when
        // both sinks are bound, or when neither is.
        const char* expected = (objectSink && arraySink) ? "object or array"
            : objectSink                                 ? "object"
            : arraySink                                  ? "array"
                                                         : "(no shape — caller wired neither sink)";
        qCWarning(lcEffect) << "Failed to parse" << name << "from D-Bus — payload is not a JSON" << expected;
    }
}

} // namespace

bool PlasmaZonesEffect::resolvedShaderAppliesToEvent(const QString& effectId, const QString& profilePath) const
{
    // See the header doc. Routed through the canonical predicate
    // (shaderEffectAppliesToEventPath) — the same one the settings pickers
    // filter with — so runtime refusal and picker filtering can never drift.
    const auto eff = m_shaderManager.m_animationShaderRegistry.effect(effectId);
    if (!eff.isValid()) {
        // Unknown id: pass through. The pack may still be scanning, and
        // beginShaderTransition's registry-miss warning stays the single
        // reporter for genuinely unknown ids.
        return true;
    }
    if (!PhosphorAnimationShaders::shaderEffectAppliesToEventPath(eff, profilePath)) {
        qCDebug(lcEffect) << "shader" << effectId << "does not apply to event" << profilePath
                          << "(appliesTo=" << eff.appliesTo << ") — skipping transition";
        return false;
    }
    return true;
}

void PlasmaZonesEffect::tryBeginShaderForEvent(KWin::EffectWindow* window, const QString& requestedPath, int durationMs,
                                               bool reverse, bool holdCloseGrab, bool holdAddedGrab,
                                               bool animateMinimized, bool* outOwnsResolvedLeg)
{
    // Fail-closed default: every early return below leaves the out-param
    // false, so a caller that mutates the transition afterwards (the
    // maximize morph endpoints) cannot touch an unrelated live leg.
    if (outOwnsResolvedLeg) {
        *outOwnsResolvedLeg = false;
    }
    if (!window || durationMs <= 0) {
        // Defensive guard. The current call sites all pass
        // `animationDurationMs()` which the daemon-bringup loader
        // clamps to `[MinAnimationDurationMs, MaxAnimationDurationMs]`
        // = [50, 2000], so 0 cannot reach this code through normal
        // flow. The authoritative no-animations gate is
        // `m_windowAnimator->isEnabled()` checked just below — that
        // covers the user-toggled case. This guard exists to fail
        // closed if a future programmatic call site bypasses the
        // clamp; a Timing Rule intentionally cannot rescue a
        // 0/negative duration since the value is treated as "caller
        // didn't supply one" rather than the "inherit per-event
        // default" sentinel that the rule layer recognises.
        return;
    }
    // Fast-path early-out on the global animations toggle. The
    // authoritative gate also lives in `beginShaderTransition` (so
    // window.movement.* callers via `applyWindowGeometry` are gated too), but
    // dispatching there would still pay the shader-tree resolve cost
    // — this skips it entirely when the global toggle is off.
    if (!m_windowAnimator->isEnabled()) {
        return;
    }
    // Which event path does this window actually animate on? Identity for an
    // application window, and the caller has named the only answer there. A
    // PLASMA SHELL SURFACE takes its own `shell.*` leg instead, or none at all
    // when the leg has no shell counterpart — see animationEventPathFor. The
    // whole rest of this function then runs against that path, so the motion
    // cascade, the shader resolve, the applicability gate and the diagnostics
    // all agree on one answer.
    const QString profilePath = animationEventPathFor(window, requestedPath);
    if (profilePath.isEmpty()) {
        return;
    }
    // A shell surface on its own leg. Two things follow, and both mirror what
    // the decoration tier does for the same surfaces:
    //
    //  • The window filter is SKIPPED. Every clause of it is written about
    //    application windows and each one rejects these outright (a panel is a
    //    dock, an applet popup is a special window), which is why
    //    shouldAnimateWindow keeps its blanket plasma-shell reject: that gate
    //    is what stops any OTHER leg (focus, minimize, a geometry morph) from
    //    ever reaching a surface plasmashell owns. Only the paths
    //    animationEventPathFor names get here.
    //
    //  • The rule tier is skipped too, by resolving with an empty query and an
    //    empty window id (the windowless convention the desktop legs use). A
    //    rule's match expression is authored against app identity — appId,
    //    class, title, PID — none of which meaningfully describes plasmashell's
    //    own surfaces, so a broad rule must not silently retarget a pack the
    //    user engaged on the Shell page.
    // Ask the resolver's own predicate rather than inferring shell-ness from the
    // fact that the path got rewritten. The two agree today — animationEventPathFor
    // rewrites for nothing but a shell leg — but that is a property of a function in
    // another TU, and this flag gates the rule tier. Inferring it means the day
    // animationEventPathFor learns any other remap, the rule tier silently re-opens
    // on these surfaces while the comment above still claims it is closed.
    const bool shellSurfaceLeg = !PhosphorAnimationShaders::shaderPathIsolationRoot(profilePath).isEmpty();
    // Window-filtering gate. `shouldAnimateWindow` honours the user's
    // Animations.WindowFiltering exclusions (transient / min-size /
    // app / class) AND lets a Rule carrying any appearance/animation
    // (Tag::Effect) action override the filter when the rule's match
    // expression resolves for the window's full WindowQuery (AppId /
    // WindowClass / Title / WindowRole / DesktopFile / WindowType / Pid /
    // state flags). Skipping this for shader transitions only would leave
    // the motion-side cascade in `applyWindowGeometry` doing its own check;
    // both call sites gate identically so the filter is a single concept
    // across the two paths.
    //
    // Caller-owned memoisation slot, the applyWindowGeometry pattern
    // (drag_snap.cpp): when the gate builds the WindowQuery for its rule
    // probes, the resolver pass below reuses it instead of walking the ~30
    // KWin accessors a second time per animated event.
    std::optional<PhosphorRules::WindowQuery> sharedQuery;
    if (!shellSurfaceLeg && !shouldAnimateWindow(window, &sharedQuery)) {
        return;
    }
    // Cascade: per-window animation Rule → ShaderProfileTree
    // (per-event default). The rule layer wins for matching windows;
    // an engaged-empty effectId on the rule deliberately blocks the
    // tree fallthrough (the user's "no animation for this app on this
    // event" sentinel).
    //
    // Reuse the gate's query when it built one (rules present) and build only
    // when the gate's fast paths never needed it — same shape
    // `shouldAnimateWindow` uses for the rule-override gate, so a rule that
    // passes the gate also resolves its slot. Caching across resolver calls is
    // built into the evaluator's `resolveCached(windowId, …)` path; the query
    // here is only the match input, not the cache key.
    //
    // A shell leg resolves WINDOWLESS instead. What closes the rule tier is that
    // BOTH resolvers short-circuit on !query.hasWindow() before any evaluator walk
    // (resolveAnimationShaderProfile in shader_resolve.cpp, and the rule overlay in
    // resolveEventMotionProfile below), so no rule is consulted and no cache slot is
    // consumed. Do NOT reduce that to "an empty query matches no rule" and move the
    // gate: an empty query is not inert on its own, because MatchExpression treats an
    // empty All{} as the always-true catch-all and a None{...} whose children all
    // miss — which every window-property leaf does on an empty query — as TRUE. This
    // is the same shape the
    // desktop legs use for an event whose subject is not an application window
    // (see the DesktopPeek resolve in lifecycle_wiring.cpp). The real window id
    // is still used everywhere else below — this pair is the rule tier's input,
    // not the transition's identity.
    const PhosphorRules::WindowQuery query =
        shellSurfaceLeg ? PhosphorRules::WindowQuery{} : (sharedQuery ? *sharedQuery : ruleQuery(window));
    const QString windowId = getWindowId(window);
    const QString ruleWindowId = shellSurfaceLeg ? QString() : windowId;
    const auto& profileTree = m_shaderManager.profileTree();
    // Per-event motion profile (curve + duration) in ONE walk, via the shared
    // SSOT: global animator profile → category "All" → per-node motion-tree
    // override → per-window Rule. The daemon mirrors its motion
    // PhosphorProfileRegistry into `motionProfileTree` over D-Bus, so a user-set
    // `window.open` = 900 ms (or an "All" curve) wins over the global default.
    //
    // The base is the animator's global profile, so when NO node overrides the
    // result is the global (never the library 150 ms default), and BOTH the
    // duration and the curve come from the SAME base — no cross-field base
    // skew. `effectiveDuration()` feeds the combined resolver as its
    // `defaultDurationMs` (the Rule timing slot still layers on top, matching
    // the resolver's documented rule → per-event → global contract), and
    // `.curve` shapes the time-driven `iTime`: paintWindow eases the linear
    // progress through it so a node's curve (e.g. "Ease Out") applies to its
    // shader exactly as it does on the animator-driven snap path. Null curve →
    // linear iTime.
    const PhosphorAnimation::Profile eventMotion = resolveEventMotionProfile(profilePath, query, ruleWindowId);
    const int baseDurationMs = qRound(eventMotion.effectiveDuration());
    const std::shared_ptr<const PhosphorAnimation::Curve> progressCurve = eventMotion.curve;
    // Combined cascade: ONE cached evaluator walk feeds BOTH the shader-slot
    // and timing-slot reads. The pre-refactor pair of `resolveAnimationShader
    // Profile` + `resolveAnimationDuration` ran two priority-order walks per
    // event (same query, both bypassing the per-window match cache); the
    // combined shim issues a single `resolveCached(windowId, …)` and reads
    // both slots from the same `ResolvedActions`. Semantics are identical:
    // rule wins per-slot, with engaged-empty effectId still blocking the tree
    // fallthrough and durationMs <= 0 still meaning "inherit".
    //
    // Clamp the resolved duration to the upstream `durationMs` floor: if
    // the cascade collapses to <= 0 (corrupt persisted rule, missing
    // motion-tree node feeding baseDurationMs), the QTimer::singleShot
    // below would fire on the next event-loop tick and tear down the
    // just-installed transition before its first paint. The input
    // `durationMs` was already clamped by the daemon-bringup loader to
    // [MinAnimationDurationMs, MaxAnimationDurationMs], and the
    // `durationMs <= 0` guard at the top of `tryBeginShaderForEvent`
    // rejects non-positive inputs, so `durationMs` here is a safe
    // positive floor.
    const auto resolved = PlasmaZones::resolveAnimationShaderProfile(m_shaderManager.animationRuleEvaluator(),
                                                                     profileTree, ruleWindowId, query, profilePath);
    const auto& profile = resolved.profile;
    // The duration comes from the motion cascade ALONE. resolveEventMotionProfile
    // already applied the Rule timing slot and clamped the result into the
    // envelope, so there is exactly one read and one clamp of that slot; the shader
    // resolver deliberately no longer re-reads it.
    int effectiveDurationMs = baseDurationMs;
    if (effectiveDurationMs <= 0) {
        effectiveDurationMs = durationMs;
    }
    // Spring lifetime, shared with the desktop switch: a stateful curve derives its
    // own lifetime from settleTime() and ignores the duration entirely. The result
    // drives BOTH the paint active-window and the teardown timer below, so the two
    // stay in lockstep.
    effectiveDurationMs = ShaderInternal::resolveTransitionLifetimeMs(effectiveDurationMs, progressCurve.get());
    if (profile.effectiveEffectId().isEmpty()) {
        // An empty resolve is the NORMAL state for any event whose cascade
        // chain carries no override — not just for an empty tree. The old
        // demotion keyed on the whole tree being empty, so a user with nine
        // overrides on open/close/move legs got a WARNING three times per
        // focus change, forever, because window.appearance.focus had none.
        // Only an override ON THIS PATH'S CASCADE (exact, or a dotted-path
        // ancestor like "window.appearance" / "window") makes an empty
        // resolve genuinely surprising (the documented prune / D-Bus-race
        // scenarios) — cascade resolution would otherwise have inherited it.
        // Rules keep a term, but only for the ONE action that can assign a
        // shader per-window: OverrideAnimationShader (timing/curve overrides
        // carry no effectId). Gating on the whole rule set restored the
        // warning for any border/opacity/layer rule — the exact population
        // the demotion exists to silence.
        // ...and not for a shell leg even then: the rule tier was skipped for it
        // above (empty query, empty window id), so no rule of any kind can be the
        // reason this resolve came back empty.
        const bool shaderAssigningRules = !shellSurfaceLeg && m_shaderManager.hasAnimationShaderRules();
        bool cascadeCovered = false;
        // The by-value QStringList is built ONCE and reused by both the loop
        // and the warn diagnostic below — the pre-change code paid it twice
        // (once for the range-for, once for .size() in the warn branch).
        const QStringList overriddenPaths = profileTree.overriddenPaths();
        // An ISOLATED path does not have the chain this loop assumes. resolve()
        // trims everything above the isolation root away, so for a shell leg an
        // override at `global` — or anywhere else outside the subtree — is not part
        // of the cascade at all and cannot be why the resolve came back empty.
        // Empty for every ordinary path, which leaves their behaviour untouched.
        const QString isolationRoot = PhosphorAnimationShaders::shaderPathIsolationRoot(profilePath);
        const auto insideIsolatedSubtree = [&isolationRoot](const QString& path) {
            return path == isolationRoot
                || (path.size() > isolationRoot.size() && path.startsWith(isolationRoot)
                    && path.at(isolationRoot.size()) == QLatin1Char('.'));
        };
        for (const QString& overridden : overriddenPaths) {
            // The literal "global" root is a genuine chain member of every
            // NON-isolated path (parentPath terminates every such cascade at
            // Global), so an override stored there covers this resolve too.
            // Ancestry is a prefix-plus-dot test rather than a per-entry
            // concatenation.
            const bool isChainMember = profilePath == overridden
                || (profilePath.size() > overridden.size() && profilePath.startsWith(overridden)
                    && profilePath.at(overridden.size()) == QLatin1Char('.'))
                || (isolationRoot.isEmpty() && overridden == PhosphorAnimation::ProfilePaths::Global);
            if (isChainMember && (isolationRoot.isEmpty() || insideIsolatedSubtree(overridden))) {
                cascadeCovered = true;
                break;
            }
        }
        if (!cascadeCovered && !shaderAssigningRules) {
            qCDebug(lcEffect) << "tryBeginShader[" << profilePath
                              << "]: no shader assigned (no override on this path's cascade)";
        } else {
            qCWarning(lcEffect) << "tryBeginShader[" << profilePath
                                << "]: no shader assigned (cascade returned empty effectId, tree size="
                                << overriddenPaths.size() << " rules=" << m_shaderManager.animationRuleSet().count()
                                << " shaderAssigningRules=" << shaderAssigningRules << ")";
        }
        return;
    }
    // Runtime applicability gate — see resolvedShaderAppliesToEvent. The
    // pickers keep class-mismatched packs unselectable, but a Rule's
    // OverrideAnimationShader slot or a stale / hand-edited config bypasses
    // them. Most material on the held-drag leg (window.movement.move): a
    // crossfade pack there would install a dead transition that pins
    // full-output repaints for the whole drag. The tree itself can no longer
    // deliver one there (ShaderProfileTree::resolve takes no ancestor overlay
    // for the move leaf), so this catches the rule-layer and stale-config
    // routes — and, symmetrically, a move-physics or desktop pack forced onto
    // any other window leg.
    if (!resolvedShaderAppliesToEvent(profile.effectiveEffectId(), profilePath)) {
        return;
    }
    // The tab swap DEFEATS the same-effect short-circuit on purpose. That
    // short-circuit exists for KWin's lifecycle-event bursts, where several
    // signals describe ONE logical event and the first leg should keep
    // running. A tab swap is never that shape: each swap is a distinct
    // discrete leg with a DIFFERENT source window, so a live leg running the
    // same pack — a repeat switch inside the previous leg's duration, or a
    // focus leg from the very activation this switch caused when the user
    // binds one pack to both classes — must be SUPERSEDED, not reused.
    // Reusing it kept the old startTimeMs (the cross-fade began mid-progress
    // or not at all), kept the old snapshot (the blend ran from the WRONG
    // tab's pixels), left the teardown timer owned by the earlier event, and
    // skipped the fresh-install repaint. Ending the same-pack leg first makes
    // this install take beginShaderTransition's ordinary fresh path, which
    // restarts the clock, re-seeds, re-arms teardown and repaints. Different-
    // pack legs already supersede through beginShaderTransition itself.
    if (profilePath == PhosphorAnimation::ProfilePaths::ScrollingTabSwitch) {
        if (auto* live = m_shaderManager.findTransition(window)) {
            const auto cacheIt = m_shaderManager.m_shaderCache.find(profile.effectiveEffectId());
            if (cacheIt != m_shaderManager.m_shaderCache.end() && live->cached == &cacheIt->second) {
                endShaderTransition(window);
            }
        }
    }
    const bool installed = beginShaderTransition(window, profile, effectiveDurationMs, reverse, holdCloseGrab,
                                                 holdAddedGrab, animateMinimized, progressCurve);
    auto* transition = m_shaderManager.findTransition(window);
    // Mark the held-move leg by IDENTITY. The drag handlers must not infer it from
    // liveness: `window.movement.move` is opt-in with no default shader, so the
    // common case installs nothing at all and `findTransition` would hand them an
    // unrelated leg to pin and reverse. See ShaderTransition::heldMove.
    //
    // We may stamp when `installed` is false, but ONLY for the same-effect
    // short-circuit — where the live leg genuinely IS the pack this event resolved
    // (a pack whose `appliesTo` admits both "move" and another class can already be
    // running for that other event). `beginShaderTransition` returns false from many
    // other places — compile failure, the cached null-shader sentinel, a registry
    // miss, a refused pack, a collapsed surface — and in EVERY one of those the live
    // leg is something else entirely, most reachably the `window.focus` leg the click
    // that began this drag just installed. Stamping that would pin it for the drag,
    // kill its teardown timer, and play the focus animation BACKWARD on release: the
    // exact bug this flag exists to prevent, re-introduced from the write side.
    //
    // So test what the short-circuit itself tests — does the live leg's cached shader
    // point at THIS event's pack? The null-shader sentinel is excluded by the
    // `->shader` check, which matters because that sentinel is sticky: once a pack
    // fails to compile, every later drag in the session would otherwise mis-stamp.
    //
    // Only ever write TRUE. A non-move event short-circuiting into a live held-move
    // leg must leave the flag alone — supersession builds a fresh ShaderTransition
    // whose default is already false, so the false case needs no code, and writing it
    // would re-introduce the mislabelling from the other direction.
    bool ownsResolvedLeg = installed;
    if (!ownsResolvedLeg && transition) {
        const auto cacheIt = m_shaderManager.m_shaderCache.find(profile.effectiveEffectId());
        ownsResolvedLeg = cacheIt != m_shaderManager.m_shaderCache.end() && cacheIt->second.shader
            && transition->cached == &cacheIt->second;
    }
    // Same identity verdict, exported for callers that mutate the transition
    // after this call (the maximize morph endpoint writes) — see the header
    // doc for why they must not trust findTransition alone.
    if (outOwnsResolvedLeg) {
        *outOwnsResolvedLeg = transition != nullptr && ownsResolvedLeg;
    }
    if (transition && ownsResolvedLeg && profilePath == PhosphorAnimation::ProfilePaths::WindowMove) {
        transition->heldMove = true;
    }
    if (!installed || !transition) {
        // Either beginShaderTransition no-op'd (compile fail, invalid id,
        // collapsed surface, animations disabled) and there is nothing
        // to teardown, OR the same-effect short-circuit kept the prior
        // leg in flight — in which case the prior leg's own teardown
        // timer (or animator-completion callback) owns the teardown.
        // Scheduling a fresh timer here would carry the prior leg's
        // generation and fire on this event's (likely shorter) duration,
        // cutting the still-running animation short.
        return;
    }
    // Capture the just-installed transition's generation so the deferred
    // teardown bails if a successor has replaced us by the time the timer
    // fires. Without this, two events overlapping on the same window
    // (window.move during window.snapIn, window.focus interrupting
    // window.maximize) leave a stale timer that tears down the SUCCESSOR
    // when its own timer hasn't fired yet.
    scheduleShaderTransitionTeardown(window, transition->generation, effectiveDurationMs);
}

void PlasmaZonesEffect::scheduleShaderTransitionTeardown(KWin::EffectWindow* window, quint64 generation, int delayMs)
{
    QPointer<KWin::EffectWindow> safeWindow(window);
    QTimer::singleShot(qMax(1, delayMs), this, [this, safeWindow, generation]() {
        // Two-tier guard: QPointer catches QObject destruction,
        // endShaderTransition's isDeleted() catches KWin's deletion-animation phase
        if (!safeWindow) {
            return;
        }
        const auto* live = m_shaderManager.findTransition(safeWindow);
        if (!live || live->generation != generation) {
            // A newer transition replaced us (last-event-wins) and owns its own
            // timer — leave it alone.
            return;
        }
        // HELD transitions (the interactive drag) outlive their nominal duration by
        // design: the user is still dragging. windowFinishUserMovedResized owns
        // their teardown (a settle tail after release), so the duration timer stands
        // down. A mesh-backed drag released BEFORE this timer fires is covered by the
        // generation check above instead: the release handler clears the hold flag
        // but bumps the generation when it hands the lifetime to the settle gate.
        if (live->holdUntilRelease) {
            return;
        }
        // Re-check against the transition's OWN clock rather than trusting the delay
        // we were armed with. `startTimeMs` is REBASED every frame a window spends
        // under restore suppression (a window repositioned on open is withheld from
        // compositing until its configure lands, and its animation must not play
        // invisibly in the meantime — see paint_pipeline). The install-time arming is
        // therefore up to kRestoreSuppressDeadlineMs (250 ms) too early, and firing
        // it would tear the leg down while its timeline still had a quarter second
        // to run — the open animation is cut mid-flight and the window pops. Re-arm
        // for the remainder instead. Any future rebase gets the same treatment for
        // free, which is why this is a re-check and not a suppression special case.
        const qint64 remaining =
            static_cast<qint64>(live->durationMs) - (ShaderInternal::shaderClockNowMs() - live->startTimeMs);
        if (remaining > 0) {
            scheduleShaderTransitionTeardown(safeWindow.data(), generation,
                                             static_cast<int>(qMin<qint64>(remaining, 60000)));
            return;
        }
        endShaderTransition(safeWindow);
    });
}

void PlasmaZonesEffect::loadShaderProfileFromDbus()
{
    // The key is named INLINE, not bound to a local alias first. An alias saves nothing
    // here and costs real safety: the registry-contract test scrapes these call sites to
    // prove every key the effect fetches is registered daemon-side, and an alias forces
    // it to resolve an identifier back to a constant — which it has now got wrong three
    // separate times, each time silently checking the wrong key while its own self-check
    // balanced. Name the constant where it is used and there is nothing to resolve.
    PhosphorProtocol::ClientHelpers::loadSettingAsync(
        this, PhosphorProtocol::Service::SettingProperty::ShaderProfileTree, [this](const QVariant& v) {
            dispatchJsonSetting(PhosphorProtocol::Service::SettingProperty::ShaderProfileTree, v,
                                [this](const QJsonObject& obj) {
                                    auto& tree = m_shaderManager.profileTree();
                                    tree = PhosphorAnimationShaders::ShaderProfileTree::fromJson(obj);
                                    qCDebug(lcEffect) << "loadShaderProfileFromDbus: tree loaded with"
                                                      << tree.overriddenPaths().size()
                                                      << "overrides — paths=" << tree.overriddenPaths();
                                    // A tree edit can assign or unassign an audio-reactive
                                    // animation pack; re-evaluate the cava run gate so the
                                    // provider is warm before the first transition needs it.
                                    scheduleEffectAudioSync();
                                    // It can also assign or clear any suppression-owning pack
                                    // (`desktop.peek`, `window.minimize`, `window.maximize`);
                                    // keep KWin's own stock effects unloaded exactly while
                                    // ours owns the event.
                                    syncStockEffectSuppression();
                                },
                                /*arraySink=*/{});
        });
}

void PlasmaZonesEffect::slotRulesChanged()
{
    // Coalesce burst signals: the daemon emits one `rulesChanged` per per-rule
    // mutation, so a 50-rule batch edit would otherwise drive 50 sequential
    // `getAllRules` round-trips + JSON parses + filter walks. The timer is a
    // single-shot 50ms debounce (set up in the constructor); each call here
    // re-arms it, so only the trailing edge of the burst triggers a refresh.
    m_animationRulesRefreshDebounce.start();
}

void PlasmaZonesEffect::loadRuleAnimationsFromDbus()
{
    // Every external invocation (bring-up, rulesChanged debounce, the
    // seed-edge re-drive) grants a fresh bounded retry budget. The retry
    // path calls fetchAllRulesOnce directly, so only its own failures
    // consume it — a fresh trigger always gets a full set of attempts.
    m_ruleFetchRetriesLeft = kRuleFetchRetryMax;
    fetchAllRulesOnce();
}

void PlasmaZonesEffect::fetchAllRulesOnce()
{
    // Fetch the unified Rule store via getAllRules (returns a JSON
    // string of a v4 RuleSet), deserialise, and split it into the two
    // effect-bound families: rules carrying an appearance/animation action
    // (Tag::Effect) and rules carrying a one-shot verdict action
    // (Tag::EffectVerdict). The shader manager mirrors each into its own
    // rule set, so the per-event slot lookup in shader_resolve.cpp resolves
    // the cascade against the unified rule store directly while the verdicts
    // resolve through an evaluator ExcludeAnimations cannot stop.
    const QDBusMessage msg = QDBusMessage::createMethodCall(
        QString(PhosphorProtocol::Service::Name), QString(PhosphorProtocol::Service::ObjectPath),
        QString(PhosphorProtocol::Service::Interface::Rules), QStringLiteral("getAllRules"));
    const QDBusPendingCall pending = QDBusConnection::sessionBus().asyncCall(msg, kRuleFetchTimeoutMs);
    auto* watcher = new QDBusPendingCallWatcher(pending, this);
    // Per-dispatch guard (see m_ruleFetchQueryGeneration): the debounce, the
    // bring-up load and the seed-edge re-drive can each dispatch while another
    // round-trip is outstanding, and this reply must lose to any later one.
    const quint64 queryGeneration = ++m_ruleFetchQueryGeneration;
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, queryGeneration](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        if (queryGeneration != m_ruleFetchQueryGeneration) {
            return; // a newer fetch superseded this one
        }
        const QDBusPendingReply<QString> reply = *w;
        if (reply.isError()) {
            // Daemon may not be up yet at startup; the rulesChanged
            // subscription below will deliver the next change. Log at debug
            // so the noise stays out of normal-startup logs.
            qCDebug(lcEffect) << "loadRuleAnimationsFromDbus: getAllRules failed:" << reply.error().message();
            // Bounded retry. This is what recovers the seed-edge re-drive
            // when its fetch fails: without it, rules sliced by the unseeded
            // clear would stay withheld until the next rulesChanged or a
            // daemon restart. The seed edge CONSUMES m_activeLayoutRulesWithheld
            // before dispatching (see TilingHandler::setActiveLayouts), so mid
            // chain the marker sits stale-FALSE; the exhaustion arm below
            // re-arms it so the NEXT seeding edge re-drives instead of
            // trusting a fetch that never landed.
            if (m_ruleFetchRetriesLeft > 0) {
                --m_ruleFetchRetriesLeft;
                QTimer::singleShot(kRuleFetchRetryDelayMs, this, [this] {
                    fetchAllRulesOnce();
                });
            } else {
                qCWarning(lcEffect) << "loadRuleAnimationsFromDbus: retry budget exhausted;"
                                    << "effect-bound rules refresh on the next rulesChanged or daemon restart";
                // Re-arm the marker the seed edge consumed before dispatching:
                // with the budget gone, the withheld rules are still out of
                // the evaluator, and a stale-FALSE marker would disarm the
                // NEXT seeding edge's re-drive too. TRUE is the safe polarity
                // (a spare re-drive is one redundant fetch).
                m_activeLayoutRulesWithheld = true;
            }
            return;
        }
        // Pre-check on the QString length before toUtf8() allocates a second
        // full copy: UTF-8 output is never SHORTER than the UTF-16 unit
        // count, so a unit count over the byte cap is already over. The
        // byte-exact check below still runs (UTF-8 can inflate up to 3x).
        // Same marker re-arm contract as every other early return here.
        if (reply.value().size() > kRuleFetchMaxPayloadBytes) {
            qCWarning(lcEffect) << "loadRuleAnimationsFromDbus: getAllRules payload of" << reply.value().size()
                                << "UTF-16 units exceeds the" << kRuleFetchMaxPayloadBytes
                                << "byte cap — refusing to convert";
            m_activeLayoutRulesWithheld = true;
            return;
        }
        const QByteArray payload = reply.value().toUtf8();
        // Every arm below that returns WITHOUT recomputing the withheld marker
        // must re-arm it: the seeding edge CONSUMED the marker before
        // dispatching this fetch (TilingHandler::setActiveLayouts), so leaving
        // it stale-FALSE disarms the NEXT unseed→seed cycle's re-drive while
        // the withheld rules are still out of every evaluator. TRUE is the safe
        // polarity — a spare re-drive costs one redundant fetch. (These arms
        // slice nothing, so the standing marker still matches the standing rule
        // sets; it is the CONSUMPTION, not this pass, that made it wrong.)
        if (payload.size() > kRuleFetchMaxPayloadBytes) {
            qCWarning(lcEffect) << "loadRuleAnimationsFromDbus: getAllRules payload of" << payload.size()
                                << "bytes exceeds the" << kRuleFetchMaxPayloadBytes << "byte cap — refusing to parse";
            m_activeLayoutRulesWithheld = true;
            return;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(payload);
        if (!doc.isObject()) {
            qCWarning(lcEffect) << "loadRuleAnimationsFromDbus: getAllRules returned non-object JSON";
            m_activeLayoutRulesWithheld = true;
            return;
        }
        const auto setOpt = PhosphorRules::RuleSet::fromJson(doc.object());
        if (!setOpt) {
            qCWarning(lcEffect) << "loadRuleAnimationsFromDbus: RuleSet::fromJson refused payload";
            m_activeLayoutRulesWithheld = true;
            return;
        }
        // Sampled once for the whole admission pass so every slice below
        // agrees on the same polarity story (see effectNeverStampedFields).
        const bool layoutsSeeded = m_tilingHandler->activeLayoutsSeeded();
        // ORed across every slice of this pass; consumed by the seeding edge
        // (see withoutNeverStampedRules and m_activeLayoutRulesWithheld).
        bool activeLayoutWithheld = false;

        QList<PhosphorRules::Rule> animationRules;
        QList<PhosphorRules::Rule> verdictRules;
        for (const PhosphorRules::Rule& rule : setOpt->rules()) {
            if (!rule.enabled) {
                // Skip disabled rules — they exist in the store but must not
                // contribute to the evaluator. (RuleEvaluator already gates
                // on enabled for its own walks, but pruning here keeps the
                // rule-set size minimal and the priority-order index smaller.)
                continue;
            }
            // Two effect-bound families, admitted in one pass because they
            // share the never-stamped filter and the withheld marker.
            //
            // Tag::Effect — the animation / APPEARANCE family, resolved
            // through the evaluator whose terminal scope honours
            // ExcludeAnimations: the three animation overrides, SetOpacity,
            // the appearance family (SetBorder*, SetHideTitleBar,
            // OverrideDecorationChain) and SetWindowLayer.
            //
            // Tag::EffectVerdict — the two per-window VERDICTS (OpenFullscreen,
            // ScrollFactor), resolved through an evaluator scoped to the
            // blanket Exclude alone. They were split out of Tag::Effect
            // because neither is an animation and neither is an appearance
            // change: riding the animation evaluator meant an
            // "exclude this app from animations" rule cancelled the app's
            // scroll multiplier and its open-fullscreen decision, and a rule
            // carrying only one of them force-animated its windows past the
            // min-size and user-exclusion filters (window_filtering.cpp).
            //
            // The authoritative membership list for both tags is the
            // descriptor tag assignments in ruleaction.cpp. A rule can carry
            // both tags and is then admitted to both sets.
            //
            // Computed BEFORE the never-stamped drop so the withheld marker
            // below can tell a rule the seeding edge would actually rescue
            // from one this rule set never wanted.
            bool admitted = false;
            bool admittedVerdict = false;
            for (const PhosphorRules::RuleAction& action : rule.actions) {
                // Two independent tests, not an else-if chain: no descriptor
                // carries both tags today (pinned in test_ruleaction), but an
                // else-if would admit such an action to the appearance set
                // ONLY, silently contradicting the both-sets rule stated above.
                if (PhosphorRules::ActionRegistry::instance().hasTag(action.type, PhosphorRules::Tag::Effect)) {
                    admitted = true;
                }
                if (PhosphorRules::ActionRegistry::instance().hasTag(action.type, PhosphorRules::Tag::EffectVerdict)) {
                    admittedVerdict = true;
                }
                if (admitted && admittedVerdict) {
                    break;
                }
            }
            if (rule.match.referencesAnyField(effectNeverStampedFields(layoutsSeeded))) {
                // No effect resolver can stamp the referenced field — admit
                // neither polarity (see effectNeverStampedFields).
                if ((admitted || admittedVerdict) && !layoutsSeeded
                    && rule.match.referencesAnyField(activeLayoutField())) {
                    activeLayoutWithheld = true;
                }
                continue;
            }
            if (admitted) {
                animationRules.append(rule);
            }
            if (admittedVerdict) {
                verdictRules.append(rule);
            }
        }
        // Sample the prior SetOpacity presence BEFORE setRuleAnimationRules
        // overwrites the rule set, through the gate the manager already
        // maintains (hasOpacityRules, recomputed in rebuildAnimationRuleSet)
        // rather than a second hand-scan of the same list. Repaint is needed on
        // BOTH bookends — rule appears (currently-natural-opacity windows need
        // to apply it) AND rule disappears (currently-dimmed windows need to
        // revert). The earlier single-bookend form left previously-dimmed
        // windows stuck at their last-painted opacity when the user removed the
        // last SetOpacity rule. Same read/overwrite ordering as the slice path
        // in sliceActiveLayoutRulesForUnseededMap.
        const bool hadSetOpacity = m_shaderManager.hasOpacityRules();
        m_shaderManager.setRuleAnimationRules(std::move(animationRules));
        // The verdict set rides the same reply — one rule-store sync point for
        // the effect, so the two sets can never describe different revisions of
        // the store. Its setter recomputes the OpenFullscreen / ScrollFactor
        // presence gates.
        m_shaderManager.setEffectVerdictRules(std::move(verdictRules));
        // A rule edit can route transitions to (or away from) an audio-reactive
        // animation pack via an EffectId payload — re-evaluate the cava run gate.
        scheduleEffectAudioSync();
        // The new-state SetOpacity predicate is computed by rebuildAnimationRuleSet
        // (see ShaderTransitionManager::hasOpacityRules) — read it back rather than
        // re-scanning the rule list a second time here.
        const bool hasSetOpacity = m_shaderManager.hasOpacityRules();
        qCDebug(lcEffect) << "loadRuleAnimationsFromDbus: forwarded" << m_shaderManager.animationRuleSet().count()
                          << "total animation rules to the evaluator";

        // Update the drag-gate exclusion rule set from the same unified
        // payload — `loadRuleAnimationsFromDbus` is the effect's one
        // and only rule-store sync point, so the snapping-exclusion gate
        // refreshes here too rather than chasing a second D-Bus fetch.
        // (The error-path returns ABOVE deliberately leave all three
        // exclusion slices at their previous contents rather than blanking
        // them — the same last-known-rules-stay-authoritative policy the
        // daemon-loss handler documents.) The
        // filter keeps only enabled rules with a terminal Exclude or
        // ExcludePlacement action; setRules bumps the bound rule set's
        // revision, which makes every stale entry in
        // m_snappingExclusionEvaluator's PER-WINDOW MATCH CACHE
        // (window_filtering.cpp resolves through resolveCached) read
        // as a miss and rebuilds the per-revision sort index. Rule EDITS
        // are therefore covered by the revision bump alone; PLACEMENT
        // changes are not, which is why rule_invalidation.cpp clears that
        // cache explicitly.
        m_snappingExclusionRuleSet.setRules(
            withoutNeverStampedRules(PhosphorRules::ExclusionRules::excludePlacementRulesFrom(*setOpt).rules(),
                                     layoutsSeeded, &activeLayoutWithheld));

        // Same refresh for the decoration-exclusion rule set (Exclude ∪
        // ExcludeDecorations), which shouldDecorateWindow gates on. Must land
        // BEFORE the updateAllDecorations() sweep below so an added or
        // removed exclusion applies to every window on this very edit, not on
        // the next incidental sweep.
        m_decorationExclusionRuleSet.setRules(
            withoutNeverStampedRules(PhosphorRules::ExclusionRules::excludeDecorationsRulesFrom(*setOpt).rules(),
                                     layoutsSeeded, &activeLayoutWithheld));

        // Recompute the geometry-scoped-rules gate for the frame-geometry
        // flush (see the member doc). Walked over the full parsed set, once
        // per rulesChanged — never per geometry tick.
        {
            static const QSet<PhosphorRules::Field> kGeometryFields = {
                PhosphorRules::Field::Width, PhosphorRules::Field::Height, PhosphorRules::Field::PositionX,
                PhosphorRules::Field::PositionY};
            m_hasGeometryScopedRules = false;
            for (const PhosphorRules::Rule& rule : setOpt->rules()) {
                if (rule.enabled && rule.match.referencesAnyField(kGeometryFields)) {
                    m_hasGeometryScopedRules = true;
                    break;
                }
            }
        }

        // Per-window border / title-bar rules ride the same animation rule set
        // (Tag::Effect admits them). Refresh borders so an edited /
        // added / removed SetBorder* / SetHideTitleBar rule applies immediately
        // — updateAllDecorations re-merges every window and reconciles rule-hidden
        // title bars against the fresh evaluator (and re-checks the freshly
        // sliced decoration exclusions above).
        updateAllDecorations();

        // Same refresh for the animation-side exclusion rule set, sliced
        // for `ExcludeAnimations`-action rules. The two slices stay
        // independent so a user can have a window excluded from animations
        // but NOT from snap (or vice versa).
        m_animationExclusionRuleSet.setRules(
            withoutNeverStampedRules(PhosphorRules::ExclusionRules::excludeAnimationsRulesFrom(*setOpt).rules(),
                                     layoutsSeeded, &activeLayoutWithheld));
        // Publish the pass verdict for the seeding edge. Always assigned, not
        // ORed into the member: this reply IS the current answer over the
        // current rule store, and a seeded pass legitimately withholds nothing.
        // Ordering against a concurrent seed is safe in both directions — a
        // seed that lands BEFORE this reply makes layoutsSeeded above read
        // true, so the pass admits ActiveLayout rules itself and correctly
        // records nothing withheld; a seed that lands AFTER reads the marker
        // this pass just wrote.
        m_activeLayoutRulesWithheld = activeLayoutWithheld;
        // Force a full repaint on EITHER bookend so a user-authored rule
        // applies to static (un-damaged) windows immediately AND so a
        // removed rule reverts previously-dimmed windows immediately, not
        // on the next incidental damage event. OverrideAnimation* rules
        // fire on lifecycle events so they don't need this kick;
        // SetOpacity continuously alters paint output regardless of
        // animation state and needs the kick on both transitions.
        if ((hasSetOpacity || hadSetOpacity) && KWin::effects) {
            KWin::effects->addRepaintFull();
        }
    });
}

void PlasmaZonesEffect::sliceActiveLayoutRulesForUnseededMap()
{
    // Same predicate the admission filter uses for the conditional half of the
    // never-stamped set, so what the clear removes is exactly what a cold-start
    // pass would have refused to admit.
    const auto referencesActiveLayout = [](const PhosphorRules::Rule& rule) {
        return rule.match.referencesAnyField(activeLayoutField());
    };
    const auto sliceRuleSet = [&referencesActiveLayout](PhosphorRules::RuleSet& set) {
        QList<PhosphorRules::Rule> kept = set.rules();
        const qsizetype before = kept.size();
        kept.removeIf(referencesActiveLayout);
        if (kept.size() == before) {
            // setRules always bumps the revision and invalidates every bound
            // evaluator's match cache, so skip the no-op rewrite.
            return false;
        }
        set.setRules(kept);
        return true;
    };

    // Non-short-circuiting OR: every set must be sliced, not just up to the
    // first one that had a match.
    bool removed = false;
    removed |= sliceRuleSet(m_snappingExclusionRuleSet);
    removed |= sliceRuleSet(m_decorationExclusionRuleSet);
    removed |= sliceRuleSet(m_animationExclusionRuleSet);

    // The shader manager's effect-rule set is written through its own setter
    // (it keeps the raw list and the bound mirror in step, and recomputes the
    // two APPEARANCE presence gates, SetOpacity and SetWindowLayer — the
    // OpenFullscreen and ScrollFactor gates belong to the verdict setter
    // below, over the verdict list). The setter no-ops on an unchanged list,
    // so the size check is only for the marker.
    QList<PhosphorRules::Rule> animationRules = m_shaderManager.animationRuleSet().rules();
    const qsizetype animationBefore = animationRules.size();
    animationRules.removeIf(referencesActiveLayout);
    if (animationRules.size() != animationBefore) {
        const bool hadSetOpacity = m_shaderManager.hasOpacityRules();
        m_shaderManager.setRuleAnimationRules(std::move(animationRules));
        removed = true;
        // A dropped rule can be the one routing transitions to an
        // audio-reactive pack; re-evaluate the cava run gate the same way the
        // admission pass does after its own setRuleAnimationRules. Deferred
        // and coalesced, so pairing it with the callers' later work is safe.
        scheduleEffectAudioSync();
        // The SetOpacity bookend, for the same reason loadRuleAnimationsFromDbus
        // takes it on a rule edit: opacity is resolved in the paint path, so a
        // window dimmed by an ActiveLayout-scoped SetOpacity rule stays at its
        // last-painted alpha forever once the rule leaves the evaluator, unless
        // something damages it. The daemon-LOSS caller happens to be covered
        // (its clearAllDecorations tears down the tint layer), but the bring-up
        // caller is not: a straight old→new owner handover emits no
        // serviceUnregistered edge, so decorations are still live there. This
        // is the one repaint this path owns; borders and rule verdicts remain
        // the callers' invalidateAllRuleCaches + scheduleBorderSweep. The
        // "after" term is kept for symmetry with the admission pass's gate;
        // a slice only removes rules, so only the "was" bookend can fire.
        if ((hadSetOpacity || m_shaderManager.hasOpacityRules()) && KWin::effects) {
            KWin::effects->addRepaintFull();
        }
    }

    // The verdict set is a fifth effect-bound set and takes the same slice for
    // the same reason: an ActiveLayout-referencing verdict rule left bound
    // over the daemon-down interval resolves against an unstamped field, so a
    // negated leaf over-matches every window — and the two verdicts it can
    // fill are a real fullscreen flip and a real scroll rescale. No repaint
    // bookend: neither verdict is painted state (OpenFullscreen is one-shot at
    // open, ScrollFactor is read per input event).
    QList<PhosphorRules::Rule> verdictRules = m_shaderManager.effectVerdictRuleSet().rules();
    const qsizetype verdictBefore = verdictRules.size();
    verdictRules.removeIf(referencesActiveLayout);
    if (verdictRules.size() != verdictBefore) {
        m_shaderManager.setEffectVerdictRules(std::move(verdictRules));
        removed = true;
    }

    if (removed) {
        // Arm the seeding edge in TilingHandler::setActiveLayouts: these rules
        // are gone from the evaluator until a getAllRules pass re-admits them,
        // and that pass only runs if the marker is set. Never cleared here —
        // an earlier true from a bring-up admission pass is still true.
        m_activeLayoutRulesWithheld = true;
        qCDebug(lcEffect) << "sliceActiveLayoutRulesForUnseededMap: dropped ActiveLayout-scoped rules for the "
                             "unseeded map — re-drive armed";
    }
}

void PlasmaZonesEffect::loadMotionProfileTreeFromDbus()
{
    // The key is named INLINE, not bound to a local alias first. An alias saves nothing
    // here and costs real safety: the registry-contract test scrapes these call sites to
    // prove every key the effect fetches is registered daemon-side, and an alias forces
    // it to resolve an identifier back to a constant — which it has now got wrong three
    // separate times, each time silently checking the wrong key while its own self-check
    // balanced. Name the constant where it is used and there is nothing to resolve.
    PhosphorProtocol::ClientHelpers::loadSettingAsync(
        this, PhosphorProtocol::Service::SettingProperty::MotionProfileTree, [this](const QVariant& v) {
            dispatchJsonSetting(PhosphorProtocol::Service::SettingProperty::MotionProfileTree, v,
                                [this](const QJsonObject& obj) {
                                    // ProfileTree::fromJson resolves each node's optional
                                    // `curve` field through a CurveRegistry. The effect now
                                    // resolves per-event curve AND duration from this tree
                                    // (resolveEventMotionProfile reads `.curve` to shape the
                                    // time-driven iTime), so parse with the effect's own
                                    // `m_curveRegistry` — the SAME registry the Rule path
                                    // resolves against (shader_resolve.cpp) — rather than a
                                    // throwaway.
                                    //
                                    // Builtins are sufficient here and no CurveLoader is
                                    // needed in the compositor: a Profile persists its curve
                                    // BY SPEC, not by registry key (Profile::toJson writes
                                    // `curve->toString()`, e.g. "0.34,1.20,0.64,1.00" or
                                    // "spring:<omega>,<zeta>"; the friendly name rides in the
                                    // separate `presetName` field). A user curve pack
                                    // (data/curves/*.json) is a named preset over a BUILTIN
                                    // typeId, so it round-trips through its spec and the
                                    // ctor-registered builtin factories resolve it. Sharing
                                    // one registry keeps both curve paths on identical
                                    // resolution rules rather than two that can drift.
                                    auto& tree = m_shaderManager.motionProfileTree();
                                    tree = PhosphorAnimation::ProfileTree::fromJson(obj, m_curveRegistry);
                                    qCDebug(lcEffect) << "loadMotionProfileTreeFromDbus: tree loaded with"
                                                      << tree.overriddenPaths().size()
                                                      << "per-event overrides — paths=" << tree.overriddenPaths();
                                },
                                /*arraySink=*/{});
        });
}

PhosphorAnimation::Profile PlasmaZonesEffect::resolveEventMotionProfile(const QString& profilePath,
                                                                        const PhosphorRules::WindowQuery& query,
                                                                        const QString& windowId) const
{
    // Cascade base: the WindowAnimator's global profile carries the authoritative
    // global curve + duration (from animationEasingCurve / animationDuration).
    //
    // Before the async settings load lands, the animator's `duration` is still
    // nullopt, so effectiveDuration() falls back to Profile::DefaultDuration
    // while `animationDurationMs()` reports Limits::DefaultAnimationDurationMs.
    // Callers rely on those two agreeing (it is what makes the pre-load window
    // resolve to the same duration either way), so pin the coupling here rather
    // than let a future bump to one silently skew it.
    static_assert(
        qRound(PhosphorAnimation::Profile::DefaultDuration) == PhosphorAnimation::Limits::DefaultAnimationDurationMs,
        "Profile::DefaultDuration and Limits::DefaultAnimationDurationMs must agree, or the pre-settings-load "
        "motion cascade skews against animationDurationMs()");
    const PhosphorAnimation::Profile& base = m_windowAnimator->profile();
    // Category "All" → per-node: overlay only the motion-tree override chain for
    // this path onto the global base (overlayChainOnto skips the tree's own
    // baseline and returns base untouched when nothing in the chain overrides,
    // so an empty tree keeps the animator's global). Gated on a non-empty
    // override set to keep the default-state fast-path (no chain walk).
    const auto& motionTree = m_shaderManager.motionProfileTree();
    PhosphorAnimation::Profile resolved =
        motionTree.hasAnyOverride() ? motionTree.overlayChainOnto(profilePath, base) : base;
    // Rule override (top of the cascade): a per-window Timing / Curve rule for
    // this (window, event) replaces the resolved curve / duration. Skipped for
    // windowless events (desktop switch) and when no rules are configured.
    if (query.hasWindow() && !m_shaderManager.animationRuleSet().isEmpty()) {
        resolved = PlasmaZones::resolveAnimationMotionProfile(m_shaderManager.animationRuleEvaluator(), resolved, query,
                                                              profilePath, windowId, m_curveRegistry);
    }
    // Clamp the resolved duration into the animation envelope HERE, at the one
    // place every consumer shares. The motion tree hands a node's duration
    // through unclamped (ProfileTree does no bounding, and Profile::fromJson
    // accepts any finite positive value up to one hour), and the tree is rebuilt
    // from hand-editable profile JSON.
    //
    // The two shader consumers re-clamp the DURATION downstream via
    // resolveTransitionLifetimeMs. The animator calls that helper too, but only
    // for the spring maxLifetimeMs cap — its PARAMETRIC duration goes from
    // applyWindowGeometry straight into WindowAnimator::startAnimation, whose
    // own clampProfile bounds to (0, 10000] ms (non-positive durations are RESET
    // to the library default rather than clamped to 0), a different, looser
    // envelope.
    // Without this a `"duration": 5000` node would run a 2 s shader leg on
    // window.open but a 5 s animator leg on a snap, with its durationMs == 0
    // shader riding along and pinning per-frame repaints for the full 5 s.
    // Clamping at the source keeps all three consumers on one envelope; the
    // downstream clamps then become idempotent.
    if (resolved.duration) {
        resolved.duration =
            static_cast<qreal>(qBound(PhosphorAnimation::Limits::MinAnimationDurationMs, qRound(*resolved.duration),
                                      PhosphorAnimation::Limits::MaxAnimationDurationMs));
    }
    return resolved;
}

void PlasmaZonesEffect::slotMotionProfileTreeChanged()
{
    // A per-event animation duration was edited (daemon rescanned a
    // `profiles/*.json` override). Re-fetch so per-event durations apply
    // live, without a logout/login. loadCachedSettings() also re-fetches
    // it on settingsChanged; this dedicated path covers the profile-file
    // edits that deliberately do NOT ride settingsChanged.
    loadMotionProfileTreeFromDbus();
}

void PlasmaZonesEffect::slotSessionIdleChanged(bool idle)
{
    if (m_sessionIdle == idle) {
        return;
    }
    m_sessionIdle = idle;
    if (!m_pauseAnimationWhenIdle) {
        // Track the state anyway — the setting can be turned on mid-session, and
        // the next paint should already know whether we are idle.
        return;
    }
    if (!idle) {
        // Waking. A paused chain emits no damage of its own, so nothing would put
        // it back in the paint loop without this.
        repaintAllDecorations();
    }
    // Going idle needs no repaint: the windows simply stop being driven and keep
    // presenting the composite they already hold.
}

void PlasmaZonesEffect::loadShaderRegistryFromDbus()
{
    // The key is named INLINE, not bound to a local alias first. An alias saves nothing
    // here and costs real safety: the registry-contract test scrapes these call sites to
    // prove every key the effect fetches is registered daemon-side, and an alias forces
    // it to resolve an identifier back to a constant — which it has now got wrong three
    // separate times, each time silently checking the wrong key while its own self-check
    // balanced. Name the constant where it is used and there is nothing to resolve.
    PhosphorProtocol::ClientHelpers::loadSettingAsync(
        this, PhosphorProtocol::Service::SettingProperty::AnimationShaderSearchPaths, [this](const QVariant& v) {
            dispatchJsonSetting(PhosphorProtocol::Service::SettingProperty::AnimationShaderSearchPaths, v,
                                /*objectSink=*/{}, [this](const QJsonArray& arr) {
                                    const QStringList paths = validatedShaderSearchPaths(arr);
                                    if (!paths.isEmpty()) {
                                        m_shaderManager.m_animationShaderRegistry.addSearchPaths(paths);
                                        // paths.size() is the REQUESTED count, pre-dedupe:
                                        // addSearchPaths silently drops already-registered
                                        // paths, so the number actually registered may be
                                        // lower. Logged inside the guard so an empty reply
                                        // doesn't print a misleading "requested 0".
                                        qCDebug(lcEffect)
                                            << "loadShaderRegistryFromDbus: requested" << paths.size()
                                            << "search paths (pre-dedupe) — registry effect count="
                                            << m_shaderManager.m_animationShaderRegistry.availableEffects().size();
                                    }
                                });
        });
}

} // namespace PlasmaZones
