// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/Curve.h>
#include <PhosphorAnimation/phosphoranimation_export.h>

#include <QJsonObject>
#include <QString>
#include <QtGlobal>

#include <memory>
#include <optional>

namespace PhosphorAnimation {

class CurveRegistry;

/**
 * @brief How a batch of animations starts.
 *
 * Strongly typed so callers cannot confuse it with @c staggerInterval
 * or other int fields. Numeric values match the historical wire format
 * (0 = AllAtOnce, 1 = Cascade) so existing D-Bus / config integers can
 * be `static_cast` without a translation table.
 */
enum class SequenceMode : int {
    AllAtOnce = 0, ///< Every target begins simultaneously
    Cascade = 1, ///< Targets begin one after another via StaggerTimer
};

/**
 * @brief Configuration for a single animation event.
 *
 * A Profile bundles the curve, timing, and orchestration knobs for one
 * animation "event" (e.g., zone-snap-in, OSD-fade, layout-switch). Shell
 * code resolves a Profile via `ProfileTree::resolve(path)` and feeds it
 * to WindowMotion / AnimationController / higher-level runners.
 *
 * ## "Set" vs "unset" semantics
 *
 * Every field is `std::optional<T>` (or a nullable shared_ptr for the
 * curve). This is load-bearing for `ProfileTree` inheritance: a child
 * override with `duration.reset()` means "inherit duration from the
 * parent", while `duration = 150` means "explicitly use 150 at this
 * path, even if the parent says otherwise". A sentinel-based design
 * (e.g., "treat 0 as unset") can't distinguish the user-chose-default
 * case from the user-said-nothing case, so an optional is the only
 * correct representation.
 *
 * Callers that want the effective runtime value (with library defaults
 * filled in for any unset field) use the `effective*()` getters or
 * `withDefaults()` which returns a `Profile` where every optional is
 * guaranteed to be engaged.
 *
 * ## Library defaults
 *
 * The library-wide fallbacks are exposed as `DefaultDuration` etc.
 * below so callers and unit tests don't need to hardcode the numbers.
 * `ProfileTree::resolve()` fills any still-unset field after chain
 * resolution with these defaults.
 *
 * ## Immutability of curve
 *
 * The `curve` pointer is `shared_ptr<const Curve>`. Profiles are
 * typically mutated by swapping the pointer; direct curve mutation is
 * forbidden by the Curve contract. Multiple Profiles can safely share
 * a curve instance.
 */
class PHOSPHORANIMATION_EXPORT Profile
{
public:
    // ─────── Library defaults ───────

    static constexpr qreal DefaultDuration = 150.0;
    static constexpr int DefaultMinDistance = 0;
    static constexpr SequenceMode DefaultSequenceMode = SequenceMode::AllAtOnce;
    static constexpr int DefaultStaggerInterval = 30;

    Profile() = default;

    Profile(const Profile&) = default;
    Profile& operator=(const Profile&) = default;
    Profile(Profile&&) = default;
    Profile& operator=(Profile&&) = default;

    // ─────── Fields (public — Profile is a value aggregate) ───────

    /// The curve evaluated for this event. `nullptr` = inherit from
    /// parent in a ProfileTree, or library default (outCubic bezier)
    /// if no parent supplies one.
    std::shared_ptr<const Curve> curve;

    /// Animation length in milliseconds. Used only by parametric curves
    /// (Easing). Spring curves derive their own settle time; this value
    /// can still be supplied as an upper bound (hinted via ProfileTree).
    /// `std::nullopt` = inherit / use `DefaultDuration`.
    std::optional<qreal> duration;

    /// Skip threshold in pixels. If the window moves less than this
    /// distance AND its size does not change, the animation is skipped.
    /// `std::nullopt` = inherit / use `DefaultMinDistance`.
    std::optional<int> minDistance;

    /// Whether cascade or simultaneous start.
    /// `std::nullopt` = inherit / use `DefaultSequenceMode`.
    std::optional<SequenceMode> sequenceMode;

    /// Milliseconds between cascade starts when `sequenceMode ==
    /// SequenceMode::Cascade`. `std::nullopt` = inherit / use
    /// `DefaultStaggerInterval`.
    std::optional<int> staggerInterval;

    /// Optional user-assigned preset name. Purely decorative for UI —
    /// the runtime never branches on this. `std::nullopt` = inherit from
    /// parent in a ProfileTree. An engaged-but-empty optional explicitly
    /// overrides the parent's name with an empty string (same semantic as
    /// every other field: an engaged optional means "I have an opinion").
    std::optional<QString> presetName;

    // ─────── Effective getters (optional + library default) ───────

    qreal effectiveDuration() const
    {
        return duration.value_or(DefaultDuration);
    }
    int effectiveMinDistance() const
    {
        return minDistance.value_or(DefaultMinDistance);
    }
    SequenceMode effectiveSequenceMode() const
    {
        return sequenceMode.value_or(DefaultSequenceMode);
    }
    int effectiveStaggerInterval() const
    {
        return staggerInterval.value_or(DefaultStaggerInterval);
    }

    /**
     * @brief Return a copy with every unset field filled from library
     * defaults. `curve` is left null if still unset — callers can then
     * substitute a default-constructed `Easing` if needed.
     */
    Profile withDefaults() const;

    // ─────── Serialization ───────

    /**
     * @brief Serialize to a JSON object.
     *
     * Only set fields are emitted; unset fields are omitted so a
     * reader can tell "inherit" from "explicitly X". Shape:
     * @code
     *   {
     *     "curve":           "spring:12.0,0.8",   // omitted if null
     *     "duration":        150,                  // omitted if unset
     *     "minDistance":     0,                    // omitted if unset
     *     "sequenceMode":    0,                    // omitted if unset
     *     "staggerInterval": 30,                   // omitted if unset
     *     "presetName":      "My Spring"           // omitted if unset
     *   }
     * @endcode
     */
    QJsonObject toJson() const;

    /// Parse from a JSON object. Missing keys produce unset fields
    /// (@c std::nullopt / null curve). The `curve` string is parsed via
    /// @p registry's `create()`, so any registered (including
    /// third-party) curve type the caller's registry knows about is
    /// supported. Per-process registries (replacing the prior
    /// `CurveRegistry::instance()` singleton) mean composition roots
    /// must thread their own registry to every fromJson call.
    static Profile fromJson(const QJsonObject& obj, const CurveRegistry& registry);

    // ─────── Equality ───────

    bool operator==(const Profile& other) const;
    bool operator!=(const Profile& other) const
    {
        return !(*this == other);
    }
};

} // namespace PhosphorAnimation
