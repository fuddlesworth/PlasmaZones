// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plasmazoneseffect.h"
#include "shader_internal.h"
#include "shader_resolve.h"
#include "window_query.h"

#include "compositor/windowanimator.h"

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

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QLoggingCategory>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariant>
#include <QVariantMap>

#include <functional>
#include <memory>
#include <utility>

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

namespace {

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
/// `excludeRulesFrom` / `excludeAnimationsRulesFrom` before sinking.
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

void PlasmaZonesEffect::tryBeginShaderForEvent(KWin::EffectWindow* window, const QString& profilePath, int durationMs,
                                               bool reverse, bool holdCloseGrab, bool holdAddedGrab,
                                               bool animateMinimized)
{
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
    // Window-filtering gate. `shouldAnimateWindow` honours the user's
    // Animations.WindowFiltering exclusions (transient / min-size /
    // app / class) AND lets a Rule carrying any effect-consumed
    // (Tag::Effect) action override the filter when the rule's match
    // expression resolves for the window's full WindowQuery (AppId /
    // WindowClass / Title / WindowRole / DesktopFile / WindowType / Pid /
    // state flags). Skipping this for shader transitions only would leave
    // the motion-side cascade in `applyWindowGeometry` doing its own check;
    // both call sites gate identically so the filter is a single concept
    // across the two paths.
    if (!shouldAnimateWindow(window)) {
        return;
    }
    // Cascade: per-window animation Rule → ShaderProfileTree
    // (per-event default). The rule layer wins for matching windows;
    // an engaged-empty effectId on the rule deliberately blocks the
    // tree fallthrough (the user's "no animation for this app on this
    // event" sentinel).
    //
    // Build the full per-window query once and reuse it for every
    // resolver call below — same shape `shouldAnimateWindow` already
    // uses for the rule-override gate, so a rule that passes the gate
    // also resolves its slot. Caching across resolver calls is built
    // into the evaluator's `resolveCached(windowId, …)` path; the query
    // here is only the match input, not the cache key.
    const PhosphorRules::WindowQuery query = ruleQuery(window);
    const QString windowId = getWindowId(window);
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
    const PhosphorAnimation::Profile eventMotion = resolveEventMotionProfile(profilePath, query, windowId);
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
                                                                     profileTree, windowId, query, profilePath);
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
        // Default-state path: a fresh user with no shader overrides
        // anywhere in the tree resolves every event to empty effectId,
        // which is correct ("no shader assigned"). Logging at WARNING
        // for that floods the journal with bogus failures every time a
        // window opens, closes, or moves. Only WARN when the tree has
        // overrides (so an empty resolve here is genuinely surprising —
        // the documented prune / D-Bus-race scenarios), otherwise
        // demote to DEBUG.
        const int ruleCount = m_shaderManager.animationRuleSet().count();
        if (profileTree.overriddenPaths().isEmpty() && ruleCount == 0) {
            qCDebug(lcEffect) << "tryBeginShader[" << profilePath
                              << "]: no shader assigned (tree empty — default state)";
        } else {
            qCWarning(lcEffect) << "tryBeginShader[" << profilePath
                                << "]: no shader assigned (cascade returned empty effectId, tree size="
                                << profileTree.overriddenPaths().size() << " rules=" << ruleCount << ")";
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
    // Fetch the unified Rule store via getAllRules (returns a JSON
    // string of a v4 RuleSet), deserialise, filter to rules whose
    // action list contains any effect-consumed (Tag::Effect) action, and
    // hand them to the shader manager. The shader manager mirrors them into
    // m_animationRuleSet so the per-event slot lookup in shader_resolve.cpp
    // resolves the cascade against the unified rule store directly.
    const QDBusMessage msg = QDBusMessage::createMethodCall(
        QString(PhosphorProtocol::Service::Name), QString(PhosphorProtocol::Service::ObjectPath),
        QString(PhosphorProtocol::Service::Interface::Rules), QStringLiteral("getAllRules"));
    const QDBusPendingCall pending = QDBusConnection::sessionBus().asyncCall(msg);
    auto* watcher = new QDBusPendingCallWatcher(pending, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        const QDBusPendingReply<QString> reply = *w;
        if (reply.isError()) {
            // Daemon may not be up yet at startup; the rulesChanged
            // subscription below will deliver the next change. Log at debug
            // so the noise stays out of normal-startup logs.
            qCDebug(lcEffect) << "loadRuleAnimationsFromDbus: getAllRules failed:" << reply.error().message();
            return;
        }
        const QByteArray payload = reply.value().toUtf8();
        const QJsonDocument doc = QJsonDocument::fromJson(payload);
        if (!doc.isObject()) {
            qCWarning(lcEffect) << "loadRuleAnimationsFromDbus: getAllRules returned non-object JSON";
            return;
        }
        const auto setOpt = PhosphorRules::RuleSet::fromJson(doc.object());
        if (!setOpt) {
            qCWarning(lcEffect) << "loadRuleAnimationsFromDbus: RuleSet::fromJson refused payload";
            return;
        }
        // Sample the prior rule set for SetOpacity BEFORE setRuleAnimationRules
        // overwrites it. Repaint is needed on BOTH bookends — rule appears
        // (currently-natural-opacity windows need to apply it) AND rule
        // disappears (currently-dimmed windows need to revert). The earlier
        // single-bookend form left previously-dimmed windows stuck at their
        // last-painted opacity when the user removed the last SetOpacity rule.
        bool hadSetOpacity = false;
        const auto& priorRules = m_shaderManager.animationRuleSet().rules();
        for (const PhosphorRules::Rule& rule : priorRules) {
            for (const PhosphorRules::RuleAction& action : rule.actions) {
                if (action.type == PhosphorRules::ActionType::SetOpacity) {
                    hadSetOpacity = true;
                    break;
                }
            }
            if (hadSetOpacity) {
                break;
            }
        }

        QList<PhosphorRules::Rule> animationRules;
        for (const PhosphorRules::Rule& rule : setOpt->rules()) {
            if (!rule.enabled) {
                // Skip disabled rules — they exist in the store but must not
                // contribute to the evaluator. (RuleEvaluator already gates
                // on enabled for its own walks, but pruning here keeps the
                // rule-set size minimal and the priority-order index smaller.)
                continue;
            }
            // Admit the rule to the evaluator if ANY action is effect-consumed,
            // i.e. carries Tag::Effect (hasTag below). The authoritative
            // membership list is the descriptor tag assignments in
            // ruleaction.cpp — animation overrides, SetOpacity, the appearance
            // family (SetBorder*, SetHideTitleBar, OverrideDecorationChain),
            // and SetWindowLayer.
            bool admitted = false;
            for (const PhosphorRules::RuleAction& action : rule.actions) {
                if (PhosphorRules::ActionRegistry::instance().hasTag(action.type, PhosphorRules::Tag::Effect)) {
                    admitted = true;
                    break;
                }
            }
            if (admitted) {
                animationRules.append(rule);
            }
        }
        m_shaderManager.setRuleAnimationRules(std::move(animationRules));
        // A rule edit can route transitions to (or away from) an audio-reactive
        // animation pack via an EffectId payload — re-evaluate the cava run gate.
        scheduleEffectAudioSync();
        // The new-state SetOpacity predicate is computed by rebuildAnimationRuleSet
        // (see ShaderTransitionManager::hasOpacityRules) — read it back rather than
        // re-scanning the rule list a second time here.
        const bool hasSetOpacity = m_shaderManager.hasOpacityRules();
        qCDebug(lcEffect) << "loadRuleAnimationsFromDbus: forwarded" << m_shaderManager.animationRuleSet().count()
                          << "total animation rules to the evaluator";

        // Per-window border / title-bar rules ride the same animation rule set
        // (Tag::Effect admits them). Refresh borders so an edited /
        // added / removed SetBorder* / SetHideTitleBar rule applies immediately
        // — updateAllDecorations re-merges every window and reconciles rule-hidden
        // title bars against the fresh evaluator.
        updateAllDecorations();

        // Update the drag-gate exclusion rule set from the same unified
        // payload — `loadRuleAnimationsFromDbus` is the effect's one
        // and only rule-store sync point, so the snapping-exclusion gate
        // refreshes here too rather than chasing a second D-Bus fetch. The
        // filter keeps only enabled rules with a terminal Exclude action;
        // setRules bumps the bound rule set's revision so
        // m_snappingExclusionEvaluator's per-revision sort index rebuilds
        // on its next walk (these evaluators call uncached `resolve()`, so
        // there is no per-window match cache to drop — the sort index is
        // the only revision-keyed artifact).
        m_snappingExclusionRuleSet.setRules(PhosphorRules::ExclusionRules::excludeRulesFrom(*setOpt).rules());

        // Same refresh for the animation-side exclusion rule set, sliced
        // for `ExcludeAnimations`-action rules. The two slices stay
        // independent so a user can have a window excluded from animations
        // but NOT from snap (or vice versa).
        m_animationExclusionRuleSet.setRules(
            PhosphorRules::ExclusionRules::excludeAnimationsRulesFrom(*setOpt).rules());
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
    // own clampProfile bounds to [0, 10000] ms, a different, looser envelope.
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
                                    QStringList paths;
                                    for (const auto& entry : arr) {
                                        if (entry.isString())
                                            paths.append(entry.toString());
                                    }
                                    if (!paths.isEmpty()) {
                                        m_shaderManager.m_animationShaderRegistry.addSearchPaths(paths);
                                    }
                                    qCDebug(lcEffect)
                                        << "loadShaderRegistryFromDbus: added" << paths.size()
                                        << "search paths — registry effect count="
                                        << m_shaderManager.m_animationShaderRegistry.availableEffects().size();
                                });
        });
}

} // namespace PlasmaZones
