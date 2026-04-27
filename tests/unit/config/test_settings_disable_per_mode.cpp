// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_settings_disable_per_mode.cpp
 * @brief Per-mode disable list independence — Phase 5b.
 *
 * Pre-v3 a single Snapping.Behavior.Display.{DisabledMonitors,DisabledDesktops,
 * DisabledActivities} list silently gated both snap AND autotile despite the
 * snapping-prefixed group name. v3 split it into independent per-mode lists
 * so a user can disable autotile on monitor X without losing snap there.
 *
 * The migration test (test_configmigration.cpp) verifies the schema-shape
 * side; these tests verify the runtime behaviour: writes go to the right
 * key, reads return only the per-mode list, and the gate helper
 * isContextDisabled() honours the mode argument.
 */

#include <QSignalSpy>
#include <QTest>

#include "../../../src/config/configdefaults.h"
#include "../../../src/config/settings.h"
#include "../../../src/core/settings_interfaces.h"
#include "../helpers/IsolatedConfigGuard.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;
using Mode = PhosphorZones::AssignmentEntry::Mode;

class TestSettingsDisablePerMode : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // =========================================================================
    // Monitor disable: per-mode independence
    // =========================================================================

    /// Disabling a monitor for snap must NOT affect the autotile gate, and
    /// vice versa. Without this guarantee the v3 split would be cosmetic.
    void testMonitorDisable_snapAndAutotileIndependent()
    {
        IsolatedConfigGuard guard;
        Settings settings;

        const QString screen = QStringLiteral("DP-1");

        // Both modes start clean.
        QVERIFY(!settings.isMonitorDisabled(Mode::Snapping, screen));
        QVERIFY(!settings.isMonitorDisabled(Mode::Autotile, screen));

        // Disable for snap only.
        settings.setDisabledMonitors(Mode::Snapping, {screen});
        QVERIFY(settings.isMonitorDisabled(Mode::Snapping, screen));
        QVERIFY2(!settings.isMonitorDisabled(Mode::Autotile, screen), "snap-side disable leaked into autotile gate");

        // Disable for autotile too — both lists now hold the screen, but
        // they are still independent (same value, different keys).
        settings.setDisabledMonitors(Mode::Autotile, {screen});
        QVERIFY(settings.isMonitorDisabled(Mode::Snapping, screen));
        QVERIFY(settings.isMonitorDisabled(Mode::Autotile, screen));

        // Re-enable snap only — autotile's list is unaffected.
        settings.setDisabledMonitors(Mode::Snapping, {});
        QVERIFY(!settings.isMonitorDisabled(Mode::Snapping, screen));
        QVERIFY2(settings.isMonitorDisabled(Mode::Autotile, screen),
                 "autotile-side disable was wiped when snap was cleared");
    }

    /// Each per-mode list survives a save → reload round-trip with its
    /// contents intact, AND keeps its content separate from the other mode's
    /// list. Catches regressions where a save accidentally writes both modes
    /// to the same key.
    void testMonitorDisable_perModeRoundTrip()
    {
        IsolatedConfigGuard guard;
        const QStringList snapList{QStringLiteral("DP-1"), QStringLiteral("HDMI-A-1")};
        const QStringList autotileList{QStringLiteral("DP-2")};

        {
            Settings settings;
            settings.setDisabledMonitors(Mode::Snapping, snapList);
            settings.setDisabledMonitors(Mode::Autotile, autotileList);
            settings.save();
        }

        Settings reloaded;
        QCOMPARE(reloaded.disabledMonitors(Mode::Snapping), snapList);
        QCOMPARE(reloaded.disabledMonitors(Mode::Autotile), autotileList);
    }

    /// Per-mode setters fire NOTIFY with the matching mode argument and
    /// nothing else. Without the mode in the signal, listeners for the OTHER
    /// mode would re-read on every change to either list — a churn vector
    /// for QML bindings.
    void testMonitorDisable_signalCarriesMode()
    {
        IsolatedConfigGuard guard;
        Settings settings;

        QSignalSpy monitorSpy(&settings, &Settings::disabledMonitorsChanged);
        QVERIFY(monitorSpy.isValid());

        settings.setDisabledMonitors(Mode::Snapping, {QStringLiteral("DP-1")});
        QCOMPARE(monitorSpy.count(), 1);
        QCOMPARE(monitorSpy.takeFirst().at(0).toInt(), static_cast<int>(Mode::Snapping));

        settings.setDisabledMonitors(Mode::Autotile, {QStringLiteral("DP-2")});
        QCOMPARE(monitorSpy.count(), 1);
        QCOMPARE(monitorSpy.takeFirst().at(0).toInt(), static_cast<int>(Mode::Autotile));
    }

    // =========================================================================
    // Desktop / activity: per-mode independence
    // =========================================================================

    void testDesktopDisable_snapAndAutotileIndependent()
    {
        IsolatedConfigGuard guard;
        Settings settings;

        const QString screen = QStringLiteral("DP-1");
        const int desktop = 2;
        const QString key = screen + QLatin1Char('/') + QString::number(desktop);

        settings.setDisabledDesktops(Mode::Snapping, {key});

        QVERIFY(settings.isDesktopDisabled(Mode::Snapping, screen, desktop));
        QVERIFY2(!settings.isDesktopDisabled(Mode::Autotile, screen, desktop),
                 "snap-side desktop disable leaked into autotile gate");

        // Confirm the lists themselves are physically distinct, not just the
        // gate function.
        QVERIFY(settings.disabledDesktops(Mode::Autotile).isEmpty());
    }

    void testActivityDisable_snapAndAutotileIndependent()
    {
        IsolatedConfigGuard guard;
        Settings settings;

        const QString screen = QStringLiteral("DP-1");
        const QString activity = QStringLiteral("uuid-foo");
        const QString key = screen + QLatin1Char('/') + activity;

        settings.setDisabledActivities(Mode::Autotile, {key});

        QVERIFY(settings.isActivityDisabled(Mode::Autotile, screen, activity));
        QVERIFY2(!settings.isActivityDisabled(Mode::Snapping, screen, activity),
                 "autotile-side activity disable leaked into snap gate");

        QVERIFY(settings.disabledActivities(Mode::Snapping).isEmpty());
    }

    // =========================================================================
    // Gate helper: isContextDisabled / contextDisabledReason honour mode
    // =========================================================================

    /// The gate helpers must read ONLY the mode they were given. The
    /// monitor list (priority 1) must not bleed across; nor must desktop
    /// (priority 2) or activity (priority 3).
    void testGateHelper_readsOnlyTargetMode()
    {
        IsolatedConfigGuard guard;
        Settings settings;

        const QString screen = QStringLiteral("DP-1");
        const QString activity = QStringLiteral("uuid-foo");

        // Disable the monitor for snap only.
        settings.setDisabledMonitors(Mode::Snapping, {screen});

        // Snap path: disabled, reason = monitor.
        QVERIFY(isContextDisabled(&settings, Mode::Snapping, screen, 1, activity));
        QCOMPARE(contextDisabledReason(&settings, Mode::Snapping, screen, 1, activity),
                 DisabledReason::MonitorDisabled);

        // Autotile path on the SAME screen / desktop / activity: not disabled.
        QVERIFY(!isContextDisabled(&settings, Mode::Autotile, screen, 1, activity));
        QCOMPARE(contextDisabledReason(&settings, Mode::Autotile, screen, 1, activity), DisabledReason::NotDisabled);
    }

    /// Priority cascade per mode: monitor > desktop > activity. With monitor
    /// clean, desktop disabled in snap, and activity disabled in autotile,
    /// the gate reports the right reason for each mode independently.
    void testGateHelper_priorityRespectsMode()
    {
        IsolatedConfigGuard guard;
        Settings settings;

        const QString screen = QStringLiteral("DP-1");
        const int desktop = 2;
        const QString activity = QStringLiteral("uuid-foo");
        const QString deskKey = screen + QLatin1Char('/') + QString::number(desktop);
        const QString actKey = screen + QLatin1Char('/') + activity;

        settings.setDisabledDesktops(Mode::Snapping, {deskKey});
        settings.setDisabledActivities(Mode::Autotile, {actKey});

        QCOMPARE(contextDisabledReason(&settings, Mode::Snapping, screen, desktop, activity),
                 DisabledReason::DesktopDisabled);
        QCOMPARE(contextDisabledReason(&settings, Mode::Autotile, screen, desktop, activity),
                 DisabledReason::ActivityDisabled);
    }

    // =========================================================================
    // load() cross-process refresh
    //
    // When a daemon shortcut or D-Bus call writes to the on-disk config, the
    // settings UI's onExternalSettingsChanged() invokes Settings::load() to
    // reparse. Because the per-mode disable accessors are not Q_PROPERTYs
    // (their getters take a Mode argument), the meta-object loop in load()
    // can't re-emit them via NOTIFY — load() must do so explicitly. Without
    // that explicit emission, QML bindings driven by SharedBridge's
    // disabledMonitorsChanged() never fire on cross-process changes.
    // =========================================================================

    /// Simulates the cross-process flow: writer instance flips a value to disk,
    /// observer instance reloads and is expected to emit the per-mode signal
    /// for whichever list actually changed (and ONLY that one).
    void testLoad_emitsPerModeSignalsForExternalWrites()
    {
        IsolatedConfigGuard guard;
        Settings writerSettings;
        Settings observerSettings;

        QSignalSpy monitorSpy(&observerSettings, &Settings::disabledMonitorsChanged);
        QSignalSpy desktopSpy(&observerSettings, &Settings::disabledDesktopsChanged);
        QSignalSpy activitySpy(&observerSettings, &Settings::disabledActivitiesChanged);
        QVERIFY(monitorSpy.isValid());
        QVERIFY(desktopSpy.isValid());
        QVERIFY(activitySpy.isValid());

        // Writer flips snap-side monitor list and persists.
        writerSettings.setDisabledMonitors(Mode::Snapping, {QStringLiteral("DP-1")});
        writerSettings.save();

        // Observer's load() picks up the on-disk delta. Only the snap-side
        // monitor signal should fire — autotile monitor list and both
        // desktop/activity lists were unchanged.
        observerSettings.load();

        QCOMPARE(monitorSpy.count(), 1);
        QCOMPARE(monitorSpy.takeFirst().at(0).toInt(), static_cast<int>(Mode::Snapping));
        QCOMPARE(desktopSpy.count(), 0);
        QCOMPARE(activitySpy.count(), 0);

        // Observer now sees the new value via its own getter.
        QCOMPARE(observerSettings.disabledMonitors(Mode::Snapping), QStringList{QStringLiteral("DP-1")});
        QVERIFY(observerSettings.disabledMonitors(Mode::Autotile).isEmpty());
    }

    /// load() must NOT fire any per-mode signal when the on-disk value matches
    /// what the observer already had. Otherwise every save() / load() cycle
    /// (e.g. discard-changes) would churn QML bindings unnecessarily.
    void testLoad_noEmitWhenUnchanged()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        settings.setDisabledMonitors(Mode::Snapping, {QStringLiteral("DP-1")});
        settings.save();

        QSignalSpy monitorSpy(&settings, &Settings::disabledMonitorsChanged);
        QSignalSpy desktopSpy(&settings, &Settings::disabledDesktopsChanged);
        QSignalSpy activitySpy(&settings, &Settings::disabledActivitiesChanged);
        QVERIFY(monitorSpy.isValid());

        // Reload without any external mutation — file content matches memory.
        settings.load();

        QCOMPARE(monitorSpy.count(), 0);
        QCOMPARE(desktopSpy.count(), 0);
        QCOMPARE(activitySpy.count(), 0);
    }

    // =========================================================================
    // reset() clears per-mode disable lists
    //
    // Pre-v3 these lived under Snapping.Behavior.Display, so the Snapping
    // top-level group deletion in Settings::reset() swept them. v3 moved them
    // into a sibling top-level Display group, which must therefore appear in
    // managedGroupNames() too — otherwise "Reset to Defaults" would silently
    // preserve user-disabled monitors/desktops/activities while every other
    // setting reset.
    // =========================================================================

    /// After Settings::reset(), every per-mode disable list must be empty.
    /// Covers all three axes (monitor, desktop, activity) for both modes.
    void testReset_clearsPerModeDisableLists()
    {
        IsolatedConfigGuard guard;
        Settings settings;

        const QString screen = QStringLiteral("DP-1");
        const int desktop = 2;
        const QString activity = QStringLiteral("uuid-foo");
        const QString deskKey = screen + QLatin1Char('/') + QString::number(desktop);
        const QString actKey = screen + QLatin1Char('/') + activity;

        settings.setDisabledMonitors(Mode::Snapping, {screen});
        settings.setDisabledMonitors(Mode::Autotile, {screen});
        settings.setDisabledDesktops(Mode::Snapping, {deskKey});
        settings.setDisabledDesktops(Mode::Autotile, {deskKey});
        settings.setDisabledActivities(Mode::Snapping, {actKey});
        settings.setDisabledActivities(Mode::Autotile, {actKey});
        settings.save();

        // Sanity check: state was actually persisted.
        QVERIFY(settings.isMonitorDisabled(Mode::Snapping, screen));
        QVERIFY(settings.isMonitorDisabled(Mode::Autotile, screen));
        QVERIFY(settings.isDesktopDisabled(Mode::Snapping, screen, desktop));
        QVERIFY(settings.isActivityDisabled(Mode::Autotile, screen, activity));

        settings.reset();

        QVERIFY(settings.disabledMonitors(Mode::Snapping).isEmpty());
        QVERIFY(settings.disabledMonitors(Mode::Autotile).isEmpty());
        QVERIFY(settings.disabledDesktops(Mode::Snapping).isEmpty());
        QVERIFY(settings.disabledDesktops(Mode::Autotile).isEmpty());
        QVERIFY(settings.disabledActivities(Mode::Snapping).isEmpty());
        QVERIFY(settings.disabledActivities(Mode::Autotile).isEmpty());

        // And — critically — the values must not come back on next construction.
        // (reset() deletes the on-disk group AND syncs; a fresh Settings instance
        // reads from the same persisted file.)
        Settings reloaded;
        QVERIFY(reloaded.disabledMonitors(Mode::Snapping).isEmpty());
        QVERIFY(reloaded.disabledMonitors(Mode::Autotile).isEmpty());
        QVERIFY(reloaded.disabledDesktops(Mode::Snapping).isEmpty());
        QVERIFY(reloaded.disabledDesktops(Mode::Autotile).isEmpty());
        QVERIFY(reloaded.disabledActivities(Mode::Snapping).isEmpty());
        QVERIFY(reloaded.disabledActivities(Mode::Autotile).isEmpty());
    }
};

QTEST_MAIN(TestSettingsDisablePerMode)
#include "test_settings_disable_per_mode.moc"
