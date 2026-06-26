// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../config/configdefaults.h"

#include <PhosphorControl/PageController.h>
#include <QObject>

namespace PlasmaZones {

/// Q_PROPERTY surface for the shared "Gaps" settings page.
///
/// CONSTANT bounds for the inner / outer gap spin boxes, sourced from the same
/// ConfigDefaults accessors the schema validator clamps against so the UI range
/// and the clamp range can never drift apart. The live gap values are on
/// Settings (Q_PROPERTY) and bind via `appSettings.innerGap` /
/// `appSettings.outerGap*`; this controller carries no editable state, so it is
/// never dirty and its apply/discard are no-ops.
class GapsController : public PhosphorControl::PageController
{
    Q_OBJECT

    Q_PROPERTY(int innerGapMin READ innerGapMin CONSTANT)
    Q_PROPERTY(int innerGapMax READ innerGapMax CONSTANT)
    Q_PROPERTY(int outerGapMin READ outerGapMin CONSTANT)
    Q_PROPERTY(int outerGapMax READ outerGapMax CONSTANT)

public:
    explicit GapsController(QObject* parent = nullptr)
        : PhosphorControl::PageController(QStringLiteral("gaps"), parent)
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

    int innerGapMin() const
    {
        return ConfigDefaults::innerGapMin();
    }
    int innerGapMax() const
    {
        return ConfigDefaults::innerGapMax();
    }
    int outerGapMin() const
    {
        return ConfigDefaults::outerGapMin();
    }
    int outerGapMax() const
    {
        return ConfigDefaults::outerGapMax();
    }
};

} // namespace PlasmaZones
