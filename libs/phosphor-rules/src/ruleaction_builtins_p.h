// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Private (non-installed) header — shared helpers for the built-in action
// descriptor tables. registerBuiltins() is split across
// ruleaction_builtins_engine.cpp, ruleaction_builtins_appearance.cpp and
// ruleaction_builtins_indicators.cpp for file-size; the param validators,
// validation bounds, and the two slot/enum-vocabulary helpers below are used
// by all three, so they live here once rather than being duplicated per TU.
// Defined `inline` (not in an anonymous namespace) so a helper unused by one
// of them raises no -Wunused-function.

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

/// Validates that @p params has a string at @p key, EMPTY INCLUDED. The
/// counterpart to hasNonEmptyString, for the slots where empty is a value the
/// user can mean rather than an unfilled field. The tab-indicator font family
/// is the one such slot today: empty there means "the system font", which is
/// how a rule takes one screen back to the default after a global family was
/// picked. Every slot naming an ID or a token wants hasNonEmptyString instead,
/// because an empty id resolves to nothing downstream.
inline bool hasStringAllowingEmpty(const QJsonObject& params, QLatin1StringView key)
{
    return params.value(key).isString();
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

/// Validates that @p params has a number in [@p minValue, @p maxValue] at
/// @p key. The explicit-floor twin of hasNumberInRange above, for slots whose
/// floor is not zero — whether below it (the tab indicator's gap, where a
/// negative draws the indicator over the window, and its corner radius, where
/// -1 is the "fully rounded" sentinel) or above it (its width and length,
/// which must reject 0 as well as negatives). Slots that genuinely floor at
/// zero should keep using hasNumberInRange, so a stray negative stays a
/// validation failure rather than silently reaching a consumer that assumes
/// non-negative.
inline bool hasNumberInSignedRange(const QJsonObject& params, QLatin1StringView key, double minValue, double maxValue)
{
    const QJsonValue v = params.value(key);
    if (!v.isDouble()) {
        return false;
    }
    const double d = v.toDouble();
    return d >= minValue && d <= maxValue;
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
// Scrolling column-width bounds. The RATIO pair is the installed
// PhosphorRules/RuleAction.h constants (shared with the zones-layer context
// resolver); the percent pair derives from it so the descriptor display
// range and the stored fraction can never drift.
inline constexpr double kMinColumnWidthRatio = MinColumnWidthRatio;
inline constexpr double kMaxColumnWidthRatio = MaxColumnWidthRatio;
inline constexpr double kMinColumnWidthPercent = kMinColumnWidthRatio * 100.0;
inline constexpr double kMaxColumnWidthPercent = kMaxColumnWidthRatio * 100.0;
// ScrollFactor multiplier bounds. Aliased from the installed RuleAction.h
// constants (the kMinColumnWidthRatio pattern) so the descriptor validator and
// the KWin-effect consumer re-validation check the same numbers. The wire
// value is the multiplier itself — a fraction below 1 slows scrolling — so the
// editor shows it as a PERCENT and the display pair derives from the wire pair
// exactly as the split-ratio and column-width pairs do.
inline constexpr double kMinScrollFactor = MinScrollFactor;
inline constexpr double kMaxScrollFactor = MaxScrollFactor;
inline constexpr double kMinScrollFactorPercent = kMinScrollFactor * 100.0;
inline constexpr double kMaxScrollFactorPercent = kMaxScrollFactor * 100.0;
// Tab-indicator bounds, mirroring the ConfigDefaults ranges the settings
// schema clamps to. As with every other bound here these only reject grossly
// malformed hand-edited payloads; the consumer re-clamps.
//
// The GAP floor is negative on purpose (a negative gap draws the indicator
// over the window, which is niri's behaviour) and so is the CORNER RADIUS
// floor, whose -1 is the config layer's "fully rounded" sentinel rather than a
// real negative radius. hasNumberInSignedRange is the EXPLICIT-FLOOR helper,
// so it serves both directions: those two below zero, and WIDTH (floor 1) and
// LENGTH (floor 0.05) above it. Only the bounds whose floor really is zero use
// the zero-floored helper.
// Aliased from the installed RuleAction.h constants, the kMinColumnWidthRatio
// pattern, so the descriptor validators and the zones-layer context resolver
// check the same numbers rather than two hand-mirrored copies.
inline constexpr double kMinTabIndicatorGap = MinTabIndicatorGap;
inline constexpr double kMaxTabIndicatorGap = MaxTabIndicatorGap;
inline constexpr double kMinTabIndicatorWidth = MinTabIndicatorWidth;
inline constexpr double kMaxTabIndicatorWidth = MaxTabIndicatorWidth;
inline constexpr double kTabIndicatorCornerRadiusPill = TabIndicatorCornerRadiusPill;
inline constexpr double kMaxTabIndicatorCornerRadius = MaxTabIndicatorCornerRadius;
inline constexpr double kMinTabIndicatorLengthRatio = MinTabIndicatorLengthRatio;
inline constexpr double kMaxTabIndicatorLengthRatio = MaxTabIndicatorLengthRatio;
// LENGTH is stored as a fraction and edited as a percent, so it carries the
// derived display pair the split ratio and column width do rather than
// open-coding the * 100.0 in the descriptor.
inline constexpr double kMinTabIndicatorLengthPercent = kMinTabIndicatorLengthRatio * 100.0;
inline constexpr double kMaxTabIndicatorLengthPercent = kMaxTabIndicatorLengthRatio * 100.0;
// The label font's weight scale, aliased the same way. Both ends are positive
// but the floor is 100 rather than 0, so this pair takes the explicit-floor
// helper too: a zero would not be a lighter font, only an off-scale number.
inline constexpr double kMinTabIndicatorFontWeight = MinTabIndicatorFontWeight;
inline constexpr double kMaxTabIndicatorFontWeight = MaxTabIndicatorFontWeight;

// Drop indicator. Every floor here really is zero — a zero border width is a
// fill with no edge and a zero radius is a square corner, neither a sentinel —
// so these take the zero-floored helper rather than the explicit-floor one the
// tab indicator's signed bounds need.
inline constexpr double kMinDropIndicatorOpacity = MinDropIndicatorOpacity;
inline constexpr double kMaxDropIndicatorOpacity = MaxDropIndicatorOpacity;
// OPACITY is stored as a fraction and edited as a percent, like every other
// opacity action; the display pair derives from the wire pair.
inline constexpr double kMinDropIndicatorOpacityPercent = kMinDropIndicatorOpacity * 100.0;
inline constexpr double kMaxDropIndicatorOpacityPercent = kMaxDropIndicatorOpacity * 100.0;
inline constexpr double kMinDropIndicatorBorderWidth = MinDropIndicatorBorderWidth;
inline constexpr double kMaxDropIndicatorBorderWidth = MaxDropIndicatorBorderWidth;
inline constexpr double kMinDropIndicatorBorderRadius = MinDropIndicatorBorderRadius;
inline constexpr double kMaxDropIndicatorBorderRadius = MaxDropIndicatorBorderRadius;

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
    // is DELIBERATELY distinct from the Mode MATCH-field vocabulary, which is
    // `PhosphorRules::ModeToken` in MatchTypes.h and has no "autotile" — its
    // middle value is ModeToken::Tiling. The action names the ENGINE; the match
    // field names the placement mode a window is IN. Do not unify them, and do
    // not build this list out of the ModeToken constants either: the two
    // vocabularies agree on two of three spellings by coincidence, and sharing
    // the constants would make a rename of one silently move the other. A Mode
    // match rule authored with "autotile" silently never matches, which is the
    // trap the separation exists to prevent.
    //
    // The literals below are therefore the CANONICAL definition of the action
    // vocabulary — this function is its single source of truth, so spelling
    // them here is the definition, not a duplicate.
    static const QStringList s_options{
        QStringLiteral("snapping"),
        QStringLiteral("autotile"),
        QStringLiteral("scrolling"),
    };
    return s_options;
}

} // namespace detail
} // namespace PhosphorRules
