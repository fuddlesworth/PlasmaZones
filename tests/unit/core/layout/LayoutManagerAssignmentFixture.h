// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDir>
#include <QObject>
#include <QRectF>
#include <QString>
#include <QUuid>
#include <memory>
#include <vector>

#include <PhosphorRules/ContextRuleBridge.h>
#include <PhosphorRules/Rule.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorRules/RuleStore.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/Zone.h>

#include "helpers/IsolatedConfigGuard.h"
#include "helpers/LayoutRegistryTestHelpers.h"

namespace PlasmaZones {

/// Shared fixture for the LayoutRegistry assignment-cascade test executables.
/// Holds the manager/layout builders, the per-context rule authors, and the
/// IsolatedConfigGuard stack that isolates each manager's config file. Test
/// classes inherit this and add their own Q_SLOTS.
class LayoutManagerAssignmentFixture : public QObject
{
    Q_OBJECT

protected:
    PhosphorZones::Layout* createTestLayout(const QString& name, QObject* parent = nullptr)
    {
        auto* layout = new PhosphorZones::Layout(name, parent);
        auto* zone = new PhosphorZones::Zone();
        zone->setRelativeGeometry(QRectF(0, 0, 1, 1));
        layout->addZone(zone);
        return layout;
    }

    PhosphorZones::LayoutRegistry* createManager(QObject* parent = nullptr)
    {
        m_guards.emplace_back(std::make_unique<TestHelpers::IsolatedConfigGuard>());
        auto* mgr = PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts"), parent);
        QString layoutDir = m_guards.back()->dataPath() + QStringLiteral("/plasmazones/layouts");
        QDir().mkpath(layoutDir);
        mgr->setLayoutDirectory(layoutDir);
        return mgr;
    }

    /// Author a per-context DefaultLayoutAssignment override rule into the
    /// registry's owned rule store. @p allow true forces the synthesized default
    /// through for the context; false suppresses it. Mirrors the shape
    /// ContextRuleBridge::makeDisableRule produces for the per-mode disable case.
    void addDefaultAssignmentRule(PhosphorZones::LayoutRegistry* mgr, const QString& screenId, int virtualDesktop,
                                  const QString& activity, bool allow)
    {
        namespace PWR = PhosphorRules;
        auto* store = mgr->findChild<PWR::RuleStore*>();
        QVERIFY(store != nullptr);

        PWR::Rule rule;
        rule.id = QUuid::createUuid();
        rule.name = QStringLiteral("test-default-assignment");
        rule.enabled = true;
        rule.priority = PWR::ContextRuleBridge::kContextBandBase;
        rule.match = PWR::ContextRuleBridge::makeContextMatch(screenId, virtualDesktop, activity);

        PWR::RuleAction action;
        action.type = QString(PWR::ActionType::DefaultLayoutAssignment);
        action.params.insert(PWR::ActionParam::Value, allow);
        rule.actions.append(action);

        QVERIFY(store->addRule(rule));
    }

    /// Author a PINNED engine-mode assignment rule (no layout) — the shape a user
    /// gets from "set this monitor to snapping/autotile" without picking a layout.
    void addEngineModeRule(PhosphorZones::LayoutRegistry* mgr, const QString& screenId, int virtualDesktop,
                           const QString& activity, const QString& modeToken)
    {
        namespace PWR = PhosphorRules;
        auto* store = mgr->findChild<PWR::RuleStore*>();
        QVERIFY(store != nullptr);
        const PWR::Rule rule = PWR::ContextRuleBridge::makeAssignmentRule(
            QStringLiteral("test-mode"), screenId, virtualDesktop, activity, modeToken, QString(), QString(),
            PWR::ContextRuleBridge::kContextBandBase);
        QVERIFY(store->addRule(rule));
    }

    std::vector<std::unique_ptr<TestHelpers::IsolatedConfigGuard>> m_guards;

private Q_SLOTS:

    void cleanup()
    {
        m_guards.clear();
    }
};

} // namespace PlasmaZones
