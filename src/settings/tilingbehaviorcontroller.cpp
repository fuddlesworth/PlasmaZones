// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tilingbehaviorcontroller.h"

#include "../config/configdefaults.h"
#include "../config/settings.h"
#include "triggerutils.h"

namespace PlasmaZones {

TilingBehaviorController::TilingBehaviorController(Settings* settings, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
{
    Q_ASSERT(m_settings);
    m_lastAlwaysReinsertIntoStack = alwaysReinsertIntoStack();

    connect(m_settings, &Settings::autotileDragInsertTriggersChanged, this, [this]() {
        Q_EMIT autotileDragInsertTriggersChanged();
        const bool newAlwaysActive = alwaysReinsertIntoStack();
        if (newAlwaysActive != m_lastAlwaysReinsertIntoStack) {
            m_lastAlwaysReinsertIntoStack = newAlwaysActive;
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
    m_settings->setAutotileDragInsertTriggers(enabled ? TriggerUtils::makeAlwaysActiveTriggerList()
                                                      : ConfigDefaults::autotileDragInsertTriggers());
}

void TilingBehaviorController::setAutotileDragInsertTriggers(const QVariantList& triggers)
{
    const QVariantList converted = TriggerUtils::convertTriggersForStorage(triggers);
    if (m_settings->autotileDragInsertTriggers() != converted) {
        m_settings->setAutotileDragInsertTriggers(converted);
    }
}

} // namespace PlasmaZones
