// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/ShaderProfile.h>
#include <PhosphorAnimation/ShaderProfileTree.h>

#include <QString>

namespace PhosphorAnimation {
class CurveRegistry;
}

namespace PhosphorWindowRule {
class RuleEvaluator;
}

namespace PlasmaZones {

/**
 * @file shader_resolve.h
 * @brief Effect-local animation App Rule cascade shims, reimplemented on
 *        PhosphorWindowRule::RuleEvaluator.
 *
 * These mirror, byte-identically, the standalone
 * `PhosphorAnimationShaders::resolveAnimation*` resolvers — but resolve the
 * App Rule layer through the unified rule engine instead of the
 * `AnimationAppRuleList` first-match walk. The animation App Rules are
 * converted to a `WindowRuleSet` by `AnimationAppRuleBridge`; the bound
 * `RuleEvaluator` is owned by `ShaderTransitionManager`.
 *
 * The duration clamp, the curve `tryCreate` fallback, the engaged-empty
 * `effectId` sentinel, and the empty-input short-circuits all live in these
 * shims — the bridge and the evaluator stay generic.
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
 * @brief Duration cascade: window-class timing rule → per-event default.
 *
 * A rule filling the `anim-timing:<eventPath>` slot with `durationMs > 0`
 * returns that value clamped to `[Min, Max]AnimationDurationMs`. A
 * `durationMs <= 0` (the "inherit" sentinel) or no rule — including an empty
 * @p windowClass / @p eventPath — returns @p defaultDurationMs.
 */
int resolveAnimationDuration(const PhosphorWindowRule::RuleEvaluator& evaluator, const QString& windowClass,
                             const QString& eventPath, int defaultDurationMs);

/**
 * @brief Motion-profile cascade: window-class timing rule → base profile.
 *
 * Returns @p base with its `curve` / `duration` replaced when a timing rule
 * fills the `anim-timing:<eventPath>` slot. A non-empty curve is parsed via
 * @p curveRegistry's `tryCreate` (a malformed curve keeps the base curve); a
 * `durationMs > 0` overrides the duration, clamped identically to
 * `resolveAnimationDuration`. An empty @p windowClass / @p eventPath or no
 * matching rule returns @p base unchanged.
 */
PhosphorAnimation::Profile resolveAnimationMotionProfile(const PhosphorWindowRule::RuleEvaluator& evaluator,
                                                         const PhosphorAnimation::Profile& base,
                                                         const QString& windowClass, const QString& eventPath,
                                                         const PhosphorAnimation::CurveRegistry& curveRegistry);

} // namespace PlasmaZones
