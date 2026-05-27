// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tilingbehaviorcontroller.h"

#include "../config/configdefaults.h"
#include "../config/settings.h"
#include "triggerutils.h"

namespace PlasmaZones {

TilingBehaviorController::TilingBehaviorController(Settings* settings, QObject* parent)
    : PhosphorSettingsUi::PageController(QStringLiteral("tiling-behavior"), parent)
    , m_settings(settings)
{
    Q_ASSERT(m_settings);
    m_lastAlwaysReinsertIntoStack = alwaysReinsertIntoStack();

    connect(m_settings, &Settings::autotileDragInsertTriggersChanged, this, [this]() {
        Q_EMIT autotileDragInsertTriggersChanged();
        const bool newAlwaysReinsert = alwaysReinsertIntoStack();
        if (newAlwaysReinsert != m_lastAlwaysReinsertIntoStack) {
            m_lastAlwaysReinsertIntoStack = newAlwaysReinsert;
            Q_EMIT alwaysReinsertIntoStackChanged();
        }
    });
}

bool TilingBehaviorController::alwaysReinsertIntoStack() const
{
    return TriggerUtils::hasAlwaysActiveTrigger(m_settings->autotileDragInsertTriggers());
}

QVariantList TilingBehaviorController::autotileDragInsertTriggers() const
{
    return TriggerUtils::convertTriggersForQml(m_settings->autotileDragInsertTriggers());
}

QVariantList TilingBehaviorController::defaultAutotileDragInsertTriggers() const
{
    return TriggerUtils::convertTriggersForQml(ConfigDefaults::autotileDragInsertTriggers());
}

void TilingBehaviorController::setAlwaysReinsertIntoStack(bool enabled)
{
    if (alwaysReinsertIntoStack() == enabled) {
        return;
    }
    // See SnappingBehaviorController::setAlwaysActivateOnDrag — both
    // master-toggle setters share the helper to keep the sentinel-cap +
    // empty-list-fallback semantics in lockstep.
    const QVariantList next = TriggerUtils::applyAlwaysActiveToggle(m_settings->autotileDragInsertTriggers(), enabled,
                                                                    ConfigDefaults::autotileDragInsertTriggers());
    m_settings->setAutotileDragInsertTriggers(next);
}

void TilingBehaviorController::setAutotileDragInsertTriggers(const QVariantList& triggers)
{
    // Same helper as SnappingBehaviorController::setDragActivationTriggers.
    const QVariantList next = TriggerUtils::normaliseExplicitEdit(triggers, alwaysReinsertIntoStack());
    if (m_settings->autotileDragInsertTriggers() != next) {
        m_settings->setAutotileDragInsertTriggers(next);
    }
}

} // namespace PlasmaZones
