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
    // Mirror SnappingBehaviorController::setAlwaysActivateOnDrag so a
    // toggle-off does NOT silently wipe the user's customised non-sentinel
    // triggers. The previous shape dropped straight to the factory default
    // on every off-toggle, losing user data; only fall back to defaults
    // when stripping the sentinel leaves the list empty.
    const QVariantList nonSentinel = TriggerUtils::stripAlwaysActiveTrigger(m_settings->autotileDragInsertTriggers());
    QVariantList next;
    if (enabled) {
        next = TriggerUtils::mergeAlwaysActiveTrigger(nonSentinel);
    } else if (nonSentinel.isEmpty()) {
        next = ConfigDefaults::autotileDragInsertTriggers();
    } else {
        next = nonSentinel;
    }
    m_settings->setAutotileDragInsertTriggers(next);
}

void TilingBehaviorController::setAutotileDragInsertTriggers(const QVariantList& triggers)
{
    // Mirror SnappingBehaviorController::setDragActivationTriggers: strip
    // the sentinel from incoming QML edits (the widget shouldn't include
    // it — sentinel ownership belongs to the master toggle), then re-merge
    // if always-active is currently set so writeTriggerList's MAX cap
    // doesn't truncate away the sentinel and silently flip the master
    // toggle off.
    const QVariantList nonSentinel =
        TriggerUtils::stripAlwaysActiveTrigger(TriggerUtils::convertTriggersForStorage(triggers));
    const QVariantList next =
        alwaysReinsertIntoStack() ? TriggerUtils::mergeAlwaysActiveTrigger(nonSentinel) : nonSentinel;
    if (m_settings->autotileDragInsertTriggers() != next) {
        m_settings->setAutotileDragInsertTriggers(next);
    }
}

} // namespace PlasmaZones
