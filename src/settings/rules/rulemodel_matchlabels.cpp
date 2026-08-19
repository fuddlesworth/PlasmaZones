// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The match-label half of RuleModel: the per-leaf human label and the match
// summary built from it. Split out of rulemodel_labels.cpp for file-size when
// the action-label side grew past the ceiling; the action labels and summary
// stay there. Everything here is either a RuleModel member or file-local, so
// there is no shared private header.

#include "rulemodel.h"

#include "ruleauthoring.h"

#include "phosphor_i18n.h"

#include <PhosphorLayoutApi/LayoutId.h>
#include <PhosphorRules/MatchTypes.h>

#include <QJsonDocument>
#include <QJsonValue>
#include <QStringList>

namespace PlasmaZones {

namespace {

using PhosphorRules::Field;
using PhosphorRules::MatchExpression;
using PhosphorRules::Operator;

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
        // the bare number and take the operator word from compose() instead,
        // which is what makes "Desktop greater than 3" legible without it.
        lookup = &virtualDesktopLookup;
    }
    const auto resolveOne = [lookup](const QString& raw) {
        if (!lookup || !*lookup) {
            return raw;
        }
        const QString label = (*lookup)(raw);
        return label.isEmpty() ? raw : label;
    };

    // Every return below composes through here so the OPERATOR survives into
    // the summary. Rendering a bare "field: value" made seven of the eight
    // operators invisible — "Title contains Firefox" and "Title is Firefox"
    // read identically, and "Width: 1000" could be a greater-than or a
    // less-than. Equals keeps the tight colon form (it is the common case, and
    // sparing it leaves most existing translations untouched). The word comes
    // from RuleAuthoring so this and the editor dropdown cannot drift.
    const auto compose = [&predicate](const QString& field, const QString& value) {
        if (predicate.op == Operator::Equals) {
            return PhosphorI18n::tr("%1: %2").arg(field, value);
        }
        return PhosphorI18n::tr("%1 %2 %3").arg(field, RuleAuthoring::operatorLabel(predicate.op), value);
    };

    // Mode is a closed wire-token vocabulary — render the friendly label
    // ("Mode: Snapping") via the same single-source table the editor dropdown uses,
    // rather than a second hardcoded copy. An unknown token round-trips verbatim.
    if (predicate.field == Field::Mode) {
        return compose(RuleModel::fieldLabel(predicate.field), RuleAuthoring::modeLabel(predicate.value.toString()));
    }

    // Screen orientation is a closed wire-token vocabulary too — resolve via the
    // shared orientationLabel() table like Mode above.
    if (predicate.field == Field::ScreenOrientation) {
        return compose(RuleModel::fieldLabel(predicate.field),
                       RuleAuthoring::orientationLabel(predicate.value.toString()));
    }

    // Colour scheme: same closed-vocabulary treatment via the shared table.
    if (predicate.field == Field::ColorScheme) {
        return compose(RuleModel::fieldLabel(predicate.field),
                       RuleAuthoring::colorSchemeLabel(predicate.value.toString()));
    }

    // Window type is the int underlying the WindowType enum — resolve it to the
    // friendly label ("Window type: Dialog") via the same single-source table the
    // editor dropdown uses, rather than showing the bare int.
    if (predicate.field == Field::WindowType) {
        return compose(RuleModel::fieldLabel(predicate.field), RuleAuthoring::windowTypeLabel(predicate.value.toInt()));
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
            // The bare mode sentinel has no layout entity to look up. The
            // wording matches the picker's sentinel entry
            // (activeLayoutMatchOptions) so the collapsed summary and the
            // editor agree.
            label = PhosphorI18n::tr("Scrolling (no template)");
        } else if (PhosphorLayout::LayoutId::isScrollingFamily(value)) {
            // The prefixed "scrolling:<uuid>" template stamp: resolve the
            // template's name through the shared layouts model, which carries
            // native template rows keyed by their raw UUID. Raw-id fallback
            // mirrors the deleted-layout behavior below.
            //
            // The bare resolved NAME, not templateDisplayLabel's "Template: …"
            // form. This label is already wrapped in the field label below, so
            // the composed variant read "Active layout: Template: Fibonacci".
            // The "Template: …" prefix stays in the PICKER (activeLayoutMatchOptions
            // and the value renderers), where entries sit unlabelled beside
            // manual layouts and need the family marker. A lookup MISS still
            // falls through to the verbatim "scrolling:<uuid>" in `label`.
            if (layoutLookup) {
                const QString templateId = PhosphorLayout::LayoutId::extractTemplateId(value);
                const QString resolved = layoutLookup(templateId);
                if (!resolved.isEmpty() && resolved != templateId) {
                    label = resolved;
                }
            }
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
        return compose(RuleModel::fieldLabel(predicate.field), label);
    }

    // Boolean fields (Maximized, Keep above, Skip taskbar, …) render their
    // value as On / Off instead of the raw JSON "true" / "false" the generic
    // toString() fallback would emit, matching the editor toggle and the
    // expanded match tree (MatchExpressionView).
    if (PhosphorRules::fieldIsBool(predicate.field)) {
        return compose(RuleModel::fieldLabel(predicate.field),
                       predicate.value.toBool() ? PhosphorI18n::tr("On") : PhosphorI18n::tr("Off"));
    }

    // A JSON array or object value converts to an EMPTY QString, which would
    // render the leaf as a bare "Title: " with nothing after it — the one place
    // a hand-edited rule's value vanishes instead of round-tripping verbatim
    // the way every other unresolvable value in this file does. No field's
    // editor authors a container, so this is reachable only from hand-edited
    // JSON; echo its compact JSON form so the user can see what the rule holds.
    QString shown = resolveOne(predicate.value.toString());
    if (shown.isEmpty()) {
        const QJsonValue json = QJsonValue::fromVariant(predicate.value);
        if (json.isArray()) {
            shown = QString::fromUtf8(QJsonDocument(json.toArray()).toJson(QJsonDocument::Compact));
        } else if (json.isObject()) {
            shown = QString::fromUtf8(QJsonDocument(json.toObject()).toJson(QJsonDocument::Compact));
        }
    }
    return compose(RuleModel::fieldLabel(predicate.field), shown);
}

} // namespace

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
    // Split at one for the reason spelled out at the SnapToZone summary in
    // rulemodel_labels.cpp:
    // a single "%n condition(s)" source would render literally for every
    // English user. Both arms keep %n so no numeral is hardcoded, and the n>1
    // arm still carries the real count for locales with more than two forms.
    if (n == 1) {
        return PhosphorI18n::tr("%n condition", nullptr, n);
    }
    return PhosphorI18n::tr("%n conditions", nullptr, n);
}

} // namespace PlasmaZones
