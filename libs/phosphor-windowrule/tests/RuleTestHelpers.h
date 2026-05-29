// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// Shared concise constructors for the phosphor-windowrule test suite. Header
// only — every test that needs them includes this directly.

#include <PhosphorWindowRule/PhosphorWindowRule.h>

#include <QJsonObject>
#include <QUuid>

namespace PhosphorWindowRule::TestHelpers {

inline RuleAction engineMode(const QString& mode)
{
    RuleAction a;
    a.type = QString(ActionType::SetEngineMode);
    a.params.insert(ActionParam::Mode, mode);
    return a;
}

inline RuleAction snappingLayout(const QString& layoutId)
{
    RuleAction a;
    a.type = QString(ActionType::SetSnappingLayout);
    a.params.insert(ActionParam::LayoutId, layoutId);
    return a;
}

inline RuleAction tilingAlgorithm(const QString& algorithm)
{
    RuleAction a;
    a.type = QString(ActionType::SetTilingAlgorithm);
    a.params.insert(ActionParam::Algorithm, algorithm);
    return a;
}

/// DisableEngine carries a mandatory `mode` wire token; an unset mode is
/// rejected by the action validator (engineModeOptions().contains("") is
/// false). The earlier zero-arg shape produced an action that always
/// failed strict load — a footgun for future test authors. Mirror
/// engineMode(QString) above.
inline RuleAction disableEngine(const QString& mode)
{
    RuleAction a;
    a.type = QString(ActionType::DisableEngine);
    a.params.insert(ActionParam::Mode, mode);
    return a;
}

inline RuleAction excludeAction()
{
    RuleAction a;
    a.type = QString(ActionType::Exclude);
    return a;
}

inline RuleAction floatAction()
{
    RuleAction a;
    a.type = QString(ActionType::Float);
    return a;
}

inline RuleAction overrideShader(const QString& event, const QString& effectId)
{
    RuleAction a;
    a.type = QString(ActionType::OverrideAnimationShader);
    a.params.insert(ActionParam::Event, event);
    a.params.insert(ActionParam::EffectId, effectId);
    return a;
}

inline RuleAction setOpacity(double value)
{
    RuleAction a;
    a.type = QString(ActionType::SetOpacity);
    a.params.insert(ActionParam::Value, value);
    return a;
}

inline WindowRule makeRule(const QString& name, int priority, const MatchExpression& match,
                           const QList<RuleAction>& actions)
{
    WindowRule r;
    r.id = QUuid::createUuid();
    r.name = name;
    r.enabled = true;
    r.priority = priority;
    r.match = match;
    r.actions = actions;
    return r;
}

} // namespace PhosphorWindowRule::TestHelpers
