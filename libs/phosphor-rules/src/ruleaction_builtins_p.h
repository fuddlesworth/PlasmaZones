// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Private (non-installed) header — shared helpers for the built-in action
// descriptor tables. registerBuiltins() is split across
// ruleaction_builtins_engine.cpp and ruleaction_builtins_appearance.cpp for
// file-size; the param validators, validation bounds, and the two
// slot/enum-vocabulary helpers below are used by both halves, so they live
// here once rather than being duplicated per TU. Defined `inline` (not in an
// anonymous namespace) so an unused helper in either half raises no
// -Wunused-function.

#pragma once

#include <PhosphorRules/RuleAction.h>

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

namespace PhosphorRules {
namespace detail {

/// A descriptor whose params payload carries no constraint — accepts any
/// object. Used for actions whose params are free-form or future-extensible.
inline bool acceptAny(const QJsonObject&)
{
    return true;
}

/// Validates that @p params has a non-empty string at @p key.
inline bool hasNonEmptyString(const QJsonObject& params, QLatin1StringView key)
{
    const QJsonValue v = params.value(key);
    return v.isString() && !v.toString().isEmpty();
}

/// Validates that @p params has a JSON bool at @p key.
inline bool hasBool(const QJsonObject& params, QLatin1StringView key)
{
    return params.value(key).isBool();
}

/// Validates that @p params has a number in [0, @p maxValue] at @p key.
/// Geometry/border consumers truncate to int; the unit-scale slots (opacity,
/// tint strength) read the double as-is. Either way the upper bound keeps a
/// hand-edited payload from carrying an absurd value downstream.
inline bool hasNumberInRange(const QJsonObject& params, QLatin1StringView key, double maxValue)
{
    const QJsonValue v = params.value(key);
    if (!v.isDouble()) {
        return false;
    }
    const double d = v.toDouble();
    return d >= 0.0 && d <= maxValue;
}

/// Validates that @p params has a `#`-prefixed hex colour string at @p key.
/// Accepts the standard QColor hex shapes the effect-side consumer parses via
/// `QColor(QString)`: `#RGB` (4), `#RRGGBB` (7) and `#AARRGGBB` (9 — QColor reads
/// a 9-digit hex as alpha-first). The picker emits `#AARRGGBB`; the wider set
/// also keeps hand-edited short-form payloads from being silently dropped on
/// load while still rejecting non-hex/garbage. Named colours ("red") are NOT
/// accepted here — the boundary stays hex-only even though the consumer's
/// QColor would resolve them.
inline bool hasHexColor(const QJsonObject& params, QLatin1StringView key)
{
    const QJsonValue v = params.value(key);
    if (!v.isString()) {
        return false;
    }
    const QString s = v.toString();
    if ((s.size() != 4 && s.size() != 7 && s.size() != 9) || s.at(0) != QLatin1Char('#')) {
        return false;
    }
    for (int i = 1; i < s.size(); ++i) {
        const QChar c = s.at(i);
        const bool hex = (c >= QLatin1Char('0') && c <= QLatin1Char('9'))
            || (c >= QLatin1Char('a') && c <= QLatin1Char('f')) || (c >= QLatin1Char('A') && c <= QLatin1Char('F'));
        if (!hex) {
            return false;
        }
    }
    return true;
}

/// A border colour param value: a hex shape `hasHexColor` admits, OR the
/// `BorderColorToken::Accent` sentinel ("track the live system accent").
inline bool hasHexColorOrAccent(const QJsonObject& params, QLatin1StringView key)
{
    if (params.value(key).toString() == BorderColorToken::Accent) {
        return true;
    }
    return hasHexColor(params, key);
}

// Upper validation bounds (display units). The effect/daemon clamp to their
// own ConfigDefaults ranges on consumption; these only reject grossly
// malformed hand-edited payloads. Kept generous so values a user could pick
// through the global UI are never dropped on load.
// Border width/radius upper bounds live in RuleAction.h (MaxBorderWidth /
// MaxBorderRadius) so the KWin-effect consumer re-validation shares them.
inline constexpr double kMaxGap = 500.0;
// Zone-overlay border dimensions have their own bounds mirroring the global
// `Snapping.Zones.Border` config ranges (width 0-10, radius 0-50) — the overlay
// radius goes wider than the per-window `MaxBorderRadius` (20).
inline constexpr double kMaxOverlayBorderRadius = 50.0;
// Autotile parameter bounds (display units), mirroring the AutotileDefaults
// clamps the engine applies on consumption. These only reject grossly malformed
// hand-edited payloads.
inline constexpr double kMaxTiledWindows = 12.0;
inline constexpr double kMaxMasterCount = 5.0;
// Split-ratio bounds. The percent-editor display range is the exact primary pair
// ([10, 90] %); the wire ratio is derived (÷ 100) so the two never drift and the
// display bounds stay exact (0.1 * 100.0 is not exactly 10.0 in IEEE-754).
inline constexpr double kMinSplitPercent = 10.0;
inline constexpr double kMaxSplitPercent = 90.0;
inline constexpr double kMinSplitRatio = kMinSplitPercent / 100.0;
inline constexpr double kMaxSplitRatio = kMaxSplitPercent / 100.0;

/// Helper to keep the registerBuiltins body legible — every built-in shares
/// the same constant slot pattern (no slot-from-params resolution).
inline ActionDescriptor::SlotResolver constantSlot(QLatin1StringView slot)
{
    return [s = QString(slot)](const QJsonObject&) {
        return s;
    };
}

/// The engine-token wire strings DisableEngine / SetEngineMode pickers
/// expose. Keeping them together makes the "both pickers share the engine
/// enum" invariant visible at the descriptor level. The order mirrors
/// `PhosphorZones::allModes()` so the editor's enum dropdown lists modes
/// in the same order across surfaces.
///
/// Returns a const reference into a function-local static. The DisableEngine
/// validator runs on every action-load (rule store load, every rule edit),
/// so the previous by-value form rebuilt the 3-element list on every call;
/// the static keeps the descriptor's enum-vocabulary stable across the
/// process and the validator's `contains` cheap.
inline const QStringList& engineModeOptions()
{
    // NOTE: this is the engine-mode ACTION vocabulary (SetEngineMode param) and
    // is DELIBERATELY distinct from the Mode MATCH-field vocabulary in
    // MatchTypes.h, which uses "snapping" / "tiling" (no "autotile"). The action
    // names the engine ("autotile"); the match field names the placement mode a
    // window is in ("tiling"). Do not unify them — a Mode match rule authored
    // with "autotile" would silently never match.
    static const QStringList s_options{
        QStringLiteral("snapping"),
        QStringLiteral("autotile"),
        QStringLiteral("scrolling"),
    };
    return s_options;
}

} // namespace detail
} // namespace PhosphorRules
