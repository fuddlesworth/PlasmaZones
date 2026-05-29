// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/ShaderProfile.h>
#include <PhosphorAnimation/ShaderProfileTree.h>

#include <QString>

#include <optional>

namespace PhosphorAnimation {
class CurveRegistry;
}

namespace PhosphorWindowRule {
class RuleEvaluator;
}

namespace PlasmaZones {

/**
 * @file shader_resolve.h
 * @brief Effect-local per-window animation cascade shims, built on
 *        PhosphorWindowRule::RuleEvaluator.
 *
 * These walk the event-scoped action slots (`anim-shader:<event>`,
 * `anim-timing:<event>`, `anim-curve:<event>`) populated by `WindowRule`s
 * carrying `OverrideAnimation{Shader,Timing,Curve}` actions, falling back to
 * the per-event ShaderProfileTree / motion-profile defaults when no rule
 * matches. The duration clamp, the curve `tryCreate` fallback, the
 * engaged-empty `effectId` sentinel, and the empty-input short-circuits all
 * live in these shims — the evaluator stays generic.
 */

/**
 * @brief Cascade resolver: window-class shader rule → per-event tree.
 *
 * If an enabled animation rule fills the `anim-shader:<eventPath>` slot for
 * @p windowClass, returns a `ShaderProfile` whose `effectId` / `parameters`
 * come verbatim from the rule (an engaged-empty `effectId` is preserved as
 * the "block the per-event default" sentinel). Otherwise — including for an
 * empty @p windowClass or @p eventPath — falls through to
 * `tree.resolve(eventPath)`.
 */
PhosphorAnimationShaders::ShaderProfile
resolveAnimationShaderProfile(const PhosphorWindowRule::RuleEvaluator& evaluator,
                              const PhosphorAnimationShaders::ShaderProfileTree& tree, const QString& windowClass,
                              const QString& eventPath);

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
 *  - Empty @p windowClass / @p eventPath: returns
 *    `{ tree.resolve(eventPath), defaultDurationMs }` without touching the
 *    evaluator (the rule layer matches exclusively on `WindowClass`).
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
};
ResolvedShaderAndDuration resolveAnimationShaderAndDuration(const PhosphorWindowRule::RuleEvaluator& evaluator,
                                                            const PhosphorAnimationShaders::ShaderProfileTree& tree,
                                                            const QString& windowId, const QString& windowClass,
                                                            const QString& eventPath, int defaultDurationMs);

/**
 * @brief Motion-profile cascade: window-class timing rule → base profile.
 *
 * Returns @p base with its `curve` / `duration` replaced when a timing rule
 * fills the `anim-timing:<eventPath>` slot. A non-empty curve is parsed via
 * @p curveRegistry's `tryCreate` (a malformed curve keeps the base curve); a
 * `durationMs > 0` overrides the duration, clamped identically to
 * `resolveAnimationShaderAndDuration`. An empty @p windowClass / @p eventPath
 * or no matching rule returns @p base unchanged.
 */
PhosphorAnimation::Profile resolveAnimationMotionProfile(const PhosphorWindowRule::RuleEvaluator& evaluator,
                                                         const PhosphorAnimation::Profile& base,
                                                         const QString& windowClass, const QString& eventPath,
                                                         const PhosphorAnimation::CurveRegistry& curveRegistry);

/**
 * @brief Per-window opacity cascade — the runtime consumer for
 *        `SetOpacity` rules.
 *
 * Returns the rule-resolved opacity in `[0.0, 1.0]` when an enabled rule
 * matching @p windowClass fills the `opacity` slot with a valid `value`
 * param, or `std::nullopt` when no rule matches / the param is missing /
 * the value falls outside the documented range. Caller applies the
 * returned value via `KWin::WindowPaintData::setOpacity` (absolute set,
 * not multiplicative — SetOpacity semantics are "make the window THIS
 * opaque," not "scale by this factor").
 *
 * Empty @p windowClass or @p windowId short-circuit to `nullopt` — same
 * shape as the animation resolvers above; rules match exclusively on
 * `WindowClass` so a missing class can't match anything.
 *
 * Caller is the effect's `paintWindow` hook. The resolver does NOT cache
 * across calls — the evaluator's per-window cache (`resolveCached`) is
 * the right cache scope for this lookup, and the resolver consumes it.
 */
std::optional<qreal> resolveWindowOpacity(const PhosphorWindowRule::RuleEvaluator& evaluator,
                                          const QString& windowClass, const QString& windowId);

} // namespace PlasmaZones
