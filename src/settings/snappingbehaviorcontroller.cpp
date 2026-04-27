// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "snappingbehaviorcontroller.h"

#include "../config/configdefaults.h"
#include "../config/settings.h"
#include "../core/enums.h"
#include "triggerutils.h"

namespace PlasmaZones {

namespace {

/// Strip every entry whose modifier is the AlwaysActive sentinel. The trigger
/// widget never displays AlwaysActive (its modifier-bitmask map renders the
/// sentinel as an empty "(none)" chip), and the master "Activate on every
/// drag" toggle is the sole owner of that bit. Anywhere the QML side reads
/// the activation list, we surface only the user-configurable non-sentinel
/// entries; the controller re-merges the sentinel on write.
QVariantList stripAlwaysActive(const QVariantList& list)
{
    QVariantList result;
    result.reserve(list.size());
    const int alwaysActive = static_cast<int>(DragModifier::AlwaysActive);
    for (const auto& v : list) {
        if (v.toMap().value(ConfigDefaults::triggerModifierField()).toInt() == alwaysActive) {
            continue;
        }
        result.append(v);
    }
    return result;
}

QVariantMap alwaysActiveEntry()
{
    QVariantMap entry;
    entry[ConfigDefaults::triggerModifierField()] = static_cast<int>(DragModifier::AlwaysActive);
    entry[ConfigDefaults::triggerMouseButtonField()] = 0;
    return entry;
}

} // namespace

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
    return TriggerUtils::convertTriggersForQml(stripAlwaysActive(m_settings->dragActivationTriggers()));
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
    // hold/toggle activation triggers. If the resulting list is empty
    // after removing the sentinel, fall back to the static default so
    // the user keeps a working hold-to-activate trigger.
    QVariantList next = stripAlwaysActive(m_settings->dragActivationTriggers());
    if (enabled) {
        next.append(alwaysActiveEntry());
    } else if (next.isEmpty()) {
        next = ConfigDefaults::dragActivationTriggers();
    }
    m_settings->setDragActivationTriggers(next);
    // Settings::dragActivationTriggersChanged drives both
    // dragActivationTriggersChanged and alwaysActivateOnDragChanged via
    // the forwarding connect() in the constructor.
}

void SnappingBehaviorController::setDragActivationTriggers(const QVariantList& triggers)
{
    QVariantList converted = stripAlwaysActive(TriggerUtils::convertTriggersForStorage(triggers));
    // Re-merge the AlwaysActive sentinel if currently set — the master
    // toggle owns it; the trigger widget edits only non-sentinel entries.
    if (alwaysActivateOnDrag()) {
        converted.append(alwaysActiveEntry());
    }
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
