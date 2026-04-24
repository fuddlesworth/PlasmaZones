// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "snappingbehaviorcontroller.h"

#include "../config/configdefaults.h"
#include "../config/settings.h"
#include "triggerutils.h"

namespace PlasmaZones {

SnappingBehaviorController::SnappingBehaviorController(Settings* settings, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
{
    Q_ASSERT(m_settings);
    m_lastAlwaysActiveOnDrag = alwaysActivateOnDrag();

    // Forward Settings NOTIFY signals to the QML-facing Q_PROPERTY signals.
    // alwaysActivateOnDrag is derived from the drag-trigger list, so it only
    // fires when the AlwaysActive modifier actually comes or goes.
    connect(m_settings, &Settings::dragActivationTriggersChanged, this, [this]() {
        Q_EMIT dragActivationTriggersChanged();
        const bool newAlwaysActive = alwaysActivateOnDrag();
        if (newAlwaysActive != m_lastAlwaysActiveOnDrag) {
            m_lastAlwaysActiveOnDrag = newAlwaysActive;
            Q_EMIT alwaysActivateOnDragChanged();
        }
    });
    connect(m_settings, &Settings::zoneSpanTriggersChanged, this, &SnappingBehaviorController::zoneSpanTriggersChanged);
    connect(m_settings, &Settings::snapAssistTriggersChanged, this,
            &SnappingBehaviorController::snapAssistTriggersChanged);
}

bool SnappingBehaviorController::alwaysActivateOnDrag() const
{
    return TriggerUtils::hasAlwaysActiveTrigger(m_settings->dragActivationTriggers());
}

QVariantList SnappingBehaviorController::dragActivationTriggers() const
{
    return TriggerUtils::convertTriggersForQml(m_settings->dragActivationTriggers());
}

QVariantList SnappingBehaviorController::defaultDragActivationTriggers() const
{
    return TriggerUtils::convertTriggersForQml(ConfigDefaults::dragActivationTriggers());
}

QVariantList SnappingBehaviorController::zoneSpanTriggers() const
{
    return TriggerUtils::convertTriggersForQml(m_settings->zoneSpanTriggers());
}

QVariantList SnappingBehaviorController::defaultZoneSpanTriggers() const
{
    return TriggerUtils::convertTriggersForQml(ConfigDefaults::zoneSpanTriggers());
}

QVariantList SnappingBehaviorController::snapAssistTriggers() const
{
    return TriggerUtils::convertTriggersForQml(m_settings->snapAssistTriggers());
}

QVariantList SnappingBehaviorController::defaultSnapAssistTriggers() const
{
    return TriggerUtils::convertTriggersForQml(ConfigDefaults::snapAssistTriggers());
}

void SnappingBehaviorController::setAlwaysActivateOnDrag(bool enabled)
{
    if (alwaysActivateOnDrag() == enabled) {
        return;
    }
    m_settings->setDragActivationTriggers(enabled ? TriggerUtils::makeAlwaysActiveTriggerList()
                                                  : ConfigDefaults::dragActivationTriggers());
    // Settings::dragActivationTriggersChanged drives both
    // dragActivationTriggersChanged and alwaysActivateOnDragChanged via
    // the forwarding connect() in the constructor.
}

void SnappingBehaviorController::setDragActivationTriggers(const QVariantList& triggers)
{
    const QVariantList converted = TriggerUtils::convertTriggersForStorage(triggers);
    if (m_settings->dragActivationTriggers() != converted) {
        m_settings->setDragActivationTriggers(converted);
    }
}

void SnappingBehaviorController::setZoneSpanTriggers(const QVariantList& triggers)
{
    const QVariantList converted = TriggerUtils::convertTriggersForStorage(triggers);
    if (m_settings->zoneSpanTriggers() != converted) {
        m_settings->setZoneSpanTriggers(converted);
    }
}

void SnappingBehaviorController::setSnapAssistTriggers(const QVariantList& triggers)
{
    const QVariantList converted = TriggerUtils::convertTriggersForStorage(triggers);
    if (m_settings->snapAssistTriggers() != converted) {
        m_settings->setSnapAssistTriggers(converted);
    }
}

int SnappingBehaviorController::adjacentThresholdMin() const
{
    return ConfigDefaults::adjacentThresholdMin();
}

int SnappingBehaviorController::adjacentThresholdMax() const
{
    return ConfigDefaults::adjacentThresholdMax();
}

} // namespace PlasmaZones
