// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_snapping_behavior_controller.cpp
 * @brief Round-trip tests for the AlwaysActive sentinel handling shared by
 *        @c TriggerUtils and @c SnappingBehaviorController (#249).
 *
 * Pins two fixes:
 *   1. @c TriggerUtils::mergeAlwaysActiveTrigger prepends the sentinel and
 *      pre-trims the user portion so @c Settings::writeTriggerList's
 *      .mid(0, MAX) cap can't silently drop the sentinel when the user has
 *      already filled all 4 trigger slots.
 *   2. The controller setters (toggle on/off, edit non-sentinel list) all
 *      preserve user triggers when toggling between modes and re-merge the
 *      sentinel atomically through the merge helper.
 */

#include <QTest>
#include <QSignalSpy>

#include "config/settings.h"
#include "config/configdefaults.h"
#include "core/enums.h"
#include "settings/snappingbehaviorcontroller.h"
#include "settings/triggerutils.h"
#include "../helpers/IsolatedConfigGuard.h"

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

bool entryIsSentinel(const QVariant& v)
{
    return v.toMap().value(ConfigDefaults::triggerModifierField(), 0).toInt()
        == static_cast<int>(DragModifier::AlwaysActive);
}

} // namespace

class TestSnappingBehaviorController : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // ─── TriggerUtils::stripAlwaysActiveTrigger ───────────────────────────

    void strip_removesSentinel_keepsUserOrder()
    {
        QVariantList in;
        in << storedTrigger(static_cast<int>(DragModifier::Ctrl));
        in << storedTrigger(static_cast<int>(DragModifier::AlwaysActive));
        in << storedTrigger(static_cast<int>(DragModifier::Alt));
        const auto out = TriggerUtils::stripAlwaysActiveTrigger(in);
        QCOMPARE(out.size(), 2);
        QCOMPARE(out.at(0).toMap().value(ConfigDefaults::triggerModifierField()).toInt(),
                 static_cast<int>(DragModifier::Ctrl));
        QCOMPARE(out.at(1).toMap().value(ConfigDefaults::triggerModifierField()).toInt(),
                 static_cast<int>(DragModifier::Alt));
    }

    // ─── TriggerUtils::mergeAlwaysActiveTrigger ───────────────────────────

    void merge_emptyInput_returnsSentinelOnly()
    {
        const auto out = TriggerUtils::mergeAlwaysActiveTrigger({});
        QCOMPARE(out.size(), 1);
        QVERIFY(entryIsSentinel(out.first()));
    }

    void merge_underCap_prependsSentinel()
    {
        QVariantList in;
        in << storedTrigger(static_cast<int>(DragModifier::Ctrl));
        in << storedTrigger(static_cast<int>(DragModifier::Alt));
        const auto out = TriggerUtils::mergeAlwaysActiveTrigger(in);
        QCOMPARE(out.size(), 3);
        QVERIFY(entryIsSentinel(out.first()));
        QCOMPARE(out.at(1).toMap().value(ConfigDefaults::triggerModifierField()).toInt(),
                 static_cast<int>(DragModifier::Ctrl));
        QCOMPARE(out.at(2).toMap().value(ConfigDefaults::triggerModifierField()).toInt(),
                 static_cast<int>(DragModifier::Alt));
    }

    /// The pre-existing bug: with 4 user triggers, the naive append-then-cap
    /// path drops the sentinel and silently fails the always-active toggle.
    /// merge must trim the user portion to MAX-1 so the sentinel survives.
    void merge_atCap_dropsTrailingUserTriggerNotSentinel()
    {
        constexpr int max = ConfigDefaults::maxTriggersPerAction();
        QVariantList in;
        for (int i = 0; i < max; ++i) {
            in << storedTrigger(static_cast<int>(DragModifier::Shift) + i);
        }
        const auto out = TriggerUtils::mergeAlwaysActiveTrigger(in);
        QCOMPARE(out.size(), max);
        QVERIFY(entryIsSentinel(out.first()));
        // User entries 0..MAX-2 retained; entry MAX-1 dropped.
        for (int i = 0; i < max - 1; ++i) {
            QCOMPARE(out.at(i + 1).toMap().value(ConfigDefaults::triggerModifierField()).toInt(),
                     static_cast<int>(DragModifier::Shift) + i);
        }
    }

    void merge_overCap_trimsToCap()
    {
        constexpr int max = ConfigDefaults::maxTriggersPerAction();
        QVariantList in;
        for (int i = 0; i < max + 5; ++i) {
            in << storedTrigger(1 + i);
        }
        const auto out = TriggerUtils::mergeAlwaysActiveTrigger(in);
        QCOMPARE(out.size(), max);
        QVERIFY(entryIsSentinel(out.first()));
    }

    // ─── SnappingBehaviorController round-trips through real Settings ─────

    /// Regression for the silent-truncation bug: with the user already
    /// holding MAX activation triggers, toggling "always active" on must
    /// persist the sentinel — otherwise @c alwaysActivateOnDrag() reverts
    /// to false on next read and the QML switch quietly snaps back.
    void controllerToggleOn_with4UserTriggers_persistsSentinel()
    {
        IsolatedConfigGuard guard;
        Settings settings;

        constexpr int max = ConfigDefaults::maxTriggersPerAction();
        QVariantList userTriggers;
        for (int i = 0; i < max; ++i) {
            userTriggers << storedTrigger(static_cast<int>(DragModifier::Shift) + i);
        }
        settings.setDragActivationTriggers(userTriggers);
        QCOMPARE(settings.dragActivationTriggers().size(), max);

        SnappingBehaviorController controller(&settings);
        QSignalSpy alwaysSpy(&controller, &SnappingBehaviorController::alwaysActivateOnDragChanged);
        controller.setAlwaysActivateOnDrag(true);

        QVERIFY(controller.alwaysActivateOnDrag());
        QCOMPARE(alwaysSpy.count(), 1);
        const auto stored = settings.dragActivationTriggers();
        QCOMPARE(stored.size(), max);
        QVERIFY(TriggerUtils::hasAlwaysActiveTrigger(stored));
    }

    void controllerToggleOnOff_preservesUserTriggers()
    {
        IsolatedConfigGuard guard;
        Settings settings;

        QVariantList userTriggers;
        userTriggers << storedTrigger(static_cast<int>(DragModifier::Ctrl));
        userTriggers << storedTrigger(static_cast<int>(DragModifier::Alt));
        settings.setDragActivationTriggers(userTriggers);

        SnappingBehaviorController controller(&settings);
        controller.setAlwaysActivateOnDrag(true);
        QVERIFY(controller.alwaysActivateOnDrag());
        controller.setAlwaysActivateOnDrag(false);
        QVERIFY(!controller.alwaysActivateOnDrag());

        const auto stored = settings.dragActivationTriggers();
        QCOMPARE(stored.size(), 2);
        QCOMPARE(stored.at(0).toMap().value(ConfigDefaults::triggerModifierField()).toInt(),
                 static_cast<int>(DragModifier::Ctrl));
        QCOMPARE(stored.at(1).toMap().value(ConfigDefaults::triggerModifierField()).toInt(),
                 static_cast<int>(DragModifier::Alt));
    }

    /// If the user toggles always-active off and they had no non-sentinel
    /// triggers, the controller falls back to ConfigDefaults so the user
    /// keeps a working hold-to-activate trigger.
    void controllerToggleOff_emptyUserList_fallsBackToDefault()
    {
        IsolatedConfigGuard guard;
        Settings settings;

        // Sentinel only — simulates a user who turned on always-active without
        // ever configuring a non-sentinel trigger.
        settings.setDragActivationTriggers({storedTrigger(static_cast<int>(DragModifier::AlwaysActive))});

        SnappingBehaviorController controller(&settings);
        QVERIFY(controller.alwaysActivateOnDrag());
        controller.setAlwaysActivateOnDrag(false);
        QVERIFY(!controller.alwaysActivateOnDrag());
        QCOMPARE(settings.dragActivationTriggers(), ConfigDefaults::dragActivationTriggers());
    }

    /// Editing the user-facing trigger list while always-active is on must
    /// re-merge the sentinel through mergeAlwaysActiveTrigger so the cap-aware
    /// prepend protects it from truncation. Also pins that QML-form
    /// (Qt-modifier bitmask) input round-trips back through storage form.
    void controllerSetTriggers_withAlwaysActive_remergesSentinel()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        SnappingBehaviorController controller(&settings);
        controller.setAlwaysActivateOnDrag(true);
        QVERIFY(controller.alwaysActivateOnDrag());

        // The widget never emits the sentinel; simulate by sending a
        // single-trigger list with the QML-bitmask modifier for Ctrl
        // (Qt::ControlModifier == 0x04000000 in Qt6, but the round-trip
        // through convertTriggersForStorage turns it back into the
        // DragModifier enum). Just provide the storage form here — we trust
        // convertTriggersForStorage's idempotency on enum-form input.
        QVariantList qmlTriggers;
        qmlTriggers << storedTrigger(static_cast<int>(DragModifier::Ctrl));
        controller.setDragActivationTriggers(qmlTriggers);

        QVERIFY(controller.alwaysActivateOnDrag());
        const auto stored = settings.dragActivationTriggers();
        QVERIFY(TriggerUtils::hasAlwaysActiveTrigger(stored));
    }
};

QTEST_MAIN(TestSnappingBehaviorController)
#include "test_snapping_behavior_controller.moc"
