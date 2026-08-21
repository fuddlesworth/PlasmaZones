// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_scrolling_behavior_controller.cpp
 * @brief Coverage for ScrollingBehaviorController, the scrolling twin of
 *        SnappingBehaviorController and TilingBehaviorController.
 *
 * The controller shipped with no tests while both siblings had them. What is
 * pinned here is the behaviour a user can actually reach from the Triggers
 * card on Scrolling → Window:
 *
 *   1. The master "always re-insert" toggle round-trips through the stored
 *      list without destroying the user's own trigger chips, and toggling it
 *      off with no chips left falls back to the factory default rather than
 *      to an empty list (an empty list means the feature is unreachable).
 *   2. The QML-facing list never shows the AlwaysActive sentinel, because
 *      convertTriggersForQml is lossy on it and would render a phantom
 *      "no modifier, no button" chip that matches every drag.
 *   3. An explicit chip edit while the master toggle is on preserves the
 *      toggle, rather than silently switching it off as a side effect.
 *   4. Each derived property emits only when it actually flips.
 *   5. The always-active predicate agrees with the daemon: only the BARE
 *      sentinel counts, because the daemon's per-tick anyTriggerHeld ANDs
 *      the mouse button in, so a sentinel paired with a button still needs
 *      that button held.
 */

#include <QSignalSpy>
#include <QTest>

#include "config/configdefaults.h"
#include "config/settings.h"
#include "core/types/enums.h"
#include "helpers/IsolatedConfigGuard.h"
#include "settings/pages/scrollingbehaviorcontroller.h"
#include "settings/utils/triggerutils.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

namespace {

QVariantMap storedTrigger(int modifier, int mouseButton = 0)
{
    QVariantMap m;
    m[ConfigDefaults::triggerModifierField()] = modifier;
    m[ConfigDefaults::triggerMouseButtonField()] = mouseButton;
    return m;
}

int modifierOf(const QVariant& v)
{
    return v.toMap().value(ConfigDefaults::triggerModifierField(), 0).toInt();
}

bool listHasSentinel(const QVariantList& l)
{
    for (const QVariant& v : l) {
        if (modifierOf(v) == static_cast<int>(DragModifier::AlwaysActive)) {
            return true;
        }
    }
    return false;
}

} // namespace

class TestScrollingBehaviorController : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    /// The predicate the daemon has to agree with. effectiveDragReorderModeFor
    /// requires modifier == AlwaysActive AND mouseButton == 0, because
    /// anyTriggerHeld ANDs the button match in per tick. A settings side that
    /// ignored the button would light the master toggle over a policy the
    /// daemon does not apply.
    void alwaysActivePredicateRequiresABareSentinel()
    {
        QVariantList bare;
        bare << storedTrigger(static_cast<int>(DragModifier::AlwaysActive));
        QVERIFY(TriggerUtils::hasAlwaysActiveTrigger(bare));

        QVariantList withButton;
        withButton << storedTrigger(static_cast<int>(DragModifier::AlwaysActive), Qt::MiddleButton);
        QVERIFY2(!TriggerUtils::hasAlwaysActiveTrigger(withButton),
                 "a sentinel paired with a mouse button still requires that button, so it is not unconditional");

        // A bare sentinel ALONGSIDE a button-paired one is still always-on.
        QVariantList mixed = withButton;
        mixed << storedTrigger(static_cast<int>(DragModifier::AlwaysActive));
        QVERIFY(TriggerUtils::hasAlwaysActiveTrigger(mixed));
    }

    void toggleOnKeepsUserTriggersAndAddsTheSentinel()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        ScrollingBehaviorController controller(settings);

        QVariantList user;
        user << storedTrigger(static_cast<int>(DragModifier::Ctrl));
        user << storedTrigger(static_cast<int>(DragModifier::Alt), Qt::MiddleButton);
        settings.setScrollingDragInsertTriggers(user);
        QVERIFY(!controller.alwaysReinsertIntoStrip());

        controller.setAlwaysReinsertIntoStrip(true);
        QVERIFY(controller.alwaysReinsertIntoStrip());
        const QVariantList stored = settings.scrollingDragInsertTriggers();
        QVERIFY(listHasSentinel(stored));
        // The user's two chips survive the toggle. Losing them is the defect
        // the shared merge helper exists to prevent, and the QML-facing list
        // is what the user sees, so assert through that.
        const QVariantList shown = controller.scrollingDragInsertTriggers();
        QCOMPARE(shown.size(), 2);
        QVERIFY2(!listHasSentinel(shown), "the sentinel must never reach QML as a chip");
    }

    void toggleOffWithNoUserTriggersRestoresTheFactoryList()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        ScrollingBehaviorController controller(settings);

        // Sentinel only: nothing of the user's is left underneath it.
        QVariantList sentinelOnly;
        sentinelOnly << storedTrigger(static_cast<int>(DragModifier::AlwaysActive));
        settings.setScrollingDragInsertTriggers(sentinelOnly);
        QVERIFY(controller.alwaysReinsertIntoStrip());

        controller.setAlwaysReinsertIntoStrip(false);
        QVERIFY(!controller.alwaysReinsertIntoStrip());
        // An empty list would leave the feature unreachable: no trigger can
        // ever be held, so a drag could never re-insert at all.
        QVERIFY2(!settings.scrollingDragInsertTriggers().isEmpty(),
                 "turning the master toggle off must leave a usable trigger, not an empty list");
        QCOMPARE(settings.scrollingDragInsertTriggers(), ConfigDefaults::scrollingDragInsertTriggers());
    }

    void explicitChipEditPreservesTheMasterToggle()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        ScrollingBehaviorController controller(settings);

        controller.setAlwaysReinsertIntoStrip(true);
        QVERIFY(controller.alwaysReinsertIntoStrip());

        // A QML-authored edit never carries the sentinel (the widget does not
        // represent it). Writing it back must not read as "the user removed
        // always-active".
        QVariantList fromQml;
        QVariantMap chip;
        chip[ConfigDefaults::triggerModifierField()] = static_cast<int>(Qt::ShiftModifier);
        chip[ConfigDefaults::triggerMouseButtonField()] = 0;
        fromQml << chip;
        controller.setScrollingDragInsertTriggers(fromQml);

        QVERIFY2(controller.alwaysReinsertIntoStrip(),
                 "editing the chips must not switch the master toggle off as a side effect");
        QVERIFY(listHasSentinel(settings.scrollingDragInsertTriggers()));
    }

    void derivedPropertiesEmitOnlyOnAFlip()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        ScrollingBehaviorController controller(settings);

        QSignalSpy alwaysSpy(&controller, &ScrollingBehaviorController::alwaysReinsertIntoStripChanged);
        QVERIFY(alwaysSpy.isValid());

        controller.setAlwaysReinsertIntoStrip(true);
        QCOMPARE(alwaysSpy.count(), 1);
        // Same value again: the setter's equality bail, and behind it the
        // cached-derived-boolean gate. A re-emit here runs every bound QML
        // element's update for nothing.
        controller.setAlwaysReinsertIntoStrip(true);
        QCOMPARE(alwaysSpy.count(), 1);
        controller.setAlwaysReinsertIntoStrip(false);
        QCOMPARE(alwaysSpy.count(), 2);
    }
};

QTEST_MAIN(TestScrollingBehaviorController)
#include "test_scrolling_behavior_controller.moc"
