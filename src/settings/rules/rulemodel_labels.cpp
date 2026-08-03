// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The label half of RuleModel: the per-leaf and per-action human labels and
// the two summaries built from them, plus the static field / section /
// fallback label tables. Split out of rulemodel.cpp for file-size; the model
// mechanics (rows, ordering, lookups) stay there. Everything here is either a
// RuleModel member or file-local, so there is no shared private header.

#include "rulemodel.h"

#include "ruleauthoring.h"

#include "phosphor_i18n.h"

#include <PhosphorLayoutApi/LayoutId.h>
#include <PhosphorRules/MatchTypes.h>
#include <PhosphorRules/RuleAction.h>

#include <PhosphorZones/AssignmentEntry.h>

#include <QJsonArray>
#include <QStringList>

namespace PlasmaZones {

namespace {

namespace ActionType = PhosphorRules::ActionType;
using PhosphorRules::Field;
using PhosphorRules::MatchExpression;
using PhosphorRules::Operator;
using PhosphorRules::RuleAction;

/// Human label for a single leaf predicate ("Monitor: LG Ultra HD").
/// @p screenLookup and @p activityLookup resolve the opaque ScreenId /
/// Activity UUID values to friendly names; an empty lookup is treated as
/// "identity" so the function stays usable in code paths that have not yet
/// wired the SettingsController-backed resolvers.
QString leafLabel(const MatchExpression::Predicate& predicate, const RuleModel::LabelLookup& screenLookup,
                  const RuleModel::LabelLookup& activityLookup, const RuleModel::LabelLookup& zoneLookup,
                  const RuleModel::LabelLookup& layoutLookup, const RuleModel::LabelLookup& tilingAlgorithmLookup,
                  const RuleModel::LabelLookup& virtualDesktopLookup)
{
    // Pick the lookup matching the leaf's field. An empty lookup degenerates
    // to identity so this stays usable from code paths that have not yet
    // wired the SettingsController-backed resolvers.
    const RuleModel::LabelLookup* lookup = nullptr;
    if (predicate.field == Field::ScreenId) {
        lookup = &screenLookup;
    } else if (predicate.field == Field::Activity) {
        lookup = &activityLookup;
    } else if (predicate.field == Field::Zone) {
        lookup = &zoneLookup;
    } else if (predicate.field == Field::VirtualDesktop && predicate.op == Operator::Equals) {
        // Resolves the desktop number to its name ("2" → "Work"); an unnamed or
        // unknown desktop falls back to the bare number via resolveOne below. Only
        // for Equals — under GreaterThan/LessThan the value is a numeric threshold,
        // where a name ("greater than Work") would read as nonsense, so those keep
        // the bare number.
        lookup = &virtualDesktopLookup;
    }
    const auto resolveOne = [lookup](const QString& raw) {
        if (!lookup || !*lookup) {
            return raw;
        }
        const QString label = (*lookup)(raw);
        return label.isEmpty() ? raw : label;
    };

    // Mode is a closed wire-token vocabulary — render the friendly label
    // ("Mode: Snapping") via the same single-source table the editor dropdown uses,
    // rather than a second hardcoded copy. An unknown token round-trips verbatim.
    if (predicate.field == Field::Mode) {
        return PhosphorI18n::tr("%1: %2").arg(RuleModel::fieldLabel(predicate.field),
                                              RuleAuthoring::modeLabel(predicate.value.toString()));
    }

    // Screen orientation is a closed wire-token vocabulary too — resolve via the
    // shared orientationLabel() table like Mode above.
    if (predicate.field == Field::ScreenOrientation) {
        return PhosphorI18n::tr("%1: %2").arg(RuleModel::fieldLabel(predicate.field),
                                              RuleAuthoring::orientationLabel(predicate.value.toString()));
    }

    // Window type is the int underlying the WindowType enum — resolve it to the
    // friendly label ("Window type: Dialog") via the same single-source table the
    // editor dropdown uses, rather than showing the bare int.
    if (predicate.field == Field::WindowType) {
        return PhosphorI18n::tr("%1: %2").arg(RuleModel::fieldLabel(predicate.field),
                                              RuleAuthoring::windowTypeLabel(predicate.value.toInt()));
    }

    // Active layout: a snap layout id resolves via the SetSnappingLayout lookup;
    // an autotile id ("autotile:<algo>") resolves its algorithm via the tiling
    // lookup after stripping the prefix (LayoutId is the one owner of that
    // prefix; the settings controller builds ids with the same helpers), so
    // the collapsed summary matches the expanded tree — which resolves both id
    // shapes through appSettings.layouts. Unknown ids round-trip verbatim.
    if (predicate.field == Field::ActiveLayout) {
        const QString value = predicate.value.toString();
        QString label = value;
        if (PhosphorLayout::LayoutId::isScrolling(value)) {
            // The bare mode sentinel has no layout entity to look up.
            label = PhosphorI18n::tr("Scrolling", "tiling mode name");
        } else if (PhosphorLayout::LayoutId::isAutotile(value)) {
            if (tilingAlgorithmLookup) {
                const QString resolved = tilingAlgorithmLookup(PhosphorLayout::LayoutId::extractAlgorithmId(value));
                if (!resolved.isEmpty()) {
                    label = resolved;
                }
            }
        } else if (layoutLookup) {
            const QString resolved = layoutLookup(value);
            if (!resolved.isEmpty()) {
                label = resolved;
            }
        }
        return PhosphorI18n::tr("%1: %2").arg(RuleModel::fieldLabel(predicate.field), label);
    }

    // Boolean fields (Maximized, Keep above, Skip taskbar, …) render their
    // value as On / Off instead of the raw JSON "true" / "false" the generic
    // toString() fallback would emit, matching the editor toggle and the
    // expanded match tree (MatchExpressionView).
    if (PhosphorRules::fieldIsBool(predicate.field)) {
        return PhosphorI18n::tr("%1: %2").arg(RuleModel::fieldLabel(predicate.field),
                                              predicate.value.toBool() ? PhosphorI18n::tr("On")
                                                                       : PhosphorI18n::tr("Off"));
    }

    return PhosphorI18n::tr("%1: %2").arg(RuleModel::fieldLabel(predicate.field),
                                          resolveOne(predicate.value.toString()));
}

/// Localise a single engine-mode wire token. Returns an empty QString
/// for an empty input so callers can branch on it; unknown tokens
/// (a future picker option, a hand-edited rule) round-trip verbatim.
/// Routes through `PhosphorZones::modeFromWireString` so the wire-token
/// enumeration stays in one place — a future Mode enum extension lands
/// at the AssignmentEntry switch + the engineModeOptions() picker, not
/// here.
QString engineModeDisplayLabel(const QString& wire)
{
    if (wire.isEmpty()) {
        return {};
    }
    const auto mode = PhosphorZones::modeFromWireString(wire);
    if (!mode) {
        return wire;
    }
    switch (*mode) {
    case PhosphorZones::AssignmentEntry::Snapping:
        return PhosphorI18n::tr("Snapping");
    case PhosphorZones::AssignmentEntry::Autotile:
        // "Tiling" everywhere the rule editor names the engine (matches the
        // Mode predicate options and the Monitors page's button).
        return PhosphorI18n::tr("Tiling");
    case PhosphorZones::AssignmentEntry::Scrolling:
        return PhosphorI18n::tr("Scrolling", "tiling mode name");
    }
    return wire;
}

/// The scrolling fraction params as a whole percent, or -1 when the payload
/// is not a usable [0, 1] fraction. The editor can hold a staged action whose
/// validator has not run yet, so a bool, a string, or an out-of-range number
/// is reachable here; the same reject paths SetOpacity and SetTintStrength
/// mirror, so a summary never claims a size the runtime will not apply.
int scrollFractionPercent(const QJsonValue& raw)
{
    if (raw.isNull() || raw.isUndefined()) {
        return -1;
    }
    // isDouble() mirrors the validators (a JSON string or bool is rejected
    // at load), so a staged "0.5" cannot render as a confident percent.
    if (!raw.isDouble()) {
        return -1;
    }
    const double v = raw.toDouble();
    // Floor at the validators' shared minimum, not 0: all five callers'
    // descriptors reject fractions below MinColumnWidthRatio (the
    // tab-indicator length shares the same 0.05 floor via
    // MinTabIndicatorLengthRatio), so a summary confidently printing "3%"
    // would claim a size the runtime refuses.
    if (v < PhosphorRules::MinColumnWidthRatio || v > 1.0) {
        return -1;
    }
    return qRound(v * 100.0);
}

/// True when @p value has one of the hex colour shapes the descriptor
/// validators admit (#RGB / #RRGGBB / #AARRGGBB). Mirrors the lib's
/// hasHexColor so a summary never echoes a value the runtime discards.
bool isHexColorShape(const QString& value)
{
    if (value.size() != 4 && value.size() != 7 && value.size() != 9) {
        return false;
    }
    if (!value.startsWith(QLatin1Char('#'))) {
        return false;
    }
    for (qsizetype i = 1; i < value.size(); ++i) {
        const QChar c = value.at(i);
        // Explicit ASCII ranges, exactly like the validator — QChar::isDigit
        // admits Unicode digits the validator rejects.
        const bool hex = (c >= QLatin1Char('0') && c <= QLatin1Char('9'))
            || (c >= QLatin1Char('a') && c <= QLatin1Char('f')) || (c >= QLatin1Char('A') && c <= QLatin1Char('F'));
        if (!hex) {
            return false;
        }
    }
    return true;
}

/// True when a closed-vocabulary token failed to resolve to a label.
/// enumOptionLabel round-trips an unrecognised token verbatim and every
/// declared token maps to prose that differs from it, so an unchanged string
/// is the miss. The runtime resolvers ignore such a token outright, which is
/// why the callers render "(invalid)" rather than the raw wire word — the
/// same contract the SetWindowLayer branch states below.
bool isUnresolvedEnumToken(const QString& token, const QString& label)
{
    return token.isEmpty() || label == token;
}

/// Human label for one action ("Snapping", "Float", "Excluded"). @p
/// snappingLayoutLookup resolves SetSnappingLayout's layoutId UUIDs;
/// @p tilingAlgorithmLookup resolves SetTilingAlgorithm's wire tokens
/// ("bsp", …) — split so a stray cross-resolve can't surface an algorithm
/// name in a snapping action's label or vice versa.
QString actionLabel(const RuleAction& action, const RuleModel::LabelLookup& snappingLayoutLookup,
                    const RuleModel::LabelLookup& tilingAlgorithmLookup,
                    const RuleModel::LabelLookup& shaderEffectLookup, const RuleModel::LabelLookup& overlayShaderLookup,
                    const RuleModel::LabelLookup& curveLookup, const RuleModel::LabelLookup& screenLookup,
                    const RuleModel::LabelLookup& decorationPackLookup)
{
    auto resolveWith = [](const QString& wire, const RuleModel::LabelLookup& lookup) {
        if (wire.isEmpty() || !lookup) {
            return wire;
        }
        const QString resolved = lookup(wire);
        return resolved.isEmpty() ? wire : resolved;
    };

    if (action.type == ActionType::SetEngineMode) {
        const QString mode = action.params.value(PhosphorRules::ActionParam::Mode).toString();
        const QString label = engineModeDisplayLabel(mode);
        return PhosphorI18n::tr("Engine: %1").arg(label.isEmpty() ? mode : label);
    }
    if (action.type == ActionType::SetSnappingLayout) {
        const QString layoutId = action.params.value(PhosphorRules::ActionParam::LayoutId).toString();
        return layoutId.isEmpty() ? PhosphorI18n::tr("Snapping layout")
                                  : PhosphorI18n::tr("Snapping: %1").arg(resolveWith(layoutId, snappingLayoutLookup));
    }
    if (action.type == ActionType::SetTilingAlgorithm) {
        const QString algo = action.params.value(PhosphorRules::ActionParam::Algorithm).toString();
        // Algorithms are wire tokens (`bsp`, `grid`, …). The dedicated
        // tilingAlgorithm lookup knows about autotile entries — the
        // RuleController wires it from settingsController.layouts,
        // which contains the displayName ("Binary Split") for each algorithm.
        return PhosphorI18n::tr("Tiling: %1").arg(resolveWith(algo, tilingAlgorithmLookup));
    }
    if (action.type == ActionType::DisableEngine) {
        // Name the engine being disabled — a rules list with
        // "Disable: Snapping" on DP-1 and "Disable: Tiling" on DP-2
        // otherwise reads as two identical "Disabled" rows. Empty mode → fall back to
        // the generic "Disabled" label so a malformed rule still reads
        // sensibly.
        const QString mode = action.params.value(PhosphorRules::ActionParam::Mode).toString();
        const QString label = engineModeDisplayLabel(mode);
        if (label.isEmpty()) {
            return PhosphorI18n::tr("Disabled");
        }
        return PhosphorI18n::tr("Disable: %1").arg(label);
    }
    // The exclusion family: all terminal with no Value param — the action's
    // presence IS the effect, so each summary states the outcome. The
    // blanket form names its scope so it reads distinctly beside the scoped
    // siblings in a mixed list.
    if (action.type == ActionType::Exclude) {
        return PhosphorI18n::tr("Excluded (placement and decorations)");
    }
    if (action.type == ActionType::ExcludePlacement) {
        return PhosphorI18n::tr("No tiling, snapping, or scrolling");
    }
    if (action.type == ActionType::ExcludeAnimations) {
        return PhosphorI18n::tr("No animations");
    }
    if (action.type == ActionType::ExcludeDecorations) {
        return PhosphorI18n::tr("No borders or decoration packs");
    }
    if (action.type == ActionType::Float) {
        return PhosphorI18n::tr("Float");
    }
    if (action.type == ActionType::SnapToZone) {
        const QJsonArray zones = action.params.value(PhosphorRules::ActionParam::Zones).toArray();
        QStringList nums;
        nums.reserve(zones.size());
        for (const QJsonValue& z : zones) {
            nums.append(QString::number(z.toInt()));
        }
        if (nums.isEmpty()) {
            return PhosphorI18n::tr("Snap to zone");
        }
        if (nums.size() == 1) {
            return PhosphorI18n::tr("Snap to zone %1").arg(nums.first());
        }
        return PhosphorI18n::tr("Snap to zones %1").arg(nums.join(QStringLiteral(", ")));
    }
    if (action.type == ActionType::RouteToScreen) {
        // Resolve the canonical target screen id to the same friendly monitor
        // label the ScreenId match-leaf surfaces (e.g. "LG Ultra HD · DP-2");
        // fall back to the raw id when no live monitor matches so a rule pinned
        // to an absent display stays legible.
        const QString screenId = action.params.value(PhosphorRules::ActionParam::TargetScreenId).toString();
        return screenId.isEmpty() ? PhosphorI18n::tr("Open on monitor")
                                  : PhosphorI18n::tr("Open on monitor: %1").arg(resolveWith(screenId, screenLookup));
    }
    if (action.type == ActionType::RouteToDesktop) {
        const int desktop = action.params.value(PhosphorRules::ActionParam::TargetDesktop).toInt();
        return desktop >= 1 ? PhosphorI18n::tr("Open on desktop %1").arg(desktop) : PhosphorI18n::tr("Open on desktop");
    }
    if (action.type == ActionType::SetOpacity) {
        // Mirror EVERY resolver reject path (shader_resolve.cpp's
        // resolveWindowOpacity) so the label never claims a behaviour
        // the runtime won't honour: null/undefined → label-only,
        // bool payload → "Opacity (invalid)", out-of-range value → same.
        const QJsonValue raw = action.params.value(PhosphorRules::ActionParam::Value);
        if (raw.isNull() || raw.isUndefined()) {
            return PhosphorI18n::tr("Opacity");
        }
        const QVariant rv = raw.toVariant();
        if (rv.typeId() == QMetaType::Bool) {
            return PhosphorI18n::tr("Opacity (invalid)");
        }
        bool ok = false;
        const double v = rv.toDouble(&ok);
        if (!ok || v < 0.0 || v > 1.0) {
            return PhosphorI18n::tr("Opacity (invalid)");
        }
        return PhosphorI18n::tr("Opacity: %1%").arg(static_cast<int>(v * 100.0 + 0.5));
    }
    if (action.type == ActionType::OverrideAnimationShader) {
        const QString id = action.params.value(PhosphorRules::ActionParam::EffectId).toString();
        return id.isEmpty() ? PhosphorI18n::tr("Block animation shader")
                            : PhosphorI18n::tr("Shader: %1").arg(resolveWith(id, shaderEffectLookup));
    }
    if (action.type == ActionType::OverrideDecorationChain) {
        const QJsonArray chain = action.params.value(PhosphorRules::ActionParam::Chain).toArray();
        if (chain.isEmpty()) {
            // The empty-chain sentinel clears the CUSTOM packs; the config-
            // backed border and opacity-tint layers still render (easy mode).
            // "Block decoration" was wrong — that outcome belongs to
            // ExcludeDecorations' "No borders or decoration packs".
            return PhosphorI18n::tr("Decoration packs: none");
        }
        QStringList names;
        for (const QJsonValue& entry : chain) {
            const QString id = entry.toString();
            if (!id.isEmpty()) {
                names.append(resolveWith(id, decorationPackLookup));
            }
        }
        // A non-empty array of only empty-string ids (hand-edited / malformed
        // rule) leaves `names` empty; fall back to the empty-chain label
        // rather than render a bare "Decoration: " with a trailing separator.
        if (names.isEmpty()) {
            return PhosphorI18n::tr("Decoration packs: none");
        }
        return PhosphorI18n::tr("Decoration: %1").arg(names.join(QStringLiteral(", ")));
    }
    if (action.type == ActionType::OverrideAnimationTiming) {
        const int ms = action.params.value(PhosphorRules::ActionParam::DurationMs).toInt();
        return ms > 0 ? PhosphorI18n::tr("Duration: %1 ms").arg(ms) : PhosphorI18n::tr("Animation duration");
    }
    if (action.type == ActionType::OverrideAnimationCurve) {
        const QString curve = action.params.value(PhosphorRules::ActionParam::Curve).toString();
        return curve.isEmpty() ? PhosphorI18n::tr("Animation curve")
                               : PhosphorI18n::tr("Curve: %1").arg(resolveWith(curve, curveLookup));
    }
    if (action.type == ActionType::OverrideOverlayShader) {
        const QString id = action.params.value(PhosphorRules::ActionParam::EffectId).toString();
        return id.isEmpty() ? PhosphorI18n::tr("Overlay shader")
                            : PhosphorI18n::tr("Overlay shader: %1").arg(resolveWith(id, overlayShaderLookup));
    }
    if (action.type == ActionType::OverrideOverlayStyle) {
        const QString v = action.params.value(PhosphorRules::ActionParam::Value).toString();
        if (v.isEmpty()) {
            return PhosphorI18n::tr("Overlay style");
        }
        // Delegate the token→label to the shared enumOptionLabel (the same source the
        // editor uses) instead of re-hardcoding the vocabulary here, like the sibling
        // SetInsertPosition / SetOverflowBehavior / SetDragBehavior cases below.
        return PhosphorI18n::tr("Overlay style: %1")
            .arg(RuleAuthoring::enumOptionLabel(action.type, PhosphorRules::ActionParam::Value, v));
    }
    if (action.type == ActionType::SetAlgorithmParam) {
        // Keyed on ActionParam::Algorithm (the target algorithm token), not Value;
        // the free-form params blob is summarized by naming the algorithm it tunes.
        const QString algo = action.params.value(PhosphorRules::ActionParam::Algorithm).toString();
        return algo.isEmpty() ? PhosphorI18n::tr("Algorithm parameter")
                              : PhosphorI18n::tr("Algorithm: %1").arg(resolveWith(algo, tilingAlgorithmLookup));
    }
    // ── single-value actions keyed on ActionParam::Value: bool actions
    //    resolve through boolActionStateLabel first, everything else through
    //    the per-type value formatting below ──
    {
        const QJsonValue raw = action.params.value(PhosphorRules::ActionParam::Value);
        // Boolean actions render their polarity-aware phrase ("Show border" /
        // "Hide border", …). The wording lives in RuleAuthoring so the editor
        // toggle caption and this summary always read the same; a non-boolean
        // action type returns empty and falls through to the cases below.
        if (const QString boolLabel = RuleAuthoring::boolActionStateLabel(action.type, raw.toBool());
            !boolLabel.isEmpty()) {
            return boolLabel;
        }
        if (action.type == ActionType::SetBorderWidth) {
            return PhosphorI18n::tr("Border width: %1 px").arg(raw.toInt());
        }
        if (action.type == ActionType::SetBorderRadius) {
            return PhosphorI18n::tr("Corner radius: %1 px").arg(raw.toInt());
        }
        if (action.type == ActionType::SetBorderColorActive || action.type == ActionType::SetBorderColorInactive) {
            // Each action carries a single colour in `value`. The accent sentinel
            // shows as a word, hex as upper-case.
            const QString value = raw.toString();
            const QString shown =
                value == PhosphorRules::BorderColorToken::Accent ? PhosphorI18n::tr("Accent") : value.toUpper();
            if (action.type == ActionType::SetBorderColorActive) {
                return PhosphorI18n::tr("Focused border: %1").arg(shown);
            }
            return PhosphorI18n::tr("Unfocused border: %1").arg(shown);
        }
        if (action.type == ActionType::SetTintStrength) {
            // Wire value is the [0,1] strength; shown as a percent to match
            // the editor (same treatment as SetSplitRatio). Mirror the
            // runtime resolver's reject paths (unitDoubleSlot: non-number or
            // out-of-range → ignored) like SetOpacity above, so the label
            // never claims a strength the effect won't apply: null/undefined
            // (present but unset) → bare label, bool or out-of-range →
            // "(invalid)".
            if (raw.isNull() || raw.isUndefined()) {
                return PhosphorI18n::tr("Tint strength");
            }
            const QVariant rv = raw.toVariant();
            bool ok = false;
            const double v = rv.typeId() == QMetaType::Bool ? 0.0 : rv.toDouble(&ok);
            if (!ok || v < 0.0 || v > 1.0) {
                return PhosphorI18n::tr("Tint strength (invalid)");
            }
            return PhosphorI18n::tr("Tint strength: %1%").arg(qRound(v * 100.0));
        }
        if (action.type == ActionType::SetTintColor) {
            const QString value = raw.toString();
            const QString shown =
                value == PhosphorRules::BorderColorToken::Accent ? PhosphorI18n::tr("Accent") : value.toUpper();
            return PhosphorI18n::tr("Tint color: %1").arg(shown);
        }
        // ── autotile parameter overrides ──
        if (action.type == ActionType::SetMaxWindows) {
            return PhosphorI18n::tr("Max tiled windows: %1").arg(raw.toInt());
        }
        if (action.type == ActionType::SetMasterCount) {
            return PhosphorI18n::tr("Master count: %1").arg(raw.toInt());
        }
        if (action.type == ActionType::SetSplitRatio) {
            // Wire value is the [0,1] ratio; shown as a percent to match the editor.
            return PhosphorI18n::tr("Split ratio: %1%").arg(qRound(raw.toDouble() * 100.0));
        }
        if (action.type == ActionType::SetInsertPosition) {
            return PhosphorI18n::tr("Insert: %1")
                .arg(RuleAuthoring::enumOptionLabel(action.type, PhosphorRules::ActionParam::Value, raw.toString()));
        }
        if (action.type == ActionType::SetOverflowBehavior) {
            return PhosphorI18n::tr("Overflow: %1")
                .arg(RuleAuthoring::enumOptionLabel(action.type, PhosphorRules::ActionParam::Value, raw.toString()));
        }
        if (action.type == ActionType::SetDragBehavior) {
            return PhosphorI18n::tr("Drag: %1")
                .arg(RuleAuthoring::enumOptionLabel(action.type, PhosphorRules::ActionParam::Value, raw.toString()));
        }
        // ── scrolling-engine overrides ──
        // Widths and heights are work-area fractions on the wire, shown as a
        // percent like SetSplitRatio and guarded through scrollFractionPercent.
        // The enum actions delegate token→label to the shared enumOptionLabel,
        // like SetInsertPosition above, and report an unresolved token rather
        // than echoing it. OpenTabbed is a bool action and already returned by
        // boolActionStateLabel.
        if (action.type == ActionType::SetScrollDefaultColumnWidth) {
            const int pct = scrollFractionPercent(raw);
            return pct < 0 ? PhosphorI18n::tr("Column width (invalid)")
                           : PhosphorI18n::tr("Column width: %1%").arg(pct);
        }
        if (action.type == ActionType::OpenColumnWidth) {
            const int pct = scrollFractionPercent(raw);
            return pct < 0 ? PhosphorI18n::tr("Open at width (invalid)")
                           : PhosphorI18n::tr("Open at width: %1%").arg(pct);
        }
        if (action.type == ActionType::SetScrollDefaultWindowHeight) {
            const int pct = scrollFractionPercent(raw);
            return pct < 0 ? PhosphorI18n::tr("Window height (invalid)")
                           : PhosphorI18n::tr("Window height: %1%").arg(pct);
        }
        if (action.type == ActionType::OpenWindowHeight) {
            const int pct = scrollFractionPercent(raw);
            return pct < 0 ? PhosphorI18n::tr("Open at height (invalid)")
                           : PhosphorI18n::tr("Open at height: %1%").arg(pct);
        }
        if (action.type == ActionType::SetScrollInsertPosition) {
            const QString token = raw.toString();
            const QString shown = RuleAuthoring::enumOptionLabel(action.type, PhosphorRules::ActionParam::Value, token);
            // Distinct from SetScrollDefaultColumnDisplay below: this one says
            // where a new WINDOW's column enters the strip, that one says what
            // a new column looks like.
            return isUnresolvedEnumToken(token, shown) ? PhosphorI18n::tr("Insert new windows (invalid)")
                                                       : PhosphorI18n::tr("Insert new windows: %1").arg(shown);
        }
        if (action.type == ActionType::SetCenterFocusedColumn) {
            const QString token = raw.toString();
            const QString shown = RuleAuthoring::enumOptionLabel(action.type, PhosphorRules::ActionParam::Value, token);
            return isUnresolvedEnumToken(token, shown) ? PhosphorI18n::tr("Centering (invalid)")
                                                       : PhosphorI18n::tr("Centering: %1").arg(shown);
        }
        if (action.type == ActionType::SetScrollDefaultColumnDisplay) {
            const QString token = raw.toString();
            const QString shown = RuleAuthoring::enumOptionLabel(action.type, PhosphorRules::ActionParam::Value, token);
            return isUnresolvedEnumToken(token, shown) ? PhosphorI18n::tr("New columns (invalid)")
                                                       : PhosphorI18n::tr("New columns: %1").arg(shown);
        }
        if (action.type == ActionType::OpenColumnPlacement) {
            const QString token = raw.toString();
            const QString shown = RuleAuthoring::enumOptionLabel(action.type, PhosphorRules::ActionParam::Value, token);
            return isUnresolvedEnumToken(token, shown) ? PhosphorI18n::tr("Open (invalid)")
                                                       : PhosphorI18n::tr("Open: %1").arg(shown);
        }
        // ── tab-indicator overrides ──
        // The three bool actions are already returned by boolActionStateLabel,
        // like OpenTabbed. Every summary here says "tab" so a rule list mixing
        // indicator rules with column rules stays readable at a glance.
        if (action.type == ActionType::SetTabIndicatorStyle) {
            const QString token = raw.toString();
            const QString shown = RuleAuthoring::enumOptionLabel(action.type, PhosphorRules::ActionParam::Value, token);
            return isUnresolvedEnumToken(token, shown) ? PhosphorI18n::tr("Tab indicator style (invalid)")
                                                       : PhosphorI18n::tr("Tab indicator style: %1").arg(shown);
        }
        if (action.type == ActionType::SetTabIndicatorPosition) {
            const QString token = raw.toString();
            const QString shown = RuleAuthoring::enumOptionLabel(action.type, PhosphorRules::ActionParam::Value, token);
            return isUnresolvedEnumToken(token, shown) ? PhosphorI18n::tr("Tab indicator position (invalid)")
                                                       : PhosphorI18n::tr("Tab indicator position: %1").arg(shown);
        }
        if (action.type == ActionType::SetTabIndicatorGap) {
            return PhosphorI18n::tr("Tab indicator gap: %1 px").arg(raw.toInt());
        }
        if (action.type == ActionType::SetTabIndicatorWidth) {
            return PhosphorI18n::tr("Tab indicator thickness: %1 px").arg(raw.toInt());
        }
        if (action.type == ActionType::SetTabIndicatorLength) {
            const int pct = scrollFractionPercent(raw);
            return pct < 0 ? PhosphorI18n::tr("Tab indicator length (invalid)")
                           : PhosphorI18n::tr("Tab indicator length: %1%").arg(pct);
        }
        if (action.type == ActionType::SetTabIndicatorGapsBetweenTabs) {
            return PhosphorI18n::tr("Gap between tabs: %1 px").arg(raw.toInt());
        }
        if (action.type == ActionType::SetTabIndicatorCornerRadius) {
            const int px = raw.toInt();
            // The sentinel is spelled as the outcome, not as -1: a rule list
            // reading "Tab corner radius: -1 px" would look like a bad value.
            return px < 0 ? PhosphorI18n::tr("Tab corners: fully rounded")
                          : PhosphorI18n::tr("Tab corner radius: %1 px").arg(px);
        }
        if (action.type == ActionType::SetTabIndicatorActiveColor
            || action.type == ActionType::SetTabIndicatorInactiveColor
            || action.type == ActionType::SetTabIndicatorUrgentColor || action.type == ActionType::TabColorActive
            || action.type == ActionType::TabColorInactive || action.type == ActionType::TabColorUrgent) {
            // Hex shows upper-cased; anything else — INCLUDING the accent
            // sentinel, which these six validators deliberately reject
            // (hasHexColor, not hasHexColorOrAccent: only the border/tint
            // family has an accent resolver) — reads "(invalid)", mirroring
            // the SetOpacity / SetWindowLayer reject treatment so the summary
            // never claims a colour the runtime discards. The per-window trio
            // says "this window" so a mixed list cannot confuse a context
            // recolour with a per-app one.
            const QString value = raw.toString();
            const QString shown = isHexColorShape(value) ? value.toUpper() : PhosphorI18n::tr("(invalid)");
            if (action.type == ActionType::SetTabIndicatorActiveColor) {
                return PhosphorI18n::tr("Active tab: %1").arg(shown);
            }
            if (action.type == ActionType::SetTabIndicatorInactiveColor) {
                return PhosphorI18n::tr("Inactive tab: %1").arg(shown);
            }
            if (action.type == ActionType::SetTabIndicatorUrgentColor) {
                return PhosphorI18n::tr("Urgent tab: %1").arg(shown);
            }
            if (action.type == ActionType::TabColorActive) {
                return PhosphorI18n::tr("This window's active tab: %1").arg(shown);
            }
            if (action.type == ActionType::TabColorInactive) {
                return PhosphorI18n::tr("This window's inactive tab: %1").arg(shown);
            }
            return PhosphorI18n::tr("This window's urgent tab: %1").arg(shown);
        }
        // ── window-management overrides ──
        if (action.type == ActionType::SetWindowLayer) {
            const QString v = raw.toString();
            if (v.isEmpty()) {
                return PhosphorI18n::tr("Window layer");
            }
            // Mirror the resolver's closed vocabulary (shader_resolve.cpp's
            // resolveWindowLayer): an unknown token produces no runtime
            // override, so the label must not claim one — same contract as
            // the SetOpacity "(invalid)" guard above.
            namespace LayerToken = PhosphorRules::WindowLayerToken;
            if (v != LayerToken::Above && v != LayerToken::Normal && v != LayerToken::Below) {
                return PhosphorI18n::tr("Window layer (invalid)");
            }
            return PhosphorI18n::tr("Layer: %1")
                .arg(RuleAuthoring::enumOptionLabel(action.type, PhosphorRules::ActionParam::Value, v));
        }
        // ── overlay-appearance overrides (colours upper-cased hex; opacities
        //    are [0,1] on the wire, shown as a percent to match the editor).
        //    Same staged-payload reject treatment as the tab colours above
        //    and the SetOpacity / SetTintStrength guards: a non-hex colour or
        //    an out-of-shape opacity reads "(invalid)" instead of echoing a
        //    value the runtime discards. ──
        if (action.type == ActionType::SetOverlayHighlightColor) {
            const QString v = raw.toString();
            return PhosphorI18n::tr("Highlight color: %1")
                .arg(isHexColorShape(v) ? v.toUpper() : PhosphorI18n::tr("(invalid)"));
        }
        if (action.type == ActionType::SetOverlayInactiveColor) {
            const QString v = raw.toString();
            return PhosphorI18n::tr("Inactive zone color: %1")
                .arg(isHexColorShape(v) ? v.toUpper() : PhosphorI18n::tr("(invalid)"));
        }
        if (action.type == ActionType::SetOverlayBorderColor) {
            const QString v = raw.toString();
            return PhosphorI18n::tr("Overlay border color: %1")
                .arg(isHexColorShape(v) ? v.toUpper() : PhosphorI18n::tr("(invalid)"));
        }
        if (action.type == ActionType::SetOverlayActiveOpacity
            || action.type == ActionType::SetOverlayInactiveOpacity) {
            const bool active = action.type == ActionType::SetOverlayActiveOpacity;
            if (raw.isNull() || raw.isUndefined()) {
                return active ? PhosphorI18n::tr("Active opacity") : PhosphorI18n::tr("Inactive opacity");
            }
            // isDouble() mirrors the validator (hasNumberInRange requires a
            // JSON number), so a staged string like "0.5" reads "(invalid)"
            // instead of rendering a percent the runtime drops.
            const double v = raw.isDouble() ? raw.toDouble() : -1.0;
            if (v < 0.0 || v > 1.0) {
                return active ? PhosphorI18n::tr("Active opacity (invalid)")
                              : PhosphorI18n::tr("Inactive opacity (invalid)");
            }
            return active ? PhosphorI18n::tr("Active opacity: %1%").arg(qRound(v * 100.0))
                          : PhosphorI18n::tr("Inactive opacity: %1%").arg(qRound(v * 100.0));
        }
        if (action.type == ActionType::SetOverlayBorderWidth) {
            return PhosphorI18n::tr("Overlay border width: %1 px").arg(raw.toInt());
        }
        if (action.type == ActionType::SetOverlayBorderRadius) {
            return PhosphorI18n::tr("Overlay corner radius: %1 px").arg(raw.toInt());
        }
        // ── per-context gap overrides ──
        if (action.type == ActionType::SetInnerGap) {
            return PhosphorI18n::tr("Gap: %1 px").arg(raw.toInt());
        }
        if (action.type == ActionType::SetOuterGap) {
            return PhosphorI18n::tr("Outer gap: %1 px").arg(raw.toInt());
        }
        if (action.type == ActionType::SetOuterGapTop) {
            return PhosphorI18n::tr("Top gap: %1 px").arg(raw.toInt());
        }
        if (action.type == ActionType::SetOuterGapBottom) {
            return PhosphorI18n::tr("Bottom gap: %1 px").arg(raw.toInt());
        }
        if (action.type == ActionType::SetOuterGapLeft) {
            return PhosphorI18n::tr("Left gap: %1 px").arg(raw.toInt());
        }
        if (action.type == ActionType::SetOuterGapRight) {
            return PhosphorI18n::tr("Right gap: %1 px").arg(raw.toInt());
        }
    }
    return RuleModel::actionTypeFallbackLabel(action.type);
}

} // namespace

QString RuleModel::sectionLabel(Section section)
{
    switch (section) {
    case Section::Monitor:
        return PhosphorI18n::tr("Monitor & Layout");
    case Section::Application:
        return PhosphorI18n::tr("Applications");
    case Section::Activity:
        return PhosphorI18n::tr("Activities");
    case Section::Animation:
        return PhosphorI18n::tr("Animations");
    case Section::Advanced:
        return PhosphorI18n::tr("Advanced / Custom");
    case Section::System:
        return PhosphorI18n::tr("System");
    }
    return QString();
}

QString RuleModel::fieldLabel(Field field)
{
    switch (field) {
    case Field::AppId:
        return PhosphorI18n::tr("Application");
    case Field::WindowClass:
        return PhosphorI18n::tr("Window class");
    case Field::DesktopFile:
        return PhosphorI18n::tr("Desktop file");
    case Field::WindowRole:
        return PhosphorI18n::tr("Window role");
    case Field::Pid:
        return PhosphorI18n::tr("Process ID");
    case Field::Title:
        return PhosphorI18n::tr("Title");
    case Field::WindowType:
        return PhosphorI18n::tr("Window type");
    case Field::IsSticky:
        return PhosphorI18n::tr("Sticky");
    case Field::IsFullscreen:
        return PhosphorI18n::tr("Fullscreen");
    case Field::IsMaximized:
        return PhosphorI18n::tr("Maximized");
    case Field::IsMinimized:
        return PhosphorI18n::tr("Minimized");
    case Field::IsFocused:
        return PhosphorI18n::tr("Focused");
    case Field::ScreenId:
        return PhosphorI18n::tr("Monitor");
    case Field::VirtualDesktop:
        return PhosphorI18n::tr("Desktop");
    case Field::Activity:
        return PhosphorI18n::tr("Activity");
    case Field::IsTransient:
        return PhosphorI18n::tr("Transient");
    case Field::IsNotification:
        return PhosphorI18n::tr("Notification");
    case Field::Width:
        return PhosphorI18n::tr("Width");
    case Field::Height:
        return PhosphorI18n::tr("Height");
    case Field::KeepAbove:
        return PhosphorI18n::tr("Keep above");
    case Field::KeepBelow:
        return PhosphorI18n::tr("Keep below");
    case Field::SkipTaskbar:
        return PhosphorI18n::tr("Skip taskbar");
    case Field::SkipPager:
        return PhosphorI18n::tr("Skip pager");
    case Field::SkipSwitcher:
        return PhosphorI18n::tr("Skip switcher");
    case Field::IsModal:
        return PhosphorI18n::tr("Modal");
    case Field::HasDecoration:
        return PhosphorI18n::tr("Decorated");
    case Field::IsResizable:
        return PhosphorI18n::tr("Resizable");
    case Field::IsMovable:
        return PhosphorI18n::tr("Movable");
    case Field::IsMaximizable:
        return PhosphorI18n::tr("Maximizable");
    case Field::PositionX:
        return PhosphorI18n::tr("Position X");
    case Field::PositionY:
        return PhosphorI18n::tr("Position Y");
    case Field::CaptionNormal:
        return PhosphorI18n::tr("Title (no suffix)");
    case Field::IsFloating:
        return PhosphorI18n::tr("Floating");
    case Field::IsSnapped:
        return PhosphorI18n::tr("Snapped");
    case Field::IsTiled:
        return PhosphorI18n::tr("Tiled");
    case Field::Zone:
        return PhosphorI18n::tr("Zone");
    case Field::Mode:
        return PhosphorI18n::tr("Mode");
    case Field::TiledWindowCount:
        return PhosphorI18n::tr("Tiled window count");
    case Field::ScreenOrientation:
        return PhosphorI18n::tr("Screen orientation");
    case Field::ActiveLayout:
        return PhosphorI18n::tr("Active layout");
    }
    return QString();
}

QString RuleModel::matchSummary(const MatchExpression& match) const
{
    if (match.isCatchAll()) {
        return PhosphorI18n::tr("Any window");
    }
    if (match.isLeaf()) {
        return leafLabel(match.predicate(), m_screenLookup, m_activityLookup, m_zoneLookup, m_snappingLayoutLookup,
                         m_tilingAlgorithmLookup, m_virtualDesktopLookup);
    }
    // A simple AND renders its leaves joined by " · ".
    if (match.kind() == MatchExpression::Kind::All) {
        QStringList parts;
        for (const MatchExpression& child : match.children()) {
            if (child.isLeaf()) {
                parts.append(leafLabel(child.predicate(), m_screenLookup, m_activityLookup, m_zoneLookup,
                                       m_snappingLayoutLookup, m_tilingAlgorithmLookup, m_virtualDesktopLookup));
            } else {
                parts.append(PhosphorI18n::tr("(condition group)"));
            }
        }
        return parts.join(QStringLiteral(" · "));
    }
    // Any composite that is not a flat AND — count the leaves.
    const int n = conditionCount(match);
    return PhosphorI18n::tr("%n condition(s)", nullptr, n);
}

QString RuleModel::actionSummary(const QList<RuleAction>& actions) const
{
    if (actions.isEmpty()) {
        return PhosphorI18n::tr("No action");
    }
    QStringList parts;
    for (const RuleAction& a : actions) {
        parts.append(actionLabel(a, m_snappingLayoutLookup, m_tilingAlgorithmLookup, m_shaderEffectLookup,
                                 m_overlayShaderLookup, m_curveLookup, m_screenLookup, m_decorationPackLookup));
    }
    return parts.join(QStringLiteral(" · "));
}

QString RuleModel::actionTypeFallbackLabel(const QString& type)
{
    // No built-in label covers this type — it is an unknown / legacy /
    // future-schema action. Surface the raw type id rather than an empty
    // string so the user at least sees what the rule carries.
    return type;
}

} // namespace PlasmaZones
