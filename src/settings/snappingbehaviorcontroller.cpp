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
    // The trigger widget doesn't represent the AlwaysActive sentinel — that
    // bit is owned by the master "Activate on every drag" toggle. Surface
    // only the user-configurable non-sentinel entries to QML; the same
    // entries serve double duty as deactivation triggers in always-active
    // mode (see resolveActivationActive).
    return TriggerUtils::convertTriggersForQml(
        TriggerUtils::stripAlwaysActiveTrigger(m_settings->dragActivationTriggers()));
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
    // Add or remove the AlwaysActive sentinel from the existing list,
    // preserving the user's non-sentinel triggers. In always-active mode
    // those non-sentinel entries become deactivate-while-held triggers
    // (resolveActivationActive inverts the active output when the
    // AlwaysActive sentinel is present); on toggle off they revert to
    // hold/toggle activation triggers. mergeAlwaysActiveTrigger prepends
    // the sentinel so it survives the storage cap; if the resulting list
    // would be empty on toggle off, fall back to the static default so the
    // user keeps a working hold-to-activate trigger.
    const QVariantList nonSentinel = TriggerUtils::stripAlwaysActiveTrigger(m_settings->dragActivationTriggers());
    QVariantList next;
    if (enabled) {
        next = TriggerUtils::mergeAlwaysActiveTrigger(nonSentinel);
    } else if (nonSentinel.isEmpty()) {
        next = ConfigDefaults::dragActivationTriggers();
    } else {
        next = nonSentinel;
    }
    m_settings->setDragActivationTriggers(next);
    // Settings::dragActivationTriggersChanged drives both
    // dragActivationTriggersChanged and alwaysActivateOnDragChanged via
    // the forwarding connect() in the constructor.
}

void SnappingBehaviorController::setDragActivationTriggers(const QVariantList& triggers)
{
    // Strip first in case the QML side somehow sent the sentinel through
    // (the widget shouldn't, but defensive: the sentinel is owned by the
    // master toggle), then re-merge if always-active is currently set so
    // mergeAlwaysActiveTrigger's cap-aware prepend protects it from
    // truncation in writeTriggerList's .mid(0, MAX).
    const QVariantList nonSentinel =
        TriggerUtils::stripAlwaysActiveTrigger(TriggerUtils::convertTriggersForStorage(triggers));
    const QVariantList next =
        alwaysActivateOnDrag() ? TriggerUtils::mergeAlwaysActiveTrigger(nonSentinel) : nonSentinel;
    if (m_settings->dragActivationTriggers() != next) {
        m_settings->setDragActivationTriggers(next);
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
