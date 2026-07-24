// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "snappingbehaviorcontroller.h"

#include "config/configdefaults.h"
#include "core/interfaces/isettings.h"
#include "settings/utils/triggerutils.h"

namespace PlasmaZones {

SnappingBehaviorController::SnappingBehaviorController(ISettings& settings, QObject* parent)
    : PhosphorControl::PageController(QStringLiteral("snapping-behavior"), parent)
    , m_settings(&settings)
{
    m_lastAlwaysActiveOnDrag = alwaysActivateOnDrag();
    m_lastDragActivationTriggers = dragActivationTriggers();

    // Forward ISettings NOTIFY signals to the QML-facing Q_PROPERTY signals.
    // alwaysActivateOnDrag is derived from the drag-trigger list, so it only
    // fires when the AlwaysActive modifier actually comes or goes. The
    // QML-facing trigger list is ALSO derived (AlwaysActive sentinel
    // stripped) — toggling only the master flag flips the sentinel but
    // leaves the QML-visible list unchanged, so cache the stripped list
    // and only emit when it actually differs.
    connect(m_settings, &ISettings::dragActivationTriggersChanged, this, [this]() {
        const QVariantList newTriggers = dragActivationTriggers();
        if (newTriggers != m_lastDragActivationTriggers) {
            m_lastDragActivationTriggers = newTriggers;
            Q_EMIT dragActivationTriggersChanged();
        }
        const bool newAlwaysActive = alwaysActivateOnDrag();
        if (newAlwaysActive != m_lastAlwaysActiveOnDrag) {
            m_lastAlwaysActiveOnDrag = newAlwaysActive;
            Q_EMIT alwaysActivateOnDragChanged();
        }
    });
    connect(m_settings, &ISettings::zoneSpanTriggersChanged, this,
            &SnappingBehaviorController::zoneSpanTriggersChanged);
    connect(m_settings, &ISettings::snapAssistTriggersChanged, this,
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
    // Centralised in TriggerUtils::applyAlwaysActiveToggle so the
    // sentinel-cap + empty-list fallback dance stays in lockstep with
    // TilingBehaviorController::setAlwaysReinsertIntoStack. The factory
    // default protects users from a toggle-off + already-empty list
    // collapsing to a no-trigger state.
    const QVariantList next = TriggerUtils::applyAlwaysActiveToggle(m_settings->dragActivationTriggers(), enabled,
                                                                    ConfigDefaults::dragActivationTriggers());
    m_settings->setDragActivationTriggers(next);
    // Settings::dragActivationTriggersChanged drives both
    // dragActivationTriggersChanged and alwaysActivateOnDragChanged via
    // the forwarding connect() in the constructor.
}

void SnappingBehaviorController::setDragActivationTriggers(const QVariantList& triggers)
{
    // Centralised in TriggerUtils::normaliseExplicitEdit so the
    // strip-then-conditional-remerge sequence stays in lockstep with
    // the tiling controller's equivalent setter.
    const QVariantList next = TriggerUtils::normaliseExplicitEdit(triggers, alwaysActivateOnDrag());
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
