// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "scrollingbehaviorcontroller.h"

#include "config/configdefaults.h"
#include "core/interfaces/isettings.h"
#include "settings/utils/triggerutils.h"

namespace PlasmaZones {

// The PageController id is INERT here, exactly as it is for the snapping and
// tiling twins: none of the three is passed to registerPage, so nothing
// resolves a nav node or a staging domain by it. They are child Q_PROPERTY
// surfaces on SettingsController and the page they serve is registered by the
// QML side. The id satisfies the base constructor and names the page for a
// reader; do not expect a deep link or a dirty-badge to key off it.
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

    // Straight forwards, no cached compare: neither wheel list carries a
    // sentinel, so the ISettings NOTIFY already fires exactly when the
    // QML-visible list changes (Settings::writeTriggerList compares the
    // canonicalised before/after and stays silent otherwise).
    connect(m_settings, &ISettings::scrollingWheelFocusTriggersChanged, this,
            &ScrollingBehaviorController::scrollingWheelFocusTriggersChanged);
    connect(m_settings, &ISettings::scrollingWheelViewTriggersChanged, this,
            &ScrollingBehaviorController::scrollingWheelViewTriggersChanged);
    // The collision flag is derived from BOTH lists, so it re-evaluates on
    // either. Cached and compared rather than forwarded, because most edits to
    // one list leave the answer alone and the banner should not re-announce
    // itself on every keystroke of an unrelated rebind.
    m_lastWheelTriggersCollide = wheelTriggersCollide();
    const auto reEvaluateCollision = [this] {
        const bool collides = wheelTriggersCollide();
        if (collides != m_lastWheelTriggersCollide) {
            m_lastWheelTriggersCollide = collides;
            Q_EMIT wheelTriggersCollideChanged();
        }
    };
    connect(m_settings, &ISettings::scrollingWheelFocusTriggersChanged, this, reEvaluateCollision);
    connect(m_settings, &ISettings::scrollingWheelViewTriggersChanged, this, reEvaluateCollision);
}

bool ScrollingBehaviorController::wheelTriggersCollide() const
{
    // Compared in STORAGE form, which is what the effect matches on. The
    // QML-facing bitmask conversion is lossy for values the editor cannot
    // render, so two entries could look different there and still be the same
    // chord to anyTriggerHeldExact.
    const QVariantList focus = m_settings->scrollingWheelFocusTriggers();
    const QVariantList view = m_settings->scrollingWheelViewTriggers();
    for (const QVariant& f : focus) {
        if (view.contains(f)) {
            return true;
        }
    }
    return false;
}

QVariantList ScrollingBehaviorController::scrollingWheelFocusTriggers() const
{
    return TriggerUtils::convertTriggersForQml(m_settings->scrollingWheelFocusTriggers());
}

QVariantList ScrollingBehaviorController::defaultScrollingWheelFocusTriggers() const
{
    return TriggerUtils::convertTriggersForQml(ConfigDefaults::scrollingWheelFocusTriggers());
}

QVariantList ScrollingBehaviorController::scrollingWheelViewTriggers() const
{
    return TriggerUtils::convertTriggersForQml(m_settings->scrollingWheelViewTriggers());
}

QVariantList ScrollingBehaviorController::defaultScrollingWheelViewTriggers() const
{
    return TriggerUtils::convertTriggersForQml(ConfigDefaults::scrollingWheelViewTriggers());
}

void ScrollingBehaviorController::setScrollingWheelFocusTriggers(const QVariantList& triggers)
{
    const QVariantList next = TriggerUtils::convertTriggersForStorage(triggers);
    if (m_settings->scrollingWheelFocusTriggers() != next) {
        m_settings->setScrollingWheelFocusTriggers(next);
    }
}

void ScrollingBehaviorController::setScrollingWheelViewTriggers(const QVariantList& triggers)
{
    const QVariantList next = TriggerUtils::convertTriggersForStorage(triggers);
    if (m_settings->scrollingWheelViewTriggers() != next) {
        m_settings->setScrollingWheelViewTriggers(next);
    }
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

int ScrollingBehaviorController::triggerGraceMsMin() const
{
    return ConfigDefaults::triggerGraceMsMin();
}

int ScrollingBehaviorController::triggerGraceMsMax() const
{
    return ConfigDefaults::triggerGraceMsMax();
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
