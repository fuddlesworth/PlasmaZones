// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDir>
#include <QString>
#include <QUuid>
#include <memory>

#include <PhosphorRules/ContextRuleBridge.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorRules/RuleEvaluator.h>
#include <PhosphorRules/WindowQuery.h>
#include <PhosphorRules/Rule.h>
#include <PhosphorRules/RuleSet.h>
#include <PhosphorRules/RuleStore.h>

#include <PhosphorZones/AssignmentEntry.h>
#include <PhosphorZones/LayoutRegistry.h>

#include "helpers/IsolatedConfigGuard.h"
#include "config/configdefaults.h"
#include "config/configkeys.h"

namespace PWR = PhosphorRules;
namespace CRB = PhosphorRules::ContextRuleBridge;

using PlasmaZones::ConfigDefaults;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

/// Shared harness for the rule-cascade fidelity suites. Carries the windowless
/// context-query builder, the engine-mode / layout slot readers, and the
/// LayoutRegistry-boundary fixture (isolated XDG tempdir + borrowed RuleStore).
/// The test classes inherit these so their bodies call the helpers unqualified.
class RuleCascadeFixture
{
protected:
    /// A windowless context query — only the context attributes are set, so
    /// only context-only rules contribute (window-property predicates would
    /// evaluate false). This reproduces the old Assignment cascade input.
    PWR::WindowQuery contextQuery(const QString& screenId, int desktop, const QString& activity)
    {
        PWR::WindowQuery q;
        q.screenId = screenId;
        q.virtualDesktop = desktop;
        q.activity = activity;
        return q;
    }

    /// The engine-mode token resolved for a query, or empty if the engine-mode
    /// slot was unfilled.
    QString resolvedMode(const PWR::RuleEvaluator& evaluator, const PWR::WindowQuery& q)
    {
        const PWR::ResolvedActions actions = evaluator.resolve(q);
        const auto slot = actions.slot(QString(PWR::ActionSlot::EngineMode));
        if (!slot) {
            return QString();
        }
        return slot->params.value(PWR::ActionParam::Mode).toString();
    }

    /// The layout slot's layoutId / algorithm for a query.
    QString resolvedLayoutId(const PWR::RuleEvaluator& evaluator, const PWR::WindowQuery& q)
    {
        const PWR::ResolvedActions actions = evaluator.resolve(q);
        const auto slot = actions.slot(QString(PWR::ActionSlot::Layout));
        if (!slot) {
            return QString();
        }
        if (slot->type == QString(PWR::ActionType::SetSnappingLayout)) {
            return slot->params.value(PWR::ActionParam::LayoutId).toString();
        }
        return slot->params.value(PWR::ActionParam::Algorithm).toString();
    }

    /// Per-test fixture for the LayoutRegistry-boundary cases: an isolated XDG
    /// tempdir, a freshly-constructed RuleStore pointed at the isolated
    /// rules file, and a LayoutRegistry that borrows the store. Kept here
    /// (rather than shared via LayoutRegistryTestHelpers.h) because these
    /// cases need to inject hand-crafted mixed rules straight into the store;
    /// the shared helper hides the store pointer.
    struct RegistryFixture
    {
        std::unique_ptr<IsolatedConfigGuard> guard;
        std::unique_ptr<PWR::RuleStore> store;
        std::unique_ptr<PhosphorZones::LayoutRegistry> registry;
    };

    RegistryFixture makeRegistryFixture()
    {
        RegistryFixture f;
        f.guard = std::make_unique<IsolatedConfigGuard>();
        f.store = std::make_unique<PWR::RuleStore>(ConfigDefaults::rulesFilePath());
        f.registry =
            std::make_unique<PhosphorZones::LayoutRegistry>(f.store.get(), PlasmaZones::ConfigKeys::layoutsSubdir());
        const QString layoutDir = f.guard->dataPath() + QLatin1Char('/') + PlasmaZones::ConfigKeys::layoutsSubdir();
        QDir().mkpath(layoutDir);
        f.registry->setLayoutDirectory(layoutDir);
        return f;
    }

    /// Build a mixed (context + window-property) rule at @p priority. The
    /// shape is All{ScreenId == screenId, AppId == appId}: it passes the
    /// LayoutRegistry filter (engine-mode action present, not catch-all) but
    /// its AppId leaf evaluates false against a windowless query, so the
    /// All{} must fail at resolve() time. Far above any pinned-context
    /// priority band so any leak would surface as this rule's layout
    /// winning the resolution.
    PWR::Rule makeMixedScreenAppRule(const QString& screenId, const QString& appId, bool autotileMode,
                                     const QString& snappingLayout, const QString& tilingAlgorithm, int priority = 999)
    {
        PWR::Rule mixed;
        mixed.id = QUuid::createUuid();
        mixed.name = screenId + QLatin1Char(' ') + appId;
        mixed.enabled = true;
        mixed.priority = priority;
        mixed.match = PWR::MatchExpression::makeAll(
            {PWR::MatchExpression::makeLeaf(PWR::Field::ScreenId, PWR::Operator::Equals, screenId),
             PWR::MatchExpression::makeLeaf(PWR::Field::AppId, PWR::Operator::Equals, appId)});
        mixed.actions = CRB::makeAssignmentActions(
            autotileMode ? QStringLiteral("autotile") : QStringLiteral("snapping"), snappingLayout, tilingAlgorithm);
        return mixed;
    }

    /// A USER-authored catch-all engine rule — an empty-All{} match (so the
    /// match accepts every query) at an explicit priority. This is the shape
    /// the Settings UI persists for a "Default / Any window" engine+layout rule.
    /// It is just a normal rule whose match accepts every query, so at a LOW
    /// priority it acts as the floor any pinned rule outranks.
    PWR::Rule makeUserCatchAllRule(bool autotileMode, const QString& snappingLayout, const QString& tilingAlgorithm,
                                   int priority)
    {
        PWR::Rule r;
        r.id = QUuid::createUuid();
        r.name = QStringLiteral("Default");
        r.enabled = true;
        r.priority = priority;
        r.match = PWR::MatchExpression(); // default-constructed → empty All{} catch-all
        r.actions = CRB::makeAssignmentActions(autotileMode ? QStringLiteral("autotile") : QStringLiteral("snapping"),
                                               snappingLayout, tilingAlgorithm);
        return r;
    }
};
