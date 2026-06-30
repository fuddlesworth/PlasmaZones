// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/ShaderProfile.h>
#include <PhosphorAnimation/ShaderProfileTree.h>
#include <PhosphorRules/WindowQuery.h>

#include <QColor>
#include <QString>

#include <optional>

namespace PhosphorAnimation {
class CurveRegistry;
}

namespace PhosphorRules {
class RuleEvaluator;
class ResolvedActions;
}

namespace PlasmaZones {

/**
 * @file shader_resolve.h
 * @brief Effect-local per-window animation cascade shims, built on
 *        PhosphorRules::RuleEvaluator.
 *
 * These walk the event-scoped action slots (`anim-shader:<event>`,
 * `anim-timing:<event>`, `anim-curve:<event>`) populated by `Rule`s
 * carrying `OverrideAnimation{Shader,Timing,Curve}` actions, falling back to
 * the per-event ShaderProfileTree / motion-profile defaults when no rule
 * matches. The duration clamp, the curve `tryCreate` fallback, the
 * engaged-empty `effectId` sentinel, and the empty-input short-circuits all
 * live in these shims — the evaluator stays generic.
 *
 * Every resolver takes a `PhosphorRules::WindowQuery` carrying the FULL
 * window context (AppId / WindowClass / Title / WindowRole / DesktopFile /
 * WindowType / Pid / state flags / placement state), built once per window by
 * the GPL-side caller via `PlasmaZonesEffect::ruleQuery(w)`, which threads
 * the effect's floating / snapped / zone caches into the free `ruleQueryFor`
 * builder. Pre-PR the resolvers took a bare `windowClass` and the rule layer
 * matched exclusively on `WindowClass Contains <pattern>`; v4 widened the match
 * shape so a user-authored rule may pin to `AppId` / `DesktopFile` / `Title` / etc.
 * Routing the full query through to the resolver keeps the rule-override
 * gate (which already builds the full query) and the slot resolution in
 * lockstep — a rule that passes the gate also resolves its slot.
 */

/**
 * @brief Combined shader-profile + duration cascade for the per-window-event
 *        hot path, sharing a single cached evaluator walk.
 *
 * Returns the resolved `ShaderProfile` (via the rule's `anim-shader:<event>`
 * slot, falling back to `tree.resolve(eventPath)`) AND the resolved duration
 * (via the rule's `anim-timing:<event>` slot, falling back to
 * @p defaultDurationMs) from ONE `evaluator.resolveCached(windowId, …)` call.
 *
 * The standalone resolvers each call `evaluator.resolve(…)`, which costs a
 * full priority-order walk per call and bypasses the per-window cache. The
 * effect's shader hot path needs both values per event; this overload pays
 * one cached walk and reads both slots from the same `ResolvedActions`.
 *
 * Semantics match the standalone resolvers byte-for-byte:
 *  - Windowless @p query (`hasWindow()` false) / empty @p eventPath: returns
 *    `{ tree.resolve(eventPath), defaultDurationMs }` without touching the
 *    evaluator (no window attribute could match any rule predicate).
 *  - Shader slot filled: ShaderProfile taken verbatim (engaged-empty effectId
 *    preserved as the "block tree fallthrough" sentinel).
 *  - Timing slot filled with `durationMs > 0`: that value, clamped to
 *    `[Min, Max]AnimationDurationMs`. `durationMs <= 0` ("inherit" sentinel)
 *    or no rule → @p defaultDurationMs.
 */
struct ResolvedShaderAndDuration
{
    PhosphorAnimationShaders::ShaderProfile profile;
    int durationMs;
    /// True when a window RULE filled the shader slot (verbatim, including an
    /// engaged-empty "None" sentinel). False when the profile came from the
    /// tree / baseline. Callers that apply a built-in per-event default must
    /// NOT override a rule decision: a rule "None" is a deliberate per-app
    /// opt-out, so the default only applies when no rule matched.
    bool shaderSlotFromRule = false;
};
ResolvedShaderAndDuration resolveAnimationShaderAndDuration(const PhosphorRules::RuleEvaluator& evaluator,
                                                            const PhosphorAnimationShaders::ShaderProfileTree& tree,
                                                            const QString& windowId,
                                                            const PhosphorRules::WindowQuery& query,
                                                            const QString& eventPath, int defaultDurationMs);

/**
 * @brief Motion-profile cascade: per-window timing rule → base profile.
 *
 * Returns @p base with its `curve` / `duration` replaced when a timing rule
 * fills the `anim-timing:<eventPath>` slot. A non-empty curve is parsed via
 * @p curveRegistry's `tryCreate` (a malformed curve keeps the base curve); a
 * `durationMs > 0` overrides the duration, clamped identically to
 * `resolveAnimationShaderAndDuration`. A windowless @p query
 * (`hasWindow()` false) or empty @p eventPath, or no matching rule, returns
 * @p base unchanged.
 *
 * @p windowId routes the lookup through the evaluator's per-window match
 * cache so the curve / timing / shader resolvers share their walks. Pass
 * the same frozen composite the sister resolvers use; on a default-state
 * tree (no rules, no profile overrides) the result is the unchanged
 * base profile and the cache reads are O(1).
 */
PhosphorAnimation::Profile resolveAnimationMotionProfile(const PhosphorRules::RuleEvaluator& evaluator,
                                                         const PhosphorAnimation::Profile& base,
                                                         const PhosphorRules::WindowQuery& query,
                                                         const QString& eventPath, const QString& windowId,
                                                         const PhosphorAnimation::CurveRegistry& curveRegistry);

/**
 * @brief Per-window opacity cascade — the runtime consumer for
 *        `SetOpacity` rules.
 *
 * Returns the rule-resolved opacity in `[0.0, 1.0]` when an enabled rule fills
 * the `opacity` slot of @p resolved with a valid `value` param, or `std::nullopt`
 * when no rule filled it / the param is missing / the value falls outside the
 * documented range. Caller applies the returned value via
 * `KWin::WindowPaintData::setOpacity` (absolute set, not multiplicative —
 * SetOpacity semantics are "make the window THIS opaque," not "scale by this
 * factor").
 *
 * @p resolved comes from the effect's `resolveRuleActions` helper, which
 * peeks the evaluator's per-window cache and only builds the WindowQuery on a
 * miss — so this pure extractor stays off the per-frame query-build hot path. An
 * empty `resolved` (windowless / unmatched window) simply has no opacity slot →
 * `nullopt`.
 */
std::optional<qreal> resolveWindowOpacity(const PhosphorRules::ResolvedActions& resolved);

/**
 * @brief Per-window border / title-bar appearance override — the runtime
 *        consumer for the SetBorder* / SetHideTitleBar rules.
 *
 * Each field is set only when an enabled rule fills the corresponding slot of
 * @p resolved with a valid param (bool for hideTitleBar/showBorder, an int in
 * the descriptor range for width/radius, a parseable `#AARRGGBB` for the
 * colours). Unset fields mean "no override — fall back to the global
 * snap/autotile border state." Returns `std::nullopt` when no rule fills any
 * slot (including the windowless / empty `resolved` case), so the caller can
 * skip the merge entirely.
 *
 * Applies to ANY matched window (snapped OR floating), mirroring
 * `resolveWindowOpacity`. The bool/int re-reads here mirror the load-time
 * descriptor validators in ruleaction.cpp (defence-in-depth). The colour reads
 * parse via `QColor(QString)`, which accepts the same hex shapes the load-time
 * `hasHexColor` validator admits (`#RGB`, `#RRGGBB`, `#AARRGGBB`) plus named
 * colours; the load boundary stays hex-only, so a named colour can only reach
 * this reader through an in-process path that bypassed load — still safe, since
 * any `QColor::isValid()` result renders fine.
 */
struct ResolvedWindowAppearance
{
    std::optional<bool> hideTitleBar;
    std::optional<bool> showBorder;
    std::optional<int> borderWidth;
    std::optional<int> borderRadius;
    // `activeColor` is the focused colour (from SetBorderColorActive),
    // `inactiveColor` the unfocused one (from SetBorderColorInactive, already
    // defaulted to active when that action was omitted). The accent sentinel has
    // been resolved to the matching system colour by the time it lands here —
    // the accent/highlight in activeColor, the inactive colour in inactiveColor.
    // updateWindowBorder picks by the window's focus state. A focus-scoped
    // single-colour rule (matching IsFocused) still works — it just fills
    // activeColor in its matching state.
    std::optional<QColor> activeColor;
    std::optional<QColor> inactiveColor;

    bool any() const
    {
        return hideTitleBar || showBorder || borderWidth || borderRadius || activeColor || inactiveColor;
    }
};

/// @p accentColor / @p inactiveColor are the live system colours the
/// `BorderColorToken::Accent` sentinel resolves to per focus state: @p accentColor
/// (the system accent / highlight) fills the focused/active slot, @p inactiveColor
/// the unfocused/inactive slot. Pass an invalid QColor for either when none is
/// known (the sentinel then contributes no colour for that state).
std::optional<ResolvedWindowAppearance> resolveWindowAppearance(const PhosphorRules::ResolvedActions& resolved,
                                                                const QColor& accentColor, const QColor& inactiveColor);

} // namespace PlasmaZones
