// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../config/configdefaults.h"

#include <PhosphorCompositor/DecorationDefaults.h>
#include <PhosphorControl/PageController.h>
#include <QObject>
#include <QString>

namespace PlasmaZones {

/// Q_PROPERTY surface for the slim "Window Appearance" settings page.
///
/// The page is a friendly editor for the managed baseline appearance
/// WindowRule (the catch-all, lowest-priority rule the daemon seeds). It reads
/// and writes that rule's actions through `settingsController.windowRulesPage`
/// (WindowRuleController), so this controller only carries the CONSTANT slider
/// bounds and the baseline rule id. It holds no editable state of its own, so
/// it is never dirty and its apply/discard are no-ops; the dirty/save path runs
/// through the Window Rules page controller.
class WindowAppearanceController : public PhosphorControl::PageController
{
    Q_OBJECT

    Q_PROPERTY(int borderWidthMin READ borderWidthMin CONSTANT)
    Q_PROPERTY(int borderWidthMax READ borderWidthMax CONSTANT)
    Q_PROPERTY(int borderRadiusMin READ borderRadiusMin CONSTANT)
    Q_PROPERTY(int borderRadiusMax READ borderRadiusMax CONSTANT)
    /// Stable id (UUID string, braces) of the managed baseline appearance rule
    /// the page edits. QML passes it to the WindowRuleController's
    /// ruleJson() / updateRuleFromJson() calls.
    Q_PROPERTY(QString baselineRuleId READ baselineRuleId CONSTANT)

public:
    explicit WindowAppearanceController(QObject* parent = nullptr)
        : PhosphorControl::PageController(QStringLiteral("window-appearance"), parent)
    {
    }

    bool isDirty() const override
    {
        return false;
    }
    void apply() override
    {
    }
    void discard() override
    {
    }

    int borderWidthMin() const
    {
        return ::PhosphorCompositor::DecorationDefaults::BorderWidthMin;
    }
    int borderWidthMax() const
    {
        return ::PhosphorCompositor::DecorationDefaults::BorderWidthMax;
    }
    int borderRadiusMin() const
    {
        return ::PhosphorCompositor::DecorationDefaults::BorderRadiusMin;
    }
    int borderRadiusMax() const
    {
        return ::PhosphorCompositor::DecorationDefaults::BorderRadiusMax;
    }
    QString baselineRuleId() const
    {
        return ConfigDefaults::baselineAppearanceRuleId().toString();
    }
};

} // namespace PlasmaZones
