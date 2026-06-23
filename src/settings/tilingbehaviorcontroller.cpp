// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tilingbehaviorcontroller.h"

#include "../config/configdefaults.h"
#include "../core/isettings.h"
#include "triggerutils.h"

namespace PlasmaZones {

TilingBehaviorController::TilingBehaviorController(ISettings& settings, QObject* parent)
    : PhosphorControl::PageController(QStringLiteral("tiling-behavior"), parent)
    , m_settings(&settings)
{
    m_lastAlwaysReinsertIntoStack = alwaysReinsertIntoStack();
    m_lastAutotileDragInsertTriggers = autotileDragInsertTriggers();

    // Cache the AlwaysActive-stripped trigger list so a master-flag
    // toggle (which flips only the sentinel) doesn't re-emit
    // autotileDragInsertTriggersChanged to QML when the visible list
    // is identical. Symmetric with SnappingBehaviorController.
    connect(m_settings, &ISettings::autotileDragInsertTriggersChanged, this, [this]() {
        const QVariantList newTriggers = autotileDragInsertTriggers();
        if (newTriggers != m_lastAutotileDragInsertTriggers) {
            m_lastAutotileDragInsertTriggers = newTriggers;
            Q_EMIT autotileDragInsertTriggersChanged();
        }
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
    // Strip the AlwaysActive sentinel BEFORE converting so QML never sees
    // a phantom "no-modifier, no-mouse-button" chip when the master
    // toggle is on. Mirrors SnappingBehaviorController::dragActivationTriggers
    // — both surfaces use the AlwaysActive bit as a master-toggle proxy
    // stored in the trigger list, and the chip widget would render the
    // sentinel as an empty trigger row otherwise. convertTriggersForQml
    // is lossy on AlwaysActive (modifier=8 → bitmask=0), so this strip
    // is the canonical way to feed QML.
    return TriggerUtils::convertTriggersForQml(
        TriggerUtils::stripAlwaysActiveTrigger(m_settings->autotileDragInsertTriggers()));
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
