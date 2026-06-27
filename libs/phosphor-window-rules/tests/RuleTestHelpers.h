// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// Shared concise constructors for the phosphor-window-rules test suite. Header
// only — every test that needs them includes this directly.

#include <PhosphorWindowRules/PhosphorWindowRules.h>

#include <QJsonObject>
#include <QUuid>

namespace PhosphorWindowRules::TestHelpers {

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

inline RuleAction borderWidth(int px)
{
    RuleAction a;
    a.type = QString(ActionType::SetBorderWidth);
    a.params.insert(ActionParam::Value, px);
    return a;
}

inline RuleAction borderColor(const QString& hex)
{
    RuleAction a;
    a.type = QString(ActionType::SetBorderColorActive);
    a.params.insert(ActionParam::Value, hex);
    return a;
}

inline RuleAction borderColorInactive(const QString& hex)
{
    RuleAction a;
    a.type = QString(ActionType::SetBorderColorInactive);
    a.params.insert(ActionParam::Value, hex);
    return a;
}

inline RuleAction innerGap(int px)
{
    RuleAction a;
    a.type = QString(ActionType::SetInnerGap);
    a.params.insert(ActionParam::Value, px);
    return a;
}

inline RuleAction restorePosition(bool value)
{
    RuleAction a;
    a.type = QString(ActionType::RestorePosition);
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

} // namespace PhosphorWindowRules::TestHelpers
