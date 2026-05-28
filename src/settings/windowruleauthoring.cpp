// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windowruleauthoring.h"

#include "windowrulemodel.h"

#include "../pz_i18n.h"

#include <PhosphorWindowRule/MatchTypes.h>
#include <PhosphorWindowRule/RuleAction.h>

#include <QLatin1StringView>
#include <QList>
#include <QSet>

#include <algorithm>

namespace PlasmaZones::WindowRuleAuthoring {

namespace {

namespace ActionType = PhosphorWindowRule::ActionType;
using PhosphorWindowRule::Field;
using PhosphorWindowRule::Operator;
using PhosphorWindowRule::RuleAction;

/// Translated label for one param key on action @p type. The structural
/// schema (kind, min/max, scale, enum wire values) lives on the LGPL
/// `ActionDescriptor` in PhosphorWindowRule; the GPL settings layer adds
/// the user-visible label per `(type, key)` pair so translation runs
/// through `PzI18n::tr` and `lupdate` extracts the strings. A missing
/// entry falls back to the wire key — visible in the picker, so a missing
/// entry stands out for the next translator pass.
QString paramLabel(QLatin1StringView type, const QString& key)
{
    namespace ActionParam = PhosphorWindowRule::ActionParam;
    if (type == ActionType::SetEngineMode && key == ActionParam::Mode) {
        return PzI18n::tr("Engine mode");
    }
    if (type == ActionType::SetSnappingLayout && key == ActionParam::LayoutId) {
        return PzI18n::tr("Snapping layout");
    }
    if (type == ActionType::SetTilingAlgorithm && key == ActionParam::Algorithm) {
        return PzI18n::tr("Tiling algorithm");
    }
    if (type == ActionType::DisableEngine && key == ActionParam::Mode) {
        return PzI18n::tr("Engine to disable");
    }
    if (type == ActionType::SetOpacity && key == ActionParam::Value) {
        return PzI18n::tr("Opacity percentage");
    }
    if (key == ActionParam::Event) {
        return PzI18n::tr("Event");
    }
    if (key == ActionParam::EffectId) {
        return PzI18n::tr("Shader effect");
    }
    if (key == ActionParam::DurationMs) {
        return PzI18n::tr("Duration (ms)");
    }
    if (key == ActionParam::Curve) {
        return PzI18n::tr("Curve");
    }
    return key;
}

/// Translated label for one enum wire value on action @p type, param @p key.
/// Mirrors paramLabel — structural enum membership lives on the descriptor;
/// the human-facing label is per `(type, key, wireValue)`.
QString enumOptionLabel(QLatin1StringView type, const QString& key, const QString& wireValue)
{
    namespace ActionParam = PhosphorWindowRule::ActionParam;
    if ((type == ActionType::SetEngineMode || type == ActionType::DisableEngine) && key == ActionParam::Mode) {
        if (wireValue == QLatin1String("snapping")) {
            return PzI18n::tr("Snapping");
        }
        if (wireValue == QLatin1String("autotile")) {
            return PzI18n::tr("Autotile");
        }
        if (wireValue == QLatin1String("scrolling")) {
            return PzI18n::tr("Scrolling");
        }
    }
    return wireValue;
}

/// The parameter schema for @p type, derived from the LGPL ActionDescriptor's
/// structural `params` and supplemented by GPL-side translated labels. The
/// QML editor's per-param Loader dispatches on `kind`, so the wire shape
/// here is the contract between the descriptor and the editor.
QVariantList paramsForActionTypeImpl(QLatin1StringView type)
{
    QVariantList params;
    const auto descriptor = PhosphorWindowRule::ActionRegistry::instance().descriptor(QString::fromLatin1(type));
    if (!descriptor.has_value()) {
        return params;
    }
    for (const PhosphorWindowRule::ParamSchema& schema : descriptor->params) {
        // A `ParamSchema` with an empty `key` is a misregistered descriptor
        // — the strict-key check in `RuleAction::fromJson` would reject any
        // payload built against it, leaving the editor with a permanently
        // un-savable row. Skip silently: rendering the row would just
        // expose a no-op input to the user with no recovery path.
        if (schema.key.isEmpty()) {
            continue;
        }
        QVariantMap p;
        p[QStringLiteral("key")] = schema.key;
        p[QStringLiteral("kind")] = schema.kind;
        p[QStringLiteral("label")] = paramLabel(type, schema.key);
        if (schema.min.has_value()) {
            p[QStringLiteral("min")] = *schema.min;
        }
        if (schema.max.has_value()) {
            p[QStringLiteral("max")] = *schema.max;
        }
        if (schema.scale.has_value()) {
            p[QStringLiteral("scale")] = *schema.scale;
        }
        if (schema.defaultDisplay.has_value()) {
            p[QStringLiteral("defaultDisplay")] = *schema.defaultDisplay;
        }
        if (!schema.enumWireValues.isEmpty()) {
            QVariantList options;
            options.reserve(schema.enumWireValues.size());
            for (const QString& wire : schema.enumWireValues) {
                QVariantMap option;
                option[QStringLiteral("value")] = wire;
                option[QStringLiteral("label")] = enumOptionLabel(type, schema.key, wire);
                options.append(option);
            }
            p[QStringLiteral("options")] = options;
        }
        params.append(p);
    }
    return params;
}

QString actionTypeLabelImpl(QLatin1StringView type)
{
    if (type == ActionType::SetEngineMode) {
        return PzI18n::tr("Set engine mode");
    }
    if (type == ActionType::SetSnappingLayout) {
        return PzI18n::tr("Set snapping layout");
    }
    if (type == ActionType::SetTilingAlgorithm) {
        return PzI18n::tr("Set tiling algorithm");
    }
    if (type == ActionType::DisableEngine) {
        return PzI18n::tr("Disable engine");
    }
    if (type == ActionType::Exclude) {
        return PzI18n::tr("Exclude window");
    }
    if (type == ActionType::Float) {
        return PzI18n::tr("Float window");
    }
    if (type == ActionType::OverrideAnimationShader) {
        return PzI18n::tr("Override animation shader");
    }
    if (type == ActionType::OverrideAnimationTiming) {
        return PzI18n::tr("Override animation duration");
    }
    if (type == ActionType::OverrideAnimationCurve) {
        return PzI18n::tr("Override animation curve");
    }
    if (type == ActionType::SetOpacity) {
        return PzI18n::tr("Set opacity");
    }
    return WindowRuleModel::actionTypeFallbackLabel(QString::fromLatin1(type));
}

QString operatorLabelImpl(Operator op)
{
    switch (op) {
    case Operator::Equals:
        return PzI18n::tr("is");
    case Operator::Contains:
        return PzI18n::tr("contains");
    case Operator::StartsWith:
        return PzI18n::tr("starts with");
    case Operator::EndsWith:
        return PzI18n::tr("ends with");
    case Operator::Regex:
        return PzI18n::tr("matches regex");
    case Operator::AppIdMatches:
        return PzI18n::tr("matches app-id");
    case Operator::In:
        return PzI18n::tr("is one of");
    case Operator::GreaterThan:
        return PzI18n::tr("greater than");
    case Operator::LessThan:
        return PzI18n::tr("less than");
    }
    return QString();
}

} // namespace

QVariantList matchFields()
{
    // Pid and WindowRole are intentionally omitted from the picker —
    // both are footguns in a persistent rule store:
    //   * Pid is ephemeral. A `Pid equals 12345` predicate matches one
    //     specific process instance and is dead the moment that process
    //     restarts. Surfacing it in the picker invites users to author
    //     rules that silently stop working.
    //   * WindowRole is the X11 WM_WINDOW_ROLE property, empty for every
    //     Wayland-native window — PlasmaZones is Wayland-only (per
    //     CLAUDE.md), so the picker would always read as blank.
    // The Field enum keeps both values for back-compat with already-saved
    // rules; only the authoring UI hides them.
    static const QList<Field> kFields = {
        Field::AppId,       Field::WindowClass, Field::DesktopFile,    Field::Title,
        Field::WindowType,  Field::IsSticky,    Field::IsFullscreen,   Field::IsMinimized,
        Field::IsMaximized, Field::ScreenId,    Field::VirtualDesktop, Field::Activity,
    };
    QVariantList out;
    for (Field f : kFields) {
        QVariantMap entry;
        entry[QStringLiteral("value")] = static_cast<int>(f);
        // The JSON wire string for this field — QML keys off this rather than
        // reconstructing the enum↔string table itself.
        entry[QStringLiteral("wire")] = PhosphorWindowRule::fieldToString(f);
        entry[QStringLiteral("label")] = WindowRuleModel::fieldLabel(f);
        QString kind = QStringLiteral("string");
        if (PhosphorWindowRule::fieldIsNumeric(f) || f == Field::WindowType) {
            kind = QStringLiteral("number");
        } else if (PhosphorWindowRule::fieldIsBool(f)) {
            kind = QStringLiteral("bool");
        } else if (f == Field::ScreenId) {
            // QML editor swaps this for a screen-picker ComboBox driven by
            // `settingsController.screens`, so the user sees "LG Ultra HD" not
            // "LG Electronics:LG Ultra HD:115107/vs:0".
            kind = QStringLiteral("screen");
        } else if (f == Field::Activity) {
            // QML editor swaps this for an activity-picker ComboBox driven by
            // `settingsController.activities`, so the user sees the activity
            // name not its UUID.
            kind = QStringLiteral("activity");
        }
        entry[QStringLiteral("valueKind")] = kind;
        out.append(entry);
    }
    return out;
}

QVariantList operatorsForField(int fieldValue)
{
    const Field field = static_cast<Field>(fieldValue);
    QList<Operator> ops;
    if (PhosphorWindowRule::fieldIsString(field)) {
        ops = {Operator::Equals, Operator::Contains, Operator::StartsWith, Operator::EndsWith, Operator::Regex};
        if (field == Field::AppId) {
            ops.append(Operator::AppIdMatches);
        }
        if (field == Field::ScreenId || field == Field::Activity) {
            ops.append(Operator::In);
        }
    } else if (PhosphorWindowRule::fieldIsNumeric(field)) {
        ops = {Operator::Equals, Operator::GreaterThan, Operator::LessThan};
        if (field == Field::VirtualDesktop) {
            ops.append(Operator::In);
        }
    } else if (PhosphorWindowRule::fieldIsBool(field) || field == Field::WindowType) {
        ops = {Operator::Equals};
        if (field == Field::WindowType) {
            ops.append(Operator::In);
        }
    }
    QVariantList out;
    for (Operator op : ops) {
        QVariantMap entry;
        entry[QStringLiteral("value")] = static_cast<int>(op);
        // The JSON wire string for this operator — same contract as matchFields.
        entry[QStringLiteral("wire")] = PhosphorWindowRule::operatorToString(op);
        entry[QStringLiteral("label")] = operatorLabelImpl(op);
        out.append(entry);
    }
    return out;
}

QVariantList actionTypes()
{
    // The picker order is meaningful (engine-mode first, then layout-shaping,
    // then per-window overrides), but the registry returns types in QHash
    // iteration order. Anchoring the order here keeps the picker stable
    // without bringing back the hand-maintained type list — registered
    // types not in this order list are appended after, alphabetically by
    // wire string, so a future descriptor automatically shows up in the
    // picker the moment it sets `userAuthorable = true`.
    static const QList<QLatin1StringView> kPreferredOrder = {
        ActionType::SetEngineMode,
        ActionType::SetSnappingLayout,
        ActionType::SetTilingAlgorithm,
        ActionType::DisableEngine,
        ActionType::Exclude,
        ActionType::Float,
        ActionType::SetOpacity,
        ActionType::OverrideAnimationShader,
        ActionType::OverrideAnimationCurve,
        ActionType::OverrideAnimationTiming,
    };
    const PhosphorWindowRule::ActionRegistry& registry = PhosphorWindowRule::ActionRegistry::instance();
    QList<QString> orderedTypes;
    QSet<QString> seen;
    for (QLatin1StringView t : kPreferredOrder) {
        const QString type = QString::fromLatin1(t);
        const auto desc = registry.descriptor(type);
        if (desc.has_value() && desc->userAuthorable) {
            orderedTypes.append(type);
            seen.insert(type);
        }
    }
    QStringList trailing;
    for (const QString& type : registry.registeredTypes()) {
        if (seen.contains(type)) {
            continue;
        }
        const auto desc = registry.descriptor(type);
        if (desc.has_value() && desc->userAuthorable) {
            trailing.append(type);
        }
    }
    std::sort(trailing.begin(), trailing.end());
    orderedTypes.append(trailing);

    QVariantList out;
    for (const QString& typeStr : orderedTypes) {
        // Materialise a stable QByteArray for the QLatin1StringView's
        // backing storage — a temporary `toLatin1()` would dangle past
        // the semicolon and `actionTypeLabelImpl` / `paramsForActionTypeImpl`
        // would read invalid memory.
        const QByteArray typeBytes = typeStr.toLatin1();
        const QLatin1StringView type{typeBytes.constData(), static_cast<qsizetype>(typeBytes.size())};
        QVariantMap entry;
        entry[QStringLiteral("value")] = typeStr;
        entry[QStringLiteral("label")] = actionTypeLabelImpl(type);
        entry[QStringLiteral("params")] = paramsForActionTypeImpl(type);
        // Domain wire string drives the picker's compatibility flag — the
        // QML side disables a context-domain action type when the current
        // match references window-property fields (the silently-never-fires
        // combination). Looked up via a probe RuleAction so the descriptor's
        // own `domain` field stays the single source of truth.
        RuleAction probe;
        probe.type = QString::fromLatin1(type);
        const auto domain = PhosphorWindowRule::ActionRegistry::instance().domainFor(probe);
        entry[QStringLiteral("domain")] =
            domain == PhosphorWindowRule::ActionDomain::Context ? QStringLiteral("context") : QStringLiteral("window");
        out.append(entry);
    }
    return out;
}

QVariantList paramsForActionType(const QString& typeWire)
{
    // Materialise a stable QByteArray for the QLatin1StringView backing —
    // see the matching helper in `actionTypes()`. A temporary `toLatin1()`
    // would dangle past the end of the full expression.
    const QByteArray bytes = typeWire.toLatin1();
    return paramsForActionTypeImpl(QLatin1StringView{bytes.constData(), static_cast<qsizetype>(bytes.size())});
}

QString actionTypeLabel(const QString& typeWire)
{
    const QByteArray bytes = typeWire.toLatin1();
    return actionTypeLabelImpl(QLatin1StringView{bytes.constData(), static_cast<qsizetype>(bytes.size())});
}

QString operatorLabel(int operatorValue)
{
    return operatorLabelImpl(static_cast<Operator>(operatorValue));
}

QVariantMap defaultPayloadFor(const QString& typeWire)
{
    // Walk the action's parameter descriptor and seed each entry with a
    // kind-appropriate default. The QML side previously open-coded this in a
    // `_defaultParamValue` helper inside ActionListEditor — moving it here
    // makes the descriptor + its defaults the single source of truth, so a
    // newly-appended action and a type-switched action both land on the same
    // shape (which prevents the type-switch leaving the SpinBox at 0 with
    // canSave gating the rest of the editor).
    QVariantMap payload;
    payload[QStringLiteral("type")] = typeWire;

    const QByteArray typeBytes = typeWire.toLatin1();
    const QVariantList params =
        paramsForActionTypeImpl(QLatin1StringView{typeBytes.constData(), static_cast<qsizetype>(typeBytes.size())});
    for (const QVariant& v : params) {
        const QVariantMap p = v.toMap();
        const QString key = p.value(QStringLiteral("key")).toString();
        if (key.isEmpty()) {
            continue;
        }
        const QString kind = p.value(QStringLiteral("kind")).toString();
        if (kind == QLatin1String("enum")) {
            // Enum options carry `{value, label}` pairs — wire form is `value`.
            // Tolerate the legacy bare-string shape so a future descriptor that
            // skips the wrap doesn't silently default to "".
            const QVariantList options = p.value(QStringLiteral("options")).toList();
            if (options.isEmpty()) {
                payload[key] = QString();
                continue;
            }
            const QVariant& first = options.first();
            if (first.canConvert<QVariantMap>()) {
                payload[key] = first.toMap().value(QStringLiteral("value")).toString();
            } else {
                payload[key] = first.toString();
            }
        } else if (kind == QLatin1String("number") || kind == QLatin1String("percent")) {
            // Seed order: `defaultDisplay` (if the descriptor declared a
            // safe-but-not-`min` starting value) → `min` → 0.0. `min` and
            // `defaultDisplay` are both expressed in *display* units (see
            // `ParamSchema` doc in RuleAction.h). For `percent` the wire
            // value is `display * scale` — so the seed must run through
            // that same conversion or the rule lands with a wire value
            // far outside the validator's range. `defaultDisplay` lets a
            // descriptor like SetOpacity start at 100% (no visible change)
            // instead of seeding `min = 0%` (a saveable-but-invisible rule).
            const QVariant defaultDisplay = p.value(QStringLiteral("defaultDisplay"));
            const QVariant min = p.value(QStringLiteral("min"));
            const QVariant displaySource = defaultDisplay.isValid() ? defaultDisplay : min;
            if (!displaySource.isValid()) {
                payload[key] = QVariant(0.0);
            } else if (kind == QLatin1String("percent")) {
                // Percent kind requires `scale` per the ParamSchema doc
                // (`stored = display * scale`). A descriptor declaring
                // `percent` without `scale` is a programmer error in the
                // registry — fall back to `0.0` (scale-invariant safe
                // default) rather than seeding the un-scaled display
                // value, which would reintroduce the exact bug the
                // scale multiplication was added to fix.
                const QVariant scale = p.value(QStringLiteral("scale"));
                payload[key] = scale.isValid() ? QVariant(displaySource.toDouble() * scale.toDouble()) : QVariant(0.0);
            } else {
                payload[key] = displaySource;
            }
        } else {
            // Picker kinds (snappingLayout, tilingAlgorithm, animationEvent,
            // shaderEffect, curveEditor) and plain strings all start empty —
            // the user has to choose a value before the rule is savable, and
            // `canSave` surfaces the gap explicitly. Seeding a placeholder
            // here would mask the "user has to pick" state.
            payload[key] = QString();
        }
    }
    return payload;
}

} // namespace PlasmaZones::WindowRuleAuthoring
