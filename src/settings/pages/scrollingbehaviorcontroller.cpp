// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "scrollingbehaviorcontroller.h"

#include "config/configdefaults.h"
#include "core/interfaces/isettings.h"
#include "settings/utils/triggerutils.h"

namespace PlasmaZones {

ScrollingBehaviorController::ScrollingBehaviorController(ISettings& settings, QObject* parent)
    : PhosphorControl::PageController(QStringLiteral("scrolling-window"), parent)
    , m_settings(&settings)
{
    m_lastAlwaysReinsertIntoStrip = alwaysReinsertIntoStrip();
    m_lastScrollingDragInsertTriggers = scrollingDragInsertTriggers();

    // Cache the AlwaysActive-stripped trigger list so a master-flag toggle
    // (which flips only the sentinel) doesn't re-emit
    // scrollingDragInsertTriggersChanged to QML when the visible list is
    // identical. Symmetric with TilingBehaviorController.
    connect(m_settings, &ISettings::scrollingDragInsertTriggersChanged, this, [this]() {
        const QVariantList newTriggers = scrollingDragInsertTriggers();
        if (newTriggers != m_lastScrollingDragInsertTriggers) {
            m_lastScrollingDragInsertTriggers = newTriggers;
            Q_EMIT scrollingDragInsertTriggersChanged();
        }
        const bool newAlwaysReinsert = alwaysReinsertIntoStrip();
        if (newAlwaysReinsert != m_lastAlwaysReinsertIntoStrip) {
            m_lastAlwaysReinsertIntoStrip = newAlwaysReinsert;
            Q_EMIT alwaysReinsertIntoStripChanged();
        }
    });
}

bool ScrollingBehaviorController::alwaysReinsertIntoStrip() const
{
    return TriggerUtils::hasAlwaysActiveTrigger(m_settings->scrollingDragInsertTriggers());
}

QVariantList ScrollingBehaviorController::scrollingDragInsertTriggers() const
{
    // Strip the AlwaysActive sentinel BEFORE converting so QML never sees a
    // phantom "no-modifier, no-mouse-button" chip when the master toggle is
    // on — convertTriggersForQml is lossy on AlwaysActive (modifier 8 →
    // bitmask 0). Same contract as the tiling twin.
    return TriggerUtils::convertTriggersForQml(
        TriggerUtils::stripAlwaysActiveTrigger(m_settings->scrollingDragInsertTriggers()));
}

QVariantList ScrollingBehaviorController::defaultScrollingDragInsertTriggers() const
{
    return TriggerUtils::convertTriggersForQml(ConfigDefaults::scrollingDragInsertTriggers());
}

void ScrollingBehaviorController::setAlwaysReinsertIntoStrip(bool enabled)
{
    if (alwaysReinsertIntoStrip() == enabled) {
        return;
    }
    const QVariantList next = TriggerUtils::applyAlwaysActiveToggle(m_settings->scrollingDragInsertTriggers(), enabled,
                                                                    ConfigDefaults::scrollingDragInsertTriggers());
    m_settings->setScrollingDragInsertTriggers(next);
}

void ScrollingBehaviorController::setScrollingDragInsertTriggers(const QVariantList& triggers)
{
    const QVariantList next = TriggerUtils::normaliseExplicitEdit(triggers, alwaysReinsertIntoStrip());
    if (m_settings->scrollingDragInsertTriggers() != next) {
        m_settings->setScrollingDragInsertTriggers(next);
    }
}

} // namespace PlasmaZones
