// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/Curve.h>
#include <PhosphorAnimation/phosphoranimation_export.h>

#include <QJsonObject>
#include <QString>
#include <QtGlobal>

#include <memory>

namespace PhosphorAnimation {

/**
 * @brief Configuration for a single animation event.
 *
 * A Profile bundles the curve, timing, and orchestration knobs for one
 * animation "event" (e.g., zone-snap-in, OSD-fade, layout-switch). Shell
 * code resolves a Profile via `ProfileTree::resolve(path)` and feeds it
 * to WindowMotion / AnimationController / higher-level runners.
 *
 * ## Fields
 *
 * - `curve`            the Curve to evaluate (Easing, Spring, user type)
 * - `duration`         animation length in milliseconds (parametric use)
 * - `minDistance`      skip threshold in pixels (no animation if the
 *                      window moves less than this and doesn't resize)
 * - `sequenceMode`     0 = all targets start simultaneously,
 *                      1 = cascade via stagger timer
 * - `staggerInterval`  ms between cascade starts (sequenceMode == 1)
 * - `presetName`       optional user-readable name (e.g., "My Bouncy"),
 *                      used by the settings UI for preset management
 *                      and not consumed by the runtime
 *
 * ## Defaults
 *
 * Default construction yields: Easing outCubic (0.33, 1.0, 0.68, 1.0),
 * 150 ms, zero min-distance, all-at-once, 30 ms stagger interval. These
 * match the pre-library animation defaults shipped in Settings.
 *
 * ## Immutability of curve
 *
 * The `curve` pointer is `shared_ptr<const Curve>`. Profiles are
 * typically mutated by swapping the pointer; direct curve mutation is
 * forbidden (immutable curve contract). Multiple Profiles can safely
 * share a curve instance.
 */
class PHOSPHORANIMATION_EXPORT Profile
{
public:
    Profile() = default;

    Profile(const Profile&) = default;
    Profile& operator=(const Profile&) = default;
    Profile(Profile&&) = default;
    Profile& operator=(Profile&&) = default;

    // ─────── Fields (public — Profile is a value aggregate) ───────

    /// The curve evaluated for this event. Null = use the library
    /// default curve (outCubic bezier) at runtime; callers should treat
    /// null as "inherit from parent" when walking a ProfileTree.
    std::shared_ptr<const Curve> curve;

    /// Animation length in milliseconds. Used only by parametric curves
    /// (Easing). Spring curves derive their own settle time; this value
    /// can still be supplied as an upper bound (hinted via ProfileTree).
    qreal duration = 150.0;

    /// Skip threshold in pixels. If the window moves less than this
    /// distance AND its size does not change, the animation is skipped.
    int minDistance = 0;

    /// 0 = all targets run simultaneously.
    /// 1 = cascade start via StaggerTimer with `staggerInterval` between
    ///     adjacent targets (visually: windows animate one after another).
    int sequenceMode = 0;

    /// Milliseconds between cascade starts when `sequenceMode == 1`.
    int staggerInterval = 30;

    /// Optional user-assigned preset name. Purely decorative for UI —
    /// the runtime never branches on this.
    QString presetName;

    // ─────── Serialization ───────

    /**
     * @brief Serialize to a JSON object.
     *
     * Shape:
     * @code
     *   {
     *     "curve":          "spring:12.0,0.8",
     *     "duration":       150,
     *     "minDistance":    0,
     *     "sequenceMode":   0,
     *     "staggerInterval": 30,
     *     "presetName":     "My Spring"   // omitted if empty
     *   }
     * @endcode
     *
     * When `curve` is null, the "curve" key is omitted — callers reading
     * back get a null curve (meaning "inherit"). This is the mechanism
     * ProfileTree uses to distinguish partial overrides.
     */
    QJsonObject toJson() const;

    /// Parse from a JSON object. Missing fields fall back to defaults.
    /// The `curve` string is parsed via CurveRegistry::create(), so any
    /// registered (including third-party) curve type is supported.
    static Profile fromJson(const QJsonObject& obj);

    // ─────── Equality ───────

    bool operator==(const Profile& other) const;
    bool operator!=(const Profile& other) const
    {
        return !(*this == other);
    }
};

} // namespace PhosphorAnimation
