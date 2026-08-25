// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The action-label half of RuleModel: the per-action human label, the action
// summary built from it, and the unknown-action-type fallback. Split out of
// rulemodel.cpp for file-size; the model mechanics (rows, ordering, lookups)
// stay there, the static section / match-field tables moved on to
// rulemodel_fieldtables.cpp when this file in turn reached the ceiling, and the
// match side (the per-leaf label and matchSummary) moved to
// rulemodel_matchlabels.cpp when it reached it again. Everything here is
// either a RuleModel member or file-local, so there is no shared private
// header.

#include "rulemodel.h"

#include "ruleauthoring.h"

#include "phosphor_i18n.h"

#include <PhosphorRules/RuleAction.h>

#include <PhosphorZones/AssignmentEntry.h>

#include <QJsonArray>
#include <QJsonValue>
#include <QSet>
#include <QStringList>

#include <optional>

namespace PlasmaZones {

namespace {

namespace ActionType = PhosphorRules::ActionType;
using PhosphorRules::RuleAction;

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

/// A fraction param as a whole percent, or -1 when the payload is not a usable
/// fraction inside [@p floor, @p ceiling]. The editor can hold a staged action
/// whose validator has not run yet, so a bool, a string or an out-of-range
/// number is reachable here, mirroring the SetOpacity / SetTintStrength reject
/// paths so a summary never claims a size the runtime will not apply.
///
/// The bounds are PARAMETERS, not the single constant this used to hard-code:
/// the callers do not share a descriptor (four scrolling params, the tab
/// indicator's own ratio, and the drop-indicator opacity which really does
/// admit 0). The first two constants hold the same value today, so passing the
/// wrong one was invisible and would have stayed invisible until one moved.
int fractionPercent(const QJsonValue& raw, double floor, double ceiling)
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
    if (v < floor || v > ceiling) {
        return -1;
    }
    return qRound(v * 100.0);
}

/// Summary for a fraction-valued action: the bare @p label when the value is
/// ABSENT (the editor's normal pre-configuration state, the same contract the
/// SetOpacity / SetTintStrength / SetSplitRatio branches apply), @p invalid
/// when a value is present but not a usable fraction, else @p format with the
/// whole percent substituted. The closed-vocabulary enum family deliberately
/// differs: an absent token there renders "(invalid)", because its editor
/// always seeds a token and an absent one is never a pre-configuration state.
QString fractionSummary(const QJsonValue& raw, double floor, double ceiling, const QString& label,
                        const QString& invalid, const QString& format)
{
    if (raw.isNull() || raw.isUndefined()) {
        return label;
    }
    const int pct = fractionPercent(raw, floor, ceiling);
    return pct < 0 ? invalid : format.arg(pct);
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

/// The descriptor schema for @p type's @p key param, or nullopt when the type is
/// unregistered or declares no such param. The descriptor is the same source the
/// editor's SpinBox range comes from, so a summary guarded through it can never
/// claim a value the editor would refuse to author. Keyed rather than hard-wired
/// to `value`: OverrideAnimationTiming carries its number under `durationMs`,
/// and was the one unguarded integer renderer precisely because of that.
std::optional<PhosphorRules::ParamSchema> paramSchema(const QString& type, QLatin1StringView key)
{
    const auto descriptor = PhosphorRules::ActionRegistry::instance().descriptor(type);
    if (!descriptor.has_value()) {
        return std::nullopt;
    }
    for (const PhosphorRules::ParamSchema& schema : descriptor->params) {
        if (schema.key == key) {
            return schema;
        }
    }
    return std::nullopt;
}

/// True when @p wire (a payload already in WIRE units) falls inside the declared
/// bounds of @p type's @p key param. Descriptor bounds are declared in DISPLAY
/// units (`stored = display * scale`), so they scale up before the comparison.
/// A type with no descriptor, or a param with no declared bound on that side,
/// admits anything.
bool paramInRange(const QString& type, QLatin1StringView key, double wire)
{
    const auto schema = paramSchema(type, key);
    if (!schema.has_value()) {
        return true;
    }
    const double scale = schema->scale.value_or(1.0);
    if (schema->min.has_value() && wire < *schema->min * scale) {
        return false;
    }
    if (schema->max.has_value() && wire > *schema->max * scale) {
        return false;
    }
    return true;
}

/// The single-`value` shorthand, which is what most callers here want.
bool valueParamInRange(const QString& type, double wire)
{
    return paramInRange(type, PhosphorRules::ActionParam::Value, wire);
}

/// The integer payload of @p type's @p key param, or nullopt when the staged
/// payload is not a JSON number inside the descriptor's declared range. The
/// editor can hold an action whose validator has not run yet, so a bool, a
/// string, or an out-of-range number is reachable here; the fraction, colour and
/// enum families all reject those already, and this is the integer twin of that
/// discipline, so a pixel summary never claims a size the runtime discards.
std::optional<int> intParam(const QString& type, QLatin1StringView key, const QJsonValue& raw)
{
    if (!raw.isDouble()) {
        return std::nullopt;
    }
    const double v = raw.toDouble();
    if (!paramInRange(type, key, v)) {
        return std::nullopt;
    }
    return qRound(v);
}

/// The single-`value` shorthand, which is what most integer renderers want.
std::optional<int> intValueParam(const QString& type, const QJsonValue& raw)
{
    return intParam(type, PhosphorRules::ActionParam::Value, raw);
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
/// snappingLayoutLookup resolves SetSnappingLayout's AND
/// SetScrollingTemplate's layoutId UUIDs — it goes through the shared layouts
/// model, which carries native template rows keyed by their raw UUID
/// alongside the manual layouts; @p tilingAlgorithmLookup resolves
/// SetTilingAlgorithm's wire tokens ("bsp", …) — split so a stray
/// cross-resolve can't surface an algorithm name in a layout action's label or
/// vice versa.
QString actionLabel(const RuleAction& action, const RuleModel::LabelLookup& snappingLayoutLookup,
                    const RuleModel::LabelLookup& tilingAlgorithmLookup,
                    const RuleModel::LabelLookup& shaderEffectLookup, const RuleModel::LabelLookup& overlayShaderLookup,
                    const RuleModel::LabelLookup& curveLookup, const RuleModel::LabelLookup& screenLookup,
                    const RuleModel::LabelLookup& decorationPackLookup,
                    const RuleModel::LabelLookup& animationEventLookup)
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
        if (layoutId.isEmpty()) {
            return PhosphorI18n::tr("Snapping layout");
        }
        // The reserved "explicitly none" word — same treatment as the
        // template arm below, for the same reason: the lookup cannot resolve
        // it and the fallback would print a layout apparently named "none".
        if (layoutId == PhosphorZones::NoSnappingLayout) {
            return PhosphorI18n::tr("Snapping: None");
        }
        return PhosphorI18n::tr("Snapping: %1").arg(resolveWith(layoutId, snappingLayoutLookup));
    }
    if (action.type == ActionType::SetScrollingTemplate) {
        // Same raw-UUID value shape as SetSnappingLayout, and the shared
        // layouts model behind the lookup carries the native template rows
        // under that same id, so one lookup resolves both.
        const QString layoutId = action.params.value(PhosphorRules::ActionParam::LayoutId).toString();
        if (layoutId.isEmpty()) {
            return PhosphorI18n::tr("Scrolling template");
        }
        // The reserved "explicitly none" word is not an id the lookup can
        // resolve, and the fallback renders an unresolvable value verbatim —
        // which showed the user a template apparently named "none". Every
        // Monitors-page and picker None pick writes exactly this action, so
        // it is the common case rather than a malformed one. The payload
        // echoes the row the user chose (the pickers all label it "None").
        if (layoutId == PhosphorZones::NoScrollingTemplate) {
            return PhosphorI18n::tr("Scrolling template: None");
        }
        return PhosphorI18n::tr("Scrolling template: %1").arg(resolveWith(layoutId, snappingLayoutLookup));
    }
    if (action.type == ActionType::SetTilingAlgorithm) {
        const QString algo = action.params.value(PhosphorRules::ActionParam::Algorithm).toString();
        // Algorithms are wire tokens (`bsp`, `grid`, …). The dedicated
        // tilingAlgorithm lookup knows about autotile entries — the
        // RuleController wires it from settingsController.layouts,
        // which contains the displayName ("Binary Split") for each algorithm.
        // An empty token (hand-edited rule; the validator refuses it) keeps the
        // bare label rather than a dangling "Tiling: ", as SetSnappingLayout does.
        if (algo.isEmpty()) {
            return PhosphorI18n::tr("Tiling algorithm");
        }
        // The reserved word again — an unresolvable token would render
        // verbatim as an algorithm named "none".
        if (algo == PhosphorZones::NoTilingAlgorithm) {
            return PhosphorI18n::tr("Tiling: None");
        }
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
    // presence IS the effect, so each summary states the outcome. One shape
    // ("Excluded from <scope>") mirroring the picker labels, with the same
    // umbrella terms: "placement" is the tiling/snapping/scrolling engines,
    // "decorations" is borders plus decoration packs. The blanket form names
    // both scopes so it reads distinctly beside the scoped siblings in a
    // mixed list.
    if (action.type == ActionType::Exclude) {
        return PhosphorI18n::tr("Excluded from placement and decorations");
    }
    if (action.type == ActionType::ExcludePlacement) {
        return PhosphorI18n::tr("Excluded from placement");
    }
    if (action.type == ActionType::ExcludeAnimations) {
        return PhosphorI18n::tr("Excluded from animations");
    }
    if (action.type == ActionType::ExcludeDecorations) {
        return PhosphorI18n::tr("Excluded from decorations");
    }
    if (action.type == ActionType::Float) {
        return PhosphorI18n::tr("Float");
    }
    if (action.type == ActionType::SnapToZone) {
        const QJsonArray zones = action.params.value(PhosphorRules::ActionParam::Zones).toArray();
        const QJsonArray names = action.params.value(PhosphorRules::ActionParam::ZoneNames).toArray();
        // Ordinals first, then quoted names — the order the editor shows them.
        // Both loops apply the validator's RANGE bounds (1..MaxZoneOrdinal;
        // non-blank, at most MaxZoneNameLength after trimming), so the summary
        // never claims a target the runtime discards, and both dedupe so a
        // repeated target renders once the way the engine's zone-id union
        // collapses it. (The validator additionally refuses non-integral
        // ordinals and refuses the WHOLE action on one over-long name; a
        // summary is per entry, so such a rule, which cannot load, merely
        // renders its surviving entries while staged.)
        QStringList targets;
        targets.reserve(zones.size() + names.size());
        QSet<int> seenOrdinals;
        for (const QJsonValue& z : zones) {
            // Zone numbers are 1-based and toInt() answers 0 for a string, bool
            // or object, so an unfiltered render gave "Snap to zone 0" — a zone
            // that cannot exist.
            if (!z.isDouble()) {
                continue;
            }
            const int ordinal = z.toInt();
            if (ordinal < 1 || ordinal > PhosphorRules::MaxZoneOrdinal || seenOrdinals.contains(ordinal)) {
                continue;
            }
            seenOrdinals.insert(ordinal);
            targets.append(QString::number(ordinal));
        }
        // Zone names ride alongside the numbers: render each in quotes so a
        // name that happens to be digits cannot be read as an ordinal. The
        // dedupe key is the case-folded name, matching the engine's
        // case-insensitive zoneByName lookup.
        QSet<QString> seenNames;
        for (const QJsonValue& n : names) {
            const QString name = n.isString() ? n.toString().trimmed() : QString();
            if (name.isEmpty() || name.size() > PhosphorRules::MaxZoneNameLength) {
                continue;
            }
            const QString key = name.toCaseFolded();
            if (seenNames.contains(key)) {
                continue;
            }
            seenNames.insert(key);
            targets.append(PhosphorI18n::tr("“%1”", "a quoted zone name").arg(name));
        }
        // Empty arrays, or nothing survived the filters. Either way fall back to
        // the bare label rather than a dangling "Snap to zones ", the shape
        // OverrideDecorationChain uses for its all-empty-ids case.
        if (targets.isEmpty()) {
            return PhosphorI18n::tr("Snap to zone");
        }
        // Two source strings rather than one "zone(s)" spelling. Qt's plural
        // tr() takes a SINGLE source and falls back to it verbatim when there
        // is no translation, so a combined form would read "zone(s) 1" to
        // every English user. Splitting at one keeps English correct in both
        // numbers while the n>1 call still passes the real count, so a locale
        // with more than two plural forms can still select among them — the
        // n=21 case in Russian, which needs its singular form, resolves
        // through that call rather than through the size==1 branch.
        if (targets.size() == 1) {
            return PhosphorI18n::tr("Snap to zone %1").arg(targets.first());
        }
        return PhosphorI18n::tr("Snap to zones %1", nullptr, static_cast<int>(targets.size()))
            .arg(targets.join(QStringLiteral(", ")));
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
        // Both ends of the descriptor's bound, not just the floor: a
        // hand-edited ordinal the loader would have rejected must fall back to
        // the bare label rather than being printed as a real target.
        const int desktop = action.params.value(PhosphorRules::ActionParam::TargetDesktop).toInt();
        return (desktop >= 1 && desktop <= PhosphorRules::MaxVirtualDesktopOrdinal)
            ? PhosphorI18n::tr("Open on desktop %1").arg(desktop)
            : PhosphorI18n::tr("Open on desktop");
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
        return PhosphorI18n::tr("Opacity: %1%").arg(qRound(v * 100.0));
    }
    // The three per-event animation overrides key their slot on the event, so
    // one rule legitimately carries several of each; the event label leads so
    // two of them never summarise identically. An empty event (only a staged,
    // not-yet-saved editor row: the validators refuse it at load) falls back to
    // the event-less wording.
    const auto animationEvent = [&]() -> QString {
        const QString event = action.params.value(PhosphorRules::ActionParam::Event).toString();
        return event.isEmpty() ? QString() : resolveWith(event, animationEventLookup);
    };
    if (action.type == ActionType::OverrideAnimationShader) {
        const QString id = action.params.value(PhosphorRules::ActionParam::EffectId).toString();
        const QString event = animationEvent();
        if (id.isEmpty()) {
            return event.isEmpty() ? PhosphorI18n::tr("Block animation shader")
                                   : PhosphorI18n::tr("Block %1 shader").arg(event);
        }
        const QString shader = resolveWith(id, shaderEffectLookup);
        return event.isEmpty() ? PhosphorI18n::tr("Shader: %1").arg(shader)
                               : PhosphorI18n::tr("%1 shader: %2").arg(event, shader);
    }
    if (action.type == ActionType::OverrideDecorationChain) {
        const QJsonArray chain = action.params.value(PhosphorRules::ActionParam::Chain).toArray();
        if (chain.isEmpty()) {
            // The empty-chain sentinel clears the CUSTOM packs; the config-
            // backed border and opacity-tint layers still render (easy mode).
            // "Block decoration" was wrong — that outcome belongs to
            // ExcludeDecorations' "Excluded from decorations".
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
        // Descriptor-guarded like every sibling integer, replacing a bare
        // toInt() with a `> 0` test: the declared range is 0..60000 ms, so a
        // hand-edited 999999 printed a duration the runtime discards and a
        // legal 0 read as unset. Keyed on DurationMs, not `value`.
        const QJsonValue ms = action.params.value(PhosphorRules::ActionParam::DurationMs);
        const auto duration = intParam(action.type, PhosphorRules::ActionParam::DurationMs, ms);
        const QString event = animationEvent();
        if (!duration) {
            // Absent is the editor's normal pre-configuration state and keeps
            // the bare label; present but unusable says so.
            if (ms.isUndefined()) {
                return event.isEmpty() ? PhosphorI18n::tr("Animation duration")
                                       : PhosphorI18n::tr("%1 duration").arg(event);
            }
            return event.isEmpty() ? PhosphorI18n::tr("Animation duration (invalid)")
                                   : PhosphorI18n::tr("%1 duration (invalid)").arg(event);
        }
        return event.isEmpty() ? PhosphorI18n::tr("Duration: %1 ms").arg(*duration)
                               : PhosphorI18n::tr("%1 duration: %2 ms").arg(event, QString::number(*duration));
    }
    if (action.type == ActionType::OverrideAnimationCurve) {
        const QString curve = action.params.value(PhosphorRules::ActionParam::Curve).toString();
        const QString event = animationEvent();
        if (curve.isEmpty()) {
            return event.isEmpty() ? PhosphorI18n::tr("Animation curve") : PhosphorI18n::tr("%1 curve").arg(event);
        }
        const QString curveLabel = resolveWith(curve, curveLookup);
        return event.isEmpty() ? PhosphorI18n::tr("Curve: %1").arg(curveLabel)
                               : PhosphorI18n::tr("%1 curve: %2").arg(event, curveLabel);
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
        // SetInsertPosition / SetOverflowBehavior / SetDragBehavior cases below, and
        // report an unresolved token the way the scrolling enum family does.
        const QString shown = RuleAuthoring::enumOptionLabel(action.type, PhosphorRules::ActionParam::Value, v);
        return isUnresolvedEnumToken(v, shown) ? PhosphorI18n::tr("Overlay style (invalid)")
                                               : PhosphorI18n::tr("Overlay style: %1").arg(shown);
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
        //
        // Gated on the payload being a bool OR absent, the same reject-don't-
        // coerce posture every numeric branch below takes through its
        // *ValueParam helper: toBool() on a staged string or number invents
        // a polarity ("Hide border" for a payload nobody set that way), and
        // this family was the one branch with no such guard. An ABSENT value
        // (a freshly-added action) still renders the off-polarity phrase —
        // missing is the editor's normal pre-configuration state, not a
        // staged mistake.
        if (raw.isBool() || raw.isUndefined() || raw.isNull()) {
            if (const QString boolLabel = RuleAuthoring::boolActionStateLabel(action.type, raw.toBool());
                !boolLabel.isEmpty()) {
                return boolLabel;
            }
        } else if (!RuleAuthoring::boolActionStateLabel(action.type, true).isEmpty()) {
            // A bool action whose staged payload is present but not a bool.
            // Named rather than a bare "Invalid value", because every sibling
            // reject here says WHICH setting is wrong and an unqualified row is
            // the one the user cannot place in a mixed list.
            return PhosphorI18n::tr("%1 (invalid)").arg(RuleAuthoring::actionTypeLabel(action.type));
        }
        if (action.type == ActionType::SetBorderWidth) {
            const auto px = intValueParam(action.type, raw);
            return px ? PhosphorI18n::tr("Border width: %1 px").arg(*px) : PhosphorI18n::tr("Border width (invalid)");
        }
        if (action.type == ActionType::SetBorderRadius) {
            const auto px = intValueParam(action.type, raw);
            return px ? PhosphorI18n::tr("Corner radius: %1 px").arg(*px) : PhosphorI18n::tr("Corner radius (invalid)");
        }
        if (action.type == ActionType::SetBorderColorActive || action.type == ActionType::SetBorderColorInactive) {
            // Accent shows as a word, hex upper-cased, anything else
            // "(invalid)": this family validates with hasHexColorOrAccent, so
            // those are the only two payloads the runtime honours. Upper-casing
            // whatever was staged made it the one colour renderer that echoed a
            // value the runtime drops.
            const QString value = raw.toString();
            const QString shown = value == PhosphorRules::BorderColorToken::Accent ? PhosphorI18n::tr("Accent")
                : isHexColorShape(value)                                           ? value.toUpper()
                                                                                   : PhosphorI18n::tr("(invalid)");
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
            // Same accent-or-hex vocabulary as the two border colours above,
            // and the same hasHexColorOrAccent validator behind it.
            const QString value = raw.toString();
            const QString shown = value == PhosphorRules::BorderColorToken::Accent ? PhosphorI18n::tr("Accent")
                : isHexColorShape(value)                                           ? value.toUpper()
                                                                                   : PhosphorI18n::tr("(invalid)");
            return PhosphorI18n::tr("Tint color: %1").arg(shown);
        }
        // ── autotile parameter overrides ──
        if (action.type == ActionType::SetMaxWindows) {
            const auto n = intValueParam(action.type, raw);
            return n ? PhosphorI18n::tr("Max tiled windows: %1").arg(*n)
                     : PhosphorI18n::tr("Max tiled windows (invalid)");
        }
        if (action.type == ActionType::SetMasterCount) {
            const auto n = intValueParam(action.type, raw);
            return n ? PhosphorI18n::tr("Master count: %1").arg(*n) : PhosphorI18n::tr("Master count (invalid)");
        }
        if (action.type == ActionType::SetSplitRatio) {
            // Wire value is a ratio inside the descriptor's declared band, shown
            // as a percent to match the editor. Mirror the validator's reject
            // paths the way SetTintStrength and the scrolling fractions do, so a
            // staged payload the runtime discards never renders as a confident
            // percent: null/undefined (present but unset) → bare label, a
            // non-number or an out-of-band ratio → "(invalid)".
            if (raw.isNull() || raw.isUndefined()) {
                return PhosphorI18n::tr("Split ratio");
            }
            const double v = raw.toDouble();
            if (!raw.isDouble() || !valueParamInRange(action.type, v)) {
                return PhosphorI18n::tr("Split ratio (invalid)");
            }
            return PhosphorI18n::tr("Split ratio: %1%").arg(qRound(v * 100.0));
        }
        // The three tiling enums delegate token→label to the shared
        // enumOptionLabel and report an unresolved token (empty, or one the
        // vocabulary does not know) rather than echoing it, the same contract
        // the scrolling enum family below states.
        if (action.type == ActionType::SetInsertPosition) {
            const QString token = raw.toString();
            const QString shown = RuleAuthoring::enumOptionLabel(action.type, PhosphorRules::ActionParam::Value, token);
            return isUnresolvedEnumToken(token, shown) ? PhosphorI18n::tr("Insert (invalid)")
                                                       : PhosphorI18n::tr("Insert: %1").arg(shown);
        }
        if (action.type == ActionType::SetOverflowBehavior) {
            const QString token = raw.toString();
            const QString shown = RuleAuthoring::enumOptionLabel(action.type, PhosphorRules::ActionParam::Value, token);
            return isUnresolvedEnumToken(token, shown) ? PhosphorI18n::tr("Overflow (invalid)")
                                                       : PhosphorI18n::tr("Overflow: %1").arg(shown);
        }
        if (action.type == ActionType::SetDragBehavior) {
            const QString token = raw.toString();
            const QString shown = RuleAuthoring::enumOptionLabel(action.type, PhosphorRules::ActionParam::Value, token);
            return isUnresolvedEnumToken(token, shown) ? PhosphorI18n::tr("Drag (invalid)")
                                                       : PhosphorI18n::tr("Drag: %1").arg(shown);
        }
        // ── scrolling-engine overrides ──
        // Widths and heights are work-area fractions on the wire, shown as a
        // percent like SetSplitRatio and guarded through fractionPercent.
        // The enum actions delegate token→label to the shared enumOptionLabel,
        // like SetInsertPosition above, and report an unresolved token rather
        // than echoing it. OpenTabbed is a bool action and already returned by
        // boolActionStateLabel.
        if (action.type == ActionType::SetScrollDefaultColumnWidth) {
            return fractionSummary(raw, PhosphorRules::MinColumnWidthRatio, PhosphorRules::MaxColumnWidthRatio,
                                   PhosphorI18n::tr("Column width"), PhosphorI18n::tr("Column width (invalid)"),
                                   PhosphorI18n::tr("Column width: %1%"));
        }
        if (action.type == ActionType::OpenColumnWidth) {
            return fractionSummary(raw, PhosphorRules::MinColumnWidthRatio, PhosphorRules::MaxColumnWidthRatio,
                                   PhosphorI18n::tr("Open at width"), PhosphorI18n::tr("Open at width (invalid)"),
                                   PhosphorI18n::tr("Open at width: %1%"));
        }
        if (action.type == ActionType::SetScrollDefaultWindowHeight) {
            return fractionSummary(raw, PhosphorRules::MinColumnWidthRatio, PhosphorRules::MaxColumnWidthRatio,
                                   PhosphorI18n::tr("Window height"), PhosphorI18n::tr("Window height (invalid)"),
                                   PhosphorI18n::tr("Window height: %1%"));
        }
        if (action.type == ActionType::OpenWindowHeight) {
            return fractionSummary(raw, PhosphorRules::MinColumnWidthRatio, PhosphorRules::MaxColumnWidthRatio,
                                   PhosphorI18n::tr("Open at height"), PhosphorI18n::tr("Open at height (invalid)"),
                                   PhosphorI18n::tr("Open at height: %1%"));
        }
        if (action.type == ActionType::SetScrollFocusFollowsMouseMaxScroll) {
            return fractionSummary(
                raw, PhosphorRules::MinFocusFollowsMouseMaxScrollRatio,
                PhosphorRules::MaxFocusFollowsMouseMaxScrollRatio, PhosphorI18n::tr("Strip scroll limit"),
                PhosphorI18n::tr("Strip scroll limit (invalid)"), PhosphorI18n::tr("Strip scroll limit: %1%"));
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
        if (action.type == ActionType::SetScrollStickyWindowHandling) {
            const QString token = raw.toString();
            const QString shown = RuleAuthoring::enumOptionLabel(action.type, PhosphorRules::ActionParam::Value, token);
            return isUnresolvedEnumToken(token, shown) ? PhosphorI18n::tr("Sticky windows (invalid)")
                                                       : PhosphorI18n::tr("Sticky windows: %1").arg(shown);
        }
        if (action.type == ActionType::SetScrollStripAxis) {
            const QString token = raw.toString();
            const QString shown = RuleAuthoring::enumOptionLabel(action.type, PhosphorRules::ActionParam::Value, token);
            return isUnresolvedEnumToken(token, shown) ? PhosphorI18n::tr("Strip direction (invalid)")
                                                       : PhosphorI18n::tr("Strip direction: %1").arg(shown);
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
            const auto px = intValueParam(action.type, raw);
            return px ? PhosphorI18n::tr("Tab indicator gap: %1 px").arg(*px)
                      : PhosphorI18n::tr("Tab indicator gap (invalid)");
        }
        if (action.type == ActionType::SetTabIndicatorWidth) {
            const auto px = intValueParam(action.type, raw);
            return px ? PhosphorI18n::tr("Tab indicator thickness: %1 px").arg(*px)
                      : PhosphorI18n::tr("Tab indicator thickness (invalid)");
        }
        if (action.type == ActionType::SetTabIndicatorLength) {
            return fractionSummary(raw, PhosphorRules::MinTabIndicatorLengthRatio,
                                   PhosphorRules::MaxTabIndicatorLengthRatio, PhosphorI18n::tr("Tab indicator length"),
                                   PhosphorI18n::tr("Tab indicator length (invalid)"),
                                   PhosphorI18n::tr("Tab indicator length: %1%"));
        }
        if (action.type == ActionType::SetTabIndicatorGapsBetweenTabs) {
            const auto px = intValueParam(action.type, raw);
            return px ? PhosphorI18n::tr("Gap between tabs: %1 px").arg(*px)
                      : PhosphorI18n::tr("Gap between tabs (invalid)");
        }
        if (action.type == ActionType::SetTabIndicatorCornerRadius) {
            const auto px = intValueParam(action.type, raw);
            if (!px) {
                return PhosphorI18n::tr("Tab corner radius (invalid)");
            }
            // The sentinel is spelled as the outcome, not as -1: a rule list
            // reading "Tab corner radius: -1 px" would look like a bad value.
            return *px < 0 ? PhosphorI18n::tr("Tab corners: fully rounded")
                           : PhosphorI18n::tr("Tab corner radius: %1 px").arg(*px);
        }
        // Tab label font. "Tab label" rather than "Tab indicator" so a list
        // mixing these with the pill's own geometry rules stays readable, and
        // because the prefixes have to stay distinct from the ten entries
        // above. The three style flags come out of boolActionStateLabel.
        if (action.type == ActionType::SetTabIndicatorFontFamily) {
            // Three states, kept apart the way SetSplitRatio keeps them.
            // Absent or null is present-but-unset and gets the bare label, so
            // a staged payload never reads as a font choice. An EXPLICIT empty
            // string is a real value meaning the system font, and it is named
            // through the same descriptor vocabulary the rule editor's value
            // pill uses so the two summaries cannot drift. Anything else is
            // the reject case, matching the numeric branches above.
            if (raw.isUndefined() || raw.isNull()) {
                return PhosphorI18n::tr("Tab label font");
            }
            if (!raw.isString()) {
                return PhosphorI18n::tr("Tab label font (invalid)");
            }
            const QString family = raw.toString();
            return PhosphorI18n::tr("Tab label font: %1")
                .arg(family.isEmpty()
                         ? RuleAuthoring::paramEmptyValueLabel(action.type, QString(PhosphorRules::ActionParam::Value))
                         : family);
        }
        if (action.type == ActionType::SetTabIndicatorFontWeight) {
            const auto weight = intValueParam(action.type, raw);
            return weight ? PhosphorI18n::tr("Tab label weight: %1").arg(*weight)
                          : PhosphorI18n::tr("Tab label weight (invalid)");
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
        // ── drop indicator ──
        // Same treatment as the tab family: numerics carry their unit, colours
        // upper-case a valid hex and read "(invalid)" otherwise so the summary
        // never claims a colour the runtime discards.
        if (action.type == ActionType::SetDropIndicatorOpacity) {
            return fractionSummary(raw, PhosphorRules::MinDropIndicatorOpacity, PhosphorRules::MaxDropIndicatorOpacity,
                                   PhosphorI18n::tr("Drop indicator fill opacity"),
                                   PhosphorI18n::tr("Drop indicator fill opacity (invalid)"),
                                   PhosphorI18n::tr("Drop indicator fill opacity: %1%"));
        }
        if (action.type == ActionType::SetDropIndicatorBorderWidth) {
            const auto px = intValueParam(action.type, raw);
            return px ? PhosphorI18n::tr("Drop indicator border width: %1 px").arg(*px)
                      : PhosphorI18n::tr("Drop indicator border width (invalid)");
        }
        if (action.type == ActionType::SetDropIndicatorBorderRadius) {
            // No sentinel here, unlike the tab corner radius: 0 is square.
            const auto px = intValueParam(action.type, raw);
            return px ? PhosphorI18n::tr("Drop indicator corner radius: %1 px").arg(*px)
                      : PhosphorI18n::tr("Drop indicator corner radius (invalid)");
        }
        if (action.type == ActionType::SetDropIndicatorColor || action.type == ActionType::SetDropIndicatorBorderColor
            || action.type == ActionType::DropIndicatorColor || action.type == ActionType::DropIndicatorBorderColor) {
            const QString value = raw.toString();
            const QString shown = isHexColorShape(value) ? value.toUpper() : PhosphorI18n::tr("(invalid)");
            if (action.type == ActionType::SetDropIndicatorColor) {
                return PhosphorI18n::tr("Drop indicator fill: %1").arg(shown);
            }
            if (action.type == ActionType::SetDropIndicatorBorderColor) {
                return PhosphorI18n::tr("Drop indicator border: %1").arg(shown);
            }
            // The per-window pair says "when dragging this window", matching
            // its authoring label: these paint a slot elsewhere on screen
            // because this window is the one in hand, rather than painting on
            // the window itself the way the tab colours do.
            if (action.type == ActionType::DropIndicatorColor) {
                return PhosphorI18n::tr("Drop indicator fill when dragging this window: %1").arg(shown);
            }
            return PhosphorI18n::tr("Drop indicator border when dragging this window: %1").arg(shown);
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
        if (action.type == ActionType::ScrollFactor) {
            // Mirror the resolver's reject-not-clamp bounds
            // (shader_resolve.cpp's resolveScrollFactor): an out-of-range or
            // non-numeric payload produces no runtime scaling, so the label
            // must not claim one.
            const double factor = raw.toDouble();
            if (!raw.isDouble() || factor < PhosphorRules::MinScrollFactor || factor > PhosphorRules::MaxScrollFactor) {
                return PhosphorI18n::tr("Scroll speed (invalid)");
            }
            // Rendered as a percent, not the raw multiplier: the editor is a
            // percent spin box (the descriptor is `percent` with scale 0.01),
            // and every other fraction family in this file already shows a
            // percent. An "0.75x" summary beside a "75" editor would be the
            // only place the two disagree on units. Whole percents match what
            // the editor can author, the same rounding fractionPercent
            // applies.
            return PhosphorI18n::tr("Scroll speed: %1%").arg(qRound(factor * 100.0));
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
            const auto px = intValueParam(action.type, raw);
            return px ? PhosphorI18n::tr("Overlay border width: %1 px").arg(*px)
                      : PhosphorI18n::tr("Overlay border width (invalid)");
        }
        if (action.type == ActionType::SetOverlayBorderRadius) {
            const auto px = intValueParam(action.type, raw);
            return px ? PhosphorI18n::tr("Overlay corner radius: %1 px").arg(*px)
                      : PhosphorI18n::tr("Overlay corner radius (invalid)");
        }
        // ── per-context gap overrides ──
        if (action.type == ActionType::SetInnerGap) {
            const auto px = intValueParam(action.type, raw);
            return px ? PhosphorI18n::tr("Gap: %1 px").arg(*px) : PhosphorI18n::tr("Gap (invalid)");
        }
        if (action.type == ActionType::SetOuterGap) {
            const auto px = intValueParam(action.type, raw);
            return px ? PhosphorI18n::tr("Outer gap: %1 px").arg(*px) : PhosphorI18n::tr("Outer gap (invalid)");
        }
        if (action.type == ActionType::SetOuterGapTop) {
            const auto px = intValueParam(action.type, raw);
            return px ? PhosphorI18n::tr("Top gap: %1 px").arg(*px) : PhosphorI18n::tr("Top gap (invalid)");
        }
        if (action.type == ActionType::SetOuterGapBottom) {
            const auto px = intValueParam(action.type, raw);
            return px ? PhosphorI18n::tr("Bottom gap: %1 px").arg(*px) : PhosphorI18n::tr("Bottom gap (invalid)");
        }
        if (action.type == ActionType::SetOuterGapLeft) {
            const auto px = intValueParam(action.type, raw);
            return px ? PhosphorI18n::tr("Left gap: %1 px").arg(*px) : PhosphorI18n::tr("Left gap (invalid)");
        }
        if (action.type == ActionType::SetOuterGapRight) {
            const auto px = intValueParam(action.type, raw);
            return px ? PhosphorI18n::tr("Right gap: %1 px").arg(*px) : PhosphorI18n::tr("Right gap (invalid)");
        }
    }
    return RuleModel::actionTypeFallbackLabel(action.type);
}

} // namespace

QString RuleModel::actionSummary(const QList<RuleAction>& actions) const
{
    if (actions.isEmpty()) {
        return PhosphorI18n::tr("No action");
    }
    QStringList parts;
    for (const RuleAction& a : actions) {
        parts.append(actionLabel(a, m_snappingLayoutLookup, m_tilingAlgorithmLookup, m_shaderEffectLookup,
                                 m_overlayShaderLookup, m_curveLookup, m_screenLookup, m_decorationPackLookup,
                                 m_animationEventLookup));
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
