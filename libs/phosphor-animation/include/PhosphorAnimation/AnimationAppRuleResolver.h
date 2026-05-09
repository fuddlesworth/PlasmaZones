// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/AnimationAppRule.h>
#include <PhosphorAnimation/ShaderProfile.h>
#include <PhosphorAnimation/ShaderProfileTree.h>
#include <PhosphorAnimation/phosphoranimation_export.h>

#include <QString>

namespace PhosphorAnimationShaders {

/**
 * @brief Cascade resolver: window-class rule → per-event tree.
 *
 * Returns the `ShaderProfile` that should drive a single (windowClass,
 * eventPath) animation event. The cascade is:
 *
 *   1. `AnimationAppRuleList::resolveShader` — first matching rule.
 *      A hit produces a `ShaderProfile` whose `effectId` and
 *      `parameters` are taken verbatim from the rule. The rule's empty
 *      `effectId` is preserved as engaged-empty (the documented
 *      "explicitly no shader" sentinel on `ShaderProfile`), so a rule
 *      can disable the per-event default for matching windows without
 *      falling through to the tree.
 *
 *   2. `ShaderProfileTree::resolve(eventPath)` — the per-event walk-up
 *      (event leaf → category → baseline → library default).
 *
 * Empty `windowClass` short-circuits to step 2 (no rule can match an
 * unidentified window). Empty `eventPath` is forwarded to the tree
 * unchanged — `ShaderProfile`'s empty-effectId handling at the
 * consumer's side stays the same as the current direct-tree-resolve
 * behaviour.
 */
PHOSPHORANIMATION_EXPORT ShaderProfile resolveAnimationShaderProfile(const AnimationAppRuleList& rules,
                                                                     const ShaderProfileTree& tree,
                                                                     const QString& windowClass,
                                                                     const QString& eventPath);

/**
 * @brief Duration cascade: window-class timing rule → per-event default.
 *
 * Returns the duration in milliseconds that should drive the animation
 * for a single (windowClass, eventPath) event:
 *
 *   1. `AnimationAppRuleList::resolveTiming` — first matching rule.
 *      A hit with `durationMs > 0` returns that value. A hit with
 *      `durationMs <= 0` is the documented "inherit per-event default"
 *      sentinel and falls through to step 2.
 *
 *   2. The caller-provided @p defaultDurationMs (typically the global
 *      `Settings::animationDuration` or a per-event-derived value).
 *
 * The curve override on a `Timing` rule is intentionally not surfaced
 * here — curve cascades through the motion `ProfileTree`, which is a
 * separate plumbing layer.
 */
PHOSPHORANIMATION_EXPORT int resolveAnimationDuration(const AnimationAppRuleList& rules, const QString& windowClass,
                                                      const QString& eventPath, int defaultDurationMs);

} // namespace PhosphorAnimationShaders
