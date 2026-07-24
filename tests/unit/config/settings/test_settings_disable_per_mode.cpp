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

#include "config/configdefaults.h"
#include "config/settings.h"
#include "core/interfaces/settings_interfaces.h"
#include "helpers/IsolatedConfigGuard.h"

#include <PhosphorRules/ContextRuleBridge.h>
#include <PhosphorRules/RuleStore.h>

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;
using Mode = PhosphorZones::AssignmentEntry::Mode;

// Stable-id-shaped screen names, required by every test that asserts on a
// screen name the code stored or returned, or on a rule's deterministic UUID.
//
// `resolveScreenId` only rewrites CONNECTOR names, which ScreenIdentity defines
// as "contains no ':'", so a "Manuf:Model:Serial" string canonicalizes to itself
// on every machine. A connector name like "DP-2" would resolve against whatever
// is actually plugged into the box running the suite: the write path
// (setDisabledMonitors → writeDisableEntries → canonicalDisableEntries) and the
// read path (disabledMonitors, which resolves on every read) would both rewrite
// it to that monitor's EDID id, so a `QCOMPARE(disabledMonitors(...), {"DP-2"})`
// fails on a host with a real DP-2 and the rebuilt rule's derived UUID changes.
// Under QT_QPA_PLATFORM=offscreen no connector matches and the name falls
// through untouched, which is why such a test can pass ctest and fail a bare run.
//
// Tests that only round-trip a name through isMonitorDisabled() are immune, and
// several below still write a plain "DP-1" for that reason. isMonitorDisabled
// resolves BOTH sides through ScreenIdentity::variantsFor, so whatever the write
// path canonicalised the name to, the read path canonicalises the query the same
// way and the two meet — the assertion never names the stored form. Adding one of
// these constants to such a test would buy nothing.
//
// The rule for a new test: use a constant here the moment the test asserts on a
// screen name the code STORED or RETURNED (a QCOMPARE against a disable-list
// entry) or on a rule's derived UUID. A bare isMonitorDisabled() round-trip can
// keep using a connector name.
static const QString kScreenA = QStringLiteral("TestVendor:PanelA:0001");
static const QString kScreenB = QStringLiteral("TestVendor:PanelB:0002");
static const QString kScreenC = QStringLiteral("TestVendor:PanelC:0003");

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

    /// A disable rule the user toggled OFF in the rule editor must not gate the
    /// engine, and must not be deleted by an unrelated write to its own (axis,
    /// mode) family.
    ///
    /// The two halves are one fix. `disableEntriesFor` skipping `!enabled` is
    /// what stops the gate, but that same getter is what `writeDisableEntries`
    /// round-trips to decide which rules its rebuild owns: once a disabled rule
    /// is invisible to the getter it is in neither `before` nor `after`, so the
    /// kept-walk has to recognise and preserve it or the next write to any other
    /// monitor silently deletes the user's rule.
    void testMonitorDisable_disabledRuleNeitherGatesNorIsDeleted()
    {
        IsolatedConfigGuard guard;
        PhosphorRules::RuleStore store(ConfigDefaults::rulesFilePath(), nullptr);
        Settings settings(&store, nullptr);

        PhosphorRules::Rule off = PhosphorRules::ContextRuleBridge::makeDisableRule(
            QStringLiteral("Snapping off · ") + kScreenB, kScreenB, 0, QString(),
            PhosphorZones::modeToWireString(Mode::Snapping), PhosphorRules::ContextRuleBridge::kContextBandBase);
        off.enabled = false;
        QVERIFY(store.addRule(off));

        // The gate reads these lists directly (isMonitorDisabled →
        // contextDisabledReason), so reporting a switched-off rule here would
        // keep snapping dead on that screen with no rule in the editor claiming
        // to do it.
        QVERIFY2(!settings.isMonitorDisabled(Mode::Snapping, kScreenB), "a disabled rule still gated the engine off");
        QVERIFY(settings.disabledMonitors(Mode::Snapping).isEmpty());

        // An unrelated write to the same family. The getter no longer reports the
        // screen-B rule, so the kept-walk is the only thing between it and
        // deletion.
        settings.setDisabledMonitors(Mode::Snapping, {kScreenA});
        QCOMPARE(settings.disabledMonitors(Mode::Snapping), QStringList{kScreenA});

        PhosphorRules::RuleStore reloaded(ConfigDefaults::rulesFilePath(), nullptr);
        reloaded.load();
        const auto survivor = reloaded.ruleSet().ruleById(off.id);
        QVERIFY2(survivor.has_value(), "an unrelated write to the family deleted the user's disabled rule");
        QVERIFY2(!survivor->enabled, "the preserved rule came back enabled");
    }

    /// Re-asserting a disabled rule's own entry through the list re-enables it
    /// rather than colliding with it. `makeDisableRule` derives a deterministic
    /// id from (screen, desktop, activity, mode), so a kept-walk that preserved
    /// the disabled rule unconditionally would leave two rules sharing one id the
    /// moment the user ticked that monitor back on.
    void testMonitorDisable_reAssertingADisabledRuleReEnablesIt()
    {
        IsolatedConfigGuard guard;
        PhosphorRules::RuleStore store(ConfigDefaults::rulesFilePath(), nullptr);
        Settings settings(&store, nullptr);

        PhosphorRules::Rule off = PhosphorRules::ContextRuleBridge::makeDisableRule(
            QStringLiteral("Snapping off · ") + kScreenB, kScreenB, 0, QString(),
            PhosphorZones::modeToWireString(Mode::Snapping), PhosphorRules::ContextRuleBridge::kContextBandBase);
        off.enabled = false;
        QVERIFY(store.addRule(off));

        settings.setDisabledMonitors(Mode::Snapping, {kScreenB});
        QVERIFY(settings.isMonitorDisabled(Mode::Snapping, kScreenB));

        PhosphorRules::RuleStore reloaded(ConfigDefaults::rulesFilePath(), nullptr);
        reloaded.load();
        QCOMPARE(reloaded.count(), 1);
        const auto revived = reloaded.ruleSet().ruleById(off.id);
        QVERIFY(revived.has_value());
        QVERIFY2(revived->enabled, "ticking the monitor back on left the rule disabled");
    }

    /// Each per-mode list survives a save → reload round-trip with its
    /// contents intact, AND keeps its content separate from the other mode's
    /// list. Catches regressions where a save accidentally writes both modes
    /// to the same key.
    void testMonitorDisable_perModeRoundTrip()
    {
        IsolatedConfigGuard guard;
        const QStringList snapList{kScreenA, kScreenC};
        const QStringList autotileList{kScreenB};

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

        settings.setDisabledMonitors(Mode::Snapping, {kScreenA});
        QCOMPARE(monitorSpy.count(), 1);
        QCOMPARE(monitorSpy.takeFirst().at(0).toInt(), static_cast<int>(Mode::Snapping));

        settings.setDisabledMonitors(Mode::Autotile, {kScreenB});
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
        writerSettings.setDisabledMonitors(Mode::Snapping, {kScreenA});
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
        QCOMPARE(observerSettings.disabledMonitors(Mode::Snapping), QStringList{kScreenA});
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

    // =========================================================================
    // Borrowed RuleStore ctor — Settings shares the caller's store
    //
    // The settings app constructs Settings with a RuleStore it owns
    // elsewhere (SettingsController::m_localRuleStore) so the disable-list
    // writes and the in-process LayoutRegistry read the SAME store instead of
    // two independent copies over rules.json. These tests pin that the
    // borrow ctor genuinely shares the caller's store (mutations land in it)
    // and that a null argument degrades to owning one.
    // =========================================================================

    /// A disable-list write through a borrow-ctor Settings must land in the
    /// caller's store object — proving the two reference the same instance, not
    /// two independent copies over the same file.
    void testBorrowedStore_writesLandInCallerStore()
    {
        IsolatedConfigGuard guard;
        PhosphorRules::RuleStore store(ConfigDefaults::rulesFilePath());
        QCOMPARE(store.count(), 0);

        Settings settings(&store, nullptr);

        settings.setDisabledMonitors(Mode::Snapping, {QStringLiteral("DP-1")});

        // The disable rule landed in the BORROWED store, not an internal one.
        QCOMPARE(store.count(), 1);
        QVERIFY(settings.isMonitorDisabled(Mode::Snapping, QStringLiteral("DP-1")));

        // Clearing it round-trips back through the same store.
        settings.setDisabledMonitors(Mode::Snapping, {});
        QCOMPARE(store.count(), 0);
    }

    // =========================================================================
    // Non-bare-screen DisableEngine rules
    //
    // The disable lists key entries by (screen), (screen/desktop) or
    // (screen/activity) and nothing else. A DisableEngine rule whose match pins
    // a FOURTH thing — a non-dimension context field such as ScreenOrientation —
    // is authorable in the rule editor with no hand-editing (the match is
    // context-only, so the editor's context-action compatibility check passes),
    // and it is not a shape the lists can represent. Both halves of the disable
    // path must therefore refuse it: the getter must not report it (or the gate
    // would fire in every orientation), and the write path's kept-walk must not
    // rewrite it (or the orientation leaf is destroyed on the next unrelated
    // write to the same family).
    // =========================================================================

    /// Build `All{ScreenId == screen, ScreenOrientation == "portrait"}` with a
    /// single DisableEngine action for @p modeToken — the shape the rule editor
    /// produces for "turn snapping off on this screen, but only in portrait".
    static PhosphorRules::Rule makeOrientationDisableRule(const QString& screen, const QString& modeToken)
    {
        using namespace PhosphorRules;
        Rule rule;
        rule.id = QUuid::createUuid();
        rule.name = QStringLiteral("Snapping off · portrait only");
        rule.enabled = true;
        rule.priority = ContextRuleBridge::kContextBandBase;
        rule.match = MatchExpression::makeAll(
            {MatchExpression::makeLeaf(Field::ScreenId, Operator::Equals, screen),
             MatchExpression::makeLeaf(Field::ScreenOrientation, Operator::Equals, QStringLiteral("portrait"))});
        RuleAction action;
        action.type = QString(ActionType::DisableEngine);
        action.params.insert(ActionParam::Mode, modeToken);
        rule.actions.append(action);
        return rule;
    }

    /// A DisableEngine rule that ALSO pins ScreenOrientation gates the engine
    /// only while that orientation holds, so it is not a monitor-axis disable
    /// entry. Reporting it as one would switch the engine off on that screen in
    /// EVERY orientation — the exact opposite of what the rule says.
    void testOrientationQualifiedDisable_isNotAMonitorEntry()
    {
        IsolatedConfigGuard guard;
        PhosphorRules::RuleStore store(ConfigDefaults::rulesFilePath());
        Settings settings(&store, nullptr);

        QVERIFY(store.setAllRules({makeOrientationDisableRule(kScreenA, QStringLiteral("snapping"))}));
        QCOMPARE(store.count(), 1);

        // The rule pins more than the screen, so it cannot be keyed by the
        // monitor disable list and must not appear in it.
        QVERIFY2(!settings.disabledMonitors(Mode::Snapping).contains(kScreenA),
                 "an orientation-qualified DisableEngine rule is not a bare monitor disable entry");
        QVERIFY2(!settings.isMonitorDisabled(Mode::Snapping, kScreenA),
                 "the monitor gate must not fire for a rule that only disables the engine in portrait");
    }

    /// An unrelated write to the same (Monitor, Snapping) family must leave an
    /// orientation-qualified disable rule byte-for-byte intact. The write path
    /// rebuilds the family from the entry strings, and the entry strings carry
    /// only the screen — so a rule wrongly admitted into the family comes back
    /// as a bare screen pin with a different UUID, i.e. the user's rule is gone.
    void testOrientationQualifiedDisable_survivesUnrelatedFamilyWrite()
    {
        IsolatedConfigGuard guard;
        PhosphorRules::RuleStore store(ConfigDefaults::rulesFilePath());
        Settings settings(&store, nullptr);

        const PhosphorRules::Rule orientationRule = makeOrientationDisableRule(kScreenA, QStringLiteral("snapping"));
        QVERIFY(store.setAllRules({orientationRule}));

        // Disable snapping on a DIFFERENT screen — a write to the same
        // (Monitor, Snapping) family that has nothing to do with the rule above.
        settings.setDisabledMonitors(Mode::Snapping, {kScreenB});

        // The new bare disable landed.
        QCOMPARE(settings.disabledMonitors(Mode::Snapping), QStringList{kScreenB});

        // And the orientation rule is still there, unchanged — same id, same
        // match. Locating it by id is the point: a rebuilt bare-screen rule
        // would carry disableRuleIdFor's derived UUID instead.
        const std::optional<PhosphorRules::Rule> survivor = store.ruleSet().ruleById(orientationRule.id);
        QVERIFY2(survivor.has_value(), "the unrelated write destroyed the orientation-qualified disable rule");
        QVERIFY2(
            survivor->match.referencesAnyField(QSet<PhosphorRules::Field>{PhosphorRules::Field::ScreenOrientation}),
            "the orientation leaf was stripped — the rule was rebuilt from a bare screen entry");
        QCOMPARE(survivor->name, orientationRule.name);
        QVERIFY(survivor->enabled);
    }

    /// A null store argument degrades to owning one (defensive parity with the
    /// backend-injecting ctor) so a misuse still yields a working object.
    void testBorrowedStore_nullDegradesToOwned()
    {
        IsolatedConfigGuard guard;
        Settings settings(nullptr, nullptr);

        // No crash, and disable-lists work against the internally-owned store.
        settings.setDisabledMonitors(Mode::Autotile, {kScreenB});
        QVERIFY(settings.isMonitorDisabled(Mode::Autotile, kScreenB));
    }

    /// A BORROWED store is NOT reloaded by Settings::load() — that is the owner's
    /// job, which is the whole reason the borrow path exists (the guard at
    /// Settings::load()). A peer's external write stays invisible through the
    /// borrowed store until the OWNER explicitly reloads it.
    void testBorrowedStore_settingsLoadDoesNotReloadIt()
    {
        IsolatedConfigGuard guard;
        PhosphorRules::RuleStore store(ConfigDefaults::rulesFilePath());
        Settings settings(&store, nullptr);

        // Establish a known baseline on disk (the Settings ctor's config
        // migration may have seeded rules.json, so don't assume empty).
        QVERIFY(store.setAllRules({}));
        QCOMPARE(store.count(), 0);

        // A peer (a separate owned Settings over the same file) writes a disable
        // rule to rules.json.
        {
            Settings peer;
            peer.setDisabledMonitors(Mode::Snapping, {QStringLiteral("DP-1")});
        }

        // Settings::load() must NOT reload the borrowed store, so the peer's
        // write is not yet visible through it.
        settings.load();
        QCOMPARE(store.count(), 0);

        // The owner reloading explicitly picks it up.
        store.load();
        QVERIFY(store.count() > 0);
    }
};

QTEST_MAIN(TestSettingsDisablePerMode)
#include "test_settings_disable_per_mode.moc"
