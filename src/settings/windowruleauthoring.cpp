// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windowruleauthoring.h"

#include "windowrulemodel.h"

#include "../pz_i18n.h"

#include <PhosphorWindowRule/MatchTypes.h>
#include <PhosphorWindowRule/RuleAction.h>

#include <QLatin1StringView>
#include <QList>

namespace PlasmaZones::WindowRuleAuthoring {

namespace {

namespace ActionType = PhosphorWindowRule::ActionType;
using PhosphorWindowRule::Field;
using PhosphorWindowRule::Operator;
using PhosphorWindowRule::RuleAction;

/// Build one parameter descriptor for the action editor. @p kind is one of
/// "string" / "number" / "enum" / "percent"; the optional trailing fields are
/// kind-specific (see actionTypes() doc).
QVariantMap paramDescriptor(QLatin1StringView key, const QString& kind, const QString& label)
{
    QVariantMap p;
    p[QStringLiteral("key")] = QString::fromLatin1(key);
    p[QStringLiteral("kind")] = kind;
    p[QStringLiteral("label")] = label;
    return p;
}

/// Enum-option list for the snapping/autotile engine-mode pickers. Shared by
/// `SetEngineMode` and `DisableEngine`: both pick from the same lowercase
/// wire tokens but show properly-cased UI labels.
QVariantList engineModeOptions()
{
    QVariantList options;
    QVariantMap snap;
    snap[QStringLiteral("value")] = QStringLiteral("snapping");
    snap[QStringLiteral("label")] = PzI18n::tr("Snapping");
    options.append(snap);
    QVariantMap tile;
    tile[QStringLiteral("value")] = QStringLiteral("autotile");
    tile[QStringLiteral("label")] = PzI18n::tr("Autotile");
    options.append(tile);
    return options;
}

/// The parameter schema for @p type — the editor `Loader` is driven entirely
/// off this, so the per-type `if (t === "...")` ladder lives in C++ only.
QVariantList paramsForActionTypeImpl(QLatin1StringView type)
{
    QVariantList params;
    if (type == ActionType::SetEngineMode) {
        QVariantMap p = paramDescriptor(QLatin1String("mode"), QStringLiteral("enum"), PzI18n::tr("Engine mode"));
        p[QStringLiteral("options")] = engineModeOptions();
        params.append(p);
    } else if (type == ActionType::SetSnappingLayout) {
        // `snappingLayout` is the picker-aware kind the QML editor recognises;
        // it swaps in a ComboBox over `settingsController.layouts` so the
        // user picks "Grid (2x2)" instead of pasting "{25828c9b-…}".
        params.append(paramDescriptor(QLatin1String("layoutId"), QStringLiteral("snappingLayout"),
                                      PzI18n::tr("Snapping layout")));
    } else if (type == ActionType::SetTilingAlgorithm) {
        // Tiling algorithms are still string tokens (`bsp`, `grid`, …) — the
        // editor offers a ComboBox over the catalogue but stores the token
        // verbatim.
        params.append(paramDescriptor(QLatin1String("algorithm"), QStringLiteral("tilingAlgorithm"),
                                      PzI18n::tr("Tiling algorithm")));
    } else if (type == ActionType::DisableEngine) {
        // Validator requires `mode` ∈ {snapping, autotile}; without a picker
        // the user couldn't author the action. Same enum shape as
        // SetEngineMode.
        QVariantMap p = paramDescriptor(QLatin1String("mode"), QStringLiteral("enum"), PzI18n::tr("Engine to disable"));
        p[QStringLiteral("options")] = engineModeOptions();
        params.append(p);
    } else if (type == ActionType::SetOpacity) {
        // The wire value is a 0.0–1.0 fraction; the editor shows a 0–100
        // percentage, so the stored value is `display * scale`.
        QVariantMap p =
            paramDescriptor(QLatin1String("value"), QStringLiteral("percent"), PzI18n::tr("Opacity percentage"));
        p[QStringLiteral("min")] = 0;
        p[QStringLiteral("max")] = 100;
        p[QStringLiteral("scale")] = 0.01;
        params.append(p);
    } else if (type == ActionType::OverrideAnimationShader) {
        // Wire keys must match PhosphorWindowRule::ActionRegistry's
        // OverrideAnimationShader descriptor (`event`, `effectId`, `params`).
        // `params` is a free-form shader-uniform object — not authorable
        // through a flat key/kind descriptor, so it is intentionally omitted;
        // a shader-uniform editor would graduate the rule to Advanced.
        //
        // `animationEvent` / `shaderEffect` are picker-aware kinds the QML
        // editor recognises — they swap ComboBoxes driven by
        // `AnimationsPageController::eventSections()` and
        // `availableShaderEffects()` in place of the freeform string field.
        params.append(paramDescriptor(QLatin1String("event"), QStringLiteral("animationEvent"), PzI18n::tr("Event")));
        params.append(
            paramDescriptor(QLatin1String("effectId"), QStringLiteral("shaderEffect"), PzI18n::tr("Shader effect")));
    } else if (type == ActionType::OverrideAnimationTiming) {
        // Duration-only override. Curve lives in `OverrideAnimationCurve`
        // (separate slot) so the user can override curve and duration
        // independently per event. The descriptor still allows `curve` for
        // back-compat with legacy rules; the editor doesn't expose it here.
        params.append(paramDescriptor(QLatin1String("event"), QStringLiteral("animationEvent"), PzI18n::tr("Event")));
        QVariantMap p =
            paramDescriptor(QLatin1String("durationMs"), QStringLiteral("number"), PzI18n::tr("Duration (ms)"));
        p[QStringLiteral("min")] = 0;
        p[QStringLiteral("max")] = 60000;
        params.append(p);
    } else if (type == ActionType::OverrideAnimationCurve) {
        // Curve-only override. `OverrideAnimationDuration` lives under
        // `OverrideAnimationTiming` (kept as the duration-only override).
        params.append(paramDescriptor(QLatin1String("event"), QStringLiteral("animationEvent"), PzI18n::tr("Event")));
        params.append(paramDescriptor(QLatin1String("curve"), QStringLiteral("curveEditor"), PzI18n::tr("Curve")));
    }
    // float / disableEngine / exclude carry no parameters — empty list.
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
    // SetOpacity is registered as an action type (and validated) but no
    // consumer ever reads its slot — KWin per-window opacity is achievable
    // via `EffectWindow::setOpacity`, but the plumbing was never wired up.
    // Exposing it here would let users create rules that silently never
    // fire. Reinstate when the effect-side handler lands.
    static const QList<QLatin1StringView> kTypes = {
        ActionType::SetEngineMode,
        ActionType::SetSnappingLayout,
        ActionType::SetTilingAlgorithm,
        ActionType::DisableEngine,
        ActionType::Exclude,
        ActionType::Float,
        ActionType::OverrideAnimationShader,
        ActionType::OverrideAnimationCurve,
        ActionType::OverrideAnimationTiming,
    };
    QVariantList out;
    for (QLatin1StringView type : kTypes) {
        QVariantMap entry;
        entry[QStringLiteral("value")] = QString::fromLatin1(type);
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
    return paramsForActionTypeImpl(QLatin1StringView(typeWire.toLatin1()));
}

QString actionTypeLabel(const QString& typeWire)
{
    return actionTypeLabelImpl(QLatin1StringView(typeWire.toLatin1()));
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

    const QVariantList params = paramsForActionTypeImpl(QLatin1StringView(typeWire.toLatin1()));
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
            // Number/percent share the same min-anchored default — `percent`
            // stores `display * scale`, so the min is in wire units already.
            const QVariant min = p.value(QStringLiteral("min"));
            payload[key] = min.isValid() ? min : QVariant(0);
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
