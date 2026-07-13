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

/// How a batch of animations starts. Numeric values match the historical wire format.
enum class SequenceMode : int {
    AllAtOnce = 0,
    Cascade = 1,
};

/// Configuration for a single animation event.
///
/// Fields are std::optional so ProfileTree inheritance can distinguish
/// "inherit from parent" (nullopt) from "explicitly set" (engaged).
/// Use effective*() getters or withDefaults() for runtime values with
/// library defaults filled in.
class PHOSPHORANIMATION_EXPORT Profile
{
public:
    static constexpr qreal DefaultDuration = 150.0;
    static constexpr int DefaultMinDistance = 0;
    static constexpr SequenceMode DefaultSequenceMode = SequenceMode::AllAtOnce;
    static constexpr int DefaultStaggerInterval = 30;

    // Upper bounds — anything beyond 1 hour is clearly malformed.
    static constexpr qreal MaxDurationMs = 60.0 * 60.0 * 1000.0;
    static constexpr int MaxStaggerIntervalMs = 60 * 60 * 1000;

    Profile() = default;

    Profile(const Profile&) = default;
    Profile& operator=(const Profile&) = default;
    Profile(Profile&&) = default;
    Profile& operator=(Profile&&) = default;

    /// nullptr = inherit from parent / library default (outCubic bezier).
    std::shared_ptr<const Curve> curve;

    /// Animation length in ms. Spring curves derive their own settle time.
    std::optional<qreal> duration;

    /// Skip threshold in px — animation skipped if distance < this and no size change.
    std::optional<int> minDistance;

    std::optional<SequenceMode> sequenceMode;

    /// Milliseconds between cascade starts.
    std::optional<int> staggerInterval;

    /// Optional user-assigned preset name. Purely decorative for UI.
    std::optional<QString> presetName;

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

    /// Copy with every unset field filled from library defaults — `curve` is
    /// backfilled with a default-constructed Easing (OutCubic) when null, and
    /// `duration` / `minDistance` / `sequenceMode` / `staggerInterval` take
    /// their Default* constants. `presetName` is the ONE exception: it has no
    /// library default and stays disengaged, so callers must still treat it as
    /// optional (`value_or`), never dereference it blind.
    Profile withDefaults() const;

    // JSON field names — shared with consumers that build Profile blobs externally.
    static constexpr auto JsonFieldCurve = "curve";
    static constexpr auto JsonFieldDuration = "duration";
    static constexpr auto JsonFieldMinDistance = "minDistance";
    static constexpr auto JsonFieldSequenceMode = "sequenceMode";
    static constexpr auto JsonFieldStaggerInterval = "staggerInterval";
    static constexpr auto JsonFieldPresetName = "presetName";

    /// Serialize to JSON. Only set fields are emitted.
    QJsonObject toJson() const;

    /// Parse from JSON. Missing keys produce unset fields. Curve resolved
    /// via @p registry. Out-of-range values are rejected (logged, left unset).
    static Profile fromJson(const QJsonObject& obj, const CurveRegistry& registry);

    bool operator==(const Profile& other) const;
    bool operator!=(const Profile& other) const
    {
        return !(*this == other);
    }
};

} // namespace PhosphorAnimation
